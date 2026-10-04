/* GPL-2.0-or-later. Bounded HTTP/1.0 transport for the trusted LAN preview. */
typedef struct { int fd, status; unsigned at, size; unsigned char data[8192]; SDL_atomic_t *cancel; } Http;
static char host[64], password[129];
static unsigned port=8091;
static int output_height=480;

static int config(void) {
    FILE *f=fopen("D:\\server.cfg","r"); char line[256];
    if (!f) return 0;
    while (fgets(line,sizeof(line),f)) {
        line[strcspn(line,"\r\n")]=0;
        if (!strncmp(line,"host=",5)) snprintf(host,sizeof(host),"%s",line+5);
        else if (!strncmp(line,"password=",9)) snprintf(password,sizeof(password),"%s",line+9);
        else if (!strncmp(line,"port=",5)) port=(unsigned)strtoul(line+5,NULL,10);
        else if (!strncmp(line,"output_height=",14)) output_height=atoi(line+14);
    }
    fclose(f); struct in_addr a;
    return port && port<=65535 && inet_aton(host,&a);
}
static void base64(const unsigned char *p,size_t n,char *o) {
    static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    while (n) {size_t z=n<3?n:3; unsigned v=(unsigned)p[0]<<16;
        if(z>1)v|=(unsigned)p[1]<<8; if(z>2)v|=p[2];
        *o++=alphabet[v>>18]; *o++=alphabet[v>>12&63];
        *o++=z>1?alphabet[v>>6&63]:'='; *o++=z>2?alphabet[v&63]:'='; p+=z;n-=z;
    } *o=0;
}
static int url_encode(const char *p,char *out,size_t cap) {
    static const char hex[]="0123456789ABCDEF"; size_t n=0;
    while(*p) {unsigned char c=*p++;
        if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||strchr("-_.~",c)) {
            if(n+1>=cap)return 0;out[n++]=c;
        } else {if(n+3>=cap)return 0;out[n++]='%';out[n++]=hex[c>>4];out[n++]=hex[c&15];}
    } out[n]=0;return 1;
}
static int net_ready(Http *h,int write,Uint32 started,unsigned timeout) {
    while(!SDL_AtomicGet(h->cancel) && SDL_GetTicks()-started<timeout) {
        fd_set s;FD_ZERO(&s);FD_SET(h->fd,&s);struct timeval t={0,100000};
        int r=select(h->fd+1,write?NULL:&s,write?&s:NULL,NULL,&t);
        if(r>0)return 1;if(r<0)return 0;
    } return 0;
}
static void http_close(Http *h) {if(h->fd>=0){closesocket(h->fd);h->fd=-1;}}
static int http_read(Http *h,void *dest,unsigned length) {
    if(h->at==h->size) {
        if(!net_ready(h,0,SDL_GetTicks(),180000))return -1;
        int n=recv(h->fd,h->data,sizeof(h->data),0);
        if(n<=0)return n;h->at=0;h->size=n;
    }
    unsigned n=h->size-h->at;if(n>length)n=length;
    memcpy(dest,h->data+h->at,n);h->at+=n;return n;
}
static int http_exact(Http *h,void *dest,unsigned length) {
    unsigned at=0;while(at<length){int n=http_read(h,(char*)dest+at,length-at);if(n<=0)return 0;at+=n;}return 1;
}
static int http_open(Http *h,const char *path,SDL_atomic_t *cancel) {
    memset(h,0,sizeof(*h));h->fd=-1;h->cancel=cancel;
    char credentials[140],encoded[192],request[12500],header[8192];
    snprintf(credentials,sizeof(credentials),"psp:%s",password);
    base64((unsigned char*)credentials,strlen(credentials),encoded);
    int size=snprintf(request,sizeof(request),"GET %s HTTP/1.0\r\nHost: %s:%u\r\nAuthorization: Basic %s\r\nConnection: close\r\n\r\n",path,host,port,encoded);
    if(size<0||size>=(int)sizeof(request))return 0;
    h->fd=socket(AF_INET,SOCK_STREAM,0);if(h->fd<0)return 0;
    unsigned long nonblock=1;ioctl(h->fd,FIONBIO,&nonblock);
    struct sockaddr_in a={0};a.sin_family=AF_INET;a.sin_port=htons(port);inet_aton(host,&a.sin_addr);
    Uint32 start=SDL_GetTicks();
    if(connect(h->fd,(struct sockaddr*)&a,sizeof(a))<0&&errno!=EINPROGRESS)goto fail;
    if(!net_ready(h,1,start,10000))goto fail;
    int error=0;socklen_t len=sizeof(error);
    if(getsockopt(h->fd,SOL_SOCKET,SO_ERROR,&error,&len)||error)goto fail;
    for(int at=0;at<size;) {if(!net_ready(h,1,start,10000))goto fail;
        int n=send(h->fd,request+at,size-at,0);if(n<=0)goto fail;at+=n;
    }
    unsigned n=0;
    while(n<sizeof(header)-1) {
        if(!http_exact(h,header+n,1))goto fail;n++;header[n]=0;
        if(n>=4&&!memcmp(header+n-4,"\r\n\r\n",4))break;
    }
    if(n==sizeof(header)-1||sscanf(header,"HTTP/%*s %d",&h->status)!=1)goto fail;
    if(h->status!=200)goto fail;
    return 1;
fail:http_close(h);return 0;
}
