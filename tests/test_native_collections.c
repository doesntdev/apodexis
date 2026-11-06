/* Packed storage/private construction regressions, including a real QBE entry. */
#define _POSIX_C_SOURCE 200809L
#define APX_LIBRARY_ONLY
#include <stdlib.h>
static int fail_backing;
static void *test_calloc(size_t n,size_t width){if(fail_backing){fail_backing=0;return NULL;}return calloc(n,width);}
#define calloc test_calloc
#include "../runtime/apx_runtime.c"
#undef calloc
#include "../compiler/apx_frontend.h"
#include <sys/wait.h>
static unsigned checks;
static void require(int ok,const char *s){checks++;if(!ok){fprintf(stderr,"native collections FAIL: %s\n",s);exit(1);}}
#ifndef APX_COLLECTION_LINKED
static const char *builder_source=
    "M collections\nF main n:i64->[i64]\n  L b (buf i64 n)\n  L i 0\n"
    "  W (< i n)\n    P b i\n    S i (+ i 1)\n  L out (finish b)\n"
    "  L total 0\n  E v out\n    S total (+ total v)\n"
    "  ? (> n 0)\n    A (= out[(- n 1)] (- n 1))\n  R out\n";
#endif
#ifdef APX_COLLECTION_LINKED
static void observe(int64_t n){
    reset_memory();fuel_used=0;fuel_limit=1000000;
    if(setjmp(escape)){fprintf(stderr,"linked trap: %s\n",error_kind);exit(1);}
    ApxValue *v=apx_entry(apx_int(n));
    require(v&&v->kind==VARRAY&&v->integer==1,"real entry produces packed i64 storage");
    size_t before=memory_used;
    for(int64_t i=0;i<n;i++)require(apx_array_i64_at(v,i)==i,"real entry raw contiguous values");
    require(memory_used==before&&!v->items,"typed reads never materialize boxes");
    require(memory_used<16384+(size_t)n*24,"private appends avoid per-item descriptors/scalars");
    printf("collection allocation n=%"PRId64" requested_bytes=%zu fuel=%"PRIu64"\n",n,memory_used,fuel_used);
}
int main(void){
#ifdef APX_COLLECTION_LITERAL
    const int64_t sizes[]={4};
#else
    const int64_t sizes[]={0,1,63,64,65,4096};
#endif
    for(size_t k=0;k<sizeof sizes/sizeof sizes[0];k++)observe(sizes[k]);
    reset_memory();printf("linked native collections: %u checks PASS\n",checks);return 0;
}
#else
static void save(const char *path,const char *s){FILE *fp=fopen(path,"wb");require(fp!=NULL,"open fixture");require(fwrite(s,1,strlen(s),fp)==strlen(s)&&!fclose(fp),"write fixture");}
static int run_to(char *const args[],const char *output){pid_t pid=fork();require(pid>=0,"fork");if(!pid){if(output&&!freopen(output,"wb",stdout))_exit(126);execvp(args[0],args);_exit(127);}int st;require(waitpid(pid,&st,0)==pid&&WIFEXITED(st),"wait");return WEXITSTATUS(st);}
static int run(char *const args[]){return run_to(args,NULL);}
static char fixture_dir[4096];
static int fixture_active;
static void cleanup(void){
    if(!fixture_active)return;
    static const char *const names[]={"program.ssa","program.s","program.o","linked","source.apx","input.json","output"};
    for(size_t i=0;i<sizeof names/sizeof names[0];i++){char p[4200];snprintf(p,sizeof p,"%s/%s",fixture_dir,names[i]);unlink(p);}
    if(!rmdir(fixture_dir))fixture_active=0;
    else fprintf(stderr,"collection scratch cleanup failed: %s\n",fixture_dir);
}
static char *compile(const char *source){char *il=NULL,*canonical=NULL,*diag=NULL;int code=apx_compile_source(source,&il,&canonical,&diag);if(code)fprintf(stderr,"source:\n%s\ndiagnostic: %s\n",source,diag);require(!code&&il,"compile fixture");free(canonical);free(diag);return il;}
static void linked(int literal){
    char *il=compile(literal?"M literal\nF main n:i64->[i64]\n  R [0 1 2 3]\n":builder_source);
    if(!literal)require(strstr(il,"$apx_builder_i64_private_new(")&&strstr(il,"$apx_builder_i64_private_push("),"compiler-proven private construction IR");
    else require(strstr(il,"$apx_array_i64_new(")!=NULL,"actual typed literal IR");
    const char *dir=fixture_dir;
    char ssa[4200],assembly[4200],obj[4200],exe[4200];snprintf(ssa,sizeof ssa,"%s/program.ssa",dir);snprintf(assembly,sizeof assembly,"%s/program.s",dir);snprintf(obj,sizeof obj,"%s/program.o",dir);snprintf(exe,sizeof exe,"%s/linked",dir);save(ssa,il);free(il);
    char *qbe[]={".build/tools/qbe","-t","amd64_sysv","-o",assembly,ssa,NULL};require(!run(qbe),"QBE");char *as[]={"as",assembly,"-o",obj,NULL};require(!run(as),"assembler");
    char *cc[]={"cc","-std=c99","-O2","-Wall","-Wextra","-Werror","-no-pie","-DAPX_COLLECTION_LINKED",literal?"-DAPX_COLLECTION_LITERAL":"-DAPX_COLLECTION_BUILDER",
#ifdef APX_ARENA_ASAN
        "-O1","-g","-fno-omit-frame-pointer","-fsanitize=address,undefined",
#endif
        "tests/test_native_collections.c",obj,"-o",exe,NULL};require(!run(cc),"link actual compiled-entry observer");char *args[]={exe,NULL};require(!run(args),"real compiled-entry packing/allocation");
    unlink(ssa);unlink(assembly);unlink(obj);unlink(exe);
}
static void encoded_is(ApxValue *v,const char *expected){
    char *s=apx_json_encode(v);if(!s||strcmp(s,expected))fprintf(stderr,"encoded: %s expected: %s\n",s?s:"NULL",expected);
    require(s&&!strcmp(s,expected),"full encoded output");free(s);
}
static void runtime_arrays(size_t n){
    ApxValue *packed=apx_array_i64_new((int64_t)n),*generic=apx_array_new((int64_t)n);
    for(size_t i=0;i<n;i++){int64_t x=i==0?INT64_MIN:i+1==n?INT64_MAX:(int64_t)i;apx_array_i64_put(packed,(int64_t)i,x);apx_array_put(generic,(int64_t)i,apx_int(x));}
    require(packed->integer==1&&packed->members.i64&&!packed->items,"actual contiguous allocation without boxes");
    size_t before=memory_used;
    for(size_t i=0;i<n;i++)require(apx_array_i64_at(packed,(int64_t)i)==apx_array_i64_at(generic,(int64_t)i),"typed getter supports packed and boxed arrays");
    require(memory_used==before,"typed traversal allocation-free");
    require(equal(packed,generic,0)&&equal(generic,packed,0),"cross-representation equality");
    require(memory_used==before&&!packed->items,"equality does not box elements");
    char *a=apx_json_encode(packed),*b=apx_json_encode(generic);
    require(a&&b&&!strcmp(a,b),"full packed/generic JSON equality");free(a);free(b);
    require(!packed->items,"JSON encode never materializes boxes");
    ApxValue *slice=apx_builtin3(52,packed,apx_int(0),apx_int((int64_t)n));
    require(slice->integer&&equal(slice,generic,0),"packed slice remains correct");
    ApxValue *cat=apx_builtin(53,packed,generic);require(cat->length==2*n,"mixed concatenation length");
    for(size_t i=0;i<n;i++)require(apx_array_i64_at(cat,(int64_t)(n+i))==apx_array_i64_at(packed,(int64_t)i),"mixed concat contents");
    ApxValue *both=apx_builtin(53,slice,packed);require(both->integer&&equal(both,cat,0),"packed concat");
    if(n){
        ApxValue *item=apx_json_item(packed,0);require(item&&integer(item)==INT64_MIN&&item==apx_json_item(packed,0),"public stable cached compatibility box");
        ApxValue *changed=apx_builtin3(51,packed,apx_int(0),apx_int(42));
        require(apx_array_i64_at(changed,0)==42&&integer(item)==INT64_MIN,"array_set preserves original cached item");
        ApxValue *some=apx_builtin(50,packed,apx_int(0));require(apx_payload(some)==item,"array_get returns stable arena value");
        ApxValue *mixed=apx_builtin3(51,packed,apx_int(0),apx_string("x",1));require(!mixed->integer&&mixed->items[0]->kind==VSTR,"defensive heterogeneous generic boundary");
    }
    require(!apx_json_item(packed,n),"public out-of-bounds returns NULL");
    reset_memory();
}
static void private_snapshots(void){
    const size_t sizes[]={0,1,63,64,65,4096};
    ApxValue *b=apx_builder_i64_private_new(INT64_MAX),*snapshots[6];
    require(memory_used==sizeof(ApxValue)+sizeof(struct PrivateI64Builder),"maximum capacity is metadata only");
    for(size_t k=0;k<sizeof sizes/sizeof sizes[0];k++){
        while(b->length<sizes[k]){size_t n=b->length;ApxValue *out=apx_builder_i64_private_push(b,(int64_t)n);require(out==b,"private append reuses descriptor");}
        snapshots[k]=apx_builtin(21,b,NULL);
        require(snapshots[k]==apx_builtin(21,b,NULL),"repeat private finish caches whole snapshot");
    }
    for(size_t k=0;k<sizeof sizes/sizeof sizes[0];k++){
        require(snapshots[k]->length==sizes[k],"finish captures length before later append/growth");
        for(size_t i=0;i<sizes[k];i++)require(apx_array_i64_at(snapshots[k],(int64_t)i)==(int64_t)i,"finished prefix survives growth");
    }
    require(snapshots[1]->members.i64==snapshots[2]->members.i64,"append-only zero-copy prefix sharing");
    require(snapshots[3]->members.i64!=snapshots[4]->members.i64,"growth retains old backing");
    ApxValue *old_item=apx_at_raw(snapshots[1],0);
    ApxValue *fork=apx_builtin(20,b,apx_int(-1));require(fork!=b&&b->length==4096,"ordinary push on private value is persistent");
    require(apx_array_i64_at(apx_builtin(21,fork,NULL),4096)==-1&&integer(old_item)==0,"generic persistent boundary boxes have stable lifetime");
    ApxValue *other=apx_builtin(19,apx_int(4096),NULL);
    for(size_t i=0;i<4096;i++)other=apx_builtin(20,other,apx_int((int64_t)i));
    require(equal(b,other,0)&&equal(other,b,0),"builder equality defensive private/generic boundary");
    reset_memory();
}
static void boundaries(void){
    ApxValue *a=apx_array_i64_new(2);apx_array_i64_put(a,0,INT64_MIN);apx_array_i64_put(a,1,INT64_MAX);
    ApxValue *nested=apx_array_new(1);apx_array_put(nested,0,a);
    ApxValue *record=apx_record_new("R",1);apx_record_put(record,0,"nested",nested);
    encoded_is(record,"{\"nested\":[[-9223372036854775808,9223372036854775807]]}");
    ApxValue *m=apx_builtin3(45,apx_builtin(43,NULL,NULL),apx_string("k",1),a);
    encoded_is(m,"{\"k\":[-9223372036854775808,9223372036854775807]}");
    ApxValue *vals=apx_builtin(49,m,NULL);require(apx_at_raw(vals,0)==a,"map values preserve packed nested arrays");
    const char *schema="{\"kind\":\"array\",\"items\":{\"kind\":\"array\",\"items\":{\"kind\":\"i64\"}}}";
    require(equal(apx_validate(schema,nested),nested,0),"nested validation accepts packed arrays");
    ApxValue *parsed=apx_json_parse("[1,2,3]",7);require(parsed&&!parsed->integer,"JSON input remains generic, no eager repacking");
    reset_memory();
}
static void errors(void){
    if(!setjmp(escape)){(void)apx_builder_i64_private_new(-1);require(0,"negative capacity traps");}
    else require(!strcmp(error_kind,"builder_capacity_exceeded"),"negative capacity diagnostic");
    ApxValue *zero_builder=apx_builder_i64_private_new(0);
    if(!setjmp(escape)){(void)apx_builder_i64_private_push(zero_builder,1);require(0,"zero capacity traps");}
    else require(!strcmp(error_kind,"builder_capacity_exceeded")&&!zero_builder->length,"zero capacity diagnostic");
    reset_memory();
    ApxValue *packed=apx_array_i64_new(1);apx_array_i64_put(packed,0,INT64_MAX);
    if(!setjmp(escape)){(void)apx_array_i64_at(packed,-1);require(0,"negative typed index traps");}
    else require(!strcmp(error_kind,"index_out_of_bounds"),"typed negative index diagnostic");
    if(!setjmp(escape)){(void)apx_array_i64_at(packed,1);require(0,"upper typed index traps");}
    else require(!strcmp(error_kind,"index_out_of_bounds"),"typed upper index diagnostic");
    if(!setjmp(escape)){(void)equal(packed,packed,APX_DEPTH);require(0,"packed comparison retains recursive depth charge");}
    else require(!strcmp(error_kind,"recursion_limit_exceeded"),"packed comparison depth failure");
    if(!setjmp(escape)){struct Output out={0};encode(&out,packed,APX_DEPTH);require(0,"packed JSON retains scalar depth check");}
    else require(!strcmp(error_kind,"recursion_limit_exceeded"),"packed JSON depth failure");
    if(!setjmp(escape)){(void)apx_array_i64_new(INT64_MAX);require(0,"oversized array traps");}
    else require(!strcmp(error_kind,"memory_limit_exceeded"),"packed checked-size failure");
    reset_memory();
}
static void failure_recovery(void){
    ApxValue *b=apx_builder_i64_private_new(65);
    for(size_t i=0;i<64;i++)apx_builder_i64_private_push(b,(int64_t)i);
    ApxValue *prefix=apx_builtin(21,b,NULL);int64_t *data=b->members.private_i64->data;
    fail_backing=1;
    if(!setjmp(escape)){apx_builder_i64_private_push(b,64);require(0,"growth backing failure");}
    else require(!strcmp(error_kind,"memory_limit_exceeded")&&!fail_backing,"growth R204");
    require(b->length==64&&b->members.private_i64->data==data&&apx_builtin(21,b,NULL)==prefix,"failed growth publishes no state");
    apx_builder_i64_private_push(b,64);
    volatile size_t used=memory_used;memory_used=APX_MEMORY;
    if(!setjmp(escape)){(void)apx_builtin(21,b,NULL);require(0,"private finish OOM");}
    else require(!strcmp(error_kind,"memory_limit_exceeded")&&!b->members.private_i64->finished,"failed finish publishes no cache");
    memory_used=used;
    require(apx_array_i64_at(apx_builtin(21,b,NULL),64)==64&&prefix->length==64,"growth/finish retry succeeds");
    used=memory_used;memory_used=APX_MEMORY;
    require(apx_json_item(prefix,0)==NULL,"public accessor contains allocation failure with its own live trap");
    require(!prefix->items,"failed compatibility cache not published");memory_used=used;
    ApxValue *item=apx_json_item(prefix,0);require(item&&integer(item)==0,"public accessor recovers without reset");
    used=memory_used;memory_used=APX_MEMORY;
    require(apx_json_item(prefix,1)==NULL&&apx_json_item(prefix,0)==item,"cache-item OOM contains trap and preserves prior boxes");memory_used=used;
    require(apx_json_item(prefix,1)!=NULL&&integer(item)==0,"box allocation retry stable");
    if(!setjmp(escape)){apx_builder_i64_private_push(b,65);require(0,"capacity exhaustion");}
    else require(!strcmp(error_kind,"builder_capacity_exceeded")&&b->length==65,"capacity exhausted before allocation");
    reset_memory();
    fail_backing=1;
    if(!setjmp(escape)){apx_array_i64_new(4096);require(0,"constructor OOM");}
    else require(!strcmp(error_kind,"memory_limit_exceeded"),"constructor structured R204");
    reset_memory();encoded_is(apx_array_i64_new(0),"[]");reset_memory();
}
static char *readall(const char *path){
    FILE *fp=fopen(path,"rb");require(fp!=NULL,"open output");require(!fseek(fp,0,SEEK_END),"seek output");long n=ftell(fp);require(n>=0&&n<12*1024*1024,"bounded output");rewind(fp);
    char *s=malloc((size_t)n+1);require(s&&fread(s,1,(size_t)n,fp)==(size_t)n&&!fclose(fp),"read output");s[n]=0;return s;
}
static uint64_t native_case(const char *source,const char *input,unsigned fuel,const char *expected,int success){
    char path[4200],in[4200],out[4200],budget[32];snprintf(path,sizeof path,"%s/source.apx",fixture_dir);snprintf(in,sizeof in,"%s/input.json",fixture_dir);snprintf(out,sizeof out,"%s/output",fixture_dir);snprintf(budget,sizeof budget,"%u",fuel);
    save(path,source);save(in,input);char *args[]={"bin/apodexis","run",path,"--input",in,"--fuel",budget,"--json",NULL};
    int status=run_to(args,out);char *s=readall(out);ApxValue *v=apx_json_parse(s,strlen(s));
    if(status!=(success?0:2)||!v)fprintf(stderr,"source:\n%s\nresponse: %s\n",source,s);
    require(status==(success?0:2)&&v,"native complete envelope and exit");
    if(success)encoded_is(apx_json_get(v,"output"),expected);
    else {const char *kind=apx_json_string(apx_json_get(apx_json_get(v,"diagnostic"),"kind"));require(kind&&!strcmp(kind,expected),"native first failure kind");}
    uint64_t used=0;require(apx_json_unsigned(apx_json_get(v,"fuel_used"),&used),"native fuel field");free(s);reset_memory();return used;
}
static void native_semantics(void){
    native_case("M aliases\nF main->[[i64]]\n  L b (buf i64 3)\n  P b 1\n  L alias b\n  L fork (push alias 3)\n  P b 2\n  R [(finish alias) (finish b) (finish fork)]\n","null",1000000,"[[1],[1,2],[1,3]]",1);
    native_case("M embed\nF main->[[i64]]\n  L b (buf i64 3)\n  P b 1\n  L rows [b]\n  P b 2\n  R [(finish rows[0]) (finish b)]\n","null",1000000,"[[1],[1,2]]",1);
    const char *branch="M branch\nF main flag:bool->[i64]\n  L b (buf i64 2)\n  ? flag\n    P b 7\n  ? (! flag)\n    P b 9\n  A (= (count b) 1)\n  A (= (cap b) 2)\n  R (finish b)\n";
    native_case(branch,"true",1000000,"[7]",1);native_case(branch,"false",1000000,"[9]",1);
    native_case("M growth\nF main->[[i64]]\n  L b (buf i64 65)\n  L empty (finish b)\n  P b -9223372036854775808\n  L first (finish b)\n  A (= first (finish b))\n  L i 1\n  W (< i 65)\n    P b i\n    S i (+ i 1)\n  R [empty first (array_slice (finish b) 63 2)]\n","null",1000000,"[[],[-9223372036854775808],[63,64]]",1);
    native_case("M iterable\nF main->[[i64]]\n  L b (buf i64 3)\n  P b 1\n  L prefix (finish b)\n  E x prefix\n    P b (+ x 1)\n  R [prefix (finish b)]\n","null",1000000,"[[1],[1,2]]",1);
    native_case("M reuse\nF main->[[i64]]\n  L out (buf [i64] 2)\n  L i 0\n  W (< i 2)\n    L b (buf i64 1)\n    P b i\n    P out (finish b)\n    S i (+ i 1)\n  R (finish out)\n","null",1000000,"[[0],[1]]",1);
    native_case("M mixed\nF main input:[i64]->map [i64]\n  L xs [-9223372036854775808 9223372036854775807]\n  A (= xs input)\n  L m (map_put (map [i64]) \"packed\" xs)\n  R (map_put m \"generic\" (array_cat input [0]))\n","[-9223372036854775808,9223372036854775807]",1000000,"{\"generic\":[-9223372036854775808,9223372036854775807,0],\"packed\":[-9223372036854775808,9223372036854775807]}",1);
    native_case("M f64\nF main->[f64]\n  L b (buf f64 2)\n  P b 1.5\n  L prefix (finish b)\n  P b 2.5\n  A (= prefix [1.5])\n  R (finish b)\n","null",1000000,"[1.5,2.5]",1);
    native_case("M bool\nF main->[bool]\n  L b (buf bool 2)\n  P b true\n  P b false\n  R (finish b)\n","null",1000000,"[true,false]",1);
    native_case("M strings\nF main->[str]\n  L b (buf str 2)\n  P b \"a\"\n  P b \"b\"\n  R (finish b)\n","null",1000000,"[\"a\",\"b\"]",1);
    native_case("M consumers\nF reverse xs:[i64]->[i64]\n  L out (buf i64 (# xs))\n  L i (# xs)\n  W (> i 0)\n    S i (- i 1)\n    P out xs[i]\n  R (finish out)\nF sort xs:[i64]->[i64]\n  L out xs\n  L i 0\n  W (< i (# xs))\n    L j 1\n    W (< j (# xs))\n      L left (- j 1)\n      ? (> out[left] out[j])\n        L old out[left]\n        S out (array_set (array_set out left out[j]) j old)\n      S j (+ j 1)\n    S i (+ i 1)\n  R out\nF main xs:[i64]->[[i64]]\n  L packed [3 1 2]\n  R [(reverse packed) (sort packed) (sort xs) packed]\n","[3,1,2]",1000000,"[[2,1,3],[1,2,3],[1,2,3],[3,1,2]]",1);
    const char *meter="M meter\nF main->[i64]\n  L b (buf i64 0)\n  P b (+ 9223372036854775807 1)\n  R (finish b)\n";
    require(native_case(meter,"null",0,"fuel_exhausted",0)==0,"zero fuel before capacity initialization");
    require(native_case(meter,"null",1,"fuel_exhausted",0)==1,"fuel before push operand evaluation");
    require(native_case(meter,"null",2,"integer_overflow",0)==2,"eager operand overflow before exhausted capacity");
    require(native_case("M capacity\nF main->[i64]\n  L b (buf i64 0)\n  P b 1\n  R (finish b)\n","null",2,"builder_capacity_exceeded",0)==2,"capacity trap follows statement charge");
    require(native_case("M index\nF bad->i64\n  R (+ 9223372036854775807 1)\nF main->i64\n  R [1][(bad)]\n","null",2,"integer_overflow",0)==2,"eager typed index operand failure");
    native_case("M negative\nF main->[i64]\n  L b (buf i64 -1)\n  R (finish b)\n","null",1000000,"builder_capacity_exceeded",0);
    native_case("M maximum\nF main->[i64]\n  L b (buf i64 9223372036854775807)\n  R (finish b)\n","null",1000000,"[]",1);
}
static void eligibility(void){
    static const char *const fallback[]={
        "M alias\nF main->[i64]\n  L b (buf i64 2)\n  L alias b\n  P b 1\n  R (finish alias)\n",
        "M fork\nF main->[i64]\n  L b (buf i64 2)\n  L fork (push b 1)\n  R (finish fork)\n",
        "M embed\nF main->[i64]\n  L b (buf i64 2)\n  L rows [b]\n  P b 1\n  R (finish rows[0])\n",
        "M unknown\nF main->[i64]\n  L b (buf i64 2)\n  S b (buf i64 2)\n  R (finish b)\n"
    };
    for(size_t i=0;i<sizeof fallback/sizeof fallback[0];i++){char *il=compile(fallback[i]);require(!strstr(il,"$apx_builder_i64_private_"),"all alias/fork/escape/unknown uses disable private construction");free(il);}
    static const char *const rejected[]={
        "M pass\nF take b:builder i64->[i64]\n  R (finish b)\nF main->[i64]\n  L b (buf i64 2)\n  R (take b)\n",
        "M ret\nF make->builder i64\n  L b (buf i64 2)\n  R b\nF main->[i64]\n  R (finish (make))\n",
        "M comparable\nF main->bool\n  L b (buf i64 2)\n  R (= b b)\n"
    };
    for(size_t i=0;i<sizeof rejected/sizeof rejected[0];i++){
        char *ir=NULL,*canonical=NULL,*diag=NULL;
        require(apx_compile_source(rejected[i],&ir,&canonical,&diag)!=0&&diag,"existing builder boundary/comparison rejections preserved");
        free(ir);free(canonical);free(diag);
    }
    char *il=compile("M branch\nF main n:i64->[i64]\n  L b (buf i64 2)\n  ? (> n 0)\n    P b 1\n  ? (<= n 0)\n    P b 2\n  R (finish b)\n");
    require(strstr(il,"$apx_builder_i64_private_push(")!=NULL,"branch updates on proven binding eligible");free(il);
    il=compile("M bindingids\nF main flag:bool->i64\n  ? flag\n    L b (buf i64 1)\n    P b 7\n  ? (! flag)\n    L b (buf i64 1)\n    L alias b\n    P b 9\n  R 0\n");
    const char *private_new=strstr(il,"$apx_builder_i64_private_new(");
    require(private_new&&!strstr(private_new+1,"$apx_builder_i64_private_new("),"same names in disjoint scopes use distinct checked binding IDs");free(il);
    il=compile("M literal\nF main->[i64]\n  L xs [-9223372036854775808 9223372036854775807]\n  L total 0\n  E x xs\n    S total (+ total 0)\n  R [xs[0] xs[1]]\n");
    require(strstr(il,"$apx_array_i64_new(")&&strstr(il,"$apx_array_i64_put(")&&strstr(il,"$apx_array_i64_at("),"typed literal/E/index raw array IR");
    require(!strstr(il,"$apx_at_raw(")&&!strstr(il,"$apx_array_put("),"no per-element boxing for typed reads or literals");free(il);
}
int main(void){
    const size_t sizes[]={0,1,63,64,65,4096};
    for(size_t i=0;i<sizeof sizes/sizeof sizes[0];i++)runtime_arrays(sizes[i]);
    private_snapshots();boundaries();errors();failure_recovery();eligibility();
    const char *root=getenv("TMPDIR");require(root!=NULL,"TMPDIR configured");
    snprintf(fixture_dir,sizeof fixture_dir,"%s/apx-collections-XXXXXX",root);
    require(mkdtemp(fixture_dir)!=NULL,"scratch fixture");fixture_active=1;require(!atexit(cleanup),"failure cleanup registration");
    linked(0);linked(1);native_semantics();cleanup();require(!fixture_active,"scratch cleanup");
    printf("native collections: %u checks PASS\n",checks);return 0;
}
#endif
