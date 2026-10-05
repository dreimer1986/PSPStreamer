/* GPL-2.0-or-later. Bounded PGS metadata and alpha-correct YUY2 composition.
 * Never modify libmpeg2 reference pictures. Operate on the presentation copy. */
#include <stdint.h>
#include <errno.h>
#include <limits.h>
enum {XBOX_BITMAP_CUES=8192,XBOX_BITMAP_PIXELS=262144,XBOX_BITMAP_SLOTS=4};
typedef struct {int start,end,x,y,w,h,cw,ch;} XboxBitmapCue;
typedef struct {XboxBitmapCue cue;unsigned char *pixels;} XboxBitmapSlot;
static int xbox_bitmap_parse(const char *body,XboxBitmapCue *cues){
    const char *p=strstr(body,"\"c\":[");int count=0,ends[4]={0};
    if(!strstr(body,"\"t\":\"pgs\"")||!p)return -1;
    p+=5;
    while(*p!=']'){
        int v[8];if(count==XBOX_BITMAP_CUES||*p++!='[')return -1;
        for(int i=0;i<8;i++){
            char *end;errno=0;long value=strtol(p,&end,10);
            if(errno||end==p||value<0||value>INT_MAX||*end!=(i==7?']':','))return -1;
            v[i]=(int)value;p=end+1;
        }
        XboxBitmapCue c={v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7]};
        if(c.end<=c.start||!c.w||!c.h||!c.cw||!c.ch||c.cw>65535||c.ch>65535||
           c.x>=c.cw||c.y>=c.ch||c.w>c.cw-c.x||c.h>c.ch-c.y||
           (uint64_t)c.w*c.h>XBOX_BITMAP_PIXELS||(count&&c.start<cues[count-1].start))return -1;
        int slot=-1;for(int i=0;i<4;i++)if(ends[i]<=c.start){slot=i;break;}
        if(slot<0)return -1; /* preserve >4 simultaneous objects through burn-in */
        ends[slot]=c.end;cues[count++]=c;
        if(*p==',')p++;else if(*p!=']')return -1;
    }
    return p[1]=='}'&&!p[2]?count:-1;
}
static unsigned char xbox_sub_clamp(int value){return value<0?0:value>255?255:value;}
static void xbox_bitmap_blend(unsigned char *out,unsigned pitch,unsigned w,unsigned h,const XboxBitmapSlot *slot){
    if(!slot->pixels||!w||w>1920||!h||h>1080||(w&1)||pitch<w*2)return;
    const XboxBitmapCue *c=&slot->cue;const unsigned char *indices=slot->pixels+1024;
    unsigned char palette[256][4];
    for(int i=0;i<256;i++){
        const unsigned char *p=slot->pixels+i*4;int r=p[0],g=p[1],b=p[2];
        /* Match PVIDEO/SDL matrix selection: limited-range BT.601/709. */
        palette[i][0]=xbox_sub_clamp(16+(((h>576?47*r+157*g+16*b:66*r+129*g+25*b)+128)>>8));
        palette[i][1]=xbox_sub_clamp(128+(((h>576?-26*r-87*g+112*b:-38*r-74*g+112*b)+128)>>8));
        palette[i][2]=xbox_sub_clamp(128+(((h>576?112*r-102*g-10*b:112*r-94*g-18*b)+128)>>8));
        palette[i][3]=p[3];
    }
    unsigned left=(unsigned)c->x*w/c->cw,right=(unsigned)(c->x+c->w)*w/c->cw;
    unsigned top=(unsigned)c->y*h/c->ch,bottom=(unsigned)(c->y+c->h)*h/c->ch;
    if(right<=left||bottom<=top)return;
    int columns[1920];
    for(unsigned x=left;x<right;x++){int sx=(int)(x*c->cw/w)-c->x;columns[x]=sx<0?0:sx>=c->w?c->w-1:sx;}
    for(unsigned y=top;y<bottom;y++){
        int sy=(int)(y*c->ch/h)-c->y;if(sy<0)sy=0;if(sy>=c->h)sy=c->h-1;
        const unsigned char *row=indices+sy*c->w;
        for(unsigned x=left&~1u;x<right;x+=2){
            static const unsigned char clear[4]={0,0,0,0};
            const unsigned char *a=x>=left?palette[row[columns[x]]]:clear;
            const unsigned char *b=x+1<right?palette[row[columns[x+1]]]:clear;
            unsigned aa=a[3],ab=b[3];if(!aa&&!ab)continue;
            unsigned char *d=out+y*pitch+x*2;
            d[0]=(a[0]*aa+d[0]*(255-aa)+127)/255;
            d[2]=(b[0]*ab+d[2]*(255-ab)+127)/255;
            d[1]=(a[1]*aa+b[1]*ab+d[1]*(510-aa-ab)+255)/510;
            d[3]=(a[2]*aa+b[2]*ab+d[3]*(510-aa-ab)+255)/510;
        }
    }
}
