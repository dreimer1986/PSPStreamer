/* Deferred completion deliberately retains the original buffer. A copy below
 * detects reuse before completion; it is not used to satisfy the actual write. */
static const void *io_buffer;
static unsigned char *io_copy;
static unsigned io_length;
static int io_fd,io_busy,io_read,io_poll,io_disabled,io_fail,io_cancel;
static void sceKernelDelayThread(unsigned us){(void)us;}
static int sceIoWriteAsync(int fd,const void *buffer,unsigned length) {
    if(io_disabled)return -1;
    assert(!io_busy);io_busy=1;io_read=0;io_poll=0;io_fd=fd;io_buffer=buffer;io_length=length;
    io_copy=malloc(length);assert(io_copy);memcpy(io_copy,buffer,length);return 0;
}
#ifdef OFFLINE_TEST_READ
static int sceIoReadAsync(int fd,void *buffer,unsigned length) {
    if(io_disabled)return -1;
    assert(!io_busy);io_busy=1;io_read=1;io_poll=0;io_fd=fd;io_buffer=buffer;io_length=length;return 0;
}
#endif
static int sceIoPollAsync(int fd,long long *result) {
    assert(io_busy && io_fd==fd);
    if(!io_poll++)return 1;
    if(io_read) {
#ifdef OFFLINE_TEST_READ
        *result=io_fail?-1:sceIoRead(fd,(void *)io_buffer,io_length);
#else
        assert(0);
#endif
    } else {
        assert(!memcmp(io_buffer,io_copy,io_length));free(io_copy);io_copy=NULL;
        *result=io_fail?-1:sceIoWrite(fd,io_buffer,io_length);
    }
    io_busy=0;if(io_cancel)download_running=0;return 0;
}
