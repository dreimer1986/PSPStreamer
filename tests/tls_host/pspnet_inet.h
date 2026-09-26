#ifndef TLS_HOST_PSPNET_INET_H
#define TLS_HOST_PSPNET_INET_H
#include <sys/socket.h>
#include <poll.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#define SO_NONBLOCK 0x1009
#define SceNetInetPollfd pollfd
#define SCE_NET_INET_POLLOUT POLLOUT
#define SCE_NET_INET_POLLIN POLLIN
#define sceNetInetSend send
#define sceNetInetRecv recv
#define sceNetInetPoll poll
#define sceNetInetClose close
/* Match the abortive-close semantics used by the PSP export. */
static inline int sceNetInetCloseWithRST(int fd) {
    struct linger mode={1,0};
    if(setsockopt(fd,SOL_SOCKET,SO_LINGER,&mode,sizeof(mode))<0)return -1;
    return close(fd);
}
static inline int sceNetInetGetErrno(void) {return errno==EAGAIN?35:errno;}
static inline int sceNetInetSetsockopt(int fd,int level,int option,const void *value,int size) {
    (void)level;(void)size;
    return option==SO_NONBLOCK?fcntl(fd,F_SETFL,*(const int *)value?O_NONBLOCK:0):-1;
}
#endif
