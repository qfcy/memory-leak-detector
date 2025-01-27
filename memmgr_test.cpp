#include<iostream>
#include<cstdlib>
#include<cstring>
#include<cstdio>
#define DISABLE_OVERWRITING_STD
#define MEMLEAK_NO_EXTERNAL // 不使用外部导出的实现
#include "memleak.h"
using namespace std;
using namespace memleak;

MemMgr **get_max_memory(size_t *memmgr_count){ // 申请最大内存
	MemMgr **mems=(MemMgr **)malloc(sizeof(MemMgr *)*1024);
	int level = 30; // 1<<30
	size_t i = 0;
	while(level>=0){
		try{
			mems[i]=new MemMgr(1<<level,nullptr,1);
			memset(mems[i]->memStart,0,mems[i]->total);
			i++;
		}catch(bad_alloc){
			level-=1;
		}catch(runtime_error){
			level-=1;
		}
	}
	*memmgr_count=i;
	return mems;
}
MemMgr *get_max_continuous_memory(){ // 申请最大的连续内存
	int level = 30; // 1<<30
	size_t total = 0;
	char *new_mem,*mem=NULL;
	while(level>=0){
		new_mem=(char *)std::realloc(mem,(total+(1<<level))*sizeof(char));
		if(new_mem==NULL){
			level-=1;
		}else{
			mem=new_mem;
			total+=1<<level;
		}
	}
	return new MemMgr(total,mem);
}

static void test_malloc_free(){
	MemMgr *mem=new MemMgr(1<<20);
	char *s=(char *)mem->malloc(sizeof(char)*50);
	if(s==NULL){
		cerr<<"Memory allocation error"<<endl;
		delete mem;return;
	}
	int *a=(int *)mem->malloc(sizeof(int)*2);
	a[0]=1;a[1]=2;
	mem->showInfo();
	strcpy(s,"Hello world.");
	cout<<s<<" "<<a[0]<<" "<<a[1]<<endl;
	mem->free(s);
	mem->showInfo();
	int *b=(int *)mem->malloc(sizeof(int));
	*b=2147483647;
	cout<<s<<endl;//s仍然在申请的内存上，仍然可访问，但数据可能被覆盖
	mem->showInfo();
	mem->free(b);
	mem->showInfo();
	mem->free(a);
	mem->showInfo();
	//mem->dumpToFile("memdump.bin");
	delete mem;
}
static void test_realloc(){
	MemMgr *mem=new MemMgr(1<<20);
	//测试1
	char *block1=(char *)mem->malloc(sizeof(char)*32);
	char *block2=(char *)mem->malloc(sizeof(char)*256);
	mem->free(block1);
	int *a=(int *)mem->malloc(sizeof(int)*2);
	mem->showInfo();
	a=(int *)mem->realloc(a,sizeof(int)*6); //后部可用空间足够
	mem->showInfo();
	for(size_t i=0;i<6;i++) a[i]=i;

	a=(int *)mem->realloc(a,sizeof(int)*50); //后部可用空间不足
	mem->showInfo();
	for(size_t i=6;i<50;i++) a[i]=i;
	for(size_t i=0;i<50;i++) cout<<a[i]<<" "; // 检查realloc中0~5索引的内存是否被复制
	cout<<endl<<endl;
	mem->free(a);mem->free(block2);
	//mem->dumpToFile("memdump_realloc_test1.bin");

	//测试2
	block1=(char *)mem->malloc(sizeof(char)*(1<<17));
	block2=(char *)mem->malloc(sizeof(char)*(1<<17)*3);
	char *block3=(char *)mem->malloc(sizeof(char)*(1<<19));
	mem->realloc(block1,0); // 释放block1
	mem->showInfo();
	strcpy(block2,"Hello MemMgr!");
	char *new_block2=(char *)mem->realloc(block2,1<<19);//后部没有可用空间，但前面空闲
	cout<<new_block2<<endl;
	mem->showInfo();
	mem->free(new_block2);mem->free(block3);
	//mem->dumpToFile("memdump_realloc_test2.bin");
	delete mem;
}
static void test_calloc(){
	MemMgr *mem=new MemMgr(1<<20);
	char *all=(char *)mem->malloc(sizeof(char)*(1<<20));
	mem->showInfo(true);
	memset(all,255,sizeof(char)*(1<<20)); //初始化为0FFh
	mem->free(all);
	int *a=(int *)mem->calloc(50,sizeof(int));
	for(size_t i=0;i<55;i++){ // 使用55而不是50，测试读取后部的内存
		cout<<a[i]<<" ";
	}
	cout<<endl;
	//mem->dumpToFile("memdump_calloc_test.bin");
	mem->free(a);
	delete mem;
}
static void test_max_mcb(){
	MemMgr *mem=new MemMgr(1<<20,nullptr,32);
	char *arr;
	try{
		while((arr=(char *)mem->malloc(1))!=nullptr);
	} catch(runtime_error){}
	mem->showInfo(true);
}
static void test_max_memory(){
	size_t n;char *result_buffer=new char[20];
	MemMgr **mems=get_max_memory(&n);
	size_t total=0;
	for(size_t i=0;i<n;i++){
		mems[i]->showInfo(true);
		total+=mems[i]->total+mems[i]->max_MCBs*sizeof(MemCtrlBlock);
	}
	cout<<"Maximum memory size: "<<
		convert_size(total,result_buffer)<<" ("<<total<<" B)"<<endl;
	for(size_t i=0;i<n;i++){
		delete mems[i];
	}
	free(mems);
}
static void test_max_continuous_memory(){
	MemMgr *mem=get_max_continuous_memory();
	mem->showInfo(true);
	delete mem;
}
int main(){
	test_malloc_free();
	//test_realloc();
	//test_calloc();
	//test_max_mcb();
	//test_max_memory();
	//test_max_continuous_memory();
	return 0;
}