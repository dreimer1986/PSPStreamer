#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include "../xbox-client/visual_copy.h"
int main(void){
    unsigned char src[2200],dst[2200],reference[2200];
    for(unsigned i=0;i<sizeof(src);i++)src[i]=(unsigned char)(i*37+11);
    for(unsigned a=0;a<16;a++)for(unsigned b=0;b<16;b++)for(unsigned n=0;n<=2048;n+=n<80?1:31){
        memset(dst,0xa5,sizeof(dst));memset(reference,0xa5,sizeof(reference));
        memcpy(reference+b,src+a,n);xbox_visual_copy(dst+b,src+a,n);
        assert(!memcmp(dst,reference,sizeof(dst)));
    }
    /* Inline assembly is not ASan-instrumented: real inaccessible pages catch
     * overread/overwrite at every tail length, including misaligned pointers. */
    size_t page=(size_t)sysconf(_SC_PAGESIZE);
    unsigned char *s=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    unsigned char *d=mmap(NULL,page*2,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    assert(s!=MAP_FAILED&&d!=MAP_FAILED);memset(s,0x79,page);
    assert(!mprotect(s+page,page,PROT_NONE)&&!mprotect(d+page,page,PROT_NONE));
    for(unsigned n=0;n<1024;n++){xbox_visual_copy(d+page-n,s+page-n,n);assert(!memcmp(d+page-n,s+page-n,n));}
    munmap(s,page*2);munmap(d,page*2);
    puts("SSE1 copy: offsets, all tails, canaries and inaccessible-page bounds passed");
}
