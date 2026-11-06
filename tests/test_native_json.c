/* JSON representation/ABI tests, including a real generated entry and linked
 * runtime. Counters are test-only; normal builds contain no instrumentation. */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <sys/wait.h>
#include <unistd.h>
static size_t formats, tokens, copies, lengths;
static int fail_calloc, fail_malloc;
static int observed_snprintf(char *s,size_t n,const char *fmt,...){
    formats++;va_list ap;va_start(ap,fmt);int r=vsnprintf(s,n,fmt,ap);va_end(ap);return r;
}
static long long observed_strtoll(const char *s,char **e,int base){tokens++;return strtoll(s,e,base);}
static size_t observed_strlen(const char *s){lengths++;return strlen(s);}
static void *observed_memcpy(void *d,const void *s,size_t n){copies++;return memcpy(d,s,n);}
static void *observed_calloc(size_t n,size_t s){if(fail_calloc){fail_calloc=0;return NULL;}return calloc(n,s);}
static void *observed_malloc(size_t n){if(fail_malloc){fail_malloc=0;return NULL;}return malloc(n);}
#define snprintf observed_snprintf
#define strtoll observed_strtoll
#define strlen observed_strlen
#define memcpy observed_memcpy
#define calloc observed_calloc
#define malloc observed_malloc
#define APX_LIBRARY_ONLY
#ifndef APX_RUNTIME_SOURCE
#define APX_RUNTIME_SOURCE "../runtime/apx_runtime.c"
#endif
#include APX_RUNTIME_SOURCE
#undef snprintf
#undef strtoll
#undef strlen
#undef memcpy
#undef calloc
#undef malloc
#include "../compiler/apx_frontend.h"
static unsigned checks;
static void require(int ok,const char *why){checks++;if(!ok){fprintf(stderr,"native JSON FAIL: %s\n",why);exit(1);}}
static ApxValue *parsed(const char *s){ApxValue *v=apx_json_parse(s,strlen(s));require(v!=NULL,"valid JSON parses");return v;}
static void encoded(ApxValue *v,const char *want){char *s=apx_json_encode(v);if(!s||strcmp(s,want))fprintf(stderr,"got=%s want=%s\n",s?s:"NULL",want);require(s&&!strcmp(s,want),"exact encoded bytes");free(s);}
static void rejected(const char *s,size_t n,const char *kind){
    require(!apx_json_parse(s,n),"invalid JSON rejected by guarded API");
    require(!strcmp(error_code,"A103")&&!strcmp(error_phase,"abi_input")&&!strcmp(error_kind,kind)&&!strcmp(error_message,!strcmp(kind,"numeric_out_of_range")?"number out of range":!strcmp(kind,"type_nesting_limit_exceeded")?"JSON nesting exceeds its bounded parser budget":"invalid input JSON"),"exact JSON diagnostic");
    apx_json_reset();encoded(parsed("17"),"17");apx_json_reset();
}
static const struct Number {const char *input,*output;int kind;int64_t signed_value;int is_unsigned;uint64_t unsigned_value;} numbers[]={
    {"0","0",2,0,1,0},{"-0","0",2,0,1,0},{"-1","-1",2,-1,0,0},
    {"9223372036854775807","9223372036854775807",2,INT64_MAX,1,INT64_MAX},
    {"-9223372036854775808","-9223372036854775808",2,INT64_MIN,0,0},
    {"9223372036854775808","9223372036854775808",3,0,1,UINT64_C(9223372036854775808)},
    {"18446744073709551615","18446744073709551615",3,0,1,UINT64_MAX},
    {"18446744073709551616","18446744073709551616",3,0,0,0},
    {"-9223372036854775809","-9223372036854775809",3,0,0,0},
    {"0.1","0.1",3,0,0,0},{"-0.0","-0.0",3,0,0,0},
    {"1e-999","0.0",3,0,0,0},{"-1e-999","-0.0",3,0,0,0},
    {"5e-324","5e-324",3,0,0,0},{"2.2250738585072014e-308","2.2250738585072014e-308",3,0,0,0},
    {"1.7976931348623157e308","1.7976931348623157e+308",3,0,0,0},
    {"42e0","42.0",3,0,0,0},{"1E+2","1e+02",3,0,0,0}
};
static void parse_path(void){
    (void)observed_strtoll; /* Keep the forbidden-call observer after removal. */
    apx_json_reset();tokens=copies=0;ApxValue *v=parsed("-9223372036854775808");
    require(v->kind==VINT&&v->integer==INT64_MIN,"signed MIN parse");
    require(memory_used==sizeof(ApxValue)&&tokens==0&&copies==0,"i64 token has no copy/allocation/strtoll");apx_json_reset();
}
static void format_path(void){
    ApxValue *v=apx_int(INT64_MIN);formats=lengths=0;encoded(v,"-9223372036854775808");
    require(!formats&&!lengths,"VINT encoding never calls variadic snprintf or strlen");apx_json_reset();
}
static void ordinary_path(void){
    char s[4099];s[0]='"';memset(s+1,'x',4096);s[4097]='"';s[4098]=0;
    copies=0;ApxValue *v=parsed(s);require(copies==1,"ordinary parser copies one complete run");
    /* Three runs (quotes/content), one growth copy, one caller-owned copy. */
    copies=0;encoded(v,s);require(copies==5,"ordinary encoder bulk run, not per-byte put");apx_json_reset();
}
static void whitespace_rejected(const char *s,size_t n){
    rejected(s,n,"invalid_json");
    /* The generated executable uses this ordinary parser with its outer trap. */
    if(!setjmp(escape)){(void)parse_complete(s,n);require(0,"invalid JSON rejected by ordinary parser");}
    else require(!strcmp(error_code,"A103")&&!strcmp(error_phase,"abi_input")&&!strcmp(error_kind,"invalid_json")&&!strcmp(error_message,"invalid input JSON"),"ordinary invalid JSON diagnostic");
    apx_json_reset();encoded(parsed("17"),"17");apx_json_reset();
}
static void nul_path(void){
    /* Raw NUL is not JSON whitespace, including every formerly accepted form. */
    static const struct {const char *wire;size_t size;} invalid[]={
        {"\0" "1",2},{"1\0",2},{"[0\0]",4},
        {"null\0",5},{"\"a\"\0",4},
        {"\0[\0true\0,\0false\0]\0",18},
        {"{\0\"a\"\0:\0null\0}\0",15},
        {"\0",1},{"\"\0\"",3},{"\v1",2},{"1\f",2},{"[0\x1f]",4}
    };
    for(size_t i=0;i<sizeof invalid/sizeof invalid[0];i++){
        /* No readable sentinel outside the explicitly supplied byte span. */
        char *bounded=malloc(invalid[i].size);require(bounded!=NULL,"invalid whitespace fixture");memcpy(bounded,invalid[i].wire,invalid[i].size);
        whitespace_rejected(bounded,invalid[i].size);free(bounded);
    }
    static const char legal[]={' ','\t','\r','\n'};
    for(size_t i=0;i<sizeof legal/sizeof legal[0];i++){
        char c=legal[i],wire[]={c,'{',c,'"','a','"',c,':',c,'[',c,'0',c,',',c,'1',c,']',c,'}',c};
        ApxValue *v=apx_json_parse(wire,sizeof wire);require(v!=NULL,"only legal JSON whitespace accepted");encoded(v,"{\"a\":[0,1]}");apx_json_reset();
    }
    ApxValue *escaped=parsed("\"\\u0000\"");require(escaped->length==1&&!escaped->text[0],"escaped NUL remains length-aware");encoded(escaped,"\"\\u0000\"");apx_json_reset();
}
static void numeric(void){
    for(size_t i=0;i<sizeof numbers/sizeof numbers[0];i++){
        const struct Number *n=&numbers[i];ApxValue *v=parsed(n->input);uint64_t u=123;
        require(apx_json_kind(v)==n->kind,"exact signed/float kind");
        if(n->kind==2)require(apx_json_integer(v)==n->signed_value,"exact signed accessor");
        require(apx_json_unsigned(v,&u)==n->is_unsigned,"unsigned accept/refuse");
        if(n->is_unsigned)require(u==n->unsigned_value,"unsigned retains all raw token bits");
        encoded(v,n->output);apx_json_reset();
    }
    static const char *bad[]={"","-","+1","01","-01","00","1.",".1","1e","1e+","1e-","--1","1 2","1x","NaN","Infinity","0x10","[1,]","{\"x\":1,}","[1 2]","truefalse","\"\\x\"","\"\\u000g\"","\"\\ud800\"","\"\\udfff\"","\"\\ud800\\u0041\""};
    for(size_t i=0;i<sizeof bad/sizeof bad[0];i++)rejected(bad[i],strlen(bad[i]),"invalid_json");
    static const char *range[]={"1e309","-1e309","1.7976931348623159e308"};
    for(size_t i=0;i<sizeof range/sizeof range[0];i++)rejected(range[i],strlen(range[i]),"numeric_out_of_range");
    char huge[4096];memset(huge,'9',sizeof huge);rejected(huge,sizeof huge,"numeric_out_of_range");
    huge[307]=0;encoded(parsed(huge),huge);uint64_t u=0;
    require(!apx_json_unsigned(parsed(huge),&u),"long finite integer retains text but exceeds unsigned64");apx_json_reset();
    huge[0]='0';huge[1]='.';memset(huge+2,'0',sizeof huge-2);encoded(apx_json_parse(huge,sizeof huge),"0.0");apx_json_reset();
    encoded(parsed(" \t\r\n-17 \n"),"-17");apx_json_reset();
}
static void strings(void){
    encoded(parsed("\"\\\"\\\\\\/\\b\\f\\n\\r\\t\\u0000\\u001f\\u007f\\u00e9\\ud83d\\ude00\""),"\"\\\"\\\\/\\b\\f\\n\\r\\t\\u0000\\u001f\177é😀\"");apx_json_reset();
    encoded(parsed("{\"x\\u0000y\":1,\"x\":2,\"é\":3}"),"{\"x\":2,\"x\\u0000y\":1,\"é\":3}");apx_json_reset();
    const char *duplicate="{\"x\":1,\"\\u0078\":2}";
    require(!apx_json_parse(duplicate,strlen(duplicate)),"escaped duplicate key rejected");
    require(!strcmp(error_code,"A104")&&!strcmp(error_kind,"duplicate_field"),"duplicate exact classification");apx_json_reset();
    const char *dup_bad="{\"x\":1,\"x\":1e999}";rejected(dup_bad,strlen(dup_bad),"numeric_out_of_range");
    char mutable[]="\"owned\"";ApxValue *owned=parsed(mutable);memset(mutable,'?',sizeof mutable-1);encoded(owned,"\"owned\"");apx_json_reset();
    const char *escape_wire="\"prefix\\u0000\\ud83d\\ude00\\\\\\\"suffix\"";
    for(size_t n=0;n<strlen(escape_wire);n++){
        char *bounded=malloc(n?n:1);require(bounded!=NULL,"escape truncation fixture");memcpy(bounded,escape_wire,n);
        rejected(bounded,n,"invalid_json");free(bounded);
    }
    static const unsigned char invalid[][6]={{'"',0xc0,0x80,'"'},{'"',0xed,0xa0,0x80,'"'},{'"',0xf4,0x90,0x80,0x80,'"'},{'"',0x80,'"'},{'"',0xe2,0x82,'"'}};
    static const size_t lengths[]={4,5,6,3,4};
    for(size_t i=0;i<sizeof lengths/sizeof lengths[0];i++)rejected((const char*)invalid[i],lengths[i],"invalid_json");
    for(unsigned c=0;c<32;c++){char raw[3]={'"',(char)c,'"'};rejected(raw,sizeof raw,"invalid_json");}
    static const size_t sizes[]={0,1,15,16,63,64,65,255,256,257,511,512,513,1023,1024,4095,4096,65536};
    for(size_t i=0;i<sizeof sizes/sizeof sizes[0];i++){
        size_t n=sizes[i];char *wire=malloc(n+3);require(wire!=NULL,"string fixture allocation");wire[0]='"';memset(wire+1,'q',n);wire[n+1]='"';wire[n+2]=0;
        ApxValue *v=parsed(wire);require(apx_json_length(v)==n,"string length at growth boundary");encoded(v,wire);
        /* Exactly sized, non-NUL-terminated supplied buffers catch overreads. */
        char *bounded=malloc(n+2);require(bounded!=NULL,"bounded fixture");memcpy(bounded,wire,n+2);require(apx_json_parse(bounded,n+2)!=NULL,"no sentinel needed");
        if(n)rejected(bounded,n+1,"invalid_json");
        free(bounded);free(wire);apx_json_reset();
    }
    char long_escape[4106];long_escape[0]='"';memset(long_escape+1,'r',4096);memcpy(long_escape+4097,"\\n\\u0000\"",9);
    ApxValue *escaped=apx_json_parse(long_escape,sizeof long_escape);require(escaped&&escaped->length==4098&&escaped->text[4096]=='\n'&&!escaped->text[4097],"long escaped string retains prefix and NUL");apx_json_reset();
    /* C-created strings intentionally keep old byte-preserving UTF-8 behavior. */
    const char raw[]={(char)0xff,'x',0};ApxValue *v=apx_string(raw,3);char *s=apx_json_encode(v);
    const char want[]={'"',(char)0xff,'x','\\','u','0','0','0','0','"',0};require(s&&!memcmp(s,want,sizeof want),"encoder preserves C-created non-UTF8 bytes and escapes NUL");free(s);apx_json_reset();
}
static uint32_t rng=UINT32_C(0x91a2b3c4);
static uint32_t random32(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static void fuzz(void){
    /* Independent libc integer oracle, including all magnitudes/signs. */
    for(unsigned i=0;i<6000;i++){
        uint64_t bits=((uint64_t)random32()<<32)|random32();int64_t n;memcpy(&n,&bits,sizeof n);
        char oracle[32];snprintf(oracle,sizeof oracle,"%"PRId64,n);ApxValue *v=parsed(oracle);
        require(v->kind==VINT&&v->integer==n,"random full-width signed token");encoded(v,oracle);apx_json_reset();
    }
    for(unsigned i=0;i<3000;i++){
        unsigned cp=random32()%0x110000;if(cp>=0xd800&&cp<=0xdfff)cp='a';
        char raw[4];size_t n=0;utf8(raw,&n,cp);char escaped[16];
        if(cp<0x10000)snprintf(escaped,sizeof escaped,"\"\\u%04x\"",cp);
        else {unsigned x=cp-0x10000;snprintf(escaped,sizeof escaped,"\"\\u%04x\\u%04x\"",0xd800+(x>>10),0xdc00+(x&1023));}
        ApxValue *v=parsed(escaped);require(v->length==n&&!memcmp(v->text,raw,n),"random escaped scalar exact UTF-8");
        char *wire=apx_json_encode(v);ApxValue *again=wire?apx_json_parse(wire,strlen(wire)):NULL;require(again&&equal(v,again,0),"random scalar roundtrip");free(wire);apx_json_reset();
    }
    /* Deterministic arbitrary bytes plus truncated/mutated valid seeds. Accepted
     * values must produce a stable canonical JSON reparse/re-encode. */
    static const char *seeds[]={"[1,-2,3.0]","{\"a\\u0000b\":\"é😀\"}","\"a\\nlong ordinary run\"","18446744073709551615","-1e-999"};
    for(unsigned i=0;i<6000;i++){
        char b[96];size_t n;
        if(i&1){const char *seed=seeds[random32()%5];n=strlen(seed);memcpy(b,seed,n);if(i&2)n=random32()%(n+1);else b[random32()%n]=(char)random32();}
        else {n=random32()%sizeof b;for(size_t j=0;j<n;j++)b[j]=(char)random32();}
        ApxValue *v=apx_json_parse(b,n);if(v){char *a=apx_json_encode(v);require(a!=NULL,"fuzz accepted encode");ApxValue *w=apx_json_parse(a,strlen(a));require(w!=NULL,"fuzz encoded reparse");char *c=apx_json_encode(w);require(c&&!strcmp(a,c),"fuzz stable exact spelling");free(a);free(c);}apx_json_reset();
    }
}
static void recovery(void){
    char depth[JSON_DEPTH*2+1];memset(depth,'[',JSON_DEPTH);depth[JSON_DEPTH]='0';memset(depth+JSON_DEPTH+1,']',JSON_DEPTH-1);
    rejected(depth,sizeof depth-1,"type_nesting_limit_exceeded");
    depth[JSON_DEPTH-1]='0';memset(depth+JSON_DEPTH,']',JSON_DEPTH-1);require(apx_json_parse(depth,JSON_DEPTH*2-1)!=NULL,"exact JSON depth accepted");apx_json_reset();
    require(!apx_json_parse("0",APX_INPUT+1),"input hard quota before read");
    size_t n=APX_INPUT;char *s=malloc(n);require(s!=NULL,"input limit fixture");memset(s,' ',n);s[0]='0';encoded(apx_json_parse(s,n),"0");free(s);apx_json_reset();
    fail_calloc=1;require(!apx_json_parse("17",2)&&!fail_calloc&&!strcmp(error_code,"R204"),"guarded parse allocator failure");encoded(parsed("18"),"18");apx_json_reset();
    /* Fail between the value and byte allocation of a plain string, then retry
     * without reset; partial storage must not masquerade as a cached value. */
    memory_used=APX_MEMORY-sizeof(ApxValue);require(!apx_json_parse("\"abc\"",5)&&!strcmp(error_code,"R204"),"partial string parse OOM contained");
    memory_used=sizeof(ApxValue);encoded(parsed("\"abc\""),"\"abc\"");apx_json_reset();
    ApxValue *v=parsed("\"stable\"");fail_malloc=1;require(!apx_json_encode(v)&&!fail_malloc,"caller-owned encode malloc failure contained");encoded(v,"\"stable\"");
    size_t before=memory_used;memory_used=APX_MEMORY;require(!apx_json_encode(v)&&!strcmp(error_code,"R204"),"encoder arena exhaustion trapped");memory_used=before;encoded(v,"\"stable\"");
    char *owned=apx_json_encode(v);fuel_used=73;call_depth=5;apx_json_reset();require(owned&&!strcmp(owned,"\"stable\"")&&fuel_used==73&&!call_depth&&!memory_used&&!arena_chunks,"owned output survives arena reset and fuel preserved");free(owned);
    char *large=malloc(APX_OUTPUT);require(large!=NULL,"output limit fixture");memset(large,'a',APX_OUTPUT);
    v=apx_string(large,APX_OUTPUT-2);owned=apx_json_encode(v);require(owned&&strlen(owned)==APX_OUTPUT,"exact hard output quota");free(owned);apx_json_reset();
    v=apx_string(large,APX_OUTPUT-1);require(!apx_json_encode(v)&&!strcmp(error_code,"R203")&&!strcmp(error_kind,"output_limit_exceeded"),"output hard quota rejection");free(large);apx_json_reset();encoded(parsed("19"),"19");apx_json_reset();
    /* Public failure boundary must restore the caller's existing trap target. */
    if(!setjmp(escape)){require(!apx_json_parse("?",1),"nested public parse fails locally");apx_trap("outer");require(0,"outer trap");}
    else require(!strcmp(error_message,"outer"),"public parser restores previous trap target");
}
#ifdef APX_JSON_LINKED
int main(void){
    if(setjmp(escape)){fprintf(stderr,"linked JSON unexpected trap %s\n",error_message);return 1;}
    for(volatile size_t i=0;i<sizeof numbers/sizeof numbers[0];i++){
        ApxValue *input=parsed(numbers[i].input);fuel_used=0;fuel_limit=1000000;
#if APX_JSON_LINKED == 0
        if(numbers[i].kind==2){encoded(apx_entry(input),numbers[i].output);require(fuel_used==1&&!call_depth,"linked i64 exact fuel/depth");}
        else if(!setjmp(escape)){(void)apx_entry(input);require(0,"linked rejects non-i64");}
        else require(!strcmp(error_kind,"numeric_out_of_range")&&!strcmp(error_message,"expected i64")&&fuel_used==0,"linked range before fuel");
#else
        (void)input;
#endif
        apx_json_reset();
    }
#if APX_JSON_LINKED == 1
    const char *wire="[\"ordinary text\",\"a\\u0000b\",\"é😀\",\"\\n\\t\\\\\\\"\"]";
    fuel_used=0;encoded(apx_entry(parsed(wire)),wire);require(fuel_used==1&&!call_depth,"linked string array pipeline");apx_json_reset();
    ApxValue *input=parsed("[\"ok\"]");size_t before=memory_used;memory_used=APX_MEMORY;
    if(!setjmp(escape)){(void)apx_entry(input);require(0,"linked validation OOM");}
    else require(!strcmp(error_code,"R204"),"linked allocation failure exact");
    memory_used=before;fuel_used=0;encoded(apx_entry(input),"[\"ok\"]");require(fuel_used==1&&!call_depth,"linked OOM retry");apx_json_reset();
#endif
    printf("linked JSON %d: %u checks PASS\n",APX_JSON_LINKED,checks);return 0;
}
#else
static char directory[4096];
static void cleanup(void){
    if(!*directory)return;
    const char *files[]={"p.ssa","p.s","p.o","linked"};char p[4200];
    for(size_t i=0;i<sizeof files/sizeof files[0];i++){snprintf(p,sizeof p,"%s/%s",directory,files[i]);unlink(p);}
    if(!rmdir(directory))*directory=0;
}
static void save(const char *p,const char *s){FILE *f=fopen(p,"wb");require(f&&fwrite(s,1,strlen(s),f)==strlen(s)&&!fclose(f),"save generated artifact");}
static void run(char *const argv[]){fflush(NULL);pid_t p=fork();require(p>=0,"fork");if(!p){execvp(argv[0],argv);_exit(127);}int status;require(waitpid(p,&status,0)==p&&WIFEXITED(status)&&!WEXITSTATUS(status),"generated tool/entry success");}
static void linked(void){
    const char *tmp=getenv("TMPDIR");require(tmp&&strlen(tmp)+24<sizeof directory,"TMPDIR available");snprintf(directory,sizeof directory,"%s/apx-json-XXXXXX",tmp);require(mkdtemp(directory)!=NULL,"private fixture directory");require(!atexit(cleanup),"register fixture cleanup");
    char ssa[4200],assembly[4200],obj[4200],exe[4200],define[48];snprintf(ssa,sizeof ssa,"%s/p.ssa",directory);snprintf(assembly,sizeof assembly,"%s/p.s",directory);snprintf(obj,sizeof obj,"%s/p.o",directory);snprintf(exe,sizeof exe,"%s/linked",directory);
    const char *sources[]={"M integer\nF main x:i64->i64\n  R x\n","M strings\nF main x:[str]->[str]\n  R x\n"};
    for(size_t i=0;i<2;i++){
        char *il=NULL,*canon=NULL,*diag=NULL;int code=apx_compile_source(sources[i],&il,&canon,&diag);if(code)fprintf(stderr,"%s\n",diag);require(!code&&il,"compile JSON entry");save(ssa,il);free(il);free(canon);free(diag);
        char *qbe[]={".build/tools/qbe","-t","amd64_sysv","-o",assembly,ssa,NULL};run(qbe);char *as[]={"as",assembly,"-o",obj,NULL};run(as);snprintf(define,sizeof define,"-DAPX_JSON_LINKED=%zu",i);
        char *cc[]={"cc","-std=c99","-O2","-Wall","-Wextra","-Werror","-Wno-unused-function","-no-pie",define,
#ifdef APX_ARENA_ASAN
            "-O1","-g","-fno-omit-frame-pointer","-fsanitize=address,undefined",
#endif
            "tests/test_native_json.c",obj,"-o",exe,NULL};run(cc);char *entry[]={exe,NULL};run(entry);
    }
    cleanup();require(!*directory,"remove owned generated fixtures");
}
int main(int argc,char **argv){
    if(argc==2){if(!strcmp(argv[1],"--parse-path"))parse_path();else if(!strcmp(argv[1],"--format-path"))format_path();else if(!strcmp(argv[1],"--ordinary-path"))ordinary_path();else if(!strcmp(argv[1],"--nul-path"))nul_path();else return 2;printf("native JSON %s: %u checks PASS\n",argv[1],checks);return 0;}
    parse_path();format_path();ordinary_path();nul_path();numeric();strings();fuzz();recovery();linked();apx_json_reset();printf("native JSON: %u checks PASS\n",checks);return 0;
}
#endif
