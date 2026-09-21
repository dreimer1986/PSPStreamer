#include <stdio.h>
typedef struct {int start_ms,end_ms;char text[160];} SubtitleCue;
#include "subtitle_pages.h"
int main(void) {
    char buffer[65536];SubtitlePage page;
    size_t n=fread(buffer,1,sizeof(buffer)-1,stdin);buffer[n]=0;
    return subtitle_page_parse(buffer,&page)?0:1;
}
