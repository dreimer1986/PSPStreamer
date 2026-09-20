#include <assert.h>
#include "preset_sequence.h"
int main(int argc,char **argv) {
    assert(argc==2);PresetSequence *s=malloc(sizeof(*s));assert(s);
    assert(preset_sequence_load(s,argv[1],123)==3);
    assert(!strcmp(s->catalog.names[0],"Zulu.milk"));
    assert(!strcmp(s->catalog.names[1],"Alpha.milk"));
    assert(preset_sequence_next(s,"Zulu.milk",1)==1);
    assert(preset_sequence_next(s,"Alpha.milk",1)==2);
    assert(s->rating[0]==0 && s->rating[1]==4 && s->rating[2]==1);
    for(int i=0;i<100;i++) assert(preset_sequence_next(s,"Alpha.milk",3)==2);
    int seen=0;
    for(int i=0;i<100;i++) {int n=preset_sequence_next(s,"Zulu.milk",2);assert(n==1 || n==2);seen|=1<<n;}
    assert(seen==6);
    s->rating[2]=-1;
    assert(preset_sequence_next(s,"Alpha.milk",3)==-1);
    assert(preset_sequence_next(s,"Alpha.milk",1)==0);
    s->catalog.count=0;assert(preset_sequence_next(s,"Alpha.milk",1)==-1);
    assert(preset_sequence_next(s,"Alpha.milk",0)==-1);
    assert(preset_sequence_load_selected(s,argv[1],"Geiss/A.milk",123)==2);
    assert(!strcmp(s->catalog.names[0],"Geiss/B.milk") && s->rating[0]==5);
    assert(!strcmp(s->catalog.names[1],"Geiss/A.milk") && s->rating[1]==3);
    assert(preset_sequence_next(s,"Geiss/A.milk",1)==0);
    free(s);return 0;
}
