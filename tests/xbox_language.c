#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../xbox-client/catalog.h"
int main(void){
    assert(!strcmp(XL(SETTINGS),"SETTINGS"));
    dashboard_german=1;assert(!strcmp(XL(SETTINGS),"OPTIONEN"));
    ui_language=1;assert(!strcmp(XL(SETTINGS),"SETTINGS"));
    ui_language=2;assert(!strcmp(language_label("ger"),"Deutsch"));
    assert(strstr(XL(BACK),"Zurück"));
    for(int i=0;i<XL_COUNT;i++)assert(ui_strings[i][0][0]&&ui_strings[i][1][0]);
    char page[]="{\"path\":\":xbox:queue:\",\"parent\":\"\",\"revision\":4,\"enabled\":1,\"repeat\":2,\"shuffle\":1,\"root\":0,\"total\":2,\"offset\":0,\"entries\":[{\"id\":\"two\",\"name\":\"Äpfel\",\"kind\":\"video\",\"position\":0},{\"id\":\"one\",\"name\":\"One\",\"kind\":\"audio\",\"favorite\":1,\"position\":1}]}";
    assert(parse_catalog(page));assert(queue_revision==4&&queue_enabled&&queue_repeat==2&&queue_shuffle);
    entry_index=1;assert(parse_catalog(page));assert(entry_index==1&&entries[1].favorite);
    assert(!strcmp(entries[0].name,"Äpfel"));
    puts("PASS: language override/fallback, UTF-8, queue state, selection preservation");
}
