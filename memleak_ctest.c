#include <stdlib.h>
#include "memleak.h"

int main(){
    setup_mem(1<<20,0,0); // 1MB，使用默认内存块数量，并初始化为0
    set_leak_detect(true);

    int *a=(int *)calloc(sizeof(int),0);
    //free(a+1); // DoubleFreeError
    return 0;
}