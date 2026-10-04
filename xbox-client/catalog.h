/* Bounded jsmn adapter; UTF-8 labels, including escaped JSON Unicode. */
#include "vendor/jsmn.h"
typedef struct {char name[256],target[1536];int folder,audio;} Entry;
static Entry entries[64];static int entry_count,entry_index,page_offset,total_entries,root_index;
static char folder_path[1536],parent_path[1536];static int has_parent;
static char media_id[1536],media_name[256];static int media_audio,audio_track,subtitle_track=-1,quality,option_row;
static double media_duration;
static char audio_labels[32][120],subtitle_labels[32][120];static int audio_count,subtitle_count;
static jsmntok_t tokens[8192];static char *json;
static int tok_count;
static int next_tok(int i){int end=tokens[i].end;i++;while(i<tok_count&&tokens[i].start<end)i++;return i;}
static int field(int object,const char *key){
    for(int i=object+1;i<tok_count&&tokens[i].start<tokens[object].end;) {
        int value=i+1;if(tokens[i].end-tokens[i].start==(int)strlen(key)&&!memcmp(json+tokens[i].start,key,strlen(key)))return value;
        i=next_tok(value);
    }return -1;
}
static int hex4(const char *s){int value=0;for(int i=0;i<4;i++){char c=s[i];int n=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;if(n<0)return -1;value=value*16+n;}return value;}
static int text_tok(int i,char *out,size_t cap){
    size_t used=0;if(i<0){out[0]=0;return 0;}
    for(int at=tokens[i].start;at<tokens[i].end;) {
        unsigned c=(unsigned char)json[at++];char bytes[4];int n=1;bytes[0]=c;
        if(c=='\\'&&at<tokens[i].end) {
            c=json[at++];bytes[0]=c;
            if(c=='u') {
                if(at+4>tokens[i].end)return 0;int value=hex4(json+at);at+=4;if(value<0)return 0;c=value;
                if(c>=0xD800&&c<=0xDBFF&&at+6<=tokens[i].end&&json[at]=='\\'&&json[at+1]=='u') {
                    int low=hex4(json+at+2);if(low<0xDC00||low>0xDFFF)return 0;at+=6;c=0x10000+((c-0xD800)<<10)+(low-0xDC00);
                }
                if(c<128){bytes[0]=c;}else if(c<2048){n=2;bytes[0]=0xC0|(c>>6);bytes[1]=0x80|(c&63);}
                else if(c<65536){n=3;bytes[0]=0xE0|(c>>12);bytes[1]=0x80|(c>>6&63);bytes[2]=0x80|(c&63);}
                else{n=4;bytes[0]=0xF0|(c>>18);bytes[1]=0x80|(c>>12&63);bytes[2]=0x80|(c>>6&63);bytes[3]=0x80|(c&63);}
            }else if(c=='n'||c=='r'||c=='t')bytes[0]=' ';
        }
        if(used+n>=cap){out[used]=0;return 0;}memcpy(out+used,bytes,n);used+=n;
    }out[used]=0;return 1;
}
static int number(int obj,const char *key){char s[32];text_tok(field(obj,key),s,sizeof(s));return atoi(s);}
static int parse_json(char *data){json=data;jsmn_parser p;jsmn_init(&p);tok_count=jsmn_parse(&p,json,strlen(json),tokens,8192);return tok_count>0&&tokens[0].type==JSMN_OBJECT;}
static int parse_catalog(char *data){
    if(!parse_json(data))return 0;int list=field(0,"entries");if(list<0||tokens[list].type!=JSMN_ARRAY||tokens[list].size>64)return 0;
    if(!text_tok(field(0,"path"),folder_path,sizeof(folder_path)))return 0;
    int p=field(0,"parent");has_parent=p>=0&&tokens[p].type==JSMN_STRING;
    if(has_parent&&!text_tok(p,parent_path,sizeof(parent_path)))return 0;
    root_index=number(0,"root");total_entries=number(0,"total");page_offset=number(0,"offset");entry_count=0;entry_index=0;
    for(int i=list+1;i<tok_count&&tokens[i].start<tokens[list].end;i=next_tok(i)) {
        Entry *e=&entries[entry_count++];char kind[16];text_tok(field(i,"kind"),kind,sizeof(kind));
        e->folder=!strcmp(kind,"folder");e->audio=!strcmp(kind,"audio");
        text_tok(field(i,"name"),e->name,sizeof(e->name));
        if(!text_tok(field(i,e->folder?"path":"id"),e->target,sizeof(e->target)))return 0;
    }return 1;
}
static int parse_metadata(char *data){
    if(!parse_json(data))return 0;char duration[40];text_tok(field(0,"d"),duration,sizeof(duration));media_duration=strtod(duration,NULL);
    audio_count=subtitle_count=0;audio_track=0;subtitle_track=-1;
    for(int type=0;type<2;type++){int a=field(0,type?"s":"a");if(a<0||tokens[a].type!=JSMN_ARRAY)continue;
        int count=0;for(int i=a+1;i<tok_count&&tokens[i].start<tokens[a].end&&count<32;i=next_tok(i)) {
            char lang[32],title[80];text_tok(field(i,"l"),lang,sizeof(lang));text_tok(field(i,"t"),title,sizeof(title));
            snprintf(type?subtitle_labels[count]:audio_labels[count],120,"%d: %s %s",count+1,lang,title);count++;
        }if(type)subtitle_count=count;else audio_count=count;
    }return 1;
}
