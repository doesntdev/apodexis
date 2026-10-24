/* Compatibility regressions run entirely through the native C CLI. */
#define _POSIX_C_SOURCE 200809L
#include "../runtime/apx_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
static void save(const char *path,const char *data){FILE *f=fopen(path,"wb");assert(f);assert(fwrite(data,1,strlen(data),f)==strlen(data));assert(!fclose(f));}
static char *readall(const char *path){FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>=0&&n<16*1024*1024);rewind(f);char *data=calloc(1,(size_t)n+1);assert(data&&fread(data,1,(size_t)n,f)==(size_t)n);fclose(f);return data;}
static ApxValue *invoke(const char *command,const char *source,const char *input,const char *directory,int code){char file[4096],json[4096],output[4096];snprintf(file,sizeof file,"%s/source.apx",directory);snprintf(json,sizeof json,"%s/input.json",directory);snprintf(output,sizeof output,"%s/output",directory);save(file,source);save(json,input);fflush(NULL);pid_t pid=fork();assert(pid>=0);if(!pid){assert(freopen(output,"wb",stdout));execl("bin/apodexis","bin/apodexis",command,file,"--input",json,"--json",(char*)NULL);_exit(127);}int status;assert(waitpid(pid,&status,0)==pid&&WIFEXITED(status));if(WEXITSTATUS(status)!=code){char *text=readall(output);fprintf(stderr,"regression %s exit %d expected %d: %s\n",command,WEXITSTATUS(status),code,text);free(text);abort();}char *text=readall(output);ApxValue *value=apx_json_parse(text,strlen(text));assert(value);free(text);return value;}
static void ok(ApxValue *v){const char *s=apx_json_string(apx_json_get(v,"status"));assert(s&&!strcmp(s,"ok"));}
int main(void){char directory[]=".build/regressions-XXXXXX";assert(mkdtemp(directory));
    const char *library="M test_only\nF square x:i64->i64\n  R (* x x)\nX square_test\n  A (= (square 3) 9)\n";
    const char *option="M option_input_test\nF main v:option i64->i64\n  C v\n    some x\n      R x\n    none\n      R 0\nX direct\n  A (= (main (some 3)) 3)\n";
    const char *two="M two\nF main a:i64 b:i64->i64\n  R (+ a b)\nX direct\n  A (= (main 1 2) 3)\n";
    for(size_t i=0;i<3;i++){const char *source=i==0?library:i==1?option:two;ApxValue *result=invoke("test",source,"null",directory,0);ok(result);assert(apx_json_length(apx_json_get(result,"tests"))==1);assert(apx_json_integer(apx_json_get(apx_json_item(apx_json_get(result,"tests"),0),"fuel_used"))==(i==1?3:2));apx_json_reset();ok(invoke("verify",source,"null",directory,0));apx_json_reset();ApxValue *failed=invoke("run",source,"null",directory,2);assert(apx_json_integer(apx_json_get(failed,"fuel_used"))==0);apx_json_reset();}
    ApxValue *result=invoke("run","M float_minimal\nF main->f64\n  R 5e-324\n","null",directory,0);ok(result);char *number=apx_json_encode(apx_json_get(result,"output"));assert(number&&!strcmp(number,"5e-324"));free(number);assert(apx_json_integer(apx_json_get(result,"fuel_used"))==1);apx_json_reset();
    ok(invoke("run","M float_input\nF main v:f64->f64\n  R v\n","-5e-324",directory,0));apx_json_reset();
    result=invoke("run","M trap_binary\nF main->str\n  Z \"before\\0after\"\n","null",directory,2);const char *message=apx_json_string(apx_json_get(apx_json_get(result,"diagnostic"),"message"));assert(message&&apx_json_length(apx_json_get(apx_json_get(result,"diagnostic"),"message"))==12&&!memcmp(message,"before\0after",12));apx_json_reset();
    result=invoke("test","M recovery\nF main->i64\n  R 1\nX failed\n  A false\nX succeeded\n  A true\n","null",directory,1);ApxValue *tests=apx_json_get(result,"tests");assert(apx_json_length(tests)==2);assert(!strcmp(apx_json_string(apx_json_get(apx_json_item(tests,0),"status")),"error"));assert(!strcmp(apx_json_string(apx_json_get(apx_json_item(tests,1),"status")),"ok"));apx_json_reset();
    result=invoke("run","M recursive\nF recurse n:i64->i64\n  R (recurse n)\nF main->i64\n  R (recurse 0)\n","null",directory,2);assert(!strcmp(apx_json_string(apx_json_get(apx_json_get(result,"diagnostic"),"kind")),"recursion_limit_exceeded"));apx_json_reset();
    ApxValue *ordinary_check=invoke("check","M ordinary\nF main->i64\n  R 1\n","null",directory,0);
    assert(!strcmp(apx_json_string(apx_json_get(ordinary_check,"schema")),"apodexis.check_response.v1"));
    assert(!apx_json_get(ordinary_check,"repairs"));apx_json_reset();
    ApxValue *errors=invoke("check","M errors\nF main->i64\n  L one absent_one\n  L two absent_two\n  R 0\n","null",directory,2);
    assert(!strcmp(apx_json_string(apx_json_get(errors,"schema")),"apodexis.check_response.v1"));
    ApxValue *diagnostics=apx_json_get(errors,"diagnostics");assert(apx_json_length(diagnostics)==2);
    assert(apx_json_integer(apx_json_get(apx_json_get(apx_json_item(diagnostics,0),"span"),"start_byte"))==29);
    assert(apx_json_integer(apx_json_get(apx_json_get(apx_json_item(diagnostics,1),"span"),"start_byte"))==48);apx_json_reset();
    errors=invoke("run","M named\nF add a:i64 b:i64->i64\n  R (+ a b)\nF main->i64\n  R (add b=2 a=1)\n","null",directory,2);
    assert(!strcmp(apx_json_string(apx_json_get(apx_json_get(errors,"diagnostic"),"code")),"N104"));apx_json_reset();
    errors=invoke("run","M shadow\nF main->i64\n  L x 1\n  ? true\n    L x 2\n  R x\n","null",directory,2);
    assert(!strcmp(apx_json_string(apx_json_get(apx_json_get(errors,"diagnostic"),"code")),"N102"));apx_json_reset();
    char nested_source[16384],nested_input[16384];
    for(unsigned depth=63;depth<=64;depth++){
        size_t a=(size_t)snprintf(nested_source,sizeof nested_source,"M nested_arrays\nF main input:");
        for(unsigned i=0;i<depth;i++)nested_source[a++]='[';
        memcpy(nested_source+a,"i64",3);a+=3;for(unsigned i=0;i<depth;i++)nested_source[a++]=']';
        snprintf(nested_source+a,sizeof nested_source-a,"->i64\n  R 0\n");
        size_t b=0;for(unsigned i=0;i<depth;i++)nested_input[b++]='[';nested_input[b++]='0';for(unsigned i=0;i<depth;i++)nested_input[b++]=']';nested_input[b]=0;
        result=invoke("run",nested_source,nested_input,directory,0);ok(result);assert(apx_json_integer(apx_json_get(result,"output"))==0);apx_json_reset();
    }
    for(unsigned depth=30;depth<=64;depth+=34){
        size_t a=(size_t)snprintf(nested_source,sizeof nested_source,"M nested_records\n");
        for(unsigned i=0;i<depth;i++){
            if(i+1==depth)a+=(size_t)snprintf(nested_source+a,sizeof nested_source-a,"T Box%u struct\n  item:i64\n",i);
            else a+=(size_t)snprintf(nested_source+a,sizeof nested_source-a,"T Box%u struct\n  item:Box%u\n",i,i+1);
        }
        snprintf(nested_source+a,sizeof nested_source-a,"F main input:Box0->i64\n  R 0\n");size_t b=0;
        for(unsigned i=0;i<depth;i++){memcpy(nested_input+b,"{\"item\":",8);b+=8;}nested_input[b++]='0';for(unsigned i=0;i<depth;i++)nested_input[b++]='}';nested_input[b]=0;
        result=invoke("run",nested_source,nested_input,directory,0);ok(result);assert(apx_json_integer(apx_json_get(result,"output"))==0);apx_json_reset();
    }
    char file[4096];const char *names[]={"source.apx","input.json","output"};for(size_t i=0;i<3;i++){snprintf(file,sizeof file,"%s/%s",directory,names[i]);unlink(file);}assert(!rmdir(directory));puts("native review regressions: test/verify independent of main ABI, restricted host run, subnormal input/output, binary trap, failed-test recovery and recursion bound PASS");return 0;
}
