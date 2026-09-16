/* SPDX-License-Identifier: GPL-2.0-or-later
 * Private recursive-descent compiler for bounded EEL expressions. */
static int writable(Parser *p,int id) {
    if(id==PM_MONITOR || id==PM_WRAP) return p->pixel==0;
    if(id<0 || id>=PM_ENGINE_BASE || (id>=9 && id<23) ||
       (id>=PM_COORD_BASE && id<PM_DYNAMIC_BASE) ||
       (id>PM_WAVE_BASE && id<PM_EFFECT_BASE) || (p->pixel==4 && id==PM_WAVE_BASE)) return 0;
    if(p->pixel==1) return id<3 || (id>=23 && id<=29) || (id>=PM_Q_BASE && id<PM_COORD_BASE);
    return 1;
}
static int sequence(Parser *p);
static int assignment(Parser *p);
static int precedence(Parser *p,int minimum);
static int register_id(const char *s) {
    return strlen(s)==5 && !strncmp(s,"reg",3) && isdigit((unsigned char)s[3]) &&
        isdigit((unsigned char)s[4]) ? (s[3]-'0')*10+s[4]-'0' : -1;
}
static int argument(Parser *p,char end) {
    if(!sequence(p)) return 0;
    space(p);
    if(*p->p!=end) return 0;
    p->p++; return 1;
}
static int call(Parser *p,int op) {
    p->p++; /* '(' */
    if(op==ASSIGN) {
        int begin=p->code->count;
        if(!argument(p,',')) return 0;
        int last=p->code->count-1;if(last<begin) return 0;
        PmOp target=p->code->code[last];int store;
        if(target.op==LOAD && last==begin) {
            if(!writable(p,target.arg)) {p->error=PM_UNSUPPORTED;return 0;}
            store=KEEP;
        } else if(target.op==REGL && last==begin) store=REGS;
        else if(target.op==MEML || target.op==GMEML) store=target.op==MEML?MEMS:GMEMS;
        else return 0;
        p->code->count--;
        return argument(p,')') && emit(p,store,target.arg,0);
    }
    if(op==IF) {
        if(!argument(p,',')) return 0;
        int otherwise=p->code->count;
        if(!emit(p,JZ,0,0) || !argument(p,',')) return 0;
        int finish=p->code->count;
        if(!emit(p,JUMP,0,0)) return 0;
        p->code->code[otherwise].arg=p->code->count;
        if(!argument(p,')')) return 0;
        p->code->code[finish].arg=p->code->count; return 1;
    }
    if(op==LOOP || op==WHILE) {
        if(op==LOOP && !argument(p,',')) return 0;
        int start=p->code->count;
        if(!emit(p,op,0,0) || !argument(p,')') || !emit(p,op==LOOP?LOOPEND:WHILEEND,start,0)) return 0;
        p->code->code[start].arg=p->code->count; return 1;
    }
    int argc=(op==EXEC3 || op==MEMCPY || op==MEMSET)?3:(binary(op) || op==EXEC2)?2:1;
    for(int i=0;i<argc;i++) {
        if(!argument(p,i==argc-1?')':',')) return 0;
        if((op==EXEC2 || op==EXEC3) && i<argc-1 && !emit(p,DROP,0,0)) return 0;
    }
    return op==EXEC2 || op==EXEC3 || emit(p,op,0,0);
}
static int unary(Parser *p) {
    char text[32],*end;
    int ok=0;
    space(p);
    if(++p->depth>PM_DEPTH) {p->error=PM_INVALID;p->depth--;return 0;}
    if(*p->p=='+' || *p->p=='-' || *p->p=='!') {
        char ch=*p->p++;
        ok=unary(p) && (ch=='+' || emit(p,ch=='-'?NEG:BNOT,0,0));
    } else if(*p->p=='(') {
        p->p++;ok=argument(p,')');
    } else if(*p->p=='$') {
        p->p++;
        if(*p->p=='x' || *p->p=='X') {
            p->p++;errno=0;unsigned long n=strtoul(p->p,&end,16);
            if(end!=p->p && errno!=ERANGE && n<=0xffffffffUL) {p->p=end;ok=emit(p,PUSH,0,(float)n);}
        } else if(*p->p=='\'' && p->p[1] && p->p[2]=='\'') {
            ok=emit(p,PUSH,0,(unsigned char)p->p[1]);p->p+=3;
        } else if(name(p,text)) {
            if(!strcmp(text,"pi")) ok=emit(p,PUSH,0,3.14159265358979323846f);
            else if(!strcmp(text,"e")) ok=emit(p,PUSH,0,2.71828182845904523536f);
            else if(!strcmp(text,"phi")) ok=emit(p,PUSH,0,1.6180339887498948482f);
        }
    } else if(isdigit((unsigned char)*p->p) || *p->p=='.') {
        errno=0;float v=strtof(p->p,&end);
        if(end!=p->p && errno!=ERANGE && isfinite(v)) {p->p=end;ok=emit(p,PUSH,0,v);}
    } else if(name(p,text)) {
        space(p);
        if(*p->p=='(') {
            int op=function(text);
            if(op<0) p->error=PM_UNSUPPORTED;
            else ok=call(p,op);
        } else {
            int reg=register_id(text);
            if(reg>=0) ok=emit(p,REGL,reg,0);
            else {int id=variable(p,text);if(id>=0) ok=emit(p,LOAD,id,0);}
        }
    }
    space(p);
    /* EEL's address[index] is local megabuf(address + index). */
    if(ok && *p->p=='[') {
        p->p++;space(p);
        if(*p->p==']') {p->p++;ok=emit(p,MEML,0,0);}
        else ok=argument(p,']') && emit(p,ADD,0,0) && emit(p,MEML,0,0);
    }
    p->depth--;return ok;
}
static int operator_at(Parser *p,int *length,int *rank) {
    static const struct {const char *s;int op,rank;} ops[]={
        {"||",BOR,1},{"&&",BAND,2},{"|",BITOR,3},{"&",BITAND,4},
        {"==",EQUAL,5},{"!=",NEQ,5},{"<=",LE,6},{">=",GE,6},{"<",BELOW,6},{">",ABOVE,6},
        {"+",ADD,7},{"-",SUB,7},{"*",MUL,8},{"/",DIV,8},{"%",MOD,8},{"^",POW,9}};
    space(p);
    for(unsigned int i=0;i<sizeof(ops)/sizeof(ops[0]);i++) {
        int n=(int)strlen(ops[i].s);
        if(!strncmp(p->p,ops[i].s,n)) {
            if(n==1 && p->p[1]=='=') return -1;
            *length=n;*rank=ops[i].rank;return ops[i].op;
        }
    }
    return -1;
}
static int precedence(Parser *p,int minimum) {
    if(!unary(p)) return 0;
    for(;;) {
        int length=0,rank=0,op=operator_at(p,&length,&rank);
        if(op<0 || rank<minimum) return 1;
        p->p+=length;
        if(op==BAND || op==BOR) {
            int branch=p->code->count,finish;
            if(!emit(p,JZ,0,0)) return 0;
            if(op==BOR) {
                if(!emit(p,PUSH,0,1)) return 0;
                finish=p->code->count;if(!emit(p,JUMP,0,0)) return 0;
                p->code->code[branch].arg=p->code->count;
                if(!precedence(p,rank+1) || !emit(p,BOOL,0,0)) return 0;
            } else {
                if(!precedence(p,rank+1) || !emit(p,BOOL,0,0)) return 0;
                finish=p->code->count;if(!emit(p,JUMP,0,0)) return 0;
                p->code->code[branch].arg=p->code->count;
                if(!emit(p,PUSH,0,0)) return 0;
            }
            p->code->code[finish].arg=p->code->count;
        } else {
            if(++p->depth>PM_DEPTH) {p->depth--;return 0;}
            int ok=precedence(p,rank+(op!=POW));p->depth--;
            if(!ok || !emit(p,op,0,0)) return 0;
        }
    }
}
static int assignment_body(Parser *p) {
    int begin=p->code->count;
    if(!precedence(p,1)) return 0;
    space(p);
    if(*p->p=='?') {
        p->p++;int branch=p->code->count;
        if(!emit(p,JZ,0,0) || !argument(p,':')) return 0;
        int finish=p->code->count;if(!emit(p,JUMP,0,0)) return 0;
        p->code->code[branch].arg=p->code->count;
        if(!assignment(p)) return 0;
        p->code->code[finish].arg=p->code->count;return 1;
    }
    int compound=-1,length=0;
    if(*p->p=='=' && p->p[1]!='=') length=1;
    else if(*p->p && p->p[1]=='=') {
        const char *chars="+-*/%^&|",*ch=strchr(chars,*p->p);
        const int ops[]={ADD,SUB,MUL,DIV,MOD,POW,BITAND,BITOR};
        if(ch) {length=2;compound=ops[ch-chars];}
    }
    if(!length) return 1;
    int last=p->code->count-1;
    if(last<begin) return 0;
    PmOp target=p->code->code[last];
    int store;
    if(target.op==LOAD && last==begin) {
        if(!writable(p,target.arg)) {p->error=PM_UNSUPPORTED;return 0;}
        store=KEEP;
    } else if(target.op==REGL && last==begin) store=REGS;
    else if(target.op==MEML || target.op==GMEML) store=target.op==MEML?MEMS:GMEMS;
    else return 0;
    /* Remove the load for simple assignment, keep it for += etc. Memory
     * assignments retain one address below the value on the operand stack. */
    p->code->count--;
    if(store==MEMS || store==GMEMS) {
        if(compound>=0 && (!emit(p,DUP,0,0) || !emit(p,target.op,0,0))) return 0;
    } else if(compound>=0 && !emit(p,target.op,target.arg,0)) return 0;
    p->p+=length;
    if(++p->depth>PM_DEPTH) {p->depth--;return 0;}
    int ok=assignment(p);p->depth--;
    return ok && (compound<0 || emit(p,compound,0,0)) && emit(p,store,target.arg,0);
}
static int assignment(Parser *p) {
    if(++p->depth>PM_DEPTH) {p->depth--;return 0;}
    int ok=assignment_body(p);p->depth--;return ok;
}
static int sequence(Parser *p) {
    if(!assignment(p)) return 0;
    space(p);
    while(*p->p==';') {
        p->p++;space(p);
        if(!*p->p || *p->p==')' || *p->p==',' || *p->p==']' || *p->p==':') break;
        if(!emit(p,DROP,0,0) || !assignment(p)) return 0;
        space(p);
    }
    return 1;
}
static int compile_context(PmProgram *program,const char *source,int line,PmSymbols *symbols,int pixel) {
    int before=program->count;
    PmSymbols saved;
    if(symbols) {
        if(symbols->count<0 || symbols->count>PM_USER_COUNT) return PM_INVALID;
        saved=*symbols;
    }
    Parser p={source,program,line,0,PM_INVALID,symbols,pixel};space(&p);
    if(!*p.p || program->lines>=128 || before<0 || before>PM_MAX_OPS) return PM_INVALID;
    if(!sequence(&p) || !emit(&p,DROP,0,0)) goto fail;
    space(&p);
    if(*p.p || ((pixel==1 || pixel==4) && program->count>PM_PIXEL_OPS)) goto fail;
    program->lines++;return PM_OK;
fail:
    program->count=before;if(symbols) *symbols=saved;return p.error;
}
