/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PSP_FTP_PATH_H
#define PSP_FTP_PATH_H
#include <string.h>
#define FTP_PATH 512
/* FTP paths are virtual. Reject device prefixes, backslashes and control
 * bytes before normalizing; the caller ALWAYS adds ms0: itself. */
static int ftp_path(const char *cwd,const char *arg,char out[FTP_PATH]){
    char joined[FTP_PATH];size_t a=strlen(arg),c=strlen(cwd);
    if(a+c+2>=sizeof(joined))return -1;
    for(const unsigned char *p=(const void *)arg;*p;p++)
        if(*p<32 || *p==127 || *p==':' || *p=='\\')return -1;
    if(*arg=='/')strcpy(joined,arg);
    else {strcpy(joined,cwd);strcat(joined,"/");strcat(joined,arg);}
    size_t used=1;out[0]='/';out[1]=0;
    char *p=joined;
    while(*p){
        while(*p=='/')p++;
        char *begin=p;while(*p && *p!='/')p++;
        size_t n=p-begin;if(!n || (n==1 && *begin=='.'))continue;
        if(n==2 && begin[0]=='.' && begin[1]=='.'){
            if(used>1){while(used>1 && out[used-1]!='/')used--;if(used>1)used--;out[used]=0;}
            continue;
        }
        if(used>1)out[used++]='/';
        if(used+n>=FTP_PATH)return -1;
        memcpy(out+used,begin,n);used+=n;out[used]=0;
    }
    return 0;
}
#endif
