#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "../xbox-client/catalog.h"
int main(void){
    char reply[]="{\"d\":1442.75,\"a\":[{\"l\":\"ja\",\"t\":\"Japanese\"}],\"s\":[{\"l\":\"de\",\"t\":\"Deutsch\"}]}";
    assert(parse_metadata(reply));assert(media_duration==1442.75);
    assert(audio_count==1&&subtitle_count==1);
    assert(!strcmp(language_label("und"),"Language unspecified"));
    assert(!strcmp(language_label("jpn"),"Japanese"));
    assert(strstr(subtitle_labels[0],"Deutsch"));
    assert(fabs(parse_duration("1.44275e3")-1442.75)<1e-8);
    assert(fabs(parse_duration("144275E-2")-1442.75)<1e-8);
    assert(parse_duration("120")==120);
    const char *bad[]={"","null","-1","NaN","inf","1x","1.","01","1e","1e99999999","1e309","1e30"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++)assert(parse_duration(bad[i])==0);
    char missing[]="{\"a\":[],\"s\":[]}";
    assert(parse_metadata(missing));assert(media_duration==0);
    char unknown[]="{\"d\":null}";
    assert(parse_metadata(unknown));assert(media_duration==0);
    char prefs[]="{\"d\":120,\"resume\":45,\"preferred_audio\":1,\"preferred_subtitle\":-1,\"chapters\":[{\"start\":0},{\"start\":60}]}";
    assert(parse_metadata(prefs));assert(media_resume==45&&suggested_audio==1&&suggested_subtitle==-1);
    assert(chapter_count==2&&chapter_starts[1]==60);
    return 0;
}
