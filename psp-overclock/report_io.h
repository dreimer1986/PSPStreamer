/* SPDX-License-Identifier: MIT */
#ifndef STREAMER_OC_REPORT_IO_H
#define STREAMER_OC_REPORT_IO_H
static int oc_report_flush(const char *path) {
    char device[16];
    const char *colon=strchr(path,':');
    if(!colon || colon-path>=(int)sizeof(device)-1)return -1;
    int n=(int)(colon-path)+1;
    memcpy(device,path,n);device[n]=0;
    return sceIoSync(device,0);
}
/* Called only by the worker, never by a power callback. Close each record so
 * prior events do not depend on an orderly module_stop callback. */
static int oc_report_write(const char *path, const char *text, int length, int append)
{
    SceUID fd=sceIoOpen(path,PSP_O_WRONLY|PSP_O_CREAT|
                       (append?PSP_O_APPEND:PSP_O_TRUNC),0600);
    if(fd<0)return fd;
    int written=0, result=0;
    while(written<length) {
        int n=sceIoWrite(fd,text+written,length-written);
        if(n<=0){result=n<0?n:-1;break;}
        written+=n;
    }
    int closed=sceIoClose(fd);
    return result<0?result:closed;
}
#endif
