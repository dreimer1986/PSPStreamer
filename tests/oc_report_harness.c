#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef int SceUID;
#define PSP_O_WRONLY 1
#define PSP_O_CREAT 2
#define PSP_O_APPEND 4
#define PSP_O_TRUNC 8
static char data[1024];
static int size, opened, closed, failure, chunk=3;
static int sceIoOpen(const char *path,int flags,int mode) {
    assert(!strcmp(path,"events.log"));assert(mode==0600);
    assert((flags&(PSP_O_WRONLY|PSP_O_CREAT))==(PSP_O_WRONLY|PSP_O_CREAT));
    if(failure==1)return -42;
    if(flags&PSP_O_TRUNC)size=0;
    else assert(flags&PSP_O_APPEND);
    opened++;return 7;
}
static int sceIoWrite(int fd,const char *text,int length) {
    assert(fd==7);
    if(failure==2)return -43;
    if(failure==3)return 0;
    if(length>chunk)length=chunk;
    assert(size+length<(int)sizeof(data));
    memcpy(data+size,text,length);size+=length;data[size]=0;
    return length;
}
static int sceIoClose(int fd) {assert(fd==7);closed++;return failure==4?-44:0;}
#include "report_io.h"
int main(void) {
    assert(oc_report_write("events.log","session1\n",9,1)==0);
    assert(oc_report_write("events.log","session2\n",9,1)==0);
    assert(!strcmp(data,"session1\nsession2\n") && opened==closed);
    assert(oc_report_write("events.log","latest",6,0)==0);
    assert(!strcmp(data,"latest"));
    failure=1;assert(oc_report_write("events.log","x",1,1)==-42);
    failure=2;assert(oc_report_write("events.log","x",1,1)==-43);
    failure=3;assert(oc_report_write("events.log","x",1,1)==-1);
    failure=4;assert(oc_report_write("events.log","x",1,1)==-44);
    assert(opened==closed);
    return 0;
}
