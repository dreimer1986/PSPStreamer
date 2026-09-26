#include <assert.h>
#include <stddef.h>
typedef struct {int destroyed;} TlsConnection;
static TlsConnection first,other;
static TlsConnection *connections[8];
static int lock=1,held,closes,close_result;
static TlsConnection *lookup(int fd){return fd==3?connections[2]:NULL;}
static void sceKernelWaitSema(int id,int n,void *p){assert(id==1 && n==1 && !p && !held);held=1;}
static void sceKernelSignalSema(int id,int n){assert(id==1 && n==1 && held);held=0;}
static void destroy(TlsConnection *c){assert(!held && !connections[2]);c->destroyed++;}
static int sceNetInetCloseWithRST(int fd){
    assert(!held && (fd==3 || fd==10));
    if(fd==3)assert(first.destroyed==1 && !connections[2]);
    assert(connections[5]==&other && !other.destroyed);
    closes++;return close_result;
}
/* ACTUAL_CLOSE */
int main(void){
    connections[2]=&first;connections[5]=&other;
    assert(tls_close(3)==0 && closes==1 && first.destroyed==1);
    assert(tls_close(10)==0 && closes==2);
    close_result=(int)0x80410005U;
    assert(tls_close(10)==close_result && closes==3); /* no unsafe retry */
    assert(!other.destroyed && connections[5]==&other);
    return 0;
}
