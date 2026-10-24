/* Every current reference/example program is verified by the native CLI. */
#define _POSIX_C_SOURCE 200809L
#include "../runtime/apx_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
static unsigned programs,declared_tests;
static void require(int ok,const char *message){if(!ok){fprintf(stderr,"reference program FAIL: %s\n",message);exit(1);}}
static char *load(const char *path){FILE *f=fopen(path,"rb");require(f!=NULL,"open reference");require(!fseek(f,0,SEEK_END),"seek reference");long n=ftell(f);require(n>=0&&n<2*1024*1024,"bounded reference");rewind(f);char *text=calloc(1,(size_t)n+1);require(text&&fread(text,1,(size_t)n,f)==(size_t)n&&!fclose(f),"read reference");return text;}
static void verify(const char *source,const char *directory){char file[4096],out[4096];snprintf(file,sizeof file,"%s/source.apx",directory);snprintf(out,sizeof out,"%s/output",directory);FILE *f=fopen(file,"wb");require(f&&fwrite(source,1,strlen(source),f)==strlen(source)&&!fclose(f),"save reference program");fflush(NULL);pid_t pid=fork();require(pid>=0,"fork native verify");if(!pid){if(!freopen(out,"wb",stdout))_exit(126);execl("bin/apodexis","bin/apodexis","verify",file,"--json",(char*)NULL);_exit(127);}int status;require(waitpid(pid,&status,0)==pid&&WIFEXITED(status),"native verify wait");char *response=load(out);if(WEXITSTATUS(status))fprintf(stderr,"%s\nSOURCE:\n%s\n",response,source);require(WEXITSTATUS(status)==0,"reference native verify succeeds");ApxValue *value=apx_json_parse(response,strlen(response));const char *s=apx_json_string(apx_json_get(value,"status"));require(s&&!strcmp(s,"ok"),"reference verification status");ApxValue *tests=apx_json_get(apx_json_get(value,"test"),"tests");require(apx_json_length(tests)>0,"reference declares actual self tests");declared_tests+=(unsigned)apx_json_length(tests);programs++;free(response);}
int main(void){char directory[]=".build/reference-programs-XXXXXX";require(mkdtemp(directory)!=NULL,"native reference scratch");const char *paths[]={"resources/language.md","resources/types.md","resources/stdlib.md"};const char fence[]="```apodexis\n";
    for(size_t i=0;i<sizeof paths/sizeof paths[0];i++){char *text=load(paths[i]),*p=text;
        while((p=strstr(p,fence))){p+=sizeof fence-1;char *end=strstr(p,"```");require(end!=NULL,"closed source fence");char saved=*end;*end=0;if(!strncmp(p,"M ",2))verify(p,directory);*end=saved;p=end+3;}
        free(text);
    }
    char *data=load("resources/examples.json");ApxValue *all=apx_json_parse(data,strlen(data));require(all&&apx_json_kind(all)==5&&apx_json_length(all)==4,"four current MCP examples");
    for(size_t i=0;i<apx_json_length(all);i++){const char *source=apx_json_string(apx_json_get(apx_json_item(all,i),"source"));require(source!=NULL,"MCP source exists");verify(source,directory);}
    free(data);require(programs==10,"all ten complete reference programs executed");char file[4096];snprintf(file,sizeof file,"%s/source.apx",directory);unlink(file);snprintf(file,sizeof file,"%s/output",directory);unlink(file);require(!rmdir(directory),"reference scratch cleanup");apx_json_reset();printf("native reference programs: %u modules, %u source tests, all three guides and MCP example data PASS\n",programs,declared_tests);return 0;}
