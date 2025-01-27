memleak.h是一个轻量级的内存泄漏检测库，通过在内部实现一个内存池，实现自己的`malloc`、`calloc`、`realloc`、`free`、`new`和`delete`函数并重载标准库的函数，实现了内存泄漏检测功能，支持c和c++调用。  
只需要在开头导入memleak.h，并调用`setup_mem`和`set_leak_detect`函数，就能启用退出程序时输出内存泄漏信息。  
`setup_mem`函数还能设置内存大小，模拟小内存环境，测试程序能否处理内存不足的情况。  
此外，库还提供了`show_mem_info()`和`dump_mem_to_file()`，用于调试内存。  
和Valgrind、Dr.Memory等复杂工具不同，memleak.h库只需要简单地嵌入C/C\+\+代码中，使用便捷。  

## 构建以及库的导入

memleak.h支持单头文件(在`single_header_version`目录)、静态链接和动态链接3种方式的导入。  
#### 单头文件
直接复制`single_header_version`目录中的`memleak.h`到自己项目的目录下，即可。仅支持C++项目。  

#### 静态链接
静态链接同时支持C和C++项目。  
首先编译memleak库的源代码`memleak.cpp`:
```bash
g++ -c memleak.cpp -o bin/memleak_static.o -O2 -Wall -Iinclude
```

然后编译C文件生成目标文件(也可以用C++):
```bash
gcc -c memleak_ctest.c -o bin/memleak_ctest.o -O2 -Wall -Iinclude
```

最后链接。由于gcc不支持c++编译的`memleak_static.o`的格式，这一步需要用g\+\+而不是gcc。
```bash
g++ bin/memleak_ctest.o bin/memleak_static.o -o bin/memleak_ctest_static -s -Wall -Iinclude
```

#### 动态链接
动态链接也同时支持C和C++项目。  
首先编译出DLL文件(linux下用.so):
```bash
g++ memleak.cpp -o bin/memleak.dll -s -O2 -Wall -DMEMLEAK_DLL_EXPORT -shared -Wl,--out-implib,lib\libmemleak.a -Iinclude
```
然后链接这个DLL:
```bash
gcc memleak_ctest.c -o bin/memleak_ctest -s -Wall -Iinclude -Llib -lmemleak
```

完整的编译和链接这个库的示例，详见项目中的文件`build.bat`。

## 使用方法

- `void setup_mem(size_t size,size_t mcb_count,unsigned char initVal)`:  
size: 总内存大小。  
mcb_count: 能分配的最多内存块数量，为0则表示用默认值(8192)。  
initVal: 内存初始化的字节，一般为0，但如果要调试使用未初始化变量，建议用0xcd等特殊值。  
- `void setup_mem_noinit(size_t size,size_t mcb_count)`:  
这个版本不初始化内存，参数用法和setup_mem相同。  
- `void set_leak_detect(bool enabled)`:  
设置程序退出时是否显示内存泄漏信息，如果为`true`，则显示。  
在C中使用时需要导入`stdbool.h`。  
- `void shutdown()`:  
释放调用`setup_mem()`时分配的内存，关闭自定义的内存分配器，以及泄漏检测。  

- `void show_mem_info()`:  
在标准输出显示当前内存信息，如分配的内存块数量，以及大小等。格式示例：  
```
Total: 1048576B Used: 140B Free: 1048436B
   Start - Size
0. 0 - 12
1. 12 - 40
2. 52 - 4
3. 56 - 80
4. 136 - 4
5 memory blocks.
```
- `void dump_mem_to_file(const char *filename)`
导出全部内存到二进制文件`filename`，文件大小为调用`setup_mem()`时申请的内存大小。  

完整的使用示例参见`memleak_ctest.c`和`memleak_test.cpp`。对于memleak.h库的内部实现，如`MemMgr`内存池类的使用示例，参见`memmgr_test.cpp`。  

#### 标准库函数的重载细节

对于C++，memleak.h会重载默认的`operator new`、`operator delete`和`new[]`、`delete[]`，以便于使整个STL，如`vector`、`map`都使用自定义的`new`,`delete`分配内存。  
对于C，memleak.h会用宏定义重载`malloc`,`calloc`,`realloc`,`free`,以及底层调用了`malloc`的`strdup`,`wcsdup`函数。  
未启用(未调用`setup_mem()`)时，重载的内存分配函数会直接调用标准库的函数，如`malloc`调用`std::malloc`，不影响正常的内存分配。  
启用(调用`setup_mem()`)之后，重载的函数会调用自定义的内存分配器。调用`shutdown()`即可恢复为用标准库的函数。  

可以用宏`DISABLE_OVERWRITING_STD`避免重载标准库。  
如果定义了`MEMLEAK_MACRO_WITH_ARG`，则会定义带参数的宏，如`#define malloc(size) _override_std_malloc((size))`，否则用默认的`#define malloc _override_std_malloc`。  
如果定义了`MEMLEAK_DLL_EXPORT`，则会导出函数到动态库。  
此外，如果要利用memleak.h的内部实现，如`MemMgr`类等，需要定义宏`MEMLEAK_NO_EXTERNAL`，也就是不用外部的函数。memleak.h的内部实现存放在`memleak`命名空间中，可以在C++代码中导入。    

#### 自定义函数的边界条件和异常处理

自定义的`malloc`,`calloc`,`realloc`,`free`等函数作为标准库函数的重载，与标准库函数的行为一致。  

自定义的`malloc`,`calloc`,`realloc`函数在内存不足时返回`nullptr`。  
如果`realloc`和`free`传入了错误的内存地址，则会抛出`ReallocError`异常，对于`free`则是抛出`DoubleFreeError`。在C++中可以捕获这两个异常，在C中则会直接终止程序。  
`malloc`和`calloc`传入的总大小如果是0，则会返回`nullptr`。`realloc`如果传入的大小是0，则会释放传入的内存指针。  
`free`支持传入`nullptr`作为参数。  