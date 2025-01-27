#pragma once

#ifdef MEMLEAK_DLL_EXPORT // 如果启用导出动态库
#define DLLEXPORT __attribute__((dllexport))
#else
#define DLLEXPORT
#endif

#ifndef MEMLEAK_NO_EXTERNAL
#ifndef __cplusplus
#include <stdbool.h>
#endif

#ifdef __cplusplus
extern "C"{ // 兼容C
#endif

// 外部函数，通过静态或动态链接导入
extern void *_override_std_malloc(size_t size);
extern void *_override_std_calloc(size_t num, size_t size);
extern void *_override_std_realloc(void *mem, size_t size);
extern void _override_std_free(void *mem);
extern char *_override_std_strdup(const char *str);
extern wchar_t *_override_std_wcsdup(const wchar_t *str);

extern void setup_mem(size_t size,size_t mcb_count,unsigned char initVal);
extern void setup_mem_noinit(size_t size,size_t mcb_count);
extern void set_leak_detect(bool enabled);
extern void shutdown();
extern void show_mem_info();
extern void dump_mem_to_file(const char *filename);

#ifdef __cplusplus
} // extern "C"
#endif

#else
#ifndef __cplusplus
#error "Must use C++ for MEMLEAK_NO_EXTERNAL"
#endif

// 定义具体在memleak.cpp实现的类和函数
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
	LockGuard(std::recursive_mutex *mutex);
	~LockGuard();
	std::recursive_mutex *mutex;
};
}
namespace memleak{
_EXCEPTION_DEF(ReallocError,std::runtime_error)
_EXCEPTION_DEF(DoubleFreeError,std::runtime_error)
const size_t DEFAULT_MCB_COUNT=8192;
struct MemCtrlBlock {
    size_t start;
    size_t size;
    bool used;
    MemCtrlBlock *next;
    MemCtrlBlock();
};

inline size_t ptr_diff(void *ptr1, void *ptr2);
char *convert_size(size_t size, char *result_buffer = nullptr);
class MemMgr {
public:
    MemMgr(size_t size, void *mem_start = nullptr, size_t mcb_count = DEFAULT_MCB_COUNT, bool use_mutex = true);
    ~MemMgr();
    void *malloc(size_t size);
    void *calloc(size_t num, size_t size);
    void *realloc(void *mem, size_t size);
    void free(void *mem);
    void showInfo(bool showMCBState = false);
    void dumpToFile(const char *filename);

    size_t total; // 总大小
    size_t used; // 已用空间
    void *memStart; // 内存起始地址
    size_t max_MCBs; // 最大MCB个数

private:
    MemCtrlBlock *mcbHead;
    MemCtrlBlock *MCB_mem; // 存放MCB的数组
    size_t MCB_id; // 下一个空MCB的索引
    std::recursive_mutex *_mutex;

    MemCtrlBlock *get_new_MCB();
    void delete_MCB(MemCtrlBlock *mcb);
};
}

namespace memleak{
extern "C"{
void _showLeakInfo();
void _atexit_showInfo();

DLLEXPORT void show_mem_info();
DLLEXPORT void dump_mem_to_file(const char *filename);

DLLEXPORT void setup_mem(size_t size,size_t mcb_count,unsigned char initVal);
DLLEXPORT void setup_mem_noinit(size_t size,size_t mcb_count);
DLLEXPORT void set_leak_detect(bool enabled);
DLLEXPORT void shutdown();
}
}
namespace _override_std{
    inline void *malloc(size_t size);
    inline void *calloc(size_t num,size_t size);
    inline void *realloc(void *mem,size_t size);
    inline void free(void *mem);
	inline char *strdup(const char *str);
	inline wchar_t *wcsdup(const wchar_t *str);
}
extern "C"{
DLLEXPORT void *_override_std_malloc(size_t size);
DLLEXPORT void *_override_std_calloc(size_t num, size_t size);
DLLEXPORT void *_override_std_realloc(void *mem, size_t size);
DLLEXPORT void _override_std_free(void *mem);
DLLEXPORT char *_override_std_strdup(const char *str);
DLLEXPORT wchar_t *_override_std_wcsdup(const wchar_t *str);
}

#undef _EXCEPTION_DEF
#endif

// 重载标准库函数
#ifndef DISABLE_OVERWRITING_STD
// 不启用MEMLEAK_MACRO_WITH_ARG时，必须先导入标准库，再导入memleak.h，避免标准库的命名冲突
// （如标准库中的using std::malloc被替换为using std::_override_std::malloc）。
// 启用MEMLEAK_MACRO_WITH_ARG时，可以先导入memleak.h再导入标准库，使得标准库的函数被覆盖。
// 但函数指针的用法，如decltype(malloc)会失效。
#ifndef MEMLEAK_MACRO_WITH_ARG
#define malloc _override_std_malloc
#define calloc _override_std_calloc
#define realloc _override_std_realloc
#define free _override_std_free
#define strdup _override_std_strdup
#define wcsdup _override_std_wcsdup
#else
#define malloc(size) _override_std_malloc((size))
#define calloc(num,size) _override_std_calloc((num),(size))
#define realloc(mem,size) _override_std_realloc((mem),(size))
#define free(mem) _override_std_free((mem))
#define strdup(str) _override_std_strdup((str))
#define wcsdup(str) _override_std_wcsdup((str))
#endif

#ifdef __cplusplus
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

#endif