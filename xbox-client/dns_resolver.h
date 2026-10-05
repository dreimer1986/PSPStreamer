/* lwIP callbacks own a reference after cancellation: no dangling stack data,
 * unbounded resolver threads, or uncancellable getaddrinfo on the GUI thread. */
#include <lwip/dns.h>
#include <lwip/tcpip.h>
typedef struct {SDL_atomic_t refs,done;ip_addr_t address;int ok;char name[64];} XboxDns;
static SDL_atomic_t dns_outstanding;
static void dns_release(XboxDns *q){if(SDL_AtomicAdd(&q->refs,-1)==1){free(q);SDL_AtomicAdd(&dns_outstanding,-1);}}
static void dns_found(const char *name,const ip_addr_t *address,void *arg){
    (void)name;XboxDns *q=arg;if(address){q->address=*address;q->ok=1;}
    SDL_AtomicSet(&q->done,1);dns_release(q);
}
static void dns_begin(void *arg){
    XboxDns *q=arg;ip_addr_t address;
    err_t result=dns_gethostbyname_addrtype(q->name,&address,dns_found,q,LWIP_DNS_ADDRTYPE_IPV4);
    if(result!=ERR_INPROGRESS)dns_found(q->name,result==ERR_OK?&address:NULL,q);
}
static int xbox_resolve(const char *name,struct in_addr *address,SDL_atomic_t *cancel){
    if(inet_aton(name,address))return 1;
    if(!xbox_hostname_valid(name)||SDL_AtomicAdd(&dns_outstanding,1)>=4){
        if(xbox_hostname_valid(name))SDL_AtomicAdd(&dns_outstanding,-1);return 0;
    }
    XboxDns *q=calloc(1,sizeof(*q));if(!q){SDL_AtomicAdd(&dns_outstanding,-1);return 0;}
    snprintf(q->name,sizeof(q->name),"%s",name);SDL_AtomicSet(&q->refs,2);
    if(tcpip_try_callback(dns_begin,q)!=ERR_OK){dns_release(q);dns_release(q);return 0;}
    Uint32 started=SDL_GetTicks();
    while(!SDL_AtomicGet(&q->done)&&!SDL_AtomicGet(cancel)&&SDL_GetTicks()-started<8000)SDL_Delay(10);
    int ok=SDL_AtomicGet(&q->done)&&q->ok&&!SDL_AtomicGet(cancel);
    if(ok)address->s_addr=ip4_addr_get_u32(ip_2_ip4(&q->address));dns_release(q);return ok;
}
