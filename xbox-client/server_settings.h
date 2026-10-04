/* Xbox adapters for the PSP settings editor's draft/save/cancel model and
 * controller keyboard. Network workers are stopped before editing credentials. */
static char server_draft_host[64],server_draft_port[8],server_draft_password[129];
static char keyboard_draft[129];static int keyboard_field,keyboard_key;
static int config_testing,connection_ready;
static char config_old_host[64],config_old_password[129];static unsigned config_old_port;
static void config_restore_test(void){
    if(!config_testing)return;
    strcpy(host,config_old_host);strcpy(password,config_old_password);port=config_old_port;config_testing=0;
}
static int server_draft_valid(void){
    struct in_addr ip;char *end;unsigned long p=strtoul(server_draft_port,&end,10);
    return *server_draft_port&&!*end&&p>0&&p<=65535&&inet_aton(server_draft_host,&ip);
}
static int server_save(void){
    if(!server_draft_valid())return 0;
    FILE *f=fopen("D:\\server.tmp","w");if(!f)return 0;
    int ok=fprintf(f,"host=%s\nport=%u\npassword=%s\noutput_height=%d\n",server_draft_host,(unsigned)strtoul(server_draft_port,NULL,10),server_draft_password,output_height)>0;
    if(fclose(f))ok=0;if(!ok)return 0;
    remove("D:\\server.bak");MoveFileA("D:\\server.cfg","D:\\server.bak");
    if(!MoveFileA("D:\\server.tmp","D:\\server.cfg")){MoveFileA("D:\\server.bak","D:\\server.cfg");return 0;}
    strcpy(host,server_draft_host);strcpy(password,server_draft_password);port=(unsigned)strtoul(server_draft_port,NULL,10);
    connection_ready=1;return 1;
}
static void keyboard_begin(int field){
    keyboard_field=field;keyboard_key=0;
    snprintf(keyboard_draft,sizeof(keyboard_draft),"%s",field==0?server_draft_host:field==1?server_draft_port:server_draft_password);
}
static void keyboard_accept(void){
    char *dest=keyboard_field==0?server_draft_host:keyboard_field==1?server_draft_port:server_draft_password;
    unsigned size=keyboard_field==0?sizeof(server_draft_host):keyboard_field==1?sizeof(server_draft_port):sizeof(server_draft_password);
    snprintf(dest,size,"%s",keyboard_draft);memset(keyboard_draft,0,sizeof(keyboard_draft));
}
static void keyboard_insert(void){
    static const int extra[]={0xE4,0xF6,0xFC,0xC4,0xD6,0xDC,0xDF};
    int cp=keyboard_key<95?keyboard_key+32:extra[keyboard_key-95],n=strlen(keyboard_draft),bytes=cp<128?1:2;
    unsigned cap=keyboard_field==0?sizeof(server_draft_host):keyboard_field==1?sizeof(server_draft_port):sizeof(server_draft_password);
    if(n+bytes>=(int)cap)return;
    if(bytes==1)keyboard_draft[n++]=cp;else{keyboard_draft[n++]=0xC0|(cp>>6);keyboard_draft[n++]=0x80|(cp&63);}keyboard_draft[n]=0;
}
