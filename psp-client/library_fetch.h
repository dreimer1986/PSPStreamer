/* Directory-only deadline, including DNS. Do not use for media preparation. */
#define LIBRARY_ATTEMPT_MS 15000
static int library_fetch(const char *path,volatile int *running) {
    unsigned long long deadline=sceKernelGetSystemTimeWide()+LIBRARY_ATTEMPT_MS*1000ULL;
    if(!*running)return -1005;
    if(!have_cached_server_address) {
        struct in_addr address;
        if(!inet_aton(server_host,&address)) {
            unsigned char workspace[1024];
            int resolver=-1,result=sceNetResolverCreate(&resolver,workspace,sizeof(workspace));
            if(result<0) {
                /* Other network clients may already have initialized it. */
                sceNetResolverInit();
                result=sceNetResolverCreate(&resolver,workspace,sizeof(workspace));
            }
            if(result<0)return -1004;
            result=sceNetResolverStartNtoA(resolver,server_host,&address,2,1);
            sceNetResolverDelete(resolver);
            if(result<0)return -1004;
        }
        if(!*running)return -1005;
        cached_server_address=address;have_cached_server_address=1;
    }
    int remaining=(int)(((long long)deadline-(long long)sceKernelGetSystemTimeWide())/1000);
    if(!*running || remaining<=0)return -1005;
    int result=remote_http_get_budget(path,response,sizeof(response),running,remaining);
    /* Wrong credentials/invalid paths need user action, not retries. */
    if(result<0 && remote_http_last_status>=400 && remote_http_last_status<500 &&
       remote_http_last_status!=408 && remote_http_last_status!=429)return -1003;
    return result;
}
