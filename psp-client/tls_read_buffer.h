#ifndef PSP_TLS_READ_BUFFER_H
#define PSP_TLS_READ_BUFFER_H
#include <stddef.h>
#include <string.h>
/* Read ahead only for tiny TLS header reads. Large records go directly into
 * Mbed TLS's existing buffer. No second record-sized allocation or extra wait. */
typedef struct { unsigned char bytes[2048]; size_t begin,end; } TlsReadBuffer;
typedef int (*TlsReadRaw)(void *,unsigned char *,size_t);
static int tls_read_buffer(TlsReadBuffer *b,TlsReadRaw read_raw,void *ctx,
                           unsigned char *out,size_t size) {
    if(!size)return 0;
    if(b->begin==b->end) {
        b->begin=b->end=0;
        if(size>=64)return read_raw(ctx,out,size);
        int n=read_raw(ctx,b->bytes,sizeof(b->bytes));
        if(n<=0)return n;
        b->end=(size_t)n;
    }
    size_t count=b->end-b->begin;if(count>size)count=size;
    memcpy(out,b->bytes+b->begin,count);b->begin+=count;
    return (int)count;
}
#endif
