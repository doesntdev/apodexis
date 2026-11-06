/* Call/metering seams: checked IL, real generated ELF, independent charge traces.
 * The private runtime inclusion exposes traps/accounting only to this observer. */
#define _POSIX_C_SOURCE 200809L
#define APX_LIBRARY_ONLY
#include "../runtime/apx_runtime.c"
#include "../compiler/apx_frontend.h"
#include <sys/wait.h>
static unsigned checks;
static void require(int ok,const char *s){checks++;if(!ok){fprintf(stderr,"native calls FAIL: %s\n",s);exit(1);}}
struct Charge {unsigned amount;int depth;};
struct Diagnostic {const char *code,*phase,*kind,*message;};
#ifdef APX_CALLS_LINKED
static const struct Diagnostic fuel_error={"R201","runtime","fuel_exhausted","fuel exhausted"};
static const struct Diagnostic depth_error={"R205","runtime","recursion_limit_exceeded","native recursion limit exceeded"};
static const struct Diagnostic missing_error={"T105","type","missing_return","function did not return"};
#endif
static const struct Diagnostic zero_error={"R101","runtime","divide_by_zero","divide by zero"};
static const struct Diagnostic overflow_error={"R102","runtime","integer_overflow","integer overflow"};
static const struct Diagnostic trap_error={"R999","runtime","trap","rhs"};
static const struct Diagnostic assert_error={"X101","test","assertion_failed","assertion failed"};
struct Vector {const char *input,*output;const struct Diagnostic *error;int depth;const struct Charge *trace;size_t count;};
struct Fixture {const char *source;const struct Vector *vectors;size_t count;};
#define C1 {1,1}
#define C2 {1,2}
#define LOOP {2,1}
#define TRACE(...) (const struct Charge[]){__VA_ARGS__}
#define V(in,out,err,depth,...) {in,out,err,depth,TRACE(__VA_ARGS__),sizeof(TRACE(__VA_ARGS__))/sizeof(struct Charge)}
static const struct Fixture fixtures[]={
    {"M calls\nF leaf x:i64 b:bool->i64\n  R (+ x x)\nF main n:i64->i64\n  L b true\n  R (leaf n b)\n",
     (const struct Vector[]){V("17","34",NULL,0,C1,C1,C2)},1},
    {"M mixed\nF pick a:i64 b:bool c:i64 d:bool e:i64 f:bool g:i64 h:bool i:i64 j:bool->bool\n  R (& (= (+ (+ a c) (+ (+ e g) i)) 15) (& (& b d) (& f (| h j))))\nF main->bool\n  L a 99\n  L b false\n  R (pick 1 true 2 true 3 true 4 false 5 true)\n",
     (const struct Vector[]){V("null","true",NULL,0,C1,C1,C1,C2)},1},
    {"M once\nF twice x:i64 y:i64->i64\n  R (+ x x)\nF main->i64\n  R (twice (+ 1 1) (+ 4 1))\n",
     (const struct Vector[]){V("null","4",NULL,0,C1,C2)},1},
    {"M args\nF twice x:i64 y:i64->i64\n  R (+ x x)\nF main->i64\n  R (twice (+ 1 1) (/ 1 0))\n",
     (const struct Vector[]){V("null",NULL,&zero_error,1,C1)},1},
    {"M overflow\nF leaf x:i64->i64\n  R (+ x x)\nF main n:i64->i64\n  R (leaf n)\n",
     (const struct Vector[]){V("9223372036854775807",NULL,&overflow_error,2,C1,C2)},1},
    {"M first\nF twice x:i64 y:i64->i64\n  R (+ x x)\nF main->i64\n  R (twice (/ 1 0) (+ 9223372036854775807 1))\n",
     (const struct Vector[]){V("null",NULL,&zero_error,1,C1)},1},
    {"M eager\nF leaf->bool\n  R (& false (= (/ 1 0) 0))\nF main->bool\n  R (leaf)\n",
     (const struct Vector[]){V("null",NULL,&zero_error,2,C1,C2)},1},
    {"M trap\nF fail->bool\n  Z \"rhs\"\nF main->bool\n  R (| true (fail))\n",
     (const struct Vector[]){V("null",NULL,&trap_error,2,C1,C2)},1},
    {"M loops\nF leaf x:i64->i64\n  R (+ x x)\nF main n:i64->i64\n  L i 0\n  L sum 0\n  W (< i n)\n    S sum (+ sum (leaf i))\n    S i (+ i 1)\n  E v [2 3]\n    S sum (+ sum (leaf v))\n  ? (> sum 0)\n    R sum\n  R -1\n",
     (const struct Vector[]){
        V("0","10",NULL,0,C1,C1,C1,C1,LOOP,C1,C2,LOOP,C1,C2,C1,C1),
        V("2","12",NULL,0,C1,C1,C1,LOOP,C1,C2,C1,LOOP,C1,C2,C1,C1,LOOP,C1,C2,LOOP,C1,C2,C1,C1)},2},
    {"M condition\nF less x:i64 y:i64->bool\n  R (< x y)\nF main n:i64->i64\n  L i 0\n  W (less i n)\n    S i (+ i 1)\n  R i\n",
     (const struct Vector[]){V("0","0",NULL,0,C1,C1,C2,C1),V("2","2",NULL,0,C1,C1,C2,LOOP,C1,C2,LOOP,C1,C2,C1)},2},
    {"M containers\nT Row struct\n  n:i64\n  b:bool\nF twice x:i64->i64\n  R (+ x x)\nF main input:i64->[i64]\n  L row (Row b=(= (twice input) 4) n=(twice (twice input)))\n  L opt:option i64 (some row.n)\n  C opt\n    some value\n      R [value (twice input)]\n    none\n      R []\n",
     (const struct Vector[]){V("2","[8,4]",NULL,0,C1,C2,C2,C2,C1,C1,C1,C2)},1},
    {"M recursion\nF leaf x:i64->i64\n  R (+ x x)\nF recur n:i64->i64\n  ? (= n 0)\n    R (leaf 7)\n  R (recur (- n 1))\nF main n:i64->i64\n  R (recur n)\n",NULL,0},
    {"M direct\nF main n:i64->i64\n  ? (= n 0)\n    R n\n  R (main (- n 1))\n",NULL,0},
    {"M mutual\nF first n:i64->i64\n  ? (= n 0)\n    R 0\n  R (second (- n 1))\nF second n:i64->i64\n  ? (= n 0)\n    R 0\n  R (first (- n 1))\nF main n:i64->i64\n  R (first n)\n",NULL,0},
    {"M missing\nF maybe n:i64->i64\n  ? (= n 0)\n    R 7\nF main n:i64->i64\n  R (maybe n)\n",
     NULL,0},
    {"M assertion\nF positive n:i64->bool\n  R (> n 0)\nF main n:i64->i64\n  A (positive n)\n  R n\n",
     (const struct Vector[]){V("1","1",NULL,0,C1,C2,C1),V("-1",NULL,&assert_error,1,C1,C2)},2},
    {"M nested\nF flip b:bool->bool\n  R (! b)\nF main b:bool->bool\n  R (flip (flip b))\n",
     (const struct Vector[]){V("true","true",NULL,0,C1,C2,C2),V("false","false",NULL,0,C1,C2,C2)},2},
    {"M library\nF quotient n:i64 d:i64->i64\n  R (/ n d)\nX fails\n  A (= (quotient 1 0) 0)\nX recovers\n  A (= (quotient 8 2) 4)\n",NULL,0}
};
#undef V
#undef TRACE
#undef C1
#undef C2
#undef LOOP
#ifdef APX_CALLS_LINKED
static unsigned operations, probes;
static int compatibility;
static int operation_depth[1024];static uint64_t operation_fuel[1024];
static int64_t operation_left[1024];
int64_t __wrap_apx_i64_checked(int32_t op,int64_t x,int64_t y){
    require(operations<1024,"bounded arithmetic observations");
    operation_depth[operations]=call_depth;operation_fuel[operations]=fuel_used;operation_left[operations]=x;operations++;
    if(compatibility>0){int before=call_depth;apx_charge(3);apx_enter();apx_leave();require(call_depth==before,"compatibility depth interleaves with inline execution");}
    if(compatibility<0){while(call_depth)apx_leave();}
    return apx_i64_checked(op,x,y);
}
static void diagnostic_is(const struct Diagnostic *d){
    if(strcmp(error_code,d->code)||strcmp(error_phase,d->phase)||strcmp(error_kind,d->kind)||strcmp(error_message,d->message))
        fprintf(stderr,"actual diagnostic=%s/%s/%s/%s expected=%s/%s/%s/%s\n",error_code,error_phase,error_kind,error_message,d->code,d->phase,d->kind,d->message);
    require(!strcmp(error_code,d->code)&&!strcmp(error_phase,d->phase)&&!strcmp(error_kind,d->kind)&&!strcmp(error_message,d->message),"exact diagnostic code/phase/kind/message");
}
static void observe(const struct Vector *v,uint64_t seed,uint64_t limit,int initial_depth,uint64_t used,int depth,const struct Diagnostic *error){
    ++probes;reset_memory();fuel_used=seed;fuel_limit=limit;call_depth=initial_depth;operations=0;
    ApxValue *input=apx_json_parse(v->input,strlen(v->input));require(input!=NULL,"observer input");
    if(!setjmp(escape)){
        ApxValue *out=apx_entry(input);require(error==NULL,"expected generated failure");
        const char *expected=v->output?v->output:"<expected failure>";
        char *text=apx_json_encode(out);if(!text||strcmp(text,expected))fprintf(stderr,"actual=%s expected=%s\n",text?text:"NULL",expected);
        require(text&&!strcmp(text,expected),"complete generated output");free(text);
    }else{require(error!=NULL,"unexpected generated failure");diagnostic_is(error);}
    if(fuel_used!=used||call_depth!=depth)fprintf(stderr,"state actual=%"PRIu64"/%d expected=%"PRIu64"/%d seed=%"PRIu64" limit=%"PRIu64"\n",fuel_used,call_depth,used,depth,seed,limit);
    require(fuel_used==used&&call_depth==depth,"exact shared fuel/depth before reset");
}
static void trace(const struct Vector *v,uint64_t seed,uint64_t limit){
    uint64_t used=seed;const struct Diagnostic *error=v->error;int depth=v->depth;
    /* Finite hand-authored semantic trace, independent of emitted IL/optimizer. */
    for(size_t i=0;i<v->count;i++){
        unsigned n=v->trace[i].amount;
        if(n>limit||used>limit-n){error=&fuel_error;depth=v->trace[i].depth;break;}
        used+=n;
    }
    observe(v,seed,limit,0,used,depth,error);
}
static void budgets(const struct Vector *v){
    uint64_t sum=0;for(size_t i=0;i<v->count;i++)sum+=v->trace[i].amount;
    for(uint64_t budget=0;budget<=sum+1;budget++)trace(v,0,budget);
}
static void unsigned_edges(const struct Vector *v){
    static const uint64_t seeds[]={0,1,2,INT64_MAX-1,INT64_MAX,(uint64_t)INT64_MAX+1,UINT64_MAX-4,UINT64_MAX-3,UINT64_MAX-2,UINT64_MAX-1,UINT64_MAX};
    static const uint64_t limits[]={0,1,2,3,INT64_MAX,(uint64_t)INT64_MAX+1,UINT64_MAX-1,UINT64_MAX};
    for(size_t i=0;i<sizeof seeds/sizeof seeds[0];i++)for(size_t j=0;j<sizeof limits/sizeof limits[0];j++)trace(v,seeds[i],limits[j]);
}
static void recursive(unsigned n,int leaf,int direct,int failure){
    struct Charge charges[514];size_t at=0;char input[32];snprintf(input,sizeof input,"%u",n);
    if(!direct)charges[at++]=(struct Charge){1,1};
    unsigned frames=failure&&!leaf?256-(direct?0:1):n+1;
    for(unsigned i=0;i<frames;i++){
        int depth=(int)i+(direct?1:2);
        charges[at++]=(struct Charge){1,depth};charges[at++]=(struct Charge){1,depth};
    }
    if(leaf&&!failure)charges[at++]=(struct Charge){1,(int)n+3};
    struct Vector v={input,leaf?"14":"0",failure?&depth_error:NULL,failure?257:0,charges,at};
    budgets(&v);
}
static void compatibility_state(void){
    const struct Vector *v=&fixtures[0].vectors[0];reset_memory();fuel_limit=1000;fuel_used=0;operations=0;
    apx_charge(5);apx_enter();compatibility=1;
    if(setjmp(escape)){fprintf(stderr,"compatibility unexpected trap: %s\n",error_kind);exit(1);}
    require(integer(apx_entry(apx_int(17)))==34,"compatibility generated result");compatibility=0;
    require(fuel_used==11&&call_depth==1&&apx_execution_state.used==11&&apx_execution_state.depth==1,"one live state across APIs and generated entry");
    apx_leave();apx_leave();apx_charge(0);require(!call_depth&&fuel_used==11,"guarded compatibility leave and zero charge");
    if(!setjmp(escape)){apx_charge(-1);require(0,"negative compatibility charge traps");}else diagnostic_is(&fuel_error);
    require(fuel_used==11,"negative charge does not update fuel");
    fuel_limit=UINT64_MAX;fuel_used=UINT64_MAX-(uint64_t)INT64_MAX;
    apx_charge(INT64_MAX);require(fuel_used==UINT64_MAX,"large positive compatibility charge");
    if(!setjmp(escape)){apx_charge(1);require(0,"compatibility overflow traps");}else diagnostic_is(&fuel_error);
    require(fuel_used==UINT64_MAX,"overflow does not update fuel");
    fuel_limit=1000;fuel_used=19;call_depth=256;
    if(!setjmp(escape)){apx_enter();require(0,"compatibility depth traps");}else diagnostic_is(&depth_error);
    require(call_depth==257,"compatibility enter increments before failure");apx_leave();require(call_depth==256,"compatibility leave after failure");
    reset_memory();require(call_depth==0&&fuel_used==19&&fuel_limit==1000,"reset clears only depth, not fuel state");
    if(!setjmp(escape)){apx_missing_return();require(0,"compatibility missing return traps");}else diagnostic_is(&missing_error);
    trace(v,19,1000);require(fuel_used==22&&!call_depth,"generated recovery after compatibility failure/reset");
    /* Repeated entry without reset must consume, not silently reset, live fuel. */
    require(integer(apx_entry(apx_int(17)))==34&&fuel_used==25&&!call_depth,"repeated entry keeps live shared fuel");
    fuel_used=0;compatibility=-1;
    require(integer(apx_entry(apx_int(17)))==34&&fuel_used==3&&!call_depth,"generated leave guards zero state written by compatibility API");compatibility=0;
    static const int64_t amounts[]={INT64_MIN,-1,0,1,2,INT64_MAX};
    static const uint64_t seeds[]={0,1,UINT64_MAX-1,UINT64_MAX};
    static const uint64_t limits[]={0,1,INT64_MAX,UINT64_MAX};
    for(volatile size_t i=0;i<sizeof seeds/sizeof seeds[0];i++)for(volatile size_t j=0;j<sizeof limits/sizeof limits[0];j++)for(volatile size_t k=0;k<sizeof amounts/sizeof amounts[0];k++){
        fuel_used=seeds[i];fuel_limit=limits[j];int64_t amount=amounts[k];
        volatile int fails=amount<0||(uint64_t)amount>fuel_limit||fuel_used>fuel_limit-(uint64_t)amount;
        if(!setjmp(escape)){apx_charge(amount);require(!fails&&fuel_used==seeds[i]+(uint64_t)amount,"compatibility charge unsigned matrix success");}
        else{require(fails&&fuel_used==seeds[i],"compatibility charge unsigned matrix failure preserves fuel");diagnostic_is(&fuel_error);}
    }
}
static void test_artifact(void){
    reset_memory();fuel_used=0;fuel_limit=1000;require(apx_test_count()==2,"no-main test count");
    if(!setjmp(escape)){apx_test_run(0);require(0,"first declared test traps");}else diagnostic_is(&zero_error);
    require(fuel_used==2&&call_depth==2,"source-test failure retains logical state");
    reset_memory();require(fuel_used==2&&!call_depth,"source-test recovery reset preserves accumulated fuel");
    if(setjmp(escape)){require(0,"following source test must recover");}
    apx_test_run(1);require(fuel_used==4&&!call_depth,"following source test succeeds on same shared state");
}
int main(void){
    require(apx_execution_state.limit==1000000&&!fuel_used&&!call_depth&&!apx_execution_state.reserved,"execution-state static initialization");
    require(sizeof(ApxExecutionState)==24&&offsetof(struct ApxExecutionAlignment,state)==8&&offsetof(ApxExecutionState,limit)==0&&offsetof(ApxExecutionState,used)==8&&offsetof(ApxExecutionState,depth)==16&&offsetof(ApxExecutionState,reserved)==20,"explicit align8 execution-state ABI");
    const struct Fixture *f=&fixtures[APX_CALLS_CASE];
    if(APX_CALLS_CASE==17)test_artifact();
    else if(APX_CALLS_CASE==11){recursive(253,1,0,0);recursive(254,1,0,1);}
    else if(APX_CALLS_CASE==12){recursive(255,0,1,0);recursive(256,0,1,1);}
    else if(APX_CALLS_CASE==13){recursive(254,0,0,0);recursive(255,0,0,1);}
    else for(size_t i=0;i<f->count;i++)budgets(&f->vectors[i]);
    if(APX_CALLS_CASE==0){unsigned_edges(&f->vectors[0]);compatibility_state();observe(&f->vectors[0],0,1000,255,2,257,&depth_error);}
    if(APX_CALLS_CASE==8)unsigned_edges(&f->vectors[1]);
    if(APX_CALLS_CASE==2){
        trace(&f->vectors[0],0,1000);require(operations==3&&operation_left[0]==1&&operation_left[1]==4&&operation_left[2]==2,"arguments evaluated once, textually, including unused parameter");
        require(operation_depth[0]==1&&operation_depth[1]==1&&operation_depth[2]==2&&operation_fuel[0]==1&&operation_fuel[1]==1&&operation_fuel[2]==2,"arguments before logical enter; body after return charge");
    }
    if(APX_CALLS_CASE==3||APX_CALLS_CASE==5){observe(&f->vectors[0],0,1000,255,1,256,&zero_error);require(operations==(APX_CALLS_CASE==3?2u:1u),"argument trap order before depth entry");}
    reset_memory();require(!call_depth&&!arena_chunks&&!memory_used,"linked cleanup");
    printf("linked native calls case %d: %u checks; %u generated-entry probes PASS\n",APX_CALLS_CASE,checks,probes);return 0;
}
#else
static char directory[4096];static int active;
static const char *const files[]={"program.ssa","program.s","program.o","linked","source.apx","output","policy.json","effect"};
static void cleanup(void){if(!active)return;for(size_t i=0;i<sizeof files/sizeof files[0];i++){char p[4200];snprintf(p,sizeof p,"%s/%s",directory,files[i]);unlink(p);}if(!rmdir(directory))active=0;}
static int run_to(char *const args[],const char *output){fflush(NULL);pid_t p=fork();require(p>=0,"fork");if(!p){if(output&&!freopen(output,"wb",stdout))_exit(126);execvp(args[0],args);_exit(127);}int s;require(waitpid(p,&s,0)==p&&WIFEXITED(s),"wait");return WEXITSTATUS(s);}
static int run(char *const args[]){return run_to(args,NULL);}
static void save(const char *p,const char *text){FILE *f=fopen(p,"wb");require(f!=NULL,"open");require(fwrite(text,1,strlen(text),f)==strlen(text)&&!fclose(f),"write");}
static char *compile(const char *source,int tests){char *il=NULL,*canonical=NULL,*diag=NULL;int code=(tests?apx_compile_tests:apx_compile_source)(source,&il,&canonical,&diag);if(code)fprintf(stderr,"%s\n",diag);require(!code&&il,"compile fixture");free(canonical);free(diag);return il;}
static void inline_ir(void){char *il=compile(fixtures[0].source,0);require(!strstr(il,"call $f0("),"eligible leaf has no physical user call");require(strstr(il,"function l $f0(l %p0, w %p1)")!=NULL,"out-of-line native ABI remains available");free(il);}
static void meter_ir(void){char *il=compile(fixtures[0].source,0);require(!strstr(il,"call $apx_charge(")&&!strstr(il,"call $apx_enter(")&&!strstr(il,"call $apx_leave("),"fixed fuel/depth use generated common path");require(strstr(il,"$apx_execution_state")&&strstr(il,"cultl ")&&strstr(il,"cugtl ")&&strstr(il,"call $apx_fuel_exhausted()\n    hlt")&&strstr(il,"call $apx_depth_exhausted()\n    hlt"),"unsigned guards/shared state/nonreturning cold traps");free(il);}
static void fallback_ir(void){
    const char *sources[]={
        "M f\nF leaf x:i64->i64\n  L y x\n  R y\nF main n:i64->i64\n  R (leaf n)\n",
        "M f\nF leaf x:i64->i64\n  ? (= x 0)\n    R 1\n  R x\nF main n:i64->i64\n  R (leaf n)\n",
        "M f\nF leaf x:f64->f64\n  R (+ x 1.0)\nF main n:f64->f64\n  R (leaf n)\n",
        "M f\nF leaf x:[i64]->i64\n  R (# x)\nF main n:[i64]->i64\n  R (leaf n)\n",
        "M f\nF leaf->i64\n  R (leaf)\nF main->i64\n  R (leaf)\n",
        "M f\nF leaf->i64\n  R (other)\nF other->i64\n  R (leaf)\nF main->i64\n  R (leaf)\n"};
    for(size_t i=0;i<sizeof sources/sizeof sources[0];i++){char *il=compile(sources[i],0);require(strstr(il,"call $f0(")!=NULL,"unsupported/control/effect/recursive helper retains physical fallback");free(il);}
    char *il=compile(fixtures[1].source,0);require(!strstr(il,"call $f0("),"many mixed bool/i64 parameters eligible");free(il);
    char source[16384];size_t at=(size_t)snprintf(source,sizeof source,"M bounds\nF leaf x:i64->i64\n  R x\nF main->i64\n");
    for(unsigned i=0;i<40;i++)at+=(size_t)snprintf(source+at,sizeof source-at,"  L x%u (leaf %u)\n",i,i);
    snprintf(source+at,sizeof source-at,"  R x39\n");il=compile(source,0);
    unsigned calls=0;for(char *p=il;(p=strstr(p,"call $f0("));p++)calls++;require(calls==8,"per-function inline-site expansion bound retains fallback");free(il);
    at=(size_t)snprintf(source,sizeof source,"M bounds\nF leaf x:i64->i64\n  R ");
    for(unsigned i=0;i<17;i++)at+=(size_t)snprintf(source+at,sizeof source-at,"(+ x ");
    at+=(size_t)snprintf(source+at,sizeof source-at,"x");for(unsigned i=0;i<17;i++)source[at++]=')';
    snprintf(source+at,sizeof source-at,"\nF main->i64\n  R (leaf 0)\n");il=compile(source,0);require(strstr(il,"call $f0(")!=NULL,"large expression retains fallback");free(il);
    at=(size_t)snprintf(source,sizeof source,"M expansion\nF leaf x:i64->i64\n  R ");
    for(unsigned i=0;i<15;i++)at+=(size_t)snprintf(source+at,sizeof source-at,"(+ x ");
    source[at++]='x';for(unsigned i=0;i<15;i++)source[at++]=')';
    at+=(size_t)snprintf(source+at,sizeof source-at,"\nF main->i64\n");
    for(unsigned i=0;i<10;i++)at+=(size_t)snprintf(source+at,sizeof source-at,"  L x%u (leaf 0)\n",i);
    snprintf(source+at,sizeof source-at,"  R x9\n");il=compile(source,0);calls=0;
    for(char *p=il;(p=strstr(p,"call $f0("));p++)calls++;
    require(calls==2,"per-function total expanded AST-node bound retains fallback");free(il);
    at=(size_t)snprintf(source,sizeof source,"M parameters\nF leaf");
    for(unsigned i=0;i<17;i++)at+=(size_t)snprintf(source+at,sizeof source-at," p%u:i64",i);
    at+=(size_t)snprintf(source+at,sizeof source-at,"->i64\n  R p0\nF main->i64\n  R (leaf");
    for(unsigned i=0;i<17;i++)at+=(size_t)snprintf(source+at,sizeof source-at," 0");
    snprintf(source+at,sizeof source-at,")\n");il=compile(source,0);require(strstr(il,"call $f0(")!=NULL,"parameter count bound retains fallback");free(il);
}
static void linked(size_t index){
    if(index==14){
        char *il=NULL,*canonical=NULL,*diag=NULL;
        require(apx_compile_source(fixtures[index].source,&il,&canonical,&diag)!=0&&!il&&diag,"missing return rejected before native emission");
        ApxValue *d=apx_json_parse(diag,strlen(diag));require(d!=NULL,"missing-return diagnostic");
        require(!strcmp(apx_json_string(apx_json_get(d,"code")),"T105")&&!strcmp(apx_json_string(apx_json_get(d,"phase")),"type")&&!strcmp(apx_json_string(apx_json_get(d,"kind")),"missing_return")&&!strcmp(apx_json_string(apx_json_get(d,"message")),"function may not return a value"),"unchanged missing-return compile diagnostic");
        free(canonical);free(diag);reset_memory();return;
    }
    char *il=compile(fixtures[index].source,index==17);char ssa[4200],assembly[4200],obj[4200],exe[4200],define[64];
    snprintf(define,sizeof define,"-DAPX_CALLS_CASE=%zu",index);snprintf(ssa,sizeof ssa,"%s/program.ssa",directory);snprintf(assembly,sizeof assembly,"%s/program.s",directory);snprintf(obj,sizeof obj,"%s/program.o",directory);snprintf(exe,sizeof exe,"%s/linked",directory);save(ssa,il);free(il);
    char *qbe[]={".build/tools/qbe","-t","amd64_sysv","-o",assembly,ssa,NULL};require(!run(qbe),"real QBE lowering");char *as[]={"as",assembly,"-o",obj,NULL};require(!run(as),"assemble real program");
    char *cc[]={"cc","-std=c99","-O2","-Wall","-Wextra","-Werror","-no-pie","-Wl,--wrap=apx_i64_checked","-DAPX_CALLS_LINKED",define,
#ifdef APX_ARENA_ASAN
        "-O1","-g","-fno-omit-frame-pointer","-fsanitize=address,undefined",
#endif
        "tests/test_native_calls.c",obj,"-o",exe,NULL};require(!run(cc),"link generated-entry observer");char *args[]={exe,NULL};require(!run(args),"linked semantic/state/charge proof");
}
static ApxValue *response(const char *path){
    FILE *f=fopen(path,"rb");require(f!=NULL,"open native envelope");char text[32768];size_t n=fread(text,1,sizeof text-1,f);
    require(!ferror(f)&&feof(f)&&!fclose(f),"bounded native envelope");text[n]=0;
    ApxValue *v=apx_json_parse(text,n);require(v!=NULL,"native envelope JSON");return v;
}
static void driver_host(void){
    char source[4200],output[4200],policy[4200],effect[4200],json[8192];
    snprintf(source,sizeof source,"%s/source.apx",directory);snprintf(output,sizeof output,"%s/output",directory);
    snprintf(policy,sizeof policy,"%s/policy.json",directory);snprintf(effect,sizeof effect,"%s/effect",directory);
    snprintf(json,sizeof json,"{\"schema\":\"apodexis.host_policy.v1\",\"write_root\":\"%s\",\"max_requests\":1}",directory);save(policy,json);
    save(source,"M effect\nF leaf x:i64 y:i64->i64\n  R x\nF write->i64\n  C (fs_write \"effect\" \"written\")\n    ok count\n      R count\n    err problem\n      Z \"write failed\"\nF main->i64\n  R (leaf (write) (/ 1 0))\n");
    for(unsigned budget=0;budget<=4;budget++){
        unlink(effect);char fuel[32];snprintf(fuel,sizeof fuel,"%u",budget);
        char *args[]={"bin/apodexis","run",source,"--capabilities",policy,"--fuel",fuel,"--json",NULL};
        require(run_to(args,output)==2,"actual host-argument failure exit");ApxValue *v=response(output),*d=apx_json_get(v,"diagnostic");
        const char *kind=apx_json_string(apx_json_get(d,"kind"));
        if(!kind||strcmp(kind,budget<3?"fuel_exhausted":"divide_by_zero")){char *text=apx_json_encode(v);fprintf(stderr,"host budget=%u response=%s\n",budget,text?text:"NULL");free(text);}
        require(kind&&!strcmp(kind,budget<3?"fuel_exhausted":"divide_by_zero"),"host argument fuel/error ordering");
        require(apx_json_integer(apx_json_get(v,"fuel_used"))==(budget<3?budget:3),"host argument exact native fuel");
        if(budget<2)require(access(effect,F_OK)!=0,"no speculative host effect before statement charge");
        else{FILE *f=fopen(effect,"rb");char bytes[8]={0};require(f&&fread(bytes,1,8,f)==7&&feof(f)&&!fclose(f)&&!strcmp(bytes,"written"),"real authorized argument effect precedes callee enter/next argument trap");}
        reset_memory();
    }
}
static void driver_tests(void){
    char source[4200],output[4200];snprintf(source,sizeof source,"%s/source.apx",directory);snprintf(output,sizeof output,"%s/output",directory);save(source,fixtures[17].source);
    char *args[]={"bin/apodexis","test",source,"--json",NULL};require(run_to(args,output)==1,"actual no-main native test failure exit");
    ApxValue *v=response(output),*tests=apx_json_get(v,"tests");require(v&&apx_json_length(tests)==2,"both declared tests really ran");
    const char *status0=apx_json_string(apx_json_get(apx_json_item(tests,0),"status")),*status1=apx_json_string(apx_json_get(apx_json_item(tests,1),"status"));
    require(status0&&status1&&!strcmp(status0,"error")&&!strcmp(status1,"ok"),"driver recovers after source-test trap");
    require(apx_json_integer(apx_json_get(apx_json_item(tests,0),"fuel_used"))==2&&apx_json_integer(apx_json_get(apx_json_item(tests,1),"fuel_used"))==2,"driver source-test exact separate fuel deltas");reset_memory();
}
int main(int argc,char **argv){
    if(argc==2&&!strcmp(argv[1],"--red-inline")){inline_ir();return 0;}
    if(argc==2&&!strcmp(argv[1],"--red-meter")){meter_ir();return 0;}
    require(argc==1,"valid arguments");const char *tmp=getenv("TMPDIR");require(tmp!=NULL,"TMPDIR");snprintf(directory,sizeof directory,"%s/apx-calls-XXXXXX",tmp);require(mkdtemp(directory)!=NULL,"scratch");active=1;require(!atexit(cleanup),"cleanup registration");
    inline_ir();meter_ir();fallback_ir();for(size_t i=0;i<sizeof fixtures/sizeof fixtures[0];i++)linked(i);driver_tests();driver_host();
    cleanup();require(!active,"scratch cleanup");printf("native calls: %u checks; linked output/error/fuel/depth/bounds PASS\n",checks);return 0;
}
#endif
