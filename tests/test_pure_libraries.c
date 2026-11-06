/* C99 frontend and actual QBE/native pure-library regressions. No interpreter.
 * Compile with FRONTEND sources and .build/apx_json.o, then run at repo root. */
#define _POSIX_C_SOURCE 200809L
#include "../compiler/apx_frontend.h"
#include "../runtime/apx_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
static unsigned checks,native_cases,semantic_cases;
static void require(int ok,const char *message){checks++;if(!ok){fprintf(stderr,"pure-library FAIL: %s\n",message);exit(1);}}
static void save(const char *path,const char *text){FILE *f=fopen(path,"wb");require(f!=NULL,"fixture open");require(fwrite(text,1,strlen(text),f)==strlen(text)&&!fclose(f),"fixture write");}
static char *readall(const char *path){FILE *f=fopen(path,"rb");require(f!=NULL,"response open");require(!fseek(f,0,SEEK_END),"response seek");long n=ftell(f);require(n>=0&&n<12*1024*1024,"bounded response");rewind(f);char *s=calloc(1,(size_t)n+1);require(s&&fread(s,1,(size_t)n,f)==(size_t)n&&!fclose(f),"response read");return s;}
static void native(const char *source,const char *input,const char *want,const char *error,const char *directory){
    char file[4096],in[4096],out[4096];snprintf(file,sizeof file,"%s/source.apx",directory);snprintf(in,sizeof in,"%s/input.json",directory);snprintf(out,sizeof out,"%s/output",directory);save(file,source);save(in,input);fflush(NULL);pid_t pid=fork();require(pid>=0,"fork native CLI");
    if(!pid){alarm(15);if(!freopen(out,"wb",stdout))_exit(126);execl("bin/apodexis","bin/apodexis","run",file,"--input",in,"--json",(char*)NULL);_exit(127);}
    int status;require(waitpid(pid,&status,0)==pid&&WIFEXITED(status),"bounded native process exit");char *text=readall(out);ApxValue *v=apx_json_parse(text,strlen(text));
    if(!v||WEXITSTATUS(status)!=(error?2:0)){fprintf(stderr,"SOURCE:\n%s\nRESULT:\n%s\n",source,text);require(0,"native success/error status");}
    require(v!=NULL,"native envelope JSON");
    if(!strncmp(source,"M once\n",7))require(apx_json_integer(apx_json_get(v,"fuel_used"))==5,"record and update payload evaluated exactly once");
    if(!strncmp(source,"M decode_once\n",14))require(apx_json_integer(apx_json_get(v,"fuel_used"))==2,"decode text evaluated exactly once, marker costs no fuel");
    if(error){ApxValue *diag=apx_json_get(v,"diagnostic");const char *code=apx_json_string(apx_json_get(diag,"code"));require(code&&!strcmp(code,error),"native diagnostic code");if(want){const char *msg=apx_json_string(apx_json_get(diag,"message"));require(msg&&!strcmp(msg,want),"native trap operand order");}}
    else{char *got=apx_json_encode(apx_json_get(v,"output"));ApxValue *expected=apx_json_parse(want,strlen(want));char *canonical=apx_json_encode(expected);if(!got||!canonical||strcmp(got,canonical))fprintf(stderr,"SOURCE:\n%s\nGOT %s\nWANT %s\n",source,got?got:"NULL",want);require(got&&canonical&&!strcmp(got,canonical),"exact native output");free(got);free(canonical);}
    free(text);apx_json_reset();native_cases++;
}
static void expression(const char *type,const char *expr,const char *want,const char *error,const char *directory){char source[8192];snprintf(source,sizeof source,"M pure\nF main->%s\n  R %s\n",type,expr);native(source,"null",want,error,directory);}
static void invalid(const char *source,const char *code){char *canonical=NULL,*diag=NULL;int result=apx_check_source(source,&canonical,&diag);if(!result||!diag||!strstr(diag,code))fprintf(stderr,"invalid source:\n%s\ndiag %s\n",source,diag?diag:"NULL");require(result&&diag&&strstr(diag,code),"recoverable semantic diagnostic");free(canonical);free(diag);semantic_cases++;}
/* Exercise decode through the same native CLI as the other pure libraries. */
static void decode(const char *type,const char *wire,size_t length,const char *want,const char *error,const char *d){
    char source[8192];snprintf(source,sizeof source,"M decode\nF main text:str->result %s str\n  R (json_decode %s text)\n",type,type);
    char *input=apx_json_encode(apx_string(wire,(int64_t)length));require(input!=NULL,"decode input envelope");
    native(source,input,want,error,d);free(input);
}
static void json_decoding(const char *d){
    decode("i64","9223372036854775807",19,"{\"tag\":\"ok\",\"value\":9223372036854775807}",NULL,d);
    decode("i64","-9223372036854775808",20,"{\"tag\":\"ok\",\"value\":-9223372036854775808}",NULL,d);
    const char *bad_integer[]={"9223372036854775808","-9223372036854775809","1.0","true","\"1\"","null"};
    for(size_t i=0;i<sizeof bad_integer/sizeof bad_integer[0];i++)decode("i64",bad_integer[i],strlen(bad_integer[i]),"{\"tag\":\"err\",\"error\":\"numeric_out_of_range\"}",NULL,d);
    const char *floats[]={"1","0.1","5e-324","1e-999","-1e-999","1.7976931348623157e308","9223372036854775808"};
    const char *values[]={"1.0","0.1","5e-324","0.0","-0.0","1.7976931348623157e+308","9.223372036854776e+18"};
    for(size_t i=0;i<sizeof floats/sizeof floats[0];i++){char want[160];snprintf(want,sizeof want,"{\"tag\":\"ok\",\"value\":%s}",values[i]);decode("f64",floats[i],strlen(floats[i]),want,NULL,d);}
    decode("f64","1e999",5,"{\"tag\":\"err\",\"error\":\"numeric_out_of_range\"}",NULL,d);
    decode("f64","true",4,"{\"tag\":\"err\",\"error\":\"numeric_out_of_range\"}",NULL,d);
    decode("bool","true",4,"{\"tag\":\"ok\",\"value\":true}",NULL,d);
    decode("bool","0",1,"{\"tag\":\"err\",\"error\":\"type_mismatch\"}",NULL,d);
    decode("str","\"é😀\\u0000x\"",strlen("\"é😀\\u0000x\""),"{\"tag\":\"ok\",\"value\":\"é😀\\u0000x\"}",NULL,d);
    decode("str","\"\\ud83d\\ude00\"",14,"{\"tag\":\"ok\",\"value\":\"😀\"}",NULL,d);
    decode("[i64]","[1,-2]",6,"{\"tag\":\"ok\",\"value\":[1,-2]}",NULL,d);
    decode("map [i64]","{\"z\":[],\"a\\u0000\":[2],\"a\":[1]}",strlen("{\"z\":[],\"a\\u0000\":[2],\"a\":[1]}"),"{\"tag\":\"ok\",\"value\":{\"a\":[1],\"a\\u0000\":[2],\"z\":[]}}",NULL,d);
    decode("map option i64","{\"n\":{\"tag\":\"none\"},\"s\":{\"tag\":\"some\",\"value\":7}}",strlen("{\"n\":{\"tag\":\"none\"},\"s\":{\"tag\":\"some\",\"value\":7}}"),"{\"tag\":\"ok\",\"value\":{\"n\":{\"tag\":\"none\"},\"s\":{\"tag\":\"some\",\"value\":7}}}",NULL,d);
    decode("map result i64 str","{\"e\":{\"tag\":\"err\",\"error\":\"bad\"},\"o\":{\"tag\":\"ok\",\"value\":1}}",strlen("{\"e\":{\"tag\":\"err\",\"error\":\"bad\"},\"o\":{\"tag\":\"ok\",\"value\":1}}"),"{\"tag\":\"ok\",\"value\":{\"e\":{\"tag\":\"err\",\"error\":\"bad\"},\"o\":{\"tag\":\"ok\",\"value\":1}}}",NULL,d);
    const char *malformed[]={"","[1,]","01","1 2","\v1","\"\\ud800\"","\"\\uDC00\"","\"\\x00\""};
    for(size_t i=0;i<sizeof malformed/sizeof malformed[0];i++)decode("i64",malformed[i],strlen(malformed[i]),"{\"tag\":\"err\",\"error\":\"invalid_json\"}",NULL,d);
    const char nul[]={'1',0,'2'};decode("i64",nul,sizeof nul,"{\"tag\":\"err\",\"error\":\"invalid_json\"}",NULL,d);
    const char raw_string[]={'"','a',0,'b','"'};decode("str",raw_string,sizeof raw_string,"{\"tag\":\"err\",\"error\":\"invalid_json\"}",NULL,d);
    /* Duplicate fields are rejected document data, not terminal resources. */
    decode("map i64","{\"a\":1,\"a\":2}",13,"{\"tag\":\"err\",\"error\":\"duplicate_field\"}",NULL,d);
    const char *row="M nested\nT Row struct\n  id:i64\n  groups:map [bool]\nF main text:str->result [Row] str\n  R (json_decode [Row] text)\n";
    native(row,"\"[{\\\"groups\\\":{\\\"z\\\":[true,false],\\\"a\\\":[]},\\\"id\\\":7}]\"","{\"tag\":\"ok\",\"value\":[{\"id\":7,\"groups\":{\"a\":[],\"z\":[true,false]}}]}",NULL,d);
    native(row,"\"[{\\\"id\\\":7}]\"","{\"tag\":\"err\",\"error\":\"missing_field\"}",NULL,d);
    native(row,"\"[{\\\"id\\\":7,\\\"groups\\\":{},\\\"untrusted\\\":1}]\"","{\"tag\":\"err\",\"error\":\"extra_field\"}",NULL,d);
    native(row,"\"[{\\\"id\\\":7,\\\"groups\\\":{\\\"x\\\":[1]}}]\"","{\"tag\":\"err\",\"error\":\"type_mismatch\"}",NULL,d);
    native("M decoded_match\nF main->i64\n  C (json_decode i64 \"17\")\n    ok n\n      R (+ n 1)\n    err message\n      R (# message)\n","null","18",NULL,d);
    native("M decoded_bool\nF main->bool\n  C (json_decode bool \"true\")\n    ok b\n      R (! b)\n    err message\n      R false\n","null","false",NULL,d);
    native("M decoded_float\nF main->f64\n  C (json_decode f64 \"1\")\n    ok n\n      R (+ n 0.5)\n    err message\n      R 0.0\n","null","1.5",NULL,d);
    native("M decode_once\nF text->str\n  R \"1\"\nF main->result i64 str\n  R (json_decode i64 (text))\n","null","{\"tag\":\"ok\",\"value\":1}",NULL,d);
    native("M decode_trap\nF text->str\n  Z \"operand trap\"\nF main->result i64 str\n  R (json_decode i64 (text))\n","null","operand trap","R999",d);
}
static void decode_semantic(void){
    const char *bad[]={"(json_decode)","(json_decode i64)","(json_decode i64 \"1\" \"2\")","(json_decode i64 1)","(json_decode i64 text=\"1\")","(json_decode Missing \"1\")","(json_decode any \"1\")","(json_decode builder i64 \"1\")","(json_decode option i64 \"1\")","(json_decode result i64 str \"1\")","(json_decode [option i64] \"[]\")"};
    const char *codes[]={"T101","T101","T101","T101","N104","N101","N101","T101","A104","A104","A104"};
    for(size_t i=0;i<sizeof bad/sizeof bad[0];i++){
        char source[8192];snprintf(source,sizeof source,"M bad_decode\nF main->i64\n  L value %s\n  R 0\n",bad[i]);invalid(source,codes[i]);
        char *il=NULL,*diag=NULL;require(apx_compile_source(source,&il,NULL,&diag)&&!il&&diag&&strstr(diag,codes[i]),"decode compile agrees with check rejection");free(diag);
    }
    invalid("M marker_literal\nF main->i64\n  L value (json_decode \"i64\" \"1\")\n  R 0\n","P001");
    invalid("M marker_missing\nF main->i64\n  L value (json_decode [i64 \"[]\")\n  R 0\n","P001");
    invalid("M enum_decode\nT Kind enum\n  First\nF main->i64\n  L value (json_decode Kind \"{}\")\n  R 0\n","A104");
    invalid("M nested_decode\nT Row struct\n  value:option i64\nF main->i64\n  L value (json_decode Row \"{}\")\n  R 0\n","A104");
    invalid("M invalid_decode\nT Row struct\n  value:Missing\nF main->i64\n  L value (json_decode [Row] \"[]\")\n  R 0\n","N101");
    invalid("M recursive_decode\nT Row struct\n  value:Row\nF main->i64\n  L value (json_decode Row \"{}\")\n  R 0\n","T104");
    invalid("M result_decode\nF main->result bool str\n  R (json_decode i64 \"1\")\n","T101");
    char *diag=NULL;require(apx_check_source("M hole_decode\nF main->result i64 str\n  R (json_decode i64 _)\n",NULL,&diag)&&diag&&strstr(diag,"H001")&&strstr(diag,"\"expected_type\":\"str\""),"decode text hole expects str");free(diag);
    char *canonical=NULL,*again=NULL;const char *source="M marker_decode\nF main text:str->result map [i64] str\n  R (json_decode map [i64] text)\n";
    require(!apx_check_source(source,&canonical,&diag)&&canonical&&diag&&!strcmp(diag,"[]"),"concrete nested marker check");free(diag);diag=NULL;
    again=apx_format_source(canonical,&diag);require(again&&!strcmp(again,canonical)&&!diag,"decode marker canonical roundtrip");free(again);free(canonical);
    char deep[8192];size_t n=(size_t)snprintf(deep,sizeof deep,"M deep_decode\nF main->i64\n  L value (json_decode ");
    for(size_t i=0;i<65;i++)deep[n++]='[';
    memcpy(deep+n,"i64",3);n+=3;
    for(size_t i=0;i<65;i++)deep[n++]=']';
    snprintf(deep+n,sizeof deep-n," \"[]\")\n  R 0\n");invalid(deep,"P001");
    n=(size_t)snprintf(deep,sizeof deep,"M expanded_decode\nT Node0 struct\n  value:i64\n");
    for(size_t i=1;i<64;i++)n+=(size_t)snprintf(deep+n,sizeof deep-n,"T Node%zu struct\n  value:Node%zu\n",i,i-1);
    snprintf(deep+n,sizeof deep-n,"F main->i64\n  L decoded (json_decode [Node63] \"[]\")\n  R 0\n");invalid(deep,"T106");
    require(apx_check_source("M rejected_hole_decode\nF main->i64\n  L value (json_decode option i64 _)\n  R 0\n",NULL,&diag)&&diag&&strstr(diag,"A104")&&strstr(diag,"H001")&&strstr(diag,"\"expected_type\":\"str\""),"rejected decode retains typed text hole feedback");free(diag);
    require(apx_builtin_opcode("json_decode")==55&&apx_builtin_opcode("fs_read")==1001&&apx_builtin_opcode("http_get")==1006,"decode appends opcode without moving hosts");
}
static void decode_resources(const char *d){
    /* These are natural native decoder quotas, not injected child failures. */
    size_t n=1600000;char *wire=malloc(2*n+2);require(wire!=NULL,"decoder resource input");wire[0]='[';
    for(size_t i=0;i<n;i++){wire[2*i+1]='0';wire[2*i+2]=i+1==n?']':',';}wire[2*n+1]=0;
    decode("[i64]",wire,2*n+1,NULL,"R204",d);free(wire);
    size_t bytes=2*1024*1024;char *input=malloc(bytes+3);require(input!=NULL,"decoder overlimit input");input[0]='\"';memset(input+1,' ',bytes);input[bytes+1]='\"';input[bytes+2]=0;
    native("M decode_limit\nF main text:str->result i64 str\n  L twice (cat text text)\n  R (json_decode i64 (cat (cat twice twice) \"0\"))\n",input,NULL,"R202",d);free(input);
}
static void decode_declared(const char *d){
    const char *source="M decode_tests\nF main->result i64 str\n  R (json_decode i64 \"17\")\nX valid\n  A (= (main) (ok 17))\nX malformed\n  L decoded (json_decode i64 \"[\")\n  C decoded\n    ok n\n      A false\n    err message\n      A (= message \"invalid_json\")\nX strict\n  C (json_decode bool \"0\")\n    ok b\n      A false\n    err message\n      A (= message \"type_mismatch\")\n";
    char file[4096],out[4096];snprintf(file,sizeof file,"%s/source.apx",d);snprintf(out,sizeof out,"%s/output",d);save(file,source);fflush(NULL);pid_t p=fork();require(p>=0,"fork decode tests");
    if(!p){alarm(15);if(!freopen(out,"wb",stdout))_exit(126);execl("bin/apodexis","bin/apodexis","test",file,"--json",(char*)NULL);_exit(127);}
    int status;require(waitpid(p,&status,0)==p&&WIFEXITED(status),"decode declared tests exit");char *text=readall(out);if(WEXITSTATUS(status))fprintf(stderr,"%s\n",text);require(!WEXITSTATUS(status),"actual source-declared decoder tests pass");
    ApxValue *v=apx_json_parse(text,strlen(text)),*tests=apx_json_get(v,"tests");require(tests&&apx_json_length(tests)==3,"all decoder source tests discovered");free(text);apx_json_reset();
}
static void strings(const char *d){
    expression("i64","(str_chars \"aé😀\\0z\")","5",NULL,d);
    expression("i64","(str_chars \"\")","0",NULL,d);
    expression("i64","(str_chars \"e\\u{301}\")","2",NULL,d);
    expression("option str","(str_char \"aé😀\" 1)","{\"tag\":\"some\",\"value\":\"é\"}",NULL,d);
    expression("option str","(str_char \"aé😀\" 2)","{\"tag\":\"some\",\"value\":\"😀\"}",NULL,d);
    expression("option str","(str_char \"a\\0b\" 1)","{\"tag\":\"some\",\"value\":\"\\u0000\"}",NULL,d);
    expression("option str","(str_char \"a\" -1)","{\"tag\":\"none\"}",NULL,d);
    expression("option str","(str_char \"a\" 1)","{\"tag\":\"none\"}",NULL,d);
    expression("str","(str_slice \"aé😀z\" 1 2)","\"é😀\"",NULL,d);
    expression("str","(str_slice \"é\" 1 0)","\"\"",NULL,d);
    expression("str","(str_slice \"\" 0 0)","\"\"",NULL,d);
    expression("str","(str_slice \"é\" -1 1)",NULL,"R103",d);
    expression("str","(str_slice \"é\" 0 -1)",NULL,"R103",d);
    expression("str","(str_slice \"é\" 1 1)",NULL,"R103",d);
    expression("str","(str_slice \"é\" 9223372036854775807 1)",NULL,"R103",d);
    expression("option i64","(str_find \"é😀é\" \"😀\")","{\"tag\":\"some\",\"value\":1}",NULL,d);
    expression("option i64","(str_find \"é\" \"\")","{\"tag\":\"some\",\"value\":0}",NULL,d);
    expression("option i64","(str_find \"é\" \"a\")","{\"tag\":\"none\"}",NULL,d);
    expression("option i64","(str_find \"é\\0😀\" \"\\0\")","{\"tag\":\"some\",\"value\":1}",NULL,d);
    expression("[str]","(str_split \"é,😀,,\" \",\")","[\"é\",\"😀\",\"\",\"\"]",NULL,d);
    expression("[str]","(str_split \"\" \",\")","[\"\"]",NULL,d);
    expression("[str]","(str_split \"é😀é😀\" \"😀\")","[\"é\",\"é\",\"\"]",NULL,d);
    expression("[str]","(str_split \"a\\0b\" \"\\0\")","[\"a\",\"b\"]",NULL,d);
    expression("[str]","(str_split \"a\" \"\")",NULL,"R999",d);
    expression("str","(str_trim \" \\t\\né😀 \\u{b}\\u{c}\\r\")","\"é😀\"",NULL,d);
    expression("str","(str_trim \"\\u{a0}x\\u{a0}\")","\" x \"",NULL,d);
    expression("str","(str_trim \" \\0 \" )","\"\\u0000\"",NULL,d);
    expression("str","(str_replace \"aaaaa\" \"aa\" \"b\")","\"bba\"",NULL,d);
    expression("str","(str_replace \"é😀é\" \"é\" \"\\0\")","\"\\u0000😀\\u0000\"",NULL,d);
    expression("str","(str_replace \"abc\" \"b\" \"\")","\"ac\"",NULL,d);
    expression("str","(str_replace \"abc\" \"\" \"z\")",NULL,"R999",d);
    expression("str","(str_lower_ascii \"AZÉ😀\\0\")","\"azÉ😀\\u0000\"",NULL,d);
    expression("str","(str_upper_ascii \"azé😀\\0\")","\"AZé😀\\u0000\"",NULL,d);
    native("M input\nF main s:str->str\n  R (str_slice s 1 3)\n","\"xé\\u0000😀y\"","\"é\\u0000😀\"",NULL,d);
}
static void numbers(const char *d){
    const char *integers[]={"0","-0","001","9223372036854775807","-9223372036854775808"};const char *outputs[]={"0","0","1","9223372036854775807","-9223372036854775808"};
    for(size_t i=0;i<5;i++){char expr[128],want[128];snprintf(expr,sizeof expr,"(parse_i64 \"%s\")",integers[i]);snprintf(want,sizeof want,"{\"tag\":\"ok\",\"value\":%s}",outputs[i]);expression("result i64 str",expr,want,NULL,d);}
    const char *bad[]={"","-","+1"," 1","1 ","1x","9223372036854775808","-9223372036854775809","1.0","1\\0"};
    for(size_t i=0;i<10;i++){char expr[128];snprintf(expr,sizeof expr,"(parse_i64 \"%s\")",bad[i]);expression("result i64 str",expr,"{\"tag\":\"err\",\"error\":\"invalid i64\"}",NULL,d);}
    const char *floats[]={"1.25","-.5","1e+2","5e-324","1e-999","-0"};const char *float_out[]={"1.25","-0.5","100.0","5e-324","0.0","-0.0"};
    for(size_t i=0;i<6;i++){char expr[128],want[128];snprintf(expr,sizeof expr,"(parse_f64 \"%s\")",floats[i]);snprintf(want,sizeof want,"{\"tag\":\"ok\",\"value\":%s}",float_out[i]);expression("result f64 str",expr,want,NULL,d);}
    const char *bad_float[]={"NaN","inf","-Infinity","1e999","0x1p2","+1"," 1","1x","1e",".","1\\0"};
    for(size_t i=0;i<11;i++){char expr[128];snprintf(expr,sizeof expr,"(parse_f64 \"%s\")",bad_float[i]);expression("result f64 str",expr,"{\"tag\":\"err\",\"error\":\"invalid f64\"}",NULL,d);}
    expression("str","(str_i64 -9223372036854775808)","\"-9223372036854775808\"",NULL,d);
    expression("str","(str_f64 -0.0)","\"-0\"",NULL,d);
    expression("str","(str_f64 (* 1e308 1e308))",NULL,"R999",d);
    expression("result f64 str","(parse_f64 (str_f64 0.1))","{\"tag\":\"ok\",\"value\":0.1}",NULL,d);
}
static void collections(const char *d){
    expression("map i64","(map i64)","{}",NULL,d);
    expression("map i64","(map_put (map_put (map i64) \"z\" 1) \"a\" 2)","{\"a\":2,\"z\":1}",NULL,d);
    expression("map i64","(map_put (map_put (map i64) \"z\" 1) \"z\" 2)","{\"z\":2}",NULL,d);
    expression("option i64","(map_get (map_put (map i64) \"a\" 7) \"a\")","{\"tag\":\"some\",\"value\":7}",NULL,d);
    expression("option i64","(map_get (map i64) \"a\")","{\"tag\":\"none\"}",NULL,d);
    expression("bool","(map_has (map_put (map i64) \"\\0\" 7) \"\\0\")","true",NULL,d);
    expression("bool","(map_has (map_put (map i64) \"a\\0\" 7) \"a\")","false",NULL,d);
    expression("map i64","(map_remove (map_put (map i64) \"a\" 1) \"a\")","{}",NULL,d);
    expression("map i64","(map_remove (map_put (map i64) \"a\" 1) \"x\")","{\"a\":1}",NULL,d);
    expression("[str]","(map_keys (map_put (map_put (map_put (map str) \"é\" \"c\") \"z\" \"b\") \"a\\0\" \"a\"))","[\"a\\u0000\",\"z\",\"é\"]",NULL,d);
    expression("[i64]","(map_values (map_put (map_put (map i64) \"z\" 1) \"a\" 2))","[2,1]",NULL,d);
    expression("i64","(# (map_put (map i64) \"a\" 1))","1",NULL,d);
    expression("bool","(= (map_put (map_put (map i64) \"a\" 1) \"b\" 2) (map_put (map_put (map i64) \"b\" 2) \"a\" 1))","true",NULL,d);
    expression("map map i64","(map_put (map map i64) \"outer\" (map_put (map i64) \"inner\" 3))","{\"outer\":{\"inner\":3}}",NULL,d);
    expression("map [i64]","(map_put (map [i64]) \"empty\" [])","{\"empty\":[]}",NULL,d);
    native("M alias\nF main->[map i64]\n  L base (map_put (map i64) \"a\" 1)\n  L alias base\n  S base (map_put base \"a\" 2)\n  R [alias base]\n","null","[{\"a\":1},{\"a\":2}]",NULL,d);
    native("M input\nF main m:map i64->map i64\n  R m\n","{\"z\":1,\"a\\u0000\":2,\"a\":3}","{\"a\":3,\"a\\u0000\":2,\"z\":1}",NULL,d);
    native("M input\nF main m:map map [i64]->[str]\n  R (map_keys m)\n","{\"z\":{},\"a\":{\"x\":[1]}}","[\"a\",\"z\"]",NULL,d);
    native("M input\nF main m:map i64->map i64\n  R m\n","{\"x\":\"bad\"}",NULL,"A103",d);
    native("M input\nF main m:map i64->map i64\n  R m\n","[]",NULL,"A103",d);
    native("M input\nF main m:map i64->map i64\n  R m\n","{\"a\\u0000\":1,\"a\\u0000\":2}",NULL,"A104",d);
    expression("option i64","(array_get [1 2] 1)","{\"tag\":\"some\",\"value\":2}",NULL,d);
    expression("option i64","(array_get [1] -1)","{\"tag\":\"none\"}",NULL,d);
    expression("option i64","(array_get [] 0)","{\"tag\":\"none\"}",NULL,d);
    expression("[i64]","(array_set [1 2] 1 7)","[1,7]",NULL,d);
    expression("[i64]","(array_set [1] 1 7)",NULL,"R103",d);
    expression("[i64]","(array_set [1] -1 7)",NULL,"R103",d);
    expression("[i64]","(array_slice [1 2 3] 1 2)","[2,3]",NULL,d);
    expression("[i64]","(array_slice [] 0 0)","[]",NULL,d);
    expression("[i64]","(array_slice [1] 1 0)","[]",NULL,d);
    expression("[i64]","(array_slice [1] 0 2)",NULL,"R103",d);
    expression("[i64]","(array_slice [1] 0 -1)",NULL,"R103",d);
    expression("[i64]","(array_cat [1] [2 3])","[1,2,3]",NULL,d);
    expression("[i64]","(array_cat [] [])","[]",NULL,d);
    native("M snapshot\nF main->[[i64]]\n  L a [1 2]\n  L alias a\n  S a (array_set a 0 7)\n  R [alias a]\n","null","[[1,2],[7,2]]",NULL,d);
    native("M record\nT Row struct\n  id:i64\n  name:str\nF main->[Row]\n  L row (Row id=1 name=\"old\")\n  L alias row\n  S row (with row name=\"new\" id=7)\n  R [alias row]\n","null","[{\"id\":1,\"name\":\"old\"},{\"id\":7,\"name\":\"new\"}]",NULL,d);
    native("M order\nT Row struct\n  a:i64\n  b:i64\nF first->i64\n  Z \"text-first\"\nF second->i64\n  Z \"text-second\"\nF main->Row\n  L row (Row a=1 b=2)\n  R (with row b=(first) a=(second))\n","null","text-first","R999",d);
    expression("bool","(= (map_put (map i64) \"a\\0\" 1) (map_put (map i64) \"a\" 1))","false",NULL,d);
    expression("map i64","(map_remove (map_put (map_put (map_put (map i64) \"z\" 1) \"a\" 2) \"m\" 3) \"m\")","{\"a\":2,\"z\":1}",NULL,d);
    native("M recordmap\nT Row struct\n  id:i64\nF main m:map Row->map Row\n  R (map_put m \"a\" (Row id=7))\n","{\"z\":{\"id\":1}}","{\"a\":{\"id\":7},\"z\":{\"id\":1}}",NULL,d);
    native("M inputget\nF main m:map i64->option i64\n  R (map_get m \"a\\0\")\n","{\"a\\u0000\":2,\"a\":3}","{\"tag\":\"some\",\"value\":2}",NULL,d);
    expression("map option i64","(map_put (map option i64) \"a\" (some 7))","{\"a\":{\"tag\":\"some\",\"value\":7}}",NULL,d);
    native("M optionmap\nF main m:map option i64->map option i64\n  R m\n","{\"b\":{\"tag\":\"none\"},\"a\":{\"tag\":\"some\",\"value\":1}}","{\"a\":{\"tag\":\"some\",\"value\":1},\"b\":{\"tag\":\"none\"}}",NULL,d);
    native("M resultmap\nF main m:map result i64 str->map result i64 str\n  R m\n","{\"b\":{\"tag\":\"err\",\"error\":\"bad\"},\"a\":{\"tag\":\"ok\",\"value\":1}}","{\"a\":{\"tag\":\"ok\",\"value\":1},\"b\":{\"tag\":\"err\",\"error\":\"bad\"}}",NULL,d);
    native("M optionmap\nF main m:map option i64->map option i64\n  R m\n","{\"a\":{\"tag\":\"some\",\"value\":true}}",NULL,"A103",d);
    native("M optionmap\nF main m:map option i64->map option i64\n  R m\n","{\"a\":{\"tag\":\"none\",\"value\":1}}",NULL,"A103",d);
    native("M eager\nF base->[i64]\n  Z \"base-first\"\nF index->i64\n  Z \"index-second\"\nF payload->i64\n  Z \"payload-third\"\nF main->[i64]\n  R (array_set (base) (index) (payload))\n","null","base-first","R999",d);
    native("M once\nT Row struct\n  id:i64\nF make->Row\n  L value (Row id=1)\n  R value\nF payload->i64\n  L value 7\n  R value\nF main->Row\n  R (with (make) id=(payload))\n","null","{\"id\":7}",NULL,d);
}
/* These calls exercise the actual support implementation in sanitizer builds,
 * rather than merely parsing a response from an unsanitized native child. */
static void direct_equal(ApxValue *v,const char *expected){char *text=apx_json_encode(v);require(text&&!strcmp(text,expected),"direct runtime result");free(text);}
static void direct_runtime(void){
    const char binary[]="é\0😀";
    ApxValue *s=apx_string(binary,(int64_t)sizeof binary-1),*nul=apx_string("\0",1);
    direct_equal(apx_builtin(30,s,NULL),"3");
    direct_equal(apx_builtin3(32,s,apx_int(1),apx_int(2)),"\"\\u0000😀\"");
    direct_equal(apx_builtin(34,s,nul),"[\"é\",\"😀\"]");
    direct_equal(apx_builtin3(36,s,nul,apx_string("x",1)),"\"éx😀\"");
    ApxValue *map=apx_builtin(43,NULL,NULL);
    map=apx_builtin3(45,map,apx_string("z",1),apx_int(1));
    ApxValue *alias=map;
    map=apx_builtin3(45,map,nul,apx_int(2));
    map=apx_builtin3(45,map,apx_string("a",1),apx_int(3));
    direct_equal(alias,"{\"z\":1}");direct_equal(map,"{\"\\u0000\":2,\"a\":3,\"z\":1}");
    direct_equal(apx_builtin(48,map,NULL),"[\"\\u0000\",\"a\",\"z\"]");
    direct_equal(apx_builtin(46,map,apx_string("a",1)),"{\"\\u0000\":2,\"z\":1}");
    direct_equal(apx_builtin(39,apx_string("-9223372036854775808",20),NULL),"{\"tag\":\"ok\",\"value\":-9223372036854775808}");
    direct_equal(apx_builtin(40,apx_string("5e-324",6),NULL),"{\"tag\":\"ok\",\"value\":5e-324}");
    direct_equal(apx_builtin(42,apx_float(-0.0),NULL),"\"-0\"");
    ApxValue *a=apx_array_new(2);apx_array_put(a,0,apx_int(1));apx_array_put(a,1,apx_int(2));
    direct_equal(apx_builtin3(51,a,apx_int(0),apx_int(7)),"[7,2]");direct_equal(a,"[1,2]");
    direct_equal(apx_builtin3(52,a,apx_int(2),apx_int(0)),"[]");
    ApxValue *row=apx_record_new("Row",1);apx_record_put(row,0,"id",apx_int(1));
    ApxValue *copy=apx_record_copy(row);apx_record_put(copy,0,"id",apx_int(7));direct_equal(row,"{\"id\":1}");direct_equal(copy,"{\"id\":7}");
    apx_json_reset();
    const char *json="{\"z\":1,\"a\\u0000\":2,\"a\":3}";
    ApxValue *input=apx_json_parse(json,strlen(json));require(input&&apx_json_kind(input)==6,"raw object kind stays six");
    direct_equal(apx_validate("{\"kind\":\"map\",\"values\":{\"kind\":\"i64\"}}",input),"{\"a\":3,\"a\\u0000\":2,\"z\":1}");apx_json_reset();
}
static void semantic(void){
    const char *bad[]={"(str_chars)","(str_chars 1)","(str_slice \"x\" 0)","(str_slice \"x\" 0 true)","(str_replace \"x\" 1 \"y\")","(parse_i64 true)","(str_i64 1.0)","(str_f64 1)","(map)","(map i64 1)","(map Missing)","(map builder i64)","(map [builder i64])","(map_put (map i64) 1 2)","(map_put (map i64) \"x\" true)","(map_put (map i64) \"x\")","(map_get [1] \"x\")","(array_set [1] 0 true)","(array_cat [1] [true])","(array_slice [1] true 0)","(with)","(with 1 id=7)"};
    for(size_t i=0;i<sizeof bad/sizeof bad[0];i++){char source[8192];snprintf(source,sizeof source,"M bad\nF main->i64\n  L value %s\n  R 1\n",bad[i]);invalid(source,!strcmp(bad[i],"(map Missing)")?"N101":"T101");}
    const char *updates[]={"(with row)","(with row nope=1)","(with row id=1 id=2)","(with row id=true)","(with row 7)"};const char *codes[]={"T101","N101","N102","T101","N104"};
    for(size_t i=0;i<5;i++){char source[8192];snprintf(source,sizeof source,"M bad\nT Row struct\n  id:i64\nF main->i64\n  L row (Row id=1)\n  L value %s\n  R 1\n",updates[i]);invalid(source,codes[i]);}
    invalid("M bad\nF main->i64\n  L bad (map_put missing \"x\" true)\n  L other (str_chars unknown)\n  R 1\n","N101");
    invalid("M bad\nF f x:i64->i64\n  R x\nF main->i64\n  R (f x=1)\n","N104");
    char *explain=NULL,*contract=NULL,*pseudo=NULL,*diag=NULL;require(!apx_describe_source("M metadata\nF main m:map [i64]->map [i64]\n  R m\n",&explain,&contract,&pseudo,&diag),"map metadata renders");require(contract&&strstr(contract,"\"kind\":\"map\",\"values\":")&&pseudo&&strstr(pseudo,"map [i64]"),"map type in contract/pseudocode");free(explain);free(contract);free(pseudo);free(diag);
    char *all=NULL;require(apx_check_source("M recover\nT Row struct\n  id:i64\nF main->i64\n  L row (Row id=1)\n  L updated (with row id=true nope=missing id=7)\n  L broken (str_slice unknown true missingtwo)\n  R 1\n",NULL,&all)==1,"pure diagnostics error status");
    ApxValue *errors=apx_json_parse(all,strlen(all));require(errors&&apx_json_length(errors)>=6,"independent pure/update diagnostics aggregated");free(all);apx_json_reset();
    require(apx_builtin_opcode("none")==29&&apx_builtin_opcode("str_chars")==30&&apx_builtin_opcode("with")==54,"stable old and additive opcodes");
}
int main(void){
    char directory[]=".build/pure-libraries-XXXXXX";require(mkdtemp(directory)!=NULL,"owned native scratch");json_decoding(directory);decode_semantic();decode_resources(directory);decode_declared(directory);semantic();direct_runtime();strings(directory);numbers(directory);collections(directory);
    char path[4096];const char *files[]={"source.apx","input.json","output"};for(size_t i=0;i<3;i++){snprintf(path,sizeof path,"%s/%s",directory,files[i]);require(!unlink(path),"fixture cleanup");}require(!rmdir(directory),"scratch cleanup");
    printf("pure libraries: %u native cases, %u semantic cases, %u checks PASS\n",native_cases,semantic_cases,checks);return 0;
}
