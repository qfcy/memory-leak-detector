#include <iostream>
#include <memory>
#include <vector>
#include "memleak.h"
using namespace std;

int main(){
    setup_mem(1<<20,0,0); // 1MB，使用默认内存块数量，并初始化为0
    set_leak_detect(true);

    char *str=strdup("Heap memory");
    int *a=(int *)malloc(sizeof(int)*10);
    int *b=new int;
    int *c=new int[20];
    unique_ptr<int> u=make_unique<int>(1);
    show_mem_info();
    free(str);free(a);
    delete b;delete[] c;u.reset();

    vector<int> v;
    for(int i=0;i<129;i++){ // 略高于512字节
        v.push_back(10);
    }
    show_mem_info();
    dump_mem_to_file("memory.bin");
    c=new int[20]; // 测试内存泄漏检测
    return 0;
}