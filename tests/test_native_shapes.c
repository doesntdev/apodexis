/* Shape-specialization seams: emitted IL, real linked entries and JSON ABI. */
#define _POSIX_C_SOURCE 200809L
#define APX_LIBRARY_ONLY
#include "../runtime/apx_runtime.c"
#include "../compiler/apx_frontend.h"
#include <sys/wait.h>
#include <elf.h>
static unsigned checks;
static void require(int ok,const char *s){checks++;if(!ok){fprintf(stderr,"native shapes FAIL: %s\n",s);exit(1);}}
static const struct Fixture {
    const char *source,*schema;
    const struct Vector {const char *input,*output,*code,*kind,*message;uint64_t fuel;} *vectors;
    size_t count;
} fixtures[]={
#define V(in,out,code,kind,msg,fuel) {in,out,code,kind,msg,fuel}
    {"M linked\nF main n:i64->i64\n  R n\n","{\"kind\":\"i64\"}",
     (const struct Vector[]){V("17","17",NULL,NULL,NULL,1),V("-9223372036854775808","-9223372036854775808",NULL,NULL,NULL,1),V("9223372036854775808",NULL,"A103","numeric_out_of_range","expected i64",0),V("true",NULL,"A103","numeric_out_of_range","expected i64",0)},4},
    {"M shapes\nT A struct\n  x:i64\n  flag:bool\nT B struct\n  flag:bool\n  x:i64\nT Input struct\n  a:A\n  b:B\nF take b:B->B\n  R (with b x=(+ b.x 1))\nF main input:Input->[i64]\n  L b (take input.b)\n  L a (A flag=input.a.flag x=input.a.x)\n  R [a.x b.x input.b.x]\n",
     "{\"kind\":\"struct\",\"name\":\"Input\",\"fields\":[{\"name\":\"a\",\"type\":{\"kind\":\"struct\",\"name\":\"A\",\"fields\":[{\"name\":\"x\",\"type\":{\"kind\":\"i64\"}},{\"name\":\"flag\",\"type\":{\"kind\":\"bool\"}}]}},{\"name\":\"b\",\"type\":{\"kind\":\"struct\",\"name\":\"B\",\"fields\":[{\"name\":\"flag\",\"type\":{\"kind\":\"bool\"}},{\"name\":\"x\",\"type\":{\"kind\":\"i64\"}}]}}]}",
     (const struct Vector[]){
        V("{\"b\":{\"x\":20,\"flag\":false},\"a\":{\"flag\":true,\"x\":7}}","[7,21,20]",NULL,NULL,NULL,4),
        V("{}",NULL,"A101","missing_field","missing field `a`",0),
        V("{\"a\":{\"x\":false},\"b\":null,\"extra\":0}",NULL,"A103","numeric_out_of_range","expected i64",0),
        V("{\"a\":{\"flag\":0},\"b\":null}",NULL,"A101","missing_field","missing field `x`",0),
        V("{\"a\":{\"x\":7,\"flag\":true,\"z\":0,\"c\":0},\"b\":null}",NULL,"A102","extra_field","extra field `c`",0),
        V("{\"a\":{\"x\":7,\"flag\":true},\"b\":{\"x\":20,\"flag\":false},\"z\":0,\"c\":0}",NULL,"A102","extra_field","extra field `c`",0),
        V("{\"a\":{\"x\":7,\"flag\":true},\"b\":{\"x\":20,\"flag\":false},\"q\\u0000z\":0,\"q\\u0000a\":0}",NULL,"A102","extra_field","extra field `q`",0)
     },7},
    {"M float\nF main x:f64->f64\n  R x\n","{\"kind\":\"f64\"}",
     (const struct Vector[]){V("42","42.0",NULL,NULL,NULL,1),V("9223372036854775808","9.223372036854776e+18",NULL,NULL,NULL,1),V("5e-324","5e-324",NULL,NULL,NULL,1),V("1e-999","0.0",NULL,NULL,NULL,1),V("-0.0","-0.0",NULL,NULL,NULL,1),V("\"x\"",NULL,"A103","numeric_out_of_range","expected finite f64",0)},6},
    {"M map\nF main m:map option result [f64] str->map option result [f64] str\n  R m\n",
     "{\"kind\":\"map\",\"values\":{\"kind\":\"option\",\"item\":{\"kind\":\"result\",\"ok\":{\"kind\":\"array\",\"items\":{\"kind\":\"f64\"}},\"error\":{\"kind\":\"str\"}}}}",
     (const struct Vector[]){
        V("{\"é\":{\"tag\":\"none\"},\"z\":{\"tag\":\"some\",\"value\":{\"tag\":\"err\",\"error\":\"bad\"}},\"a\\u0000b\":{\"tag\":\"some\",\"value\":{\"tag\":\"ok\",\"value\":[1,2.5]}}}","{\"a\\u0000b\":{\"tag\":\"some\",\"value\":{\"tag\":\"ok\",\"value\":[1.0,2.5]}},\"z\":{\"tag\":\"some\",\"value\":{\"tag\":\"err\",\"error\":\"bad\"}},\"é\":{\"tag\":\"none\"}}",NULL,NULL,NULL,1),
        V("[]",NULL,"A103","type_mismatch","expected map object",0),
        V("{\"b\":{\"tag\":\"some\"},\"a\":{\"tag\":\"none\",\"value\":0}}",NULL,"A103","type_mismatch","none has no payload",0),
        V("{\"a\":{\"tag\":\"other\"}}",NULL,"A103","type_mismatch","invalid map value tag",0),
        V("{\"a\":{\"tag\":\"some\\u0000\",\"value\":0}}",NULL,"A103","type_mismatch","invalid map value tag",0),
        V("{\"a\":{\"tag\":\"some\"}}",NULL,"A103","type_mismatch","invalid tagged map value payload",0),
        V("{\"a\":{\"tag\":\"some\",\"value\":{\"tag\":\"err\",\"value\":\"bad\"}}}",NULL,"A103","type_mismatch","invalid tagged map value payload",0),
        V("{\"a\":{\"tag\":\"some\",\"value\":{\"tag\":\"ok\",\"value\":false}}}",NULL,"A103","type_mismatch","expected array",0),
        V("{\"a\":{\"tag\":\"some\",\"value\":{\"tag\":\"err\",\"error\":9}}}",NULL,"A103","type_mismatch","expected str",0),
        V("{\"a\":7}",NULL,"A103","type_mismatch","expected tagged map value",0),
        V("{\"a\":{\"tag\":false}}",NULL,"A103","type_mismatch","invalid map value tag",0)
     },11},
    {"M zero\nF main->i64\n  R 9\n","null",
     (const struct Vector[]){V("{\"anything\":[true,null]}","9",NULL,NULL,NULL,1),V("false","9",NULL,NULL,NULL,1)},2},
    {"M arrays\nF main x:[[bool]]->[[bool]]\n  R x\n","{\"kind\":\"array\",\"items\":{\"kind\":\"array\",\"items\":{\"kind\":\"bool\"}}}",
     (const struct Vector[]){V("[[true,false],[]]","[[true,false],[]]",NULL,NULL,NULL,1),V("[1]",NULL,"A103","type_mismatch","expected array",0),V("[[0]]",NULL,"A103","type_mismatch","expected bool",0)},3},
    {"M str\nF main x:str->str\n  R x\n","{\"kind\":\"str\"}",
     (const struct Vector[]){V("\"é\\u0000x\"","\"é\\u0000x\"",NULL,NULL,NULL,1),V("0",NULL,"A103","type_mismatch","expected str",0)},2},
    {"M namedmap\nT Row struct\n  flag:option bool\n  rows:[result i64 str]\nF main m:map Row->map Row\n  R m\n",
     "{\"kind\":\"map\",\"values\":{\"kind\":\"struct\",\"name\":\"Row\",\"fields\":[{\"name\":\"flag\",\"type\":{\"kind\":\"option\",\"item\":{\"kind\":\"bool\"}}},{\"name\":\"rows\",\"type\":{\"kind\":\"array\",\"items\":{\"kind\":\"result\",\"ok\":{\"kind\":\"i64\"},\"error\":{\"kind\":\"str\"}}}}]}}",
     (const struct Vector[]){
        V("{\"a\":{\"rows\":[{\"tag\":\"err\",\"error\":\"x\"},{\"tag\":\"ok\",\"value\":3}],\"flag\":{\"tag\":\"some\",\"value\":true}}}","{\"a\":{\"flag\":{\"tag\":\"some\",\"value\":true},\"rows\":[{\"tag\":\"err\",\"error\":\"x\"},{\"tag\":\"ok\",\"value\":3}]}}",NULL,NULL,NULL,1),
        V("{\"a\":{\"flag\":{\"tag\":\"none\"},\"rows\":[{\"tag\":\"ok\",\"value\":false}],\"extra\":0}}",NULL,"A103","numeric_out_of_range","expected i64",0),
        V("{\"a\":false}",NULL,"A103","type_mismatch","expected object",0)
     },3}
#undef V
};
static void encoded_is(ApxValue *v,const char *want){char *s=apx_json_encode(v);if(!s||strcmp(s,want))fprintf(stderr,"actual=%s expected=%s\n",s?s:"NULL",want);require(s&&!strcmp(s,want),"exact full JSON output");free(s);}
#ifdef APX_SHAPES_LINKED
/* Interpose only the generated object's calls, not the runtime under test. */
extern const unsigned char __executable_start[];
static const ApxSchema *observed_schema;
static unsigned entry_validations;
static int readonly_bytes(const void *p,size_t n){
    const Elf64_Ehdr *eh=(const void *)__executable_start;
    const Elf64_Phdr *ph=(const void *)(__executable_start+eh->e_phoff);
    uintptr_t address=(uintptr_t)p;
    for(unsigned i=0;i<eh->e_phnum;i++)
        if(ph[i].p_type==PT_LOAD&&(ph[i].p_flags&PF_R)&&!(ph[i].p_flags&PF_W)&&address>=ph[i].p_vaddr&&n<=ph[i].p_memsz&&address-ph[i].p_vaddr<=ph[i].p_memsz-n)return 1;
    return 0;
}
static void readonly_schema(const ApxSchema *s,unsigned depth){
    require(depth<=64&&readonly_bytes(s,sizeof *s),"descriptor in readonly ELF load segment");
    if(s->kind==APX_SCHEMA_STRUCT){
        require(!s->count||readonly_bytes(s->fields,(size_t)s->count*sizeof *s->fields),"field pairs in readonly ELF load segment");
        for(size_t i=0;i<s->count;i++)readonly_schema(s->fields[i].type,depth+1);
    }else if(s->item)readonly_schema(s->item,depth+1);
    if(s->error)readonly_schema(s->error,depth+1);
}
ApxValue *__wrap_apx_validate_static(const ApxSchema *s,ApxValue *input){
    if(!observed_schema){readonly_schema(s,0);observed_schema=s;}
    require(observed_schema==s,"static descriptor identity survives entry/reset");entry_validations++;
    return apx_validate_static(s,input);
}
ApxValue *__wrap_apx_validate(const char *s,ApxValue *input){
    (void)s;(void)input;require(0,"generated object must never call dynamic schema parser");return NULL;
}
static void vector(const struct Vector *v){
    reset_memory();fuel_used=0;fuel_limit=1000000;
    ApxValue *input=apx_json_parse(v->input,strlen(v->input));require(input!=NULL,"parse input");
    if(!setjmp(escape)){
        ApxValue *out=apx_entry(input);require(v->output!=NULL,"expected input rejection");encoded_is(out,v->output);
        require(call_depth==0,"balanced linked depth");
    }else{
        require(!v->output,"unexpected input rejection");
        if(strcmp(error_message,v->message))fprintf(stderr,"actual error=%s expected=%s\n",error_message,v->message);
        require(!strcmp(error_code,v->code)&&!strcmp(error_phase,"abi_input")&&!strcmp(error_kind,v->kind)&&!strcmp(error_message,v->message),"exact input error code/phase/kind/message");
    }
    require(fuel_used==v->fuel,"exact input validation/main fuel");
    /* The old JSON-schema API independently consumes the same exported bytes. */
    reset_memory();input=apx_json_parse(v->input,strlen(v->input));require(input!=NULL,"dynamic input");
    if(!setjmp(escape)){ApxValue *out=apx_validate(apx_schema_json,input);require(v->output!=NULL,"dynamic rejection");
#if APX_SHAPES_CASE != 1 && APX_SHAPES_CASE != 4
        encoded_is(out,v->output);
#else
        require(out!=NULL,"dynamic validation positive control");
#endif
    }else require(!v->output&&!strcmp(error_code,v->code)&&!strcmp(error_kind,v->kind)&&!strcmp(error_message,v->message),"dynamic compatibility exact error");
}
int main(void){
    const struct Fixture *fixture=&fixtures[APX_SHAPES_CASE];
    require(!strcmp(apx_schema_json,fixture->schema),"exported schema exact frozen bytes");
    for(size_t i=0;i<fixture->count;i++)vector(&fixture->vectors[i]);
#if APX_SHAPES_CASE == 1
    reset_memory();fuel_used=0;
    ApxValue *input=apx_json_parse(fixture->vectors[0].input,strlen(fixture->vectors[0].input));
    require(input!=NULL,"linked OOM input");size_t before=memory_used;
    memory_used=APX_MEMORY-sizeof(ApxValue);
    if(!setjmp(escape)){(void)apx_entry(input);require(0,"linked partial validation OOM");}
    else require(!strcmp(error_code,"R204")&&!strcmp(error_kind,"memory_limit_exceeded")&&fuel_used==0,"linked validation OOM before main/fuel");
    memory_used=before+sizeof(ApxValue);
    encoded_is(apx_entry(input),fixture->vectors[0].output);
    require(fuel_used==4&&call_depth==0,"linked validation retry publishes no stale cache");
#endif
#if APX_SHAPES_CASE == 0
    for(volatile unsigned i=0;i<8;i++){
        reset_memory();fuel_used=0;fuel_limit=1000000;
        if(setjmp(escape)){fprintf(stderr,"linked trap: %s\n",error_message);return 1;}
        ApxValue *input=apx_int(17);size_t before=memory_used;
        ApxValue *answer=apx_entry(input);
        require(integer(answer)==17&&fuel_used==1&&call_depth==0,"linked entry semantics/fuel/depth");
        require(memory_used-before==sizeof(ApxValue),"real entry has no schema-parse allocations");
        before=memory_used;answer=apx_entry(input);
        require(integer(answer)==17&&memory_used-before==sizeof(ApxValue),"repeated entry without reset does not cache/parse schema");
        require(!strcmp(apx_schema_json,fixture->schema),"exported schema exact bytes survive reset");
    }
#endif
    require(entry_validations>=fixture->count,"actual generated entry called static validator");
    reset_memory();require(!arena_chunks&&!memory_used,"linked reset clears all runtime storage");
    printf("linked native shapes case %d: %u checks PASS\n",APX_SHAPES_CASE,checks);return 0;
}
#else
static char directory[4096];static int active;
static const char *const files[]={"program.ssa","program.s","program.o","linked","source.apx","input.json","output"};
static void cleanup(void){if(!active)return;for(size_t i=0;i<sizeof files/sizeof files[0];i++){char p[4200];snprintf(p,sizeof p,"%s/%s",directory,files[i]);unlink(p);}if(!rmdir(directory))active=0;}
static int run_to(char *const args[],const char *output){fflush(NULL);pid_t p=fork();require(p>=0,"fork");if(!p){if(output&&!freopen(output,"wb",stdout))_exit(126);execvp(args[0],args);_exit(127);}int s;require(waitpid(p,&s,0)==p&&WIFEXITED(s),"wait");return WEXITSTATUS(s);}
static int run(char *const args[]){return run_to(args,NULL);}
static char *readall(const char *path){FILE *f=fopen(path,"rb");require(f!=NULL,"open output");require(!fseek(f,0,SEEK_END),"seek output");long n=ftell(f);require(n>=0&&n<65536,"bounded output");rewind(f);char *text=malloc((size_t)n+1);require(text&&fread(text,1,(size_t)n,f)==(size_t)n&&!fclose(f),"read output");text[n]=0;return text;}
static void save(const char *p,const char *text){FILE *f=fopen(p,"wb");require(f!=NULL,"open");require(fwrite(text,1,strlen(text),f)==strlen(text)&&!fclose(f),"write");}
static char *compile(const char *source){char *il=NULL,*canonical=NULL,*diag=NULL;int code=apx_compile_source(source,&il,&canonical,&diag);if(code)fprintf(stderr,"%s\n",diag);require(!code&&il,"compile fixture");free(canonical);free(diag);return il;}
static const char *shape_source=
    "M shapes\nT A struct\n  x:i64\n  flag:bool\nT B struct\n  flag:bool\n  x:i64\n"
    "T Input struct\n  a:A\n  b:B\nF take b:B->i64\n  R b.x\n"
    "F main input:Input->i64\n  R (+ input.a.x (take input.b))\n";
static void indexed_ir(void){
    char *il=compile(shape_source);
    require(strstr(il,"$apx_field_index(")!=NULL&&!strstr(il,"$apx_field("),"known named records use indexed access IR");
    require(strstr(il,", l 0)")&&strstr(il,", l 1)"),"distinct declaration positions emitted");free(il);
}
static void schema_ir(void){char *il=compile("M schema\nF main n:i64->i64\n  R n\n");require(!strstr(il,"$apx_validate("),"generated entry must not parse exported JSON schema");require(strstr(il,"$apx_validate_static(")!=NULL,"entry uses static schema validator");free(il);}
static void linked(size_t index){
    char *il=compile(fixtures[index].source);char ssa[4200],assembly[4200],obj[4200],exe[4200],define[64];
    snprintf(define,sizeof define,"-DAPX_SHAPES_CASE=%zu",index);
    snprintf(ssa,sizeof ssa,"%s/program.ssa",directory);snprintf(assembly,sizeof assembly,"%s/program.s",directory);snprintf(obj,sizeof obj,"%s/program.o",directory);snprintf(exe,sizeof exe,"%s/linked",directory);save(ssa,il);free(il);
    char *qbe[]={".build/tools/qbe","-t","amd64_sysv","-o",assembly,ssa,NULL};require(!run(qbe),"QBE");char *as[]={"as",assembly,"-o",obj,NULL};require(!run(as),"assemble");
    char *cc[]={"cc","-std=c99","-O2","-Wall","-Wextra","-Werror","-no-pie","-Wl,--wrap=apx_validate_static,--wrap=apx_validate","-DAPX_SHAPES_LINKED",define,
#ifdef APX_ARENA_ASAN
        "-O1","-g","-fno-omit-frame-pointer","-fsanitize=address,undefined",
#endif
        "tests/test_native_shapes.c",obj,"-o",exe,NULL};require(!run(cc),"link real entry observer");char *args[]={exe,NULL};require(!run(args),"linked allocation/reset proof");
}
static const ApxSchema si={.kind=APX_SCHEMA_I64};
static const ApxSchema sa={.kind=APX_SCHEMA_ARRAY,.item=&si};
static void abi(void){
    struct AlignSchema {char c;ApxSchema s;};struct AlignField {char c;ApxSchemaField f;};
    require(APX_SCHEMA_NULL==0&&APX_SCHEMA_I64==1&&APX_SCHEMA_F64==2&&APX_SCHEMA_BOOL==3&&APX_SCHEMA_STR==4&&APX_SCHEMA_ARRAY==5&&APX_SCHEMA_MAP==6&&APX_SCHEMA_OPTION==7&&APX_SCHEMA_RESULT==8&&APX_SCHEMA_STRUCT==9,"explicit descriptor tag ABI");
    require(sizeof(ApxSchema)==48&&offsetof(struct AlignSchema,s)==8&&offsetof(ApxSchema,kind)==0&&offsetof(ApxSchema,name)==8&&offsetof(ApxSchema,count)==16&&offsetof(ApxSchema,item)==24&&offsetof(ApxSchema,error)==32&&offsetof(ApxSchema,fields)==40,"six-slot align8 descriptor ABI");
    require(sizeof(ApxSchemaField)==16&&offsetof(struct AlignField,f)==8&&offsetof(ApxSchemaField,name)==0&&offsetof(ApxSchemaField,type)==8,"align8 field pair ABI");
}
static void descriptor_trap(const ApxSchema *s,ApxValue *input,const char *kind){
    if(!setjmp(escape)){(void)apx_validate_static(s,input);require(0,"invalid descriptor traps");}
    else require(!strcmp(error_kind,kind),"bounded descriptor failure");
}
static void runtime_helpers(void){
    abi();
    ApxValue *a=apx_array_i64_new(2);apx_array_i64_put(a,0,INT64_MIN);apx_array_i64_put(a,1,INT64_MAX);
    encoded_is(apx_validate_static(&sa,a),"[-9223372036854775808,9223372036854775807]");
    static const ApxSchemaField fields[]={{"x",&si},{"y",&si}};
    static const ApxSchema sr={.kind=APX_SCHEMA_STRUCT,.name="R",.count=2,.fields=fields};
    ApxValue *input=apx_json_parse("{\"y\":2,\"x\":1}",13);require(input!=NULL,"record input");
    ApxValue *r=apx_validate_static(&sr,input),*copy=apx_record_copy(r);
    require(integer(apx_field_index(r,0))==1&&integer(apx_field_index(r,1))==2,"validated declaration-order slots");
    require(apx_field(r,"x")==apx_field_index(r,0)&&apx_json_get(r,"y")==apx_field_index(r,1),"name accessor/C compatibility");
    apx_record_put(copy,0,"x",apx_int(9));require(integer(apx_field_index(r,0))==1&&integer(apx_field_index(copy,0))==9,"copied update keeps layout and snapshot");
    for(volatile int i=-1;i<=2;i+=3){if(!setjmp(escape)){apx_field_index(r,i);require(0,"bad field index");}else require(!strcmp(error_code,"R999")&&!strcmp(error_message,"invalid native record field access"),"index bounds diagnostic");}
    if(!setjmp(escape)){apx_field_index(a,0);require(0,"wrong record kind");}else require(!strcmp(error_code,"T101")&&!strcmp(error_message,". expects struct value"),"record type diagnostic");
    ApxValue *empty=apx_record_new("Empty",1);
    if(!setjmp(escape)){apx_field_index(empty,0);require(0,"uninitialized slot");}else require(!strcmp(error_code,"R999"),"empty slot bounded");
    static const ApxSchema invalid[]={ {.kind=99}, {.kind=APX_SCHEMA_ARRAY}, {.kind=APX_SCHEMA_MAP}, {.kind=APX_SCHEMA_STRUCT,.name="R",.count=1}, {.kind=APX_SCHEMA_STRUCT,.name="R",.count=UINT64_MAX}, {.kind=APX_SCHEMA_STRUCT,.count=0} };
    descriptor_trap(NULL,input,"trap");descriptor_trap(&si,NULL,"trap");
    for(size_t i=0;i<sizeof invalid/sizeof invalid[0];i++)descriptor_trap(&invalid[i],i==1?a:input,"trap");
    ApxSchemaField missing={"x",NULL};ApxSchema broken={.kind=APX_SCHEMA_STRUCT,.name="R",.count=1,.fields=&missing};descriptor_trap(&broken,input,"trap");
    static const ApxSchema sf={.kind=APX_SCHEMA_F64};ApxValue *nonfinite=apx_float(INFINITY);descriptor_trap(&sf,nonfinite,"numeric_out_of_range");
    reset_memory();
    /* Allocation failure must neither alter the input nor cache partial output. */
    input=apx_array_new(1);apx_array_put(input,0,apx_int(7));size_t before=memory_used;memory_used=APX_MEMORY;
    descriptor_trap(&sa,input,"memory_limit_exceeded");memory_used=before;encoded_is(apx_validate_static(&sa,input),"[7]");require(integer(input->items[0])==7,"input retained after OOM retry");
    reset_memory();
    ApxSchema nested[66];memset(nested,0,sizeof nested);nested[0]=si;
    for(size_t i=1;i<66;i++){nested[i].kind=APX_SCHEMA_ARRAY;nested[i].item=&nested[i-1];}
    input=apx_int(3);for(size_t i=0;i<64;i++){a=apx_array_new(1);apx_array_put(a,0,input);input=a;}
    require(apx_validate_static(&nested[64],input)!=NULL,"depth64 accepts full value");
    a=apx_array_new(1);apx_array_put(a,0,input);descriptor_trap(&nested[65],a,"type_nesting_limit_exceeded");
    reset_memory();
}
static void rejection(const char *source,const char *code,const char *kind,const char *message){
    char *il=NULL,*canonical=NULL,*diag=NULL;require(apx_compile_source(source,&il,&canonical,&diag)!=0&&diag&&!il,"compile rejection");
    ApxValue *v=apx_json_parse(diag,strlen(diag));require(v!=NULL,"diagnostic JSON");
    const char *actual=apx_json_string(apx_json_get(v,"message"));
    if(!actual||strcmp(actual,message))fprintf(stderr,"diagnostic: %s\n",diag);
    require(!strcmp(apx_json_string(apx_json_get(v,"code")),code)&&!strcmp(apx_json_string(apx_json_get(v,"kind")),kind)&&actual&&!strcmp(actual,message),"unchanged compile code/kind/message");
    free(il);free(canonical);free(diag);reset_memory();
}
static void compile_contracts(void){
    rejection("M unsupported\nF main x:option i64->i64\n  R 0\n","A104","unsupported_input_type","JSON input must use primitive, array or struct types");
    rejection("M unsupported\nF main x:result i64 str->i64\n  R 0\n","A104","unsupported_input_type","JSON input must use primitive, array or struct types");
    rejection("M recursive\nT Node struct\n  next:Node\nF main x:Node->i64\n  R 0\n","T104","recursive_type","recursive named type is not supported");
    char source[8192];size_t used=(size_t)snprintf(source,sizeof source,"M deep\n");
    /* Use an explicit named chain, so source-syntax depth cannot mask expanded
     * type rejection or the generated-schema traversal. */
    for(unsigned i=0;i<66;i++){
        if(i==65)used+=(size_t)snprintf(source+used,sizeof source-used,"T D%u struct\n  next:i64\n",i);
        else used+=(size_t)snprintf(source+used,sizeof source-used,"T D%u struct\n  next:D%u\n",i,i+1);
    }
    snprintf(source+used,sizeof source-used,"F main x:D0->i64\n  R 0\n");
    rejection(source,"T106","type_nesting_limit_exceeded","expanded type nesting exceeds maximum depth 64");
    const char *library="M library\nF choose x:option i64 y:i64->i64\n  C x\n    some n\n      R (+ n y)\n    none\n      R y\nX direct\n  A (= (choose (some 1) 2) 3)\n";
    char *il=NULL,*canonical=NULL,*diag=NULL;
    require(!apx_compile_tests(library,&il,&canonical,&diag)&&il,"tests-only library retains non-JSON function ABI");
    require(!strstr(il,"$apx_validate_static(")&&!strstr(il,"$apx_validate("),"test artifact does not validate host schema");
    free(il);free(canonical);free(diag);
    /* Contract generation is independent metadata; input/return structure stays
     * literal rather than being derived from native descriptor tags. */
    char *contract=NULL;require(!apx_describe_source(fixtures[0].source,NULL,&contract,NULL,&diag)&&contract,"contract metadata");
    ApxValue *v=apx_json_parse(contract,strlen(contract));require(v!=NULL,"contract JSON");
    require(strstr(contract,"\"parameters\":[{\"name\":\"n\",\"type\":{\"kind\":\"i64\"}}],\"return_type\":{\"kind\":\"i64\"},\"definitions\":[]")!=NULL,"contract type bytes unchanged");
    free(contract);free(diag);reset_memory();
}
static void native_depth(void){
    char source[8192],input[2048],path[4200],in[4200],output[4200];size_t used=(size_t)snprintf(source,sizeof source,"M depth\n"),at=0;
    for(unsigned i=0;i<64;i++){
        if(i==63)used+=(size_t)snprintf(source+used,sizeof source-used,"T D%u struct\n  next:i64\n",i);
        else used+=(size_t)snprintf(source+used,sizeof source-used,"T D%u struct\n  next:D%u\n",i,i+1);
        memcpy(input+at,"{\"next\":",8);at+=8;
    }
    snprintf(source+used,sizeof source-used,"F main x:D0->i64\n  R 3\n");input[at++]='3';for(unsigned i=0;i<64;i++)input[at++]='}';input[at]=0;
    snprintf(path,sizeof path,"%s/source.apx",directory);snprintf(in,sizeof in,"%s/input.json",directory);save(path,source);save(in,input);
    snprintf(output,sizeof output,"%s/output",directory);
    char *args[]={"bin/apodexis","run",path,"--input",in,"--json",NULL};require(!run_to(args,output),"actual ELF validates depth64 named input");
    char *text=readall(output);ApxValue *v=apx_json_parse(text,strlen(text));require(v!=NULL,"depth64 native envelope");
    encoded_is(apx_json_get(v,"output"),"3");require(apx_json_integer(apx_json_get(v,"fuel_used"))==1,"depth64 native fuel");free(text);reset_memory();
    save(path,"M library\nF choose x:option i64 y:i64->i64\n  C x\n    some n\n      R (+ n y)\n    none\n      R y\nX direct\n  A (= (choose (some 1) 2) 3)\n");
    char *tests[]={"bin/apodexis","test",path,"--json",NULL};require(!run_to(tests,output),"actual no-main declared tests");
    text=readall(output);v=apx_json_parse(text,strlen(text));require(v!=NULL,"declared test envelope");
    ApxValue *results=apx_json_get(v,"tests");require(apx_json_length(results)==1,"one no-main test really ran");
    const char *status=apx_json_string(apx_json_get(apx_json_item(results,0),"status"));require(status&&!strcmp(status,"ok"),"native no-main declared test status");free(text);reset_memory();
}
int main(int argc,char **argv){
    if(argc==2&&!strcmp(argv[1],"--red-indexed")){indexed_ir();return 0;}
    if(argc==2&&!strcmp(argv[1],"--red-schema")){schema_ir();return 0;}
    const char *tmp=getenv("TMPDIR");require(tmp!=NULL,"TMPDIR");snprintf(directory,sizeof directory,"%s/apx-shapes-XXXXXX",tmp);require(mkdtemp(directory)!=NULL,"scratch");active=1;require(!atexit(cleanup),"cleanup registration");
    runtime_helpers();compile_contracts();native_depth();
    for(size_t i=0;i<sizeof fixtures/sizeof fixtures[0];i++)linked(i);
    indexed_ir();schema_ir();cleanup();require(!active,"scratch cleanup");printf("native shapes: %u checks PASS\n",checks);return 0;
}
#endif
