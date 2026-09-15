/* HTTP Basic is access control, NOT encryption. Use a trusted/VPN transport
 * for the PSP's HTTP connection. Keep passwords out of URLs and diagnostics. */
static char server_password[129];
static char server_auth_header[208];
static void server_auth_update(void) {
    static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    unsigned char input[133];
    char encoded[181];
    int i,n,out=0;
    server_auth_header[0]=0;
    if(!server_password[0]) return;
    n=snprintf((char *)input,sizeof(input),"psp:%s",server_password);
    for(i=0;i<n;i+=3) {
        unsigned int bits=(unsigned int)input[i]<<16;
        if(i+1<n) bits|=(unsigned int)input[i+1]<<8;
        if(i+2<n) bits|=input[i+2];
        encoded[out++]=alphabet[(bits>>18)&63]; encoded[out++]=alphabet[(bits>>12)&63];
        encoded[out++]=i+1<n?alphabet[(bits>>6)&63]:'=';
        encoded[out++]=i+2<n?alphabet[bits&63]:'=';
    }
    encoded[out]=0;
    snprintf(server_auth_header,sizeof(server_auth_header),"Authorization: Basic %s\r\n",encoded);
    memset(input,0,sizeof(input)); memset(encoded,0,sizeof(encoded));
}
