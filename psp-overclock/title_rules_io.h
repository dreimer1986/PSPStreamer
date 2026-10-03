/* SPDX-License-Identifier: MIT */
#ifndef PSP_TITLE_RULES_IO_H
#define PSP_TITLE_RULES_IO_H
#include "title_rules.h"
/* Return 0 absent/no match, positive section line on match, negative on error.
 * Commit only after the entire file validates. Title IDs beat paths; first tie wins. */
static int title_rules_load(const char *file,const char *path,
                            const TitleRuleKey *keys,int count,int *values)
{
    if(count<1||count>TITLE_RULE_MAX_KEYS)return -1;
    char id[17]={0},line[384],chunk[256];
    SceGameInfo *info=sceKernelGetGameInfo();
    if(info){memcpy(id,info->title_id,16);id[16]=0;}
    int fd=sceIoOpen(file,PSP_O_RDONLY,0);
    if(fd<0)return fd==(int)0x80010002?0:-1;
    TitleRules r={0};int used=0,total=0,n,result=0;
    memcpy(r.base,values,count*sizeof(int));memcpy(r.values,r.base,sizeof(r.base));
    while((n=sceIoRead(fd,chunk,sizeof(chunk)))>0) {
        total+=n;if(total>65536){result=-1;break;}
        for(int i=0;i<n;i++) {
            if(chunk[i]=='\n') {
                line[used]=0;used=0;
                if(title_rules_line(&r,line,id,path?path:"",keys,count)<0){result=-r.error;break;}
            } else if(!chunk[i] || used==(int)sizeof(line)-1){result=-(r.line+1);break;}
            else line[used++]=chunk[i];
        }
        if(result<0)break;
    }
    int close_result=sceIoClose(fd);
    if(n<0||close_result<0)return -1;
    if(result<0)return result;
    if(used){line[used]=0;if(title_rules_line(&r,line,id,path?path:"",keys,count)<0)return -r.error;}
    title_rules_finish(&r);
    memcpy(values,r.values,count*sizeof(int));
    return r.selected_line;
}
#endif
