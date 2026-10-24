/* Entirely native fixture/builtin conformance runner. */
#define _POSIX_C_SOURCE 200809L
#include "../runtime/apx_runtime.h"
#include <assert.h>
#include <errno.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
static char *read_file(const char *path){FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long length=ftell(f);assert(length>=0&&length<16*1024*1024);rewind(f);char *data=calloc(1,(size_t)length+1);assert(data);assert(fread(data,1,(size_t)length,f)==(size_t)length);fclose(f);return data;}
static void write_file(const char *path,const char *text){FILE *f=fopen(path,"wb");assert(f);assert(fwrite(text,1,strlen(text),f)==strlen(text));assert(!fclose(f));}
static char *run(const char *operation,const char *source,const char *input,const char *work,int *code){char response[4096];assert(snprintf(response,sizeof response,"%s/result",work)>0);fflush(NULL);pid_t pid=fork();assert(pid>=0);
    if(!pid){FILE *out=freopen(response,"wb",stdout);assert(out);if(input)execl("bin/apodexis","bin/apodexis",operation,source,"--input",input,"--json",(char*)NULL);else execl("bin/apodexis","bin/apodexis",operation,source,"--json",(char*)NULL);_exit(127);}
    int status;assert(waitpid(pid,&status,0)==pid);assert(WIFEXITED(status));*code=WEXITSTATUS(status);return read_file(response);
}
static void equal_output(const char *response,const char *expected){ApxValue *actual=apx_json_parse(response,strlen(response)),*want=apx_json_parse(expected,strlen(expected));assert(actual&&want);const char *status=apx_json_string(apx_json_get(actual,"status"));assert(status&&!strcmp(status,"ok"));char *json=apx_json_encode(apx_json_get(actual,"output")),*other=apx_json_encode(want);assert(json&&other);if(strcmp(json,other)){fprintf(stderr,"native output mismatch: %s != %s\n",json,other);abort();}free(json);free(other);}
int main(void){
    char workspace[]=".build/conformance-XXXXXX";assert(mkdtemp(workspace));char source_path[4096],input_path[4096],output_path[4096];snprintf(source_path,sizeof source_path,"%s/source.apx",workspace);snprintf(input_path,sizeof input_path,"%s/input.json",workspace);snprintf(output_path,sizeof output_path,"%s/result",workspace);
    unsigned builtins=0,declared=0,fixtures=0;char *catalog=read_file("fixtures/builtins/catalog.json");
    for(size_t i=0;;i++){
        apx_json_reset();ApxValue *all=apx_json_parse(catalog,strlen(catalog));assert(all&&apx_json_kind(all)==5);if(i>=apx_json_length(all))break;ApxValue *item=apx_json_item(all,i);const char *source=apx_json_string(apx_json_get(item,"source"));assert(source);char *input=apx_json_encode(apx_json_get(item,"input")),*want=apx_json_encode(apx_json_get(item,"expected"));assert(input&&want);write_file(source_path,source);write_file(input_path,input);free(input);int code;char *response=run("run",source_path,input_path,workspace,&code);assert(code==0);equal_output(response,want);free(response);free(want);builtins++;
    }
    free(catalog);assert(builtins==29);
    const char *examples[]={"examples/hello.apx","examples/rowdiff.apx","examples/http_hello.apx","examples/web_static.apx"};
    for(size_t i=0;i<sizeof examples/sizeof examples[0];i++){int code;char *response=run("test",examples[i],NULL,workspace,&code);assert(code==0);ApxValue *v=apx_json_parse(response,strlen(response));assert(v);const char *status=apx_json_string(apx_json_get(v,"status"));assert(status&&!strcmp(status,"ok"));assert(apx_json_length(apx_json_get(v,"tests")));declared++;free(response);apx_json_reset();}
    const char *families[]={"rowdiff","http_hello","web_static"};
    for(size_t f=0;f<3;f++){
        char directory[4096];snprintf(directory,sizeof directory,"fixtures/%s",families[f]);DIR *entries=opendir(directory);assert(entries);struct dirent *entry;
        while((entry=readdir(entries))){const char suffix[]=".input.json";size_t length=strlen(entry->d_name);if(length<sizeof suffix-1||strcmp(entry->d_name+length-(sizeof suffix-1),suffix))continue;
        char input[4096],expect[4096],source[4096];snprintf(input,sizeof input,"fixtures/%s/%s",families[f],entry->d_name);snprintf(expect,sizeof expect,"fixtures/%s/%.*s.expected.json",families[f],(int)(length-(sizeof suffix-1)),entry->d_name);snprintf(source,sizeof source,"examples/%s.apx",families[f]);assert(!access(expect,R_OK));
        int code;char *response=run("run",source,input,workspace,&code),*want_text=read_file(expect);ApxValue *actual=apx_json_parse(response,strlen(response)),*want=apx_json_parse(want_text,strlen(want_text));assert(actual&&want);const char *s=apx_json_string(apx_json_get(actual,"status")),*w=apx_json_string(apx_json_get(want,"status"));assert(s&&w&&!strcmp(s,w));
        if(!strcmp(w,"ok")){char *output=apx_json_encode(apx_json_get(want,"output"));assert(code==0);equal_output(response,output);free(output);}
        else{assert(code==2);ApxValue *d=apx_json_get(actual,"diagnostic"),*e=apx_json_get(want,"diagnostic");const char *k=apx_json_string(apx_json_get(d,"kind")),*p=apx_json_string(apx_json_get(e,"kind"));assert(k&&p&&!strcmp(k,p));}
        free(response);free(want_text);apx_json_reset();fixtures++;
        }
        assert(!closedir(entries));
    }
    unsigned negatives=0;DIR *invalid=opendir("fixtures/check");assert(invalid);struct dirent *entry;
    while((entry=readdir(invalid))){size_t length=strlen(entry->d_name);if(length<4||strcmp(entry->d_name+length-4,".apx"))continue;char source[4096],expect[4096];snprintf(source,sizeof source,"fixtures/check/%s",entry->d_name);snprintf(expect,sizeof expect,"fixtures/check/%.*s.expected.json",(int)(length-4),entry->d_name);int code;char *response=run("check",source,NULL,workspace,&code),*expected=read_file(expect);assert(code==2);ApxValue *actual=apx_json_parse(response,strlen(response)),*want=apx_json_parse(expected,strlen(expected));assert(actual&&want);ApxValue *diag=apx_json_item(apx_json_get(actual,"diagnostics"),0),*wanted=apx_json_item(apx_json_get(want,"diagnostics"),0);const char *keys[]={"code","kind"};for(size_t i=0;i<2;i++){const char *a=apx_json_string(apx_json_get(diag,keys[i])),*b=apx_json_string(apx_json_get(wanted,keys[i]));assert(a&&b&&!strcmp(a,b));}ApxValue *wanted_span=apx_json_get(wanted,"span");if(wanted_span){char *actual_span=apx_json_encode(apx_json_get(diag,"span")),*expected_span=apx_json_encode(wanted_span);assert(actual_span&&expected_span&&!strcmp(actual_span,expected_span));free(actual_span);free(expected_span);}free(response);free(expected);apx_json_reset();negatives++;}
    assert(!closedir(invalid));assert(negatives==10);
    assert(fixtures==17);unlink(source_path);unlink(input_path);unlink(output_path);assert(!rmdir(workspace));
    printf("native conformance: %u builtin source programs, %u maintained example test suites, %u ABI fixtures, %u negative compiler fixtures PASS\n",builtins,declared,fixtures,negatives);return 0;
}
