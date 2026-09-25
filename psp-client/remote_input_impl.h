static int input_thread=-1;
static volatile int input_running,input_done;
static int input_result,input_ack,input_dialog,input_capacity,input_secret,input_serial;
static unsigned int input_buttons,input_x=128,input_y=128;
static unsigned long long input_until,input_analog_until,input_next;
static char input_client[40],input_path[192],input_reply[2048],input_text[256];
static int input_text_ready;

static int input_worker(SceSize args,void *argp) {
    (void)args;(void)argp;
    input_result=remote_http_get_budget(input_path,input_reply,sizeof(input_reply),&input_running,5000);
    __sync_synchronize();input_done=1;
    return 0;
}
static void input_clear_keys(void) {
    input_buttons=0;input_x=input_y=128;input_until=input_analog_until=0;
}
static void input_remote_stop(void) {
    /* Called before connection settings change. The worker owns its socket. */
    input_running=0;
    if(input_thread>=0) {
        sceKernelWaitThreadEnd(input_thread,NULL);
        sceKernelDeleteThread(input_thread);input_thread=-1;
    }
    input_done=0;input_next=0;input_client[0]=0;input_ack=0;
    input_clear_keys();memset(input_text,0,sizeof(input_text));input_text_ready=0;
    memset(input_reply,0,sizeof(input_reply));
}
static void input_text_begin(int capacity,int secret) {
    input_dialog=++input_serial;input_capacity=capacity;input_secret=secret;
    input_text_ready=0;memset(input_text,0,sizeof(input_text));input_next=0;
}
static void input_text_end(void) {
    input_dialog=input_capacity=input_secret=0;input_text_ready=0;
    memset(input_text,0,sizeof(input_text));input_next=0;
}
static int input_text_take(char *destination,int capacity) {
    if(!input_text_ready)return 0;
    int valid=(int)strlen(input_text)<capacity;
    if(valid)strcpy(destination,input_text);
    memset(input_text,0,sizeof(input_text));input_text_ready=0;
    return valid;
}
static int input_hex(int c) {
    if(c>='0'&&c<='9')return c-'0';
    if(c>='a'&&c<='f')return c-'a'+10;
    return -1;
}
static void input_receive(unsigned long long now) {
    unsigned int seq,kind,mask,x,y,ttl,field;
    char hex[513];
    if(sscanf(input_reply,"%u %u %u %u %u %u %u %512s",&seq,&kind,&mask,&x,&y,&ttl,&field,hex)!=8 ||
       seq>0x7fffffff || kind>3 || (mask&~0xF3F9u) || x>255 || y>255 || ttl>1000) {
        input_clear_keys();return;
    }
    if(kind==2) {
        if(seq>(unsigned)input_ack && field==(unsigned)input_dialog && input_dialog && ttl) {
            size_t n=!strcmp(hex,"-")?0:strlen(hex);int valid=!(n&1)&&n/2<(unsigned)input_capacity&&n/2<sizeof(input_text);
            for(size_t i=0;valid&&i<n;i+=2) {
                int a=input_hex(hex[i]),b=input_hex(hex[i+1]),c=a*16+b;
                if(a<0||b<0||c<32||c==127)valid=0;else input_text[i/2]=(char)c;
            }
            if(valid){input_text[n/2]=0;input_text_ready=1;}
            else {memset(input_text,0,sizeof(input_text));input_text_ready=0;}
        }
    } else if(kind==0 || kind==3 || seq>(unsigned)input_ack) {
        input_buttons=mask;input_x=x;input_y=y;
        input_analog_until=now+ttl*1000ULL;
        /* One click must release before the menu's repeat threshold even
         * when its matching UP packet is delayed by another TLS exchange. */
        input_until=now+((kind==1 && ttl>60)?60:ttl)*1000ULL;
    }
    if(seq>(unsigned)input_ack)input_ack=(int)seq;
    input_next=now+(ttl?30000ULL:1000000ULL);
    memset(input_reply,0,sizeof(input_reply));
}
static void input_remote_tick(SceCtrlData *pad) {
    unsigned long long now=sceKernelGetSystemTimeWide();
    if(input_thread>=0 && input_done) {
        unsigned int wait=1;
        if(sceKernelWaitThreadEnd(input_thread,&wait)>=0) {
            sceKernelDeleteThread(input_thread);input_thread=-1;
            __sync_synchronize();
            if(input_result>=0 && input_running)input_receive(now);
            else {input_clear_keys();memset(input_reply,0,sizeof(input_reply));input_next=now+1000000ULL;}
            input_done=input_running=0;
        }
    }
    if(now>=input_until)input_buttons=0;
    if(now>=input_analog_until)input_x=input_y=128;
    pad->Buttons|=input_buttons;
    /* A physical analog movement always has priority. */
    if((input_x!=128 || input_y!=128) && abs((int)pad->Lx-128)<24 && abs((int)pad->Ly-128)<24) {
        pad->Lx=(unsigned char)input_x;pad->Ly=(unsigned char)input_y;
    }
    if(input_thread<0 && now>=input_next && network_ready && http_ready && have_cached_server_address) {
        if(!input_client[0])snprintf(input_client,sizeof(input_client),"psp-%llu",now);
        snprintf(input_path,sizeof(input_path),"/api/input/poll?client=%s&ack=%d&dialog=%d&capacity=%d&secret=%d",
                 input_client,input_ack,input_dialog,input_capacity,input_secret);
        input_running=1;input_done=0;input_next=now+1000000ULL;
        input_thread=sceKernelCreateThread("PSP GUI input",input_worker,0x41,0x10000,PSP_THREAD_ATTR_USER,NULL);
        if(input_thread<0 || sceKernelStartThread(input_thread,0,NULL)<0) {
            if(input_thread>=0)sceKernelDeleteThread(input_thread);
            input_thread=-1;input_running=0;
        }
    }
}
