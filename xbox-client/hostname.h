/* ASCII DNS names (international names must use their IDNA ASCII spelling). */
static int xbox_hostname_valid(const char *s){
    unsigned label=0,total=0;char last=0;
    for(;*s;s++,total++){
        unsigned char c=*s;
        if(c=='.'){if(!label||last=='-')return 0;label=0;}
        else{if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-')||(!label&&c=='-')||++label>63)return 0;}
        last=c;
    }
    return total&&total<=253&&label&&last!='-';
}
