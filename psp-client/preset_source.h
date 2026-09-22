/* Import-only buffers: physical records become one Desktop-style program.
 * No source text/mapping allocation survives activation. File size still caps
 * aggregate text at 256 KiB; each context has a bounded record budget. */
enum { MD_SOURCE_BLOCKS=3+MD_SHAPES*2+MD_CUSTOM_WAVES*3 };
typedef struct {
    char *text;
    int used,count,context;
    char prefix[32];
    PmSourceLocation locations[PM_MAX_RECORDS];
    PmProgram *program;
    PmSymbols *symbols;
} MdSourceBlock;
static void md_source_bind(MdSourceBlock *b,PmProgram *program,PmSymbols *symbols,int context,const char *prefix) {
    b->program=program;b->symbols=symbols;b->context=context;
    snprintf(b->prefix,sizeof(b->prefix),"%s",prefix);
}
static MdSourceBlock *md_source_create(MdFilePreset *p) {
    MdSourceBlock *b=calloc(MD_SOURCE_BLOCKS,sizeof(*b));
    if(!b)return NULL;
    md_source_bind(b,&p->init_program,&p->symbols,0,"per_frame_init_");
    md_source_bind(b+1,&p->program,&p->symbols,0,"per_frame_");
    md_source_bind(b+2,&p->pixel_program,&p->pixel_symbols,1,"per_pixel_");
    for(int i=0;i<MD_SHAPES;i++) {
        char prefix[32];MdShapeProgram *s=&p->shape_program[i];
        snprintf(prefix,sizeof(prefix),"shape_%d_init",i);
        md_source_bind(b+3+i*2,&s->init,&s->symbols,2,prefix);
        snprintf(prefix,sizeof(prefix),"shape_%d_per_frame",i);
        md_source_bind(b+4+i*2,&s->frame,&s->symbols,2,prefix);
    }
    for(int i=0;i<MD_CUSTOM_WAVES;i++) {
        char prefix[32];MdCustomWave *w=&p->waves[i];int n=3+MD_SHAPES*2+i*3;
        snprintf(prefix,sizeof(prefix),"wave_%d_init",i);
        md_source_bind(b+n,&w->init,&w->symbols,3,prefix);
        snprintf(prefix,sizeof(prefix),"wave_%d_per_frame",i);
        md_source_bind(b+n+1,&w->frame,&w->symbols,3,prefix);
        snprintf(prefix,sizeof(prefix),"wave_%d_per_point",i);
        md_source_bind(b+n+2,&w->point,&w->point_symbols,4,prefix);
    }
    return b;
}
static int md_source_add(MdSourceBlock *b,const char *key,const char *text,int line,MdFileError *error) {
    size_t prefix=strlen(b->prefix);
    if(!strncmp(key,b->prefix,prefix)) {
        char *end;long record=strtol(key+prefix,&end,10);
        if(end!=key+prefix && !*end && record>0 && record<=b->count)return MD_FILE_OK;
    } /* GetPrivateProfileString returns the first matching numbered key. */
    char expected[48];snprintf(expected,sizeof(expected),"%s%d",b->prefix,b->count+1);
    if(strcmp(key,expected) || b->count>=PM_MAX_RECORDS)return md_file_error(error,MD_FILE_INVALID,line,key);
    if(*text=='`')text++; /* Desktop's optional exported-line marker. */
    /* state.cpp StripLinefeedCharsAndComments removes the record delimiter,
     * rather than replacing it with whitespace (despite its old comment).
     * Desktop exports can therefore even split an identifier: is_ / beat.
     * Strip both Desktop line-comment spellings before joining; block comments
     * remain for the expression parser and may span numbered records. */
    size_t size=0;
    while(text[size] && !((text[size]=='/' && text[size+1]=='/') ||
                         (text[size]=='\\' && text[size+1]=='\\')))size++;
    if(size>PM_SOURCE_BYTES-(unsigned)b->used-2U)return md_file_error(error,MD_FILE_INVALID,line,key);
    char *joined=realloc(b->text,b->used+size+2);
    if(!joined)return md_file_error(error,MD_FILE_IO,line,"formula allocation");
    b->text=joined;b->locations[b->count++]=(PmSourceLocation){b->used,line};
    memcpy(b->text+b->used,text,size);b->used+=(int)size;
    b->text[b->used]=0;
    return MD_FILE_OK;
}
static int md_source_compile(MdSourceBlock *blocks,MdFileError *error) {
    int allocated=0;
    for(int i=0;i<MD_SOURCE_BLOCKS;i++) {
        MdSourceBlock *b=blocks+i;
        if(!b->count)continue;
        int line=0;
        int result=pm_compile_mapped(b->program,b->text,b->locations,b->count,b->symbols,b->context,&line);
        if(result!=PM_OK) {
            int record=0;
            while(record+1<b->count && b->locations[record+1].line<=line)record++;
            char key[48];snprintf(key,sizeof(key),"%s%d",b->prefix,record+1);
            return md_file_error(error,result==PM_NOMEM?MD_FILE_IO:result==PM_UNSUPPORTED?MD_FILE_UNSUPPORTED:MD_FILE_INVALID,line,key);
        }
        pm_program_compact(b->program);
        allocated+=b->program->capacity;
        if(allocated>PM_TOTAL_OPS)return md_file_error(error,MD_FILE_INVALID,line,"total formula storage");
    }
    return MD_FILE_OK;
}
static void md_source_free(MdSourceBlock *b) {
    for(int i=0;i<MD_SOURCE_BLOCKS;i++)free(b[i].text);
    free(b);
}
