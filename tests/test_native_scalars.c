/* Native scalar representation and real linked-entry allocation regression.
 * The private runtime inclusion exposes accounting/traps only to this test. */
#define _POSIX_C_SOURCE 200809L
#define APX_LIBRARY_ONLY
#include "../runtime/apx_runtime.c"
#include "../compiler/apx_frontend.h"
#include <sys/types.h>
#include <sys/wait.h>

static unsigned checks;
static void require(int ok,const char *message){
    checks++;if(!ok){fprintf(stderr,"native scalars FAIL: %s\n",message);exit(1);}
}
#ifdef APX_SCALAR_LINKED
static size_t observe(int64_t count){
    reset_memory();fuel_used=0;fuel_limit=1000000;
    if(setjmp(escape)){fprintf(stderr,"linked scalar trap: %s\n",error_kind);exit(1);}
    ApxValue *answer=apx_entry(apx_int(count));
    require(integer(answer)==count,"linked entry complete output");
    require(call_depth==0,"linked entry balances logical depth");
    printf("scalar allocation n=%"PRId64" requested_bytes=%zu fuel=%"PRIu64"\n",count,memory_used,fuel_used);
    return memory_used;
}
int main(void){
    static const int64_t counts[]={0,1,2048};size_t first=0;
    for(size_t i=0;i<sizeof counts/sizeof counts[0];i++){
        size_t bytes=observe(counts[i]);
        if(!i)first=bytes;
        else require(bytes==first,"scalar-only loop allocation is independent of iteration count");
    }
    reset_memory();require(!arena_chunks&&!memory_used,"linked arena cleanup");
    printf("linked native scalars: %u checks PASS\n",checks);return 0;
}
#else
static char fixture_directory[]=".build/native-scalars-XXXXXX";
static int fixture_active;
static const char *const fixture_files[]={"program.ssa","program.s","program.o","linked","source.apx","input.json","output"};
static void cleanup(void){
    if(fixture_active){
        char path[4096];
        for(size_t i=0;i<sizeof fixture_files/sizeof fixture_files[0];i++){
            snprintf(path,sizeof path,"%s/%s",fixture_directory,fixture_files[i]);unlink(path);
        }
        if(rmdir(fixture_directory))fprintf(stderr,"native scalars cleanup failed: %s\n",fixture_directory);
        else fixture_active=0;
    }
}
static const char *numeric=
    "M scalars\nF step x:i64 enabled:bool->i64\n  ? enabled\n    R (+ x 1)\n  R x\n"
    "F flip b:bool->bool\n  R (! b)\n"
    "F main n:i64->i64\n  L i 0\n  L total 0\n  L active true\n"
    "  W (< i n)\n    S total (step total active)\n    S active (flip (flip active))\n    S i (+ i 1)\n  R total\n";
static void save(const char *path,const char *text){FILE *fp=fopen(path,"wb");require(fp!=NULL,"open fixture");require(fwrite(text,1,strlen(text),fp)==strlen(text)&&!fclose(fp),"save fixture");}
static char *readall(const char *path){
    FILE *fp=fopen(path,"rb");require(fp!=NULL,"open result");require(!fseek(fp,0,SEEK_END),"seek result");
    long n=ftell(fp);require(n>=0&&n<12*1024*1024,"bounded result");rewind(fp);
    char *s=calloc(1,(size_t)n+1);require(s&&fread(s,1,(size_t)n,fp)==(size_t)n&&!fclose(fp),"read result");return s;
}
static int execute(char *const args[],const char *output){
    fflush(NULL);pid_t pid=fork();require(pid>=0,"fork tool");
    if(!pid){if(output&&!freopen(output,"wb",stdout))_exit(126);execvp(args[0],args);_exit(127);}
    int status;require(waitpid(pid,&status,0)==pid&&WIFEXITED(status),"wait tool");return WEXITSTATUS(status);
}
static void representation(const char *dir,int inspect_ir){
    char *il=NULL,*canonical=NULL,*diagnostic=NULL;
    int code=apx_compile_source(numeric,&il,&canonical,&diagnostic);
    if(code)fprintf(stderr,"%s\n",diagnostic);
    require(!code&&il,"compile scalar fixture");
    if(inspect_ir){
        require(strstr(il,"function l $f0(l %p0, w %p1)")!=NULL,"i64/bool native parameter ABI");
        require(strstr(il,"function w $f1(w %p0)")!=NULL,"bool native return ABI");
        require(strstr(il,"storew ")&&strstr(il,"=w loadw "),"consistent bool local widths");
        require(!strstr(il,"$apx_builtin("),"scalar fixture avoids generic builtin dispatch");
    }
    char ssa[4096],assembly[4096],object[4096],exe[4096];
    snprintf(ssa,sizeof ssa,"%s/program.ssa",dir);snprintf(assembly,sizeof assembly,"%s/program.s",dir);
    snprintf(object,sizeof object,"%s/program.o",dir);snprintf(exe,sizeof exe,"%s/linked",dir);save(ssa,il);
    char *qbe[]={".build/tools/qbe","-t","amd64_sysv","-o",assembly,ssa,NULL};require(!execute(qbe,NULL),"real QBE lowering");
    char *as[]={"as",assembly,"-o",object,NULL};require(!execute(as,NULL),"assemble generated program");
    const char *cc=getenv("CC");if(!cc)cc="cc";
    char *link[]={(char*)cc,"-std=c99","-O2","-Wall","-Wextra","-Werror","-no-pie","-DAPX_SCALAR_LINKED",
#ifdef APX_ARENA_ASAN
        "-O1","-g","-fno-omit-frame-pointer","-fsanitize=address,undefined",
#endif
        "tests/test_native_scalars.c",object,"-o",exe,NULL};
    require(!execute(link,NULL),"link allocation observer to actual generated entry");
    char *run[]={exe,NULL};require(!execute(run,NULL),"constant real linked-entry allocation");
    free(il);free(canonical);free(diagnostic);
}
static uint64_t native(const char *dir,const char *source,const char *input,const char *fuel,const char *expected,int success){
    char path[4096],in[4096],out[4096];snprintf(path,sizeof path,"%s/source.apx",dir);snprintf(in,sizeof in,"%s/input.json",dir);snprintf(out,sizeof out,"%s/output",dir);
    save(path,source);save(in,input);
    char *args[]={"bin/apodexis","run",path,"--input",in,"--fuel",(char*)fuel,"--json",NULL};int code=execute(args,out);
    char *text=readall(out);ApxValue *v=apx_json_parse(text,strlen(text));require(v!=NULL,"valid native envelope");
    if(code!=(success?0:2))fprintf(stderr,"source:\n%s\nresponse:\n%s\n",source,text);
    require(code==(success?0:2),"expected native exit");
    if(success){
        char *actual=apx_json_encode(apx_json_get(v,"output"));ApxValue *want=apx_json_parse(expected,strlen(expected));
        char *encoded=want?apx_json_encode(want):NULL;
        if(!actual||!encoded||strcmp(actual,encoded))fprintf(stderr,"response: %s\n",text);
        require(actual&&encoded&&!strcmp(actual,encoded),"complete native output");free(actual);free(encoded);
    }
    else require(!strcmp(apx_json_string(apx_json_get(apx_json_get(v,"diagnostic"),"kind")),expected),"native error kind");
    ApxValue *meter=apx_json_get(v,"fuel_used");uint64_t used=0;
    require(meter&&apx_json_unsigned(meter,&used),"exact native fuel field");
    free(text);reset_memory();return used;
}
static void semantics(const char *dir){
    native(dir,numeric,"0","1000000","0",1);native(dir,numeric,"-2","1000000","0",1);native(dir,numeric,"3","1000000","3",1);
    native(dir,"M bools\nF id b:bool->bool\n  R b\nF main b:bool->bool\n  L x (id b)\n  S x (! x)\n  R (| (& x true) false)\n","false","1000000","true",1);
    native(dir,"M eager\nF fail->bool\n  Z \"rhs\"\nF main->bool\n  R (& false (fail))\n","null","1000000","trap",0);
    native(dir,"M eager\nF fail->bool\n  Z \"rhs\"\nF main->bool\n  R (| true (fail))\n","null","1000000","trap",0);
}
static void arithmetic(const char *dir){
    static const struct {const char *expr,*expected;int success;} cases[]={
        {"(+ 9223372036854775807 0)","9223372036854775807",1},
        {"(- -9223372036854775808 0)","-9223372036854775808",1},
        {"(* -9223372036854775808 1)","-9223372036854775808",1},
        {"(/ -9223372036854775808 1)","-9223372036854775808",1},
        {"(% -9223372036854775808 1)","0",1},
        {"(/ -7 3)","-2",1},{"(% -7 3)","-1",1},
        {"(/ 7 -3)","-2",1},{"(% 7 -3)","1",1},
        {"(+ 9223372036854775807 1)","integer_overflow",0},
        {"(+ -9223372036854775808 -1)","integer_overflow",0},
        {"(- -9223372036854775808 1)","integer_overflow",0},
        {"(- 9223372036854775807 -1)","integer_overflow",0},
        {"(* 9223372036854775807 2)","integer_overflow",0},
        {"(* -9223372036854775808 -1)","integer_overflow",0},
        {"(/ -9223372036854775808 -1)","integer_overflow",0},
        {"(% -9223372036854775808 -1)","integer_overflow",0},
        {"(/ 1 0)","divide_by_zero",0},{"(% 1 0)","divide_by_zero",0}
    };
    for(size_t i=0;i<sizeof cases/sizeof cases[0];i++){
        char source[512];snprintf(source,sizeof source,"M arithmetic\nF main->i64\n  R %s\n",cases[i].expr);
        require(native(dir,source,"null","1000000",cases[i].expected,cases[i].success)==1,"arithmetic statement fuel");
    }
    native(dir,"M comparisons\nF main->[bool]\n  R [(< -9223372036854775808 9223372036854775807) (<= 1 1) (> -1 0) (>= 1 1) (= false false) (!= true false) (= 1 2)]\n","null","1000000","[true,true,false,true,true,true,false]",1);
    native(dir,"M spills\nF many a:i64 b:bool c:i64 d:bool e:i64 f:bool g:i64 h:bool i:i64 j:bool->bool\n  A (= (+ (+ a c) (+ (+ e g) i)) 15)\n  R (& (& b d) (& f (| h j)))\nF main->bool\n  R (many 1 true 2 true 3 true 4 false 5 true)\n","null","1000000","true",1);
}
static void boundaries(const char *dir){
    native(dir,
        "M mixed\nT Row struct\n  n:i64\n  b:bool\n  nested:[[i64]]\n"
        "F make n:i64 b:bool->Row\n  R (Row nested=[[n]] b=b n=n)\n"
        "F main input:Row->[Row]\n  L row (make (+ input.n 1) (! input.b))\n"
        "  L alias row\n  S row (with row b=(= input.n 4) n=(* row.n 2))\n  R [alias row]\n",
        "{\"n\":4,\"b\":false,\"nested\":[[0]]}","1000000",
        "[{\"n\":5,\"b\":true,\"nested\":[[5]]},{\"n\":10,\"b\":true,\"nested\":[[5]]}]",1);
    native(dir,
        "M each\nF main flags:[bool]->[bool]\n  L out (buf bool (# flags))\n"
        "  E flag flags\n    L active flag\n    S active (! active)\n    P out active\n"
        "  A (= (count out) (# flags))\n  A (= (cap out) (# flags))\n  R (finish out)\n",
        "[true,false,true]","1000000","[false,true,false]",1);
    native(dir,"M each\nF main rows:[i64]->i64\n  L n 0\n  E row rows\n    S n (+ n row)\n  R n\n","[-2,0,9]","1000000","7",1);
    native(dir,"M variant\nF choose b:bool->result bool i64\n  ? b\n    R (ok false)\n  R (err -7)\nF main b:bool->i64\n  C (choose b)\n    ok flag\n      ? (! flag)\n        R 42\n      R 0\n    err n\n      R (+ n 1)\n","true","1000000","42",1);
    native(dir,"M variant\nF choose b:bool->result bool i64\n  ? b\n    R (ok false)\n  R (err -7)\nF main b:bool->i64\n  C (choose b)\n    ok flag\n      ? (! flag)\n        R 42\n      R 0\n    err n\n      R (+ n 1)\n","false","1000000","-6",1);
    native(dir,"M option\nF main->bool\n  L value:option bool (some true)\n  C value\n    some flag\n      S flag (! flag)\n      R flag\n    none\n      R true\n","null","1000000","false",1);
    native(dir,"M map\nF main->i64\n  L m (map_put (map i64) \"n\" 7)\n  A (map_has m \"n\")\n  C (map_get m \"n\")\n    some n\n      R (+ n (str_chars (str_i64 n)))\n    none\n      R -1\n","null","1000000","8",1);
    native(dir,"M array\nF main->bool\n  L rows (array_set [true false] 1 true)\n  A (= rows [true true])\n  C (array_get rows 1)\n    some flag\n      R (& flag rows[0])\n    none\n      R false\n","null","1000000","true",1);
    native(dir,"M casts\nF main->[i64]\n  A (= (+ 1.5 0.5) 2.0)\n  R [(i64 (f64 42)) (i64 -3.9) (i64 7) (i64 1e300) (i64 -1e300)]\n","null","1000000","[42,-3,7,9223372036854775807,-9223372036854775808]",1);
    native(dir,"M host\nF main n:i64->result str str\n  R (random_hex n)\n","4","1000000","{\"tag\":\"err\",\"error\":\"capability_denied\"}",1);
}
static void metering(const char *dir){
    static const unsigned used[]={0,1,2,3,4,4,6,7,8,9,10,11,12,13,14};
    for(unsigned i=0;i<sizeof used/sizeof used[0];i++){
        char fuel[32];snprintf(fuel,sizeof fuel,"%u",i);
        require(native(dir,numeric,"1",fuel,i==14?"1":"fuel_exhausted",i==14)==used[i],"fuel at each helper/loop charge boundary");
    }
    const char *overflow="M precedence\nF main->i64\n  L x 0\n  R (+ 9223372036854775807 1)\n";
    require(native(dir,overflow,"null","1","fuel_exhausted",0)==1,"fuel precedes overflow in next statement");
    require(native(dir,overflow,"null","2","integer_overflow",0)==2,"overflow after statement charge");
    const char *recursive="M recursion\nF recurse n:i64->i64\n  ? (= n 0)\n    R 0\n  R (recurse (- n 1))\nF main n:i64->i64\n  R (recurse n)\n";
    require(native(dir,recursive,"254","1000000","0",1)==511,"last allowed logical recursion depth");
    require(native(dir,recursive,"255","1000000","recursion_limit_exceeded",0)==511,"recursion trap before deeper statement charge");
}
static void declared_tests(const char *dir){
    const char *source="M library\nF unwrap v:option bool n:i64->i64\n  C v\n    some flag\n      ? flag\n        R (+ n 1)\n      R n\n    none\n      R -1\nX direct\n  A (= (unwrap (some true) 6) 7)\n  A (= (unwrap (none) 6) -1)\n";
    char path[4096],out[4096];snprintf(path,sizeof path,"%s/source.apx",dir);snprintf(out,sizeof out,"%s/output",dir);save(path,source);
    char *args[]={"bin/apodexis","test",path,"--json",NULL};require(!execute(args,out),"source tests without main execute natively");
    char *text=readall(out);ApxValue *v=apx_json_parse(text,strlen(text));require(v!=NULL,"source test envelope");
    ApxValue *tests=apx_json_get(v,"tests");require(apx_json_length(tests)==1,"one source test really executed");
    require(!strcmp(apx_json_string(apx_json_get(apx_json_item(tests,0),"status")),"ok"),"source test success");
    free(text);reset_memory();
}
/* Exercise the same raw helpers under the default ASan/UBSan target, including
 * every invalid arithmetic edge. No generated-worker fallback is involved. */
static void checked_error(int op,int64_t x,int64_t y,const char *kind){
    size_t before=memory_used;
    if(!setjmp(escape)){(void)apx_i64_checked(op,x,y);require(0,"checked operation must trap");}
    else require(!strcmp(error_kind,kind),"raw arithmetic diagnostic");
    require(memory_used==before,"raw failure allocates no values");
}
static void runtime_helpers(void){
    require(apx_i64_checked(1,INT64_MAX,-1)==INT64_MAX-1,"checked add extreme");
    require(apx_i64_checked(2,INT64_MIN,-1)==INT64_MIN+1,"checked subtract extreme");
    require(apx_i64_checked(3,INT64_MIN,1)==INT64_MIN,"checked multiply extreme");
    require(apx_i64_checked(4,-7,3)==-2&&apx_i64_checked(5,-7,3)==-1,"signed quotient/remainder");
    require(memory_used==0,"successful raw arithmetic allocates no values");
    checked_error(1,INT64_MAX,1,"integer_overflow");checked_error(1,INT64_MIN,-1,"integer_overflow");
    checked_error(2,INT64_MIN,1,"integer_overflow");checked_error(2,INT64_MAX,-1,"integer_overflow");
    checked_error(3,INT64_MAX,2,"integer_overflow");checked_error(3,INT64_MIN,-1,"integer_overflow");
    checked_error(4,INT64_MIN,-1,"integer_overflow");checked_error(5,INT64_MIN,-1,"integer_overflow");
    checked_error(4,1,0,"divide_by_zero");checked_error(5,1,0,"divide_by_zero");
    require(apx_i64_raw(apx_int(INT64_MIN))==INT64_MIN,"checked scalar unboxing");
    require(apx_truth(apx_bool(1))==1,"checked bool unboxing");apx_assert_raw(1);
    if(!setjmp(escape)){apx_assert_raw(0);require(0,"false raw assertion traps");}
    else require(!strcmp(error_code,"X101")&&!strcmp(error_kind,"assertion_failed"),"raw assertion diagnostic");
    if(!setjmp(escape)){(void)apx_i64_raw(apx_bool(0));require(0,"invalid scalar unbox traps");}
    else require(!strcmp(error_kind,"type_mismatch"),"unbox keeps type checks");
    reset_memory();require(!memory_used&&!arena_chunks,"runtime helper cleanup");
}
int main(int argc,char **argv){
    int allocation_only=argc==2&&!strcmp(argv[1],"--allocation-only");
    require(argc==1||allocation_only,"valid test arguments");
    char *dir=fixture_directory;require(mkdtemp(dir)!=NULL,"owned fixture directory");
    fixture_active=1;require(!atexit(cleanup),"register failure cleanup");
    runtime_helpers();representation(dir,!allocation_only);
    if(!allocation_only){semantics(dir);arithmetic(dir);boundaries(dir);metering(dir);declared_tests(dir);}
    cleanup();require(!fixture_active,"native fixture cleanup");
    printf("native scalars: %u checks; typed IR/linked allocation/native semantics PASS\n",checks);return 0;
}
#endif
