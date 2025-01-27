#pragma once

#include<cstdlib>
#include<cstring>
#include<cstdio>
#include<cerrno>
#include<stdexcept>
#include<mutex>

#define _EXCEPTION_DEF(T,parent) class T:public parent{\
public:T(const char *msg):parent(msg){}\
};

namespace _memleak_util{
class LockGuard{
public:
	LockGuard(std::recursive_mutex *mutex):mutex(mutex){ // 允许传入空指针，和std::lock_guard不同
		if(mutex)mutex->lock();
	}
	~LockGuard(){
		if(mutex)mutex->unlock();
	}
	std::recursive_mutex *mutex;
};
}
namespace memleak{
using std::printf;
_EXCEPTION_DEF(ReallocError,std::runtime_error)
_EXCEPTION_DEF(DoubleFreeError,std::runtime_error)

const size_t DEFAULT_MCB_COUNT=8192;
struct MemCtrlBlock{
	size_t start;
	size_t size;
	bool used;
	MemCtrlBlock *next;
	MemCtrlBlock():start(0),size(0),used(false),next(nullptr){}
};
inline size_t ptr_diff(void *ptr1, void *ptr2) {
    size_t size1 = (size_t)ptr1;
    size_t size2 = (size_t)ptr2;
    return (size1>size2)?(size1-size2):(size2-size1);
}
char *convert_size(size_t size,char *result_buffer=nullptr){
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
class MemMgr{
public:
	MemMgr(size_t size,void *mem_start=nullptr,
		size_t mcb_count=DEFAULT_MCB_COUNT,bool use_mutex=true):
		total(size),used(0),max_MCBs(mcb_count),MCB_id(0){
		if(mem_start==nullptr) mem_start=std::malloc(size);
		if(mem_start==nullptr)throw std::bad_alloc();
		MCB_mem=(MemCtrlBlock *)std::malloc(sizeof(MemCtrlBlock)*mcb_count);
		if(MCB_mem==nullptr)throw std::bad_alloc();
		memStart=mem_start;
		mcbHead = get_new_MCB(); // 链表头部占用一个MCB
		mcbHead->start=mcbHead->size=0;
		mcbHead->next=nullptr;
		if(use_mutex)_mutex=new std::recursive_mutex;
		else _mutex=nullptr;
	}
	~MemMgr(){ //内存管理器释放后，所有用内存管理器申请的指针无效
		MemCtrlBlock *p=mcbHead,*next;
		while(p!=nullptr){
			next=p->next;
			delete_MCB(p); // delete p;
			p=next;
		}
		std::free(memStart);std::free(MCB_mem);
		memStart=MCB_mem=nullptr;mcbHead=nullptr;
		if(_mutex)delete _mutex;
	}
	void *malloc(size_t size){
		_memleak_util::LockGuard lock_guard(_mutex);
		if(size==0)return nullptr; // 调用malloc(0)
		MemCtrlBlock *p=mcbHead;
		while(p!=nullptr){
			size_t free_;
			if(p->next==nullptr){ // 计算空闲内存
				free_=total-(p->start+p->size);
			} else {
				free_=p->next->start-(p->start+p->size);
			}
			if(free_>=size){ // 如果合适
				MemCtrlBlock *new_ = get_new_MCB();
				new_->start=p->start+p->size;
				new_->size=size;
				new_->next=p->next;
				p->next=new_;
				used+=size;
				return (void *)((char *)memStart+new_->start);
			}
			p=p->next;
		}
		return nullptr;
	}
	void *calloc(size_t num,size_t size){
		_memleak_util::LockGuard lock_guard(_mutex);
		void *mem=malloc(num*size);
		if(mem==nullptr){
			return nullptr;
		}
		memset(mem,0,num*size);
		return mem;
	}
	void *realloc(void *mem,size_t size){
		_memleak_util::LockGuard lock_guard(_mutex);
		size_t offset=(size_t)((char *)mem-(char *)memStart);
		if(mem==nullptr) return malloc(size); //改用malloc
		if(size==0) {
			free(mem);return nullptr; // size为0时等同于free(mem)
		}
		if(mcbHead->next==nullptr){ // 头节点为空
			throw ReallocError("Cannot reallocate memory as no memory is allocated.");
		}
		MemCtrlBlock *pre=mcbHead,*p=mcbHead->next;
		while(p!=nullptr && p->start<=offset){
			if(offset==p->start){
				size_t available;
				if(p->next==nullptr){ // 计算空闲内存
					available=total-p->start;
				} else {
					available=p->next->start-p->start;
				}
				if(available>=size){ //后部可用空间足够
					used+=size-p->size;
					p->size=size;
					return mem;
				}else{ // 后部空间不足
					//尝试将旧内存标记为空闲，再重新申请（多线程中要锁上）
					pre->next=p->next;
					void *new_mem=malloc(size);
					if(new_mem==nullptr){
						pre->next=p; //重新标记旧的内存为已使用
						return nullptr;
					}
					size_t old_size=p->size;
					delete_MCB(p);p=nullptr;//malloc成功之后链表已经变化，不能再使用p
					size_t delta=ptr_diff(new_mem,mem);
					if(delta>=size){
						memcpy(new_mem,mem,old_size);
					}else{
						memmove(new_mem,mem,old_size);
					}
					used-=old_size; //确保已用内存大小正确
					return new_mem;
				}
			}
			pre=p;p=p->next;
		}
		char msg[80];
		snprintf(msg,sizeof(char)*50,"Invalid offset for memory reallocation (%zu).",offset);
		throw ReallocError(msg);
	}
	void free(void *mem){
		_memleak_util::LockGuard lock_guard(_mutex);
		if(mem==nullptr)return; // 调用free(nullptr)
		size_t offset=(size_t)((char *)mem-(char *)memStart);
		MemCtrlBlock *p=mcbHead;
		if(p->next==nullptr){ // 头节点为空
			throw DoubleFreeError("Cannot free memory as no memory is allocated.");
		}
		MemCtrlBlock *pre=p;p=p->next;
		while(p!=nullptr && p->start<=offset){
			if(offset==p->start){
				pre->next=p->next;
				used-=p->size;
				delete_MCB(p); // delete p;
				return;
			}
			pre=p;p=p->next;
		}
		char msg[80];
		snprintf(msg,sizeof(char)*90,"Cannot free the memory at %zu, "\
				 "the lower block is %zu.",offset,pre->start);
		throw DoubleFreeError(msg);
	}
	void showInfo(bool showMCBState=false){ //显示当前分配内存块信息，用于调试
		_memleak_util::LockGuard lock_guard(_mutex);
		printf("Total: %zuB Used: %zuB Free: %zuB\n",total,used,total-used);
		size_t cnt=0;
		MemCtrlBlock *p=mcbHead->next;
		printf("   Start - Size\n");
		while(p!=nullptr){
			printf("%zu. %zu - %zu\n",cnt,p->start,p->size);
			p=p->next;
			cnt++;
		}
		printf("%zu memory blocks. ",cnt);
		if(showMCBState)printf("(%zu/%zu)\n\n",cnt+1,max_MCBs);
		else printf("\n\n");
	}
	void dumpToFile(const char *filename){ // 转储全部内存
		_memleak_util::LockGuard lock_guard(_mutex);
		FILE *file=fopen(filename,"wb");
		if(file==nullptr){
			throw std::runtime_error(strerror(errno));
		}
		fwrite(memStart,sizeof(char),total,file);
		fclose(file);
	}
	size_t total; // 总大小
	size_t used; // 已用空间
	void *memStart; // 内存起始地址
	size_t max_MCBs; // 最大MCB个数

private:
	MemCtrlBlock *mcbHead;
	MemCtrlBlock *MCB_mem; // 存放MCB的数组
	size_t MCB_id; // 下一个空MCB的索引
	std::recursive_mutex *_mutex;

	MemCtrlBlock *get_new_MCB(){
		if(!MCB_mem[MCB_id].used){ // 判断下一个MCB是否可用
			MCB_mem[MCB_id].used=true;
			size_t cur=MCB_id;
			MCB_id=(MCB_id+1)%max_MCBs;
			return &MCB_mem[cur];
		}
		size_t new_id=(MCB_id+1)%max_MCBs;
		while(new_id!=MCB_id){ // 使用约瑟夫环，遍历整个MCB_mem
			if(!MCB_mem[new_id].used){
				MCB_mem[new_id].used=true;
				MCB_id=(new_id+1)%max_MCBs;
				return &MCB_mem[new_id];
			}
			new_id=(new_id+1)%max_MCBs;
		}
		throw std::runtime_error("Memory for MCBs is full.");
	}
	void delete_MCB(MemCtrlBlock *mcb){
		size_t index=mcb-MCB_mem;
		if(!MCB_mem[index].used){
			throw std::runtime_error("Cannot delete an unused MCB.");
		}
		MCB_mem[index].start=MCB_mem[index].size=0;
		MCB_mem[index].next=nullptr;
		MCB_mem[index].used=false;
	}
};
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
	void dump_to_file(const char *filename){
		if(_memleak::mem==nullptr)
            throw std::runtime_error("Must call setup_mem() before calling dump_to_file(filename)");

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
	}

    void setup_mem_noinit(size_t size,size_t mcb_count=0){ // 初始化自定义的内存分配器
	    // mcb_count: 最大允许分配的内存块数量，默认是8192。如果为0，使用DEFAULT_MCB_COUNT
		if(_memleak::mem)
			throw std::runtime_error("Already set up when calling setup_mem()");

		if(mcb_count==0)mcb_count=DEFAULT_MCB_COUNT;
        _memleak::mem=new MemMgr(size,nullptr,mcb_count);
        atexit(_atexit_showInfo);
    }
	extern "C"{
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
namespace _overload_std{
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
#ifndef DISABLE_OVERWRITING_STD
// 不启用MEMLEAK_MACRO_WITH_ARG时，必须先导入标准库，再导入memleak.h，避免标准库的命名冲突
// （如标准库中的using std::malloc被替换为using std::_overload_std::malloc）。
// 启用MEMLEAK_MACRO_WITH_ARG时，可以先导入memleak.h再导入标准库，使得标准库的函数被覆盖。
// 但函数指针的用法，如decltype(malloc)会失效。
#ifndef MEMLEAK_MACRO_WITH_ARG
#define malloc _overload_std::malloc
#define calloc _overload_std::calloc
#define realloc _overload_std::realloc
#define free _overload_std::free
#define strdup _overload_std::strdup
#define wcsdup _overload_std::wcsdup
#else
#define malloc(size) _overload_std::malloc((size))
#define calloc(num,size) _overload_std::calloc((num),(size))
#define realloc(mem,size) _overload_std::realloc((mem),(size))
#define free(mem) _overload_std::free((mem))
#define strdup(str) _overload_std::strdup((str))
#define wcsdup(str) _overload_std::wcsdup((str))
#endif
void *operator new(size_t size){
	if(size==0)size=1;
	void *result=malloc(size);
	if(result==nullptr)
		throw std::bad_alloc();
	return result;
}
void *operator new[](size_t size){
	return operator new(size);
}
void operator delete(void *mem) noexcept {
	free(mem);
}
void operator delete[](void *mem) noexcept {
	free(mem);
}
void operator delete(void *mem,size_t size) noexcept {
	free(mem);
}
void operator delete[](void *mem,size_t size) noexcept {
	free(mem);
}
#endif
#undef _EXCEPTION_DEF