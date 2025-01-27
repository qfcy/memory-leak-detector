#define DISABLE_OVERWRITING_STD
#define MEMLEAK_NO_EXTERNAL
#include "memleak.h"

#define _EXCEPTION_DEF(T,parent) class T:public parent{\
public:T(const char *msg):parent(msg){}\
};

namespace _memleak_util{
LockGuard::LockGuard(std::recursive_mutex *mutex):mutex(mutex){ // 允许传入空指针，和std::lock_guard不同
	if(mutex)mutex->lock();
}
LockGuard::~LockGuard(){
	if(mutex)mutex->unlock();
}
}
namespace memleak{
using std::printf;
inline size_t ptr_diff(void *ptr1, void *ptr2) {
    size_t size1 = (size_t)ptr1;
    size_t size2 = (size_t)ptr2;
    return (size1>size2)?(size1-size2):(size2-size1);
}
char *convert_size(size_t size,char *result_buffer){
	char *result;
	if (result_buffer==nullptr)
		result = (char *)std::malloc(sizeof(char)*20);
	else result=result_buffer;

	const char *suffix[]={"B", "KB", "MB", "GB", "TB"};
    size_t index = 0;
	double num = size;
    while(num >= 1024 && index < sizeof(suffix) - 1){
        num /= 1024;
        index++;
	}
    snprintf(result,20,"%.3f %s",num,suffix[index]);
	return result;
}
MemCtrlBlock::MemCtrlBlock() : start(0), size(0), used(false), next(nullptr) {}

MemMgr::MemMgr(size_t size, void *mem_start, size_t mcb_count, bool use_mutex) :
        total(size), used(0), max_MCBs(mcb_count), MCB_id(0) {
    if (mem_start == nullptr) mem_start = std::malloc(size);
    if (mem_start == nullptr) throw std::bad_alloc();
    MCB_mem = (MemCtrlBlock *)std::malloc(sizeof(MemCtrlBlock) * mcb_count);
    if (MCB_mem == nullptr) throw std::bad_alloc();
    memStart = mem_start;
    mcbHead = get_new_MCB(); // 链表头部占用一个MCB
    mcbHead->start = mcbHead->size = 0;
    mcbHead->next = nullptr;
    if (use_mutex) _mutex = new std::recursive_mutex;
    else _mutex = nullptr;
}

MemMgr::~MemMgr() { //内存管理器释放后，所有用内存管理器申请的指针无效
    MemCtrlBlock *p = mcbHead, *next;
    while (p != nullptr) {
        next = p->next;
        delete_MCB(p); // delete p;
        p = next;
    }
    std::free(memStart);
    std::free(MCB_mem);
    memStart = MCB_mem = nullptr;
    mcbHead = nullptr;
    if (_mutex) delete _mutex;
}

void *MemMgr::malloc(size_t size) {
    _memleak_util::LockGuard lock_guard(_mutex);
    if (size == 0) return nullptr; // 调用malloc(0)
    MemCtrlBlock *p = mcbHead;
    while (p != nullptr) {
        size_t free_;
        if (p->next == nullptr) { // 计算空闲内存
            free_ = total - (p->start + p->size);
        } else {
            free_ = p->next->start - (p->start + p->size);
        }
        if (free_ >= size) { // 如果合适
            MemCtrlBlock *new_ = get_new_MCB();
            new_->start = p->start + p->size;
            new_->size = size;
            new_->next = p->next;
            p->next = new_;
            used += size;
            return (void *)((char *)memStart + new_->start);
        }
        p = p->next;
    }
    return nullptr;
}

void *MemMgr::calloc(size_t num, size_t size) {
    _memleak_util::LockGuard lock_guard(_mutex);
    void *mem = malloc(num * size);
    if (mem == nullptr) {
        return nullptr;
    }
    memset(mem, 0, num * size);
    return mem;
}

void *MemMgr::realloc(void *mem, size_t size) {
    _memleak_util::LockGuard lock_guard(_mutex);
    size_t offset = (size_t)((char *)mem - (char *)memStart);
    if (mem == nullptr) return malloc(size); //改用malloc
    if (size == 0) {
        free(mem);
        return nullptr; // size为0时等同于free(mem)
    }
    if (mcbHead->next == nullptr) { // 头节点为空
        throw ReallocError("Cannot reallocate memory as no memory is allocated.");
    }
    MemCtrlBlock *pre = mcbHead, *p = mcbHead->next;
    while (p != nullptr && p->start <= offset) {
        if (offset == p->start) {
            size_t available;
            if (p->next == nullptr) { // 计算空闲内存
                available = total - p->start;
            } else {
                available = p->next->start - p->start;
            }
            if (available >= size) { //后部可用空间足够
                used += size - p->size;
                p->size = size;
                return mem;
            } else { // 后部空间不足
                //尝试将旧内存标记为空闲，再重新申请（多线程中要锁上）
                pre->next = p->next;
                void *new_mem = malloc(size);
                if (new_mem == nullptr) {
                    pre->next = p; //重新标记旧的内存为已使用
                    return nullptr;
                }
                size_t old_size = p->size;
                delete_MCB(p);
                p = nullptr; //malloc成功之后链表已经变化，不能再使用p
                size_t delta = ptr_diff(new_mem, mem);
                if (delta >= size) {
                    memcpy(new_mem, mem, old_size);
                } else {
                    memmove(new_mem, mem, old_size);
                }
                used -= old_size; //确保已用内存大小正确
                return new_mem;
            }
        }
        pre = p;
        p = p->next;
    }
    char msg[80];
    snprintf(msg, sizeof(char) * 50, "Invalid offset for memory reallocation (%zu).", offset);
    throw ReallocError(msg);
}

void MemMgr::free(void *mem) {
    _memleak_util::LockGuard lock_guard(_mutex);
    if (mem == nullptr) return; // 调用free(nullptr)
    size_t offset = (size_t)((char *)mem - (char *)memStart);
    MemCtrlBlock *p = mcbHead;
    if (p->next == nullptr) { // 头节点为空
        throw DoubleFreeError("Cannot free memory as no memory is allocated.");
    }
    MemCtrlBlock *pre = p;
    p = p->next;
    while (p != nullptr && p->start <= offset) {
        if (offset == p->start) {
            pre->next = p->next;
            used -= p->size;
            delete_MCB(p); // delete p;
            return;
        }
        pre = p;
        p = p->next;
    }
    char msg[80];
    snprintf(msg, sizeof(char) * 90, "Cannot free the memory at %zu, "
                                     "the lower block is %zu.", offset, pre->start);
    throw DoubleFreeError(msg);
}

void MemMgr::showInfo(bool showMCBState) { //显示当前分配内存块信息，用于调试
    _memleak_util::LockGuard lock_guard(_mutex);
    printf("Total: %zuB Used: %zuB Free: %zuB\n", total, used, total - used);
    size_t cnt = 0;
    MemCtrlBlock *p = mcbHead->next;
    printf("   Start - Size\n");
    while (p != nullptr) {
        printf("%zu. %zu - %zu\n", cnt, p->start, p->size);
        p = p->next;
        cnt++;
    }
    printf("%zu memory blocks. ", cnt);
    if (showMCBState) printf("(%zu/%zu)\n\n", cnt + 1, max_MCBs);
    else printf("\n\n");
}

void MemMgr::dumpToFile(const char *filename) { // 转储全部内存
    _memleak_util::LockGuard lock_guard(_mutex);
    FILE *file = fopen(filename, "wb");
    if (file == nullptr) {
        throw std::runtime_error(strerror(errno));
    }
    fwrite(memStart, sizeof(char), total, file);
    fclose(file);
}

MemCtrlBlock *MemMgr::get_new_MCB() {
    if (!MCB_mem[MCB_id].used) { // 判断下一个MCB是否可用
        MCB_mem[MCB_id].used = true;
        size_t cur = MCB_id;
        MCB_id = (MCB_id + 1) % max_MCBs;
        return &MCB_mem[cur];
    }
    size_t new_id = (MCB_id + 1) % max_MCBs;
    while (new_id != MCB_id) { // 使用约瑟夫环，遍历整个MCB_mem
        if (!MCB_mem[new_id].used) {
            MCB_mem[new_id].used = true;
            MCB_id = (new_id + 1) % max_MCBs;
            return &MCB_mem[new_id];
        }
        new_id = (new_id + 1) % max_MCBs;
    }
    throw std::runtime_error("Memory for MCBs is full.");
}

void MemMgr::delete_MCB(MemCtrlBlock *mcb) {
    size_t index = mcb - MCB_mem;
    if (!MCB_mem[index].used) {
        throw std::runtime_error("Cannot delete an unused MCB.");
    }
    MCB_mem[index].start = MCB_mem[index].size = 0;
    MCB_mem[index].next = nullptr;
    MCB_mem[index].used = false;
}
}

namespace memleak{
	namespace _memleak{
		MemMgr *mem=nullptr; // 判断是否已经调用setup_mem (是否为空)
		bool enabled=false; // 是否在退出时显示内存泄漏信息
	}
	extern "C"{
	void show_mem_info(){
		if(_memleak::mem==nullptr)
            throw std::runtime_error("Must call setup_mem() before calling show_mem_info()");

		_memleak::mem->showInfo();
	}
	void dump_mem_to_file(const char *filename){
		if(_memleak::mem==nullptr)
            throw std::runtime_error("Must call setup_mem() before calling dump_mem_to_file(filename)");

		_memleak::mem->dumpToFile(filename);
	}

    void _showLeakInfo(){
        printf("The memory at exit:\n");
        show_mem_info();
        if(_memleak::mem->used==0){
            printf("No memory leaks detected.\n");
        } else {
			char *size=convert_size(_memleak::mem->used);
            printf("A memory leak of %s was detected.\n",size);
			std::free(size);
        }
    }
    void _atexit_showInfo(){
        if(_memleak::enabled)_showLeakInfo();
    }

    void setup_mem_noinit(size_t size,size_t mcb_count){ // 初始化自定义的内存分配器
	    // mcb_count: 最大允许分配的内存块数量，默认是8192。如果为0，使用DEFAULT_MCB_COUNT
		if(_memleak::mem)
			throw std::runtime_error("Already set up when calling setup_mem()");

		if(mcb_count==0)mcb_count=DEFAULT_MCB_COUNT;
        _memleak::mem=new MemMgr(size,nullptr,mcb_count);
        atexit(_atexit_showInfo);
    }
	void setup_mem(size_t size,size_t mcb_count,unsigned char initVal){
        setup_mem_noinit(size,mcb_count);
		memset(_memleak::mem->memStart,initVal,size); // 初始化内存
    }
    void set_leak_detect(bool enabled){ // 设置是否启用退出时打印泄漏信息
        if(_memleak::mem==nullptr)
            throw std::runtime_error("Must call setup_mem() before enabling leak detection");

        _memleak::enabled=enabled;
    }
	void shutdown(){ // 会使得调用enable_leak_detect()之后分配的内存变为无效
		if(_memleak::mem==nullptr)
            throw std::runtime_error("Must call setup_mem() before shutdowning");

		if(_memleak::mem->used>0)
			fprintf(stderr,"Warning: %zuB of memory isn't freed when calling shutdown()\n",
					_memleak::mem->used);
		delete _memleak::mem;
		_memleak::enabled=false;
		_memleak::mem=nullptr;
	}
    }
}
namespace _override_std{
    using namespace memleak;
    void *malloc(size_t size){
		if(_memleak::mem)
        	return _memleak::mem->malloc(size);
		else
			return std::malloc(size);
    }
    void *calloc(size_t num,size_t size){
		if(_memleak::mem)
        	return _memleak::mem->calloc(num,size);
		else
			return std::calloc(num,size);
    }
    void *realloc(void *mem,size_t size){
		if(_memleak::mem)
        	return _memleak::mem->realloc(mem,size);
		else
			return std::realloc(mem,size);
    }
    void free(void *mem){
		if(_memleak::mem)
        	_memleak::mem->free(mem);
		else
			std::free(mem);
    }
	char *strdup(const char *str){
		if(!str)return nullptr;
		char *result=(char *)malloc(strlen(str)+1);
		if(result==nullptr)return nullptr;
		strcpy(result,str);
		return result;
	}
	wchar_t *wcsdup(const wchar_t *str){
		if(!str)return nullptr;
		wchar_t *result=(wchar_t *)malloc(sizeof(wchar_t)*(wcslen(str)+1));
		if(result==nullptr)return nullptr;
		wcscpy(result,str);
		return result;
	}
}
void *_override_std_malloc(size_t size) {
	return _override_std::malloc(size);
}

void *_override_std_calloc(size_t num, size_t size) {
	return _override_std::calloc(num, size);
}

void *_override_std_realloc(void *mem, size_t size) {
	return _override_std::realloc(mem, size);
}

void _override_std_free(void *mem) {
	_override_std::free(mem);
}

char *_override_std_strdup(const char *str) {
	return _override_std::strdup(str);
}

wchar_t *_override_std_wcsdup(const wchar_t *str) {
	return _override_std::wcsdup(str);
}