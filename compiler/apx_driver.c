#define _GNU_SOURCE
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
/* Standalone C compiler/CLI/stdio MCP. libc + QBE + platform assembler/linker;
 * no Python/Rust interpreter, JSON/crypto library or sandbox package. */
#include "apx_hash.h"
#include "apx_frontend.h"
#include "apx_modules.h"
#include "apx_host.h"
#include "apx_http.h"
#include "apx_serve.h"
#include "../runtime/apx_runtime.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <linux/memfd.h>
#include <signal.h>
#include <poll.h>
#include <sys/socket.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/syscall.h>
#include <unistd.h>

/* Public, stable frontend interface; frontend ASTs remain private. */
extern int apx_compile_source(const char *,char **,char **,char **);
extern char *apx_format_source(const char *,char **);
extern int apx_check_source(const char *,char **,char **);
#define VERSION "0.1.11"
#define SOURCE_LIMIT 65536u
#define FRAME_LIMIT (4u*1024u*1024u)
#define PROCESS_LIMIT (12u*1024u*1024u)
/* Compiler diagnostic/teaching output has its own finite transport budget.
 * This does not change the untrusted input or public C checker limits. */
#define DIAGNOSTIC_TRANSPORT_LIMIT (2u*1024u*1024u)
#define PATH_LIMIT 4096u
struct Buffer {char *text;size_t length,capacity,maximum;int exceeded;};
static char root[PATH_LIMIT];
static volatile sig_atomic_t active_process,timed_out,shutdown_requested;
static ApxHostPolicy *host_policy;
static int host_enabled;
static int serving_invocation;
static int64_t serving_deadline;
static void die(const char *message){fprintf(stderr,"apodexis: %s\n",message);exit(70);}
static void *memory(size_t n){void *p=calloc(1,n?n:1);if(!p)die("allocation failed");return p;}
static char *duplicate(const char *s){size_t n=strlen(s)+1;char *p=memory(n);memcpy(p,s,n);return p;}
static void add(struct Buffer *b,const char *s,size_t n){
    if(b->exceeded)return;
    if(b->maximum&&(n>b->maximum||b->length>b->maximum-n)){b->exceeded=1;return;}
    if(n>PROCESS_LIMIT||b->length>PROCESS_LIMIT-n)die("response exceeds bounded storage");
    if(b->length+n+1>b->capacity){size_t next=b->capacity?b->capacity:256;while(next<b->length+n+1)next*=2;b->text=realloc(b->text,next);if(!b->text)die("allocation failed");b->capacity=next;}
    memcpy(b->text+b->length,s,n);b->length+=n;b->text[b->length]=0;
}
static void text(struct Buffer *b,const char *s){add(b,s,strlen(s));}
static void number(struct Buffer *b,uint64_t n){char out[32];snprintf(out,sizeof out,"%"PRIu64,n);text(b,out);}
static void quote(struct Buffer *b,const char *s){
    text(b,"\"");for(;*s&&!b->exceeded;s++){unsigned char c=(unsigned char)*s;
        if(c=='"')text(b,"\\\"");else if(c=='\\')text(b,"\\\\");else if(c=='\n')text(b,"\\n");else if(c=='\r')text(b,"\\r");else if(c=='\t')text(b,"\\t");
        else if(c<32){char out[7];snprintf(out,sizeof out,"\\u%04x",c);text(b,out);}else add(b,(const char*)&c,1);
    }text(b,"\"");
}
static char *finish(struct Buffer *b){return b->text?b->text:duplicate("");}
static void path(char out[PATH_LIMIT],const char *directory,const char *name){
    size_t a=strlen(directory),b=strlen(name);if(a+b+2>PATH_LIMIT)die("path too long");memcpy(out,directory,a);out[a]='/';memcpy(out+a+1,name,b+1);
}
static char *read_stream(FILE *stream,size_t maximum){struct Buffer b={0};char block[8192];size_t n;
    while((n=fread(block,1,sizeof block,stream))){if(n>maximum||b.length>maximum-n||((maximum==SOURCE_LIMIT||maximum==FRAME_LIMIT)&&memchr(block,0,n))){free(b.text);return NULL;}add(&b,block,n);}
    if(ferror(stream)){free(b.text);return NULL;}return finish(&b);
}
static char *read_path(const char *name,size_t maximum){FILE *stream=fopen(name,"rb");if(!stream)return NULL;char *s=read_stream(stream,maximum);fclose(stream);return s;}
static int write_new(const char *name,const char *data,size_t length,int mode){int fd=open(name,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,mode);if(fd<0)return -1;
    size_t offset=0;while(offset<length){ssize_t n=write(fd,data+offset,length-offset);if(n<0&&errno==EINTR)continue;if(n<=0){close(fd);unlink(name);return -1;}offset+=(size_t)n;}
    if(close(fd)){unlink(name);return -1;}return 0;
}
static void hash_text(const char *prefix,const char *value,char out[80]){char hex[65];apx_sha256(value,strlen(value),hex);snprintf(out,80,"%s%s",prefix,hex);}
static char *generic_error(const char *kind,const char *message){struct Buffer b={0};text(&b,"{\"status\":\"error\",\"backend\":\"qbe\",\"diagnostic\":{\"schema\":\"apodexis.diagnostic.v1\",\"severity\":\"error\",\"code\":\"R207\",\"phase\":\"runtime\",\"kind\":");quote(&b,kind);text(&b,",\"message\":");quote(&b,message);text(&b,"},\"fuel_used\":0}");return finish(&b);}
static int successful(const char *json){ApxValue *v=apx_json_parse(json,strlen(json));const char *status=apx_json_string(apx_json_get(v,"status"));return status&&!strcmp(status,"ok");}
static void find_root(void){ssize_t n=readlink("/proc/self/exe",root,sizeof root-1);if(n<=0||n>=(ssize_t)sizeof root-1)die("cannot resolve compiler executable");root[n]=0;
    char *end=strrchr(root,'/');if(!end)die("invalid executable path");*end=0;end=strrchr(root,'/');if(!end)die("invalid distribution layout");*end=0;
    char plain[PATH_LIMIT],tls[PATH_LIMIT];path(plain,root,".build/tools/apx-http");path(tls,root,".build/tools/apx-http-tls");apx_host_helpers(plain,tls);char crypto[PATH_LIMIT];path(crypto,root,".build/tools/apx-pqc");apx_host_pqc_helper(crypto);
}

/* Bounded event-driven child supervision, no shell/eval and no inherited secret
 * environment. SIGALRM terminates the entire owned process group on deadline. */
static void deadline(int signo){(void)signo;timed_out=1;if(active_process>0)kill(-(pid_t)active_process,SIGKILL);}
static void shutdown_signal(int signo){shutdown_requested=signo;if(active_process>0)kill(-(pid_t)active_process,SIGKILL);}
static void limit_child(int native){struct rlimit bound={30,30};int ok=setrlimit(RLIMIT_CPU,&bound)==0;bound.rlim_cur=bound.rlim_max=512u*1024u*1024u;ok=ok&&setrlimit(RLIMIT_AS,&bound)==0;
    bound.rlim_cur=bound.rlim_max=32u*1024u*1024u;ok=ok&&setrlimit(RLIMIT_FSIZE,&bound)==0;bound.rlim_cur=bound.rlim_max=64;ok=ok&&setrlimit(RLIMIT_NOFILE,&bound)==0;
    bound.rlim_cur=bound.rlim_max=0;ok=ok&&setrlimit(RLIMIT_CORE,&bound)==0;if(native)ok=ok&&setrlimit(RLIMIT_NPROC,&bound)==0;
    bound.rlim_cur=bound.rlim_max=8u*1024u*1024u;ok=ok&&setrlimit(RLIMIT_STACK,&bound)==0;
    if(!ok)_exit(125);
}
struct Process {int exit_code;char *out,*error;};
static struct Process execute_image(char *const argv[],const char *input,const char *directory,int native,int image_fd){
    struct Process result={70,NULL,NULL};char in[PATH_LIMIT],out[PATH_LIMIT],err[PATH_LIMIT];path(in,directory,"stdin");path(out,directory,"stdout");path(err,directory,"stderr");
    unlink(in);unlink(out);unlink(err);
    if(write_new(in,input,strlen(input),0600)){result.error=duplicate("cannot prepare child input");return result;}
    int fi=open(in,O_RDONLY|O_CLOEXEC|O_NOFOLLOW),fo=open(out,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600),fe=open(err,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);
    if(fi<0||fo<0||fe<0){if(fi>=0)close(fi);if(fo>=0)close(fo);if(fe>=0)close(fe);result.error=duplicate("cannot open bounded child channels");return result;}
    int broker[2]={-1,-1};int use_host=native&&host_policy&&host_enabled;if(use_host&&socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,broker))die("host broker channel failed");
    if(shutdown_requested){close(fi);close(fo);close(fe);if(use_host){close(broker[0]);close(broker[1]);}unlink(in);unlink(out);unlink(err);result.error=duplicate("server is shutting down");return result;}
    sigset_t blocked,previous;sigemptyset(&blocked);sigaddset(&blocked,SIGTERM);sigaddset(&blocked,SIGINT);sigprocmask(SIG_BLOCK,&blocked,&previous);
    int64_t execution_deadline=serving_invocation?serving_deadline:apx_http_now()+30000;pid_t parent=getpid();fflush(NULL);pid_t pid=fork();if(pid<0)die("fork failed");
    if(!pid){if(prctl(PR_SET_PDEATHSIG,SIGKILL)||getppid()!=parent)_exit(125);if(!serving_invocation)setpgid(0,0);signal(SIGALRM,SIG_DFL);signal(SIGTERM,SIG_DFL);signal(SIGINT,SIG_DFL);sigprocmask(SIG_SETMASK,&previous,NULL);dup2(fi,STDIN_FILENO);dup2(fo,STDOUT_FILENO);dup2(fe,STDERR_FILENO);close(fi);close(fo);close(fe);
        /* Duplicate both sources before remapping: either may occupy fd 3/4.
         * The image remains CLOEXEC, independently of the worker broker fd. */
        int retained=image_fd>=0?fcntl(image_fd,F_DUPFD_CLOEXEC,5):-1;
        int channel=use_host?fcntl(broker[1],F_DUPFD_CLOEXEC,5):-1;
        if(image_fd>=0&&(retained<0||dup3(retained,4,O_CLOEXEC)<0))_exit(125);
        if(use_host&&(channel<0||dup2(channel,3)<0))_exit(125);
        if(!use_host&&image_fd>=0)close(3);
        if(syscall(SYS_close_range,image_fd>=0?5u:use_host?4u:3u,~0u,0))_exit(125);
        limit_child(native);
        char *const env[]={"PATH=/usr/bin:/bin","LC_ALL=C",use_host?"APX_HOST_FD=3":NULL,NULL};
        if(image_fd>=0)syscall(SYS_execveat,4,"",argv,env,AT_EMPTY_PATH);else execve(argv[0],argv,env);_exit(127);
    }
    close(fi);close(fo);close(fe);if(!serving_invocation)setpgid(pid,pid);pid_t group=serving_invocation?getpgrp():pid;active_process=group;timed_out=0;alarm(30);if(shutdown_requested)kill(-group,SIGKILL);sigprocmask(SIG_SETMASK,&previous,NULL);int status=0;
    if(use_host){close(broker[1]);int pidfd=(int)syscall(SYS_pidfd_open,pid,0);if(pidfd<0)kill(-group,SIGKILL);else{
        apx_host_execution(host_policy,execution_deadline,group,pidfd,&timed_out,&shutdown_requested);
        struct pollfd watched[2]={{pidfd,POLLIN,0},{broker[0],POLLIN,0}};
        for(;;){if(timed_out||shutdown_requested||apx_http_now()>=execution_deadline){kill(-group,SIGKILL);break;}int ready=poll(watched,2,100);if(ready<0&&errno==EINTR)continue;if(ready<0||watched[0].revents)break;if(watched[1].revents&POLLIN){int served=apx_host_serve(host_policy,broker[0]);if(served<0){/* A serving broker shares the job group; do not kill itself on worker EOF/error. */if(serving_invocation)kill(pid,SIGKILL);else kill(-group,SIGKILL);break;}if(served>0){watched[1].fd=-1;}}else if(watched[1].revents&(POLLHUP|POLLERR)){watched[1].fd=-1;}}
        close(pidfd);
    }close(broker[0]);}
    while(waitpid(pid,&status,0)<0){if(errno!=EINTR){kill(-group,SIGKILL);waitpid(pid,&status,0);break;}}
    alarm(0);active_process=0;
    result.exit_code=timed_out?124:WIFEXITED(status)?WEXITSTATUS(status):128+WTERMSIG(status);
    result.out=read_path(out,PROCESS_LIMIT);result.error=read_path(err,1048576);
    if(!result.out)result.out=duplicate("");
    if(!result.error)result.error=duplicate("bounded process output exceeded");
    unlink(in);unlink(out);unlink(err);return result;
}
static struct Process execute(char *const argv[],const char *input,const char *directory,int native){return execute_image(argv,input,directory,native,-1);}
static void process_free(struct Process *p){free(p->out);free(p->error);}
struct Artifact {char *il,*canonical,*diagnostic,*diagnostics;ApxValue *decoded_diagnostics;char hash[80],directory[PATH_LIMIT],program[PATH_LIMIT];};
static void diagnostic_transport_failure(struct Artifact *a){
    static const char resource[]={"{\"schema\":\"apodexis.diagnostic.v1\",\"severity\":\"error\",\"code\":\"P006\",\"phase\":\"compile\",\"kind\":\"compiler_resource_limit\",\"message\":\"compiler diagnostics exceed the bounded transport budget; checking is incomplete\"}"};
    free(a->diagnostics);free(a->diagnostic);a->diagnostic=duplicate(resource);
    struct Buffer b={0};text(&b,"[");text(&b,resource);text(&b,"]");a->diagnostics=finish(&b);
    a->decoded_diagnostics=NULL;
}
static int prepare_diagnostics(struct Artifact *a,int errors){
    int incomplete=0;
    if(!a->diagnostics||strlen(a->diagnostics)>DIAGNOSTIC_TRANSPORT_LIMIT){diagnostic_transport_failure(a);incomplete=1;}
    if(!a->decoded_diagnostics)a->decoded_diagnostics=apx_json_parse(a->diagnostics,strlen(a->diagnostics));
    int valid=a->decoded_diagnostics&&apx_json_kind(a->decoded_diagnostics)==5;
    for(size_t i=0;valid&&i<apx_json_length(a->decoded_diagnostics);i++){
        ApxValue *entry=apx_json_item(a->decoded_diagnostics,i);
        valid=apx_json_kind(entry)==6&&apx_json_string(apx_json_get(entry,"code"))&&apx_json_string(apx_json_get(entry,"kind"))&&apx_json_string(apx_json_get(entry,"message"));
    }
    if(!valid||(errors&&!apx_json_length(a->decoded_diagnostics))){
        diagnostic_transport_failure(a);incomplete=1;
        a->decoded_diagnostics=apx_json_parse(a->diagnostics,strlen(a->diagnostics));
        if(!a->decoded_diagnostics||apx_json_kind(a->decoded_diagnostics)!=5||!apx_json_length(a->decoded_diagnostics))die("cannot deliver explicit incomplete compiler diagnostic");
    }
    return incomplete;
}
static int compile_front(const char *source,struct Artifact *a,int need_il){
    int code=apx_check_source(source,&a->canonical,&a->diagnostics);
    if(prepare_diagnostics(a,code!=0))code=1;
    if(a->canonical)hash_text("apx1:",a->canonical,a->hash);
    if(code){
        free(a->diagnostic);a->diagnostic=apx_json_encode(apx_json_item(a->decoded_diagnostics,0));
        if(!a->diagnostic)die("invalid compiler diagnostic collection");
    }else if(need_il){
        char *canonical=NULL;
        code=(need_il==2?apx_compile_tests:apx_compile_source)(source,&a->il,&canonical,&a->diagnostic);
        free(canonical);
        if(code){struct Buffer b={0};text(&b,"[");text(&b,a->diagnostic?a->diagnostic:"{}");text(&b,"]");free(a->diagnostics);a->diagnostics=finish(&b);a->decoded_diagnostics=NULL;prepare_diagnostics(a,1);}
    }
    return code;
}
static void artifact_free(struct Artifact *a){
    if(*a->directory){const char *files[]={"program.ssa","program.s","program.o","program","stdin","stdout","stderr"};char name[PATH_LIMIT];for(size_t i=0;i<sizeof files/sizeof files[0];i++){path(name,a->directory,files[i]);unlink(name);}rmdir(a->directory);}
    free(a->il);free(a->canonical);free(a->diagnostic);free(a->diagnostics);memset(a,0,sizeof *a);
}
static char *build_native(struct Artifact *a){
    char build[PATH_LIMIT];path(build,root,".build");mkdir(build,0700);path(a->directory,build,"native-XXXXXX");if(!mkdtemp(a->directory))return generic_error("toolchain_unavailable","cannot create native build directory");
    char il[PATH_LIMIT],assembly[PATH_LIMIT],object[PATH_LIMIT],qbe[PATH_LIMIT],runtime[PATH_LIMIT],start[PATH_LIMIT];path(il,a->directory,"program.ssa");path(assembly,a->directory,"program.s");path(object,a->directory,"program.o");path(a->program,a->directory,"program");path(qbe,root,".build/tools/qbe");path(runtime,root,".build/apx_runtime.o");path(start,root,".build/apx_start.o");
    if(access(qbe,X_OK)||access(runtime,R_OK)||access(start,R_OK)||access("/usr/bin/as",X_OK)||access("/usr/bin/ld",X_OK))return generic_error("toolchain_unavailable","run make first; QBE, support/start objects and the platform assembler/linker are required");
    if(write_new(il,a->il,strlen(a->il),0600))return generic_error("build_failed","cannot save generated QBE intermediate language");
    char *qargs[]={qbe,"-t","amd64_sysv","-o",assembly,il,NULL};struct Process p=execute(qargs,"",a->directory,0);
    if(p.exit_code){process_free(&p);return generic_error("qbe_compilation_failed","QBE did not accept generated intermediate language");}process_free(&p);
    char *asmargs[]={"/usr/bin/as",assembly,"-o",object,NULL};p=execute(asmargs,"",a->directory,0);
    if(p.exit_code){process_free(&p);return generic_error("native_assembly_failed","platform assembler failed to assemble QBE-generated code");}process_free(&p);
    char *ldargs[]={"/usr/bin/ld","-dynamic-linker","/lib64/ld-linux-x86-64.so.2","-z","noexecstack","-z","relro","-z","now","-o",a->program,start,object,runtime,"-lc",NULL};p=execute(ldargs,"",a->directory,0);
    if(p.exit_code){process_free(&p);return generic_error("native_link_failed","platform linker failed to link the QBE program and support object");}process_free(&p);return NULL;
}
struct Limits {uint64_t fuel,input,output;int has_input,has_output;};
static struct Limits defaults(void){struct Limits limits={1000000,0,0,0,0};return limits;}
static char *limits_json(struct Limits limits){struct Buffer b={0};text(&b,"{\"fuel\":");number(&b,limits.fuel);text(&b,",\"max_input_bytes\":");if(limits.has_input)number(&b,limits.input);else text(&b,"null");text(&b,",\"max_output_bytes\":");if(limits.has_output)number(&b,limits.output);else text(&b,"null");text(&b,"}");return finish(&b);}
static char *run_native_image(struct Artifact *a,const char *input,struct Limits limits,int tests,int image_fd){
    char fuel[32],maxin[32],maxout[32];snprintf(fuel,sizeof fuel,"%"PRIu64,limits.fuel);snprintf(maxin,sizeof maxin,"%"PRIu64,limits.input);snprintf(maxout,sizeof maxout,"%"PRIu64,limits.output);
    char *argv[10];int n=0;argv[n++]=image_fd>=0?"apodexis-artifact":a->program;argv[n++]="--fuel";argv[n++]=fuel;if(limits.has_input){argv[n++]="--max-input-bytes";argv[n++]=maxin;}if(limits.has_output){argv[n++]="--max-output-bytes";argv[n++]=maxout;}if(tests)argv[n++]="--test";argv[n]=NULL;
    host_enabled=!tests;struct Process p=execute_image(argv,input,a->directory,1,image_fd);host_enabled=0;
    if(p.exit_code>2){process_free(&p);return generic_error("native_worker_failed","native worker exceeded its resource/deadline bounds or failed");}
    ApxValue *response=apx_json_parse(p.out,strlen(p.out));const char *status=apx_json_string(apx_json_get(response,"status"));
    if(!status||((!strcmp(status,"ok"))!=(p.exit_code==0))){process_free(&p);return generic_error("invalid_native_response","native status does not agree with process exit");}
    size_t length=strlen(p.out);while(length&&strchr(" \t\r\n",p.out[length-1]))p.out[--length]=0;
    char *result=p.out;free(p.error);return result;
}
static char *native_result(struct Artifact *a,const char *input,struct Limits limits,int tests){char *error=build_native(a);return error?error:run_native_image(a,input,limits,tests,-1);}
static void envelope(struct Buffer *b,const char *schema,int ok){text(b,"{\"schema\":");quote(b,schema);text(b,",\"status\":");quote(b,ok?"ok":"error");text(b,",\"language\":\"apodexis\",\"version\":\""VERSION"\"");}
static void source_identity(struct Buffer *b,struct Artifact *a){if(*a->hash){text(b,",\"source_hash\":");quote(b,a->hash);}}
static char *clean_diagnostic(const char *json){ApxValue *diag=apx_json_parse(json,strlen(json));const char *keys[]={"schema","severity","code","phase","kind","message","span"};struct Buffer b={0};text(&b,"{");int first=1;for(size_t i=0;i<sizeof keys/sizeof keys[0];i++){ApxValue *value=apx_json_get(diag,keys[i]);if(!value)continue;char *encoded=apx_json_encode(value);if(!encoded)die("invalid compiler diagnostic");if(!first)text(&b,",");first=0;quote(&b,keys[i]);text(&b,":");text(&b,encoded);free(encoded);}text(&b,"}");return finish(&b);}
static void type_contract(struct Buffer *b,const char **cursor,unsigned depth){if(b->exceeded)return;if(depth>64)die("type metadata nesting exceeded");while(**cursor==' ')(*cursor)++;if(**cursor=='['){(*cursor)++;text(b,"{\"kind\":\"array\",\"items\":");type_contract(b,cursor,depth+1);if(b->exceeded)return;if(**cursor!=']')die("invalid checked array type");(*cursor)++;text(b,"}");return;}
    char name[65537];size_t n=0;while(**cursor&&**cursor!=' '&&**cursor!=']'){if(n>=sizeof name-1)die("invalid checked type name");name[n++]=*(*cursor)++;}name[n]=0;text(b,"{\"kind\":");int option=!strcmp(name,"option"),result=!strcmp(name,"result"),builder=!strcmp(name,"builder"),map=!strcmp(name,"map");int primitive=!strcmp(name,"i64")||!strcmp(name,"f64")||!strcmp(name,"bool")||!strcmp(name,"str");quote(b,primitive||option||result||builder||map?name:"named");if(option||result||builder||map){text(b,option?",\"item\":" : result?",\"ok\":" : map?",\"values\":" : ",\"items\":");type_contract(b,cursor,depth+1);if(result){text(b,",\"error\":");type_contract(b,cursor,depth+1);}}else if(!primitive){text(b,",\"name\":");quote(b,name);}text(b,"}");}
static void append_diagnostics(struct Buffer *b,struct Artifact *a){
    prepare_diagnostics(a,0);ApxValue *all=a->decoded_diagnostics;
    text(b,"[");
    for(size_t i=0;i<apx_json_length(all)&&!b->exceeded;i++){
        char *raw=apx_json_encode(apx_json_item(all,i));if(!raw)die("cannot encode compiler diagnostic");
        char *clean=clean_diagnostic(raw);free(raw);if(i)text(b,",");text(b,clean);free(clean);
    }
    text(b,"]");
}
static char *basic_check_response(struct Artifact *a,int ok){
    if(prepare_diagnostics(a,!ok))ok=0;
    struct Buffer b={0};b.maximum=DIAGNOSTIC_TRANSPORT_LIMIT;envelope(&b,"apodexis.check_response.v1",ok);source_identity(&b,a);
    text(&b,",\"diagnostics\":");append_diagnostics(&b,a);text(&b,"}");
    if(b.exceeded){free(b.text);diagnostic_transport_failure(a);return basic_check_response(a,0);}
    return finish(&b);
}
static char *format_repair(const char *source,struct Artifact *a){if(!a->canonical||!strcmp(source,a->canonical))return NULL;char source_hash[80],repair_hash[80];hash_text("apxs1:",source,source_hash);struct Buffer identity={0};text(&identity,source_hash);text(&identity,a->canonical);hash_text("apxr1:",finish(&identity),repair_hash);free(identity.text);
    uint64_t line=1,column=1;for(const unsigned char *p=(const unsigned char*)source;*p;p++){if(*p=='\n'){line++;column=1;}else if((*p&0xc0)!=0x80)column++;}
    struct Buffer b={0};text(&b,"{\"schema\":\"apodexis.repair.v1\",\"id\":");quote(&b,repair_hash);text(&b,",\"kind\":\"replace\",\"applicability\":\"safe\",\"range\":{\"start_byte\":0,\"end_byte\":");number(&b,strlen(source));text(&b,",\"start_line\":1,\"start_column\":1,\"end_line\":");number(&b,line);text(&b,",\"end_column\":");number(&b,column);text(&b,"},\"replacement\":");quote(&b,a->canonical);text(&b,"}");return finish(&b);
}
static char *check_response(const char *source,struct Artifact *a,int ok){
    if(prepare_diagnostics(a,!ok))ok=0;
    struct Buffer b={0};b.maximum=DIAGNOSTIC_TRANSPORT_LIMIT;envelope(&b,"apodexis.repair_response.v1",ok);source_identity(&b,a);
    char hash[80];hash_text("apxs1:",source,hash);text(&b,",\"source_text_hash\":");quote(&b,hash);
    text(&b,",\"diagnostics\":");append_diagnostics(&b,a);text(&b,",\"typed_holes\":[");
    prepare_diagnostics(a,0);ApxValue *all=a->decoded_diagnostics;
    int first=1;
    for(size_t index=0;index<apx_json_length(all)&&!b.exceeded;index++){
        ApxValue *diag=apx_json_item(all,index);const char *kind=apx_json_string(apx_json_get(diag,"kind"));
        if(!kind||strcmp(kind,"typed_hole"))continue;
        if(!first)text(&b,",");
        first=0;
        char *span=apx_json_encode(apx_json_get(diag,"span"));struct Buffer identity={0};
        text(&identity,hash);text(&identity,span?span:"null");char id[80];hash_text("apxh1:",finish(&identity),id);free(identity.text);
        text(&b,"{\"id\":");quote(&b,id);text(&b,",\"span\":");text(&b,span?span:"null");free(span);
        const char *expected=apx_json_string(apx_json_get(diag,"expected_type"));
        if(expected){text(&b,",\"expected_type\":");type_contract(&b,&expected,0);}
        text(&b,",\"scope\":[");ApxValue *scope=apx_json_get(diag,"in_scope");
        for(size_t i=0;i<apx_json_length(scope)&&!b.exceeded;i++){
            ApxValue *binding=apx_json_item(scope,i);
            const char *name=apx_json_string(apx_json_get(binding,"name")),*type=apx_json_string(apx_json_get(binding,"type"));
            if(i)text(&b,",");
            text(&b,"{\"name\":");quote(&b,name);text(&b,",\"type\":");type_contract(&b,&type,0);text(&b,"}");
        }
        text(&b,"]}");
    }
    text(&b,"],\"repairs\":[");char *repair=format_repair(source,a);if(repair){text(&b,repair);free(repair);}text(&b,"]}");
    if(b.exceeded){free(b.text);diagnostic_transport_failure(a);return check_response(source,a,0);}
    return finish(&b);
}
static int json_equal(ApxValue *,ApxValue *);
static char *apply_repairs(const char *source,ApxValue *request){struct Artifact a={0};int failed=compile_front(source,&a,0);char *current=format_repair(source,&a),hash[80];hash_text("apxs1:",source,hash);ApxValue *selected=apx_json_get(request,"repairs");const char *expected=apx_json_string(apx_json_get(request,"expected_source_text_hash"));const char *error=NULL;size_t count=apx_json_length(selected);
    if(!expected||strcmp(expected,hash))error="source text hash precondition failed";
    else if(count>1||(count&&!current))error="selection is not a current compiler-issued repair";
    else if(count){ApxValue *issued=apx_json_parse(current,strlen(current));if(!json_equal(issued,apx_json_item(selected,0)))error="repair identity or payload precondition failed";}
    struct Buffer b={0};envelope(&b,"apodexis.apply_repairs_response.v1",!error);text(&b,",\"input_source_text_hash\":");quote(&b,hash);text(&b,",\"applied_repair_ids\":[");if(!error&&count){ApxValue *issued=apx_json_parse(current,strlen(current));quote(&b,apx_json_string(apx_json_get(issued,"id")));}text(&b,"],\"diagnostics\":[");
    if(error){text(&b,"{\"schema\":\"apodexis.diagnostic.v1\",\"severity\":\"error\",\"code\":\"A202\",\"phase\":\"repair\",\"kind\":\"repair_precondition_failed\",\"message\":");quote(&b,error);text(&b,"}");}text(&b,"]");
    if(!error){const char *output=count?a.canonical:source;hash_text("apxs1:",output,hash);text(&b,",\"output_source_text_hash\":");quote(&b,hash);text(&b,",\"source\":");quote(&b,output);text(&b,",\"check\":");char *checked=basic_check_response(&a,!failed);text(&b,checked);free(checked);}text(&b,"}");free(current);artifact_free(&a);return finish(&b);
}
static char *format_response(const char *source){char *diagnostic=NULL,*formatted=apx_format_source(source,&diagnostic);struct Buffer b={0};char hash[80];envelope(&b,formatted?"apodexis.format_response.v1":"apodexis.check_response.v1",formatted!=NULL);
    if(formatted){hash_text("apxs1:",source,hash);text(&b,",\"source_text_hash\":");quote(&b,hash);hash_text("apx1:",formatted,hash);text(&b,",\"source_hash\":");quote(&b,hash);text(&b,",\"source\":");quote(&b,formatted);}
    else{text(&b,",\"diagnostics\":[");text(&b,diagnostic?diagnostic:"{}");text(&b,"]");}text(&b,"}");free(formatted);free(diagnostic);return finish(&b);
}
static char *failed_execution(struct Artifact *a,int tests){struct Buffer b={0};
    if(tests){envelope(&b,"apodexis.test_result.v1",0);if(*a->hash)source_identity(&b,a);text(&b,",\"tests\":[{\"name\":\"check\",\"status\":\"error\",\"fuel_used\":0,\"diagnostic\":");char *diag=clean_diagnostic(a->diagnostic?a->diagnostic:"{}");text(&b,diag);free(diag);text(&b,"}]}");}
    else{text(&b,"{\"status\":\"error\",\"language\":\"apodexis\",\"version\":\""VERSION"\",\"fuel_used\":0");if(*a->hash)source_identity(&b,a);text(&b,",\"diagnostic\":");char *diag=clean_diagnostic(a->diagnostic?a->diagnostic:"{}");text(&b,diag);free(diag);text(&b,"}");}return finish(&b);
}
static char *operation(const char *name,const char *source,const char *input,struct Limits limits){
    if(!strcmp(name,"fmt"))return format_response(source);
    struct Artifact a={0};int tests=!strcmp(name,"test")||!strcmp(name,"verify");int front=compile_front(source,&a,tests?2:!strcmp(name,"run"));char *response=NULL;
    if(!strcmp(name,"check"))response=basic_check_response(&a,!front);
    else if(!strcmp(name,"repairs"))response=check_response(source,&a,!front);
    else if(!strcmp(name,"explain")||!strcmp(name,"contract")||!strcmp(name,"pseudocode")||!strcmp(name,"context")){
        if(front)response=basic_check_response(&a,0);
        else{
            char *explanation=NULL,*contract=NULL,*pseudocode=NULL,*diag=NULL;
            int both=!strcmp(name,"context");
            int failed=apx_describe_source(source,(!strcmp(name,"explain")||both)?&explanation:NULL,(!strcmp(name,"contract")||both)?&contract:NULL,!strcmp(name,"pseudocode")?&pseudocode:NULL,&diag);
            if(failed){free(a.diagnostic);a.diagnostic=diag;struct Buffer errors={0};text(&errors,"[");text(&errors,diag?diag:"{}");text(&errors,"]");free(a.diagnostics);a.diagnostics=finish(&errors);a.decoded_diagnostics=NULL;response=basic_check_response(&a,0);}
            else if(both){struct Buffer b={0};text(&b,"{\"schema\":\"apodexis.developer_context_response.v1\",\"status\":\"ok\",\"explain\":");text(&b,explanation);text(&b,",\"contract\":");text(&b,contract);text(&b,"}");response=finish(&b);free(explanation);free(contract);}
            else response=explanation?explanation:contract?contract:pseudocode;
        }
    }
    else if(!strcmp(name,"run")||!strcmp(name,"test"))response=front?failed_execution(&a,!strcmp(name,"test")):native_result(&a,input,limits,!strcmp(name,"test"));
    else if(!strcmp(name,"verify")){
        char *formatted=format_response(source),*checked=check_response(source,&a,!front),*tested=front?failed_execution(&a,1):native_result(&a,"null",limits,1);
        struct Buffer b={0};envelope(&b,"apodexis.verify_response.v1",successful(formatted)&&successful(checked)&&successful(tested));char hash[80];hash_text("apxs1:",source,hash);text(&b,",\"source_text_hash\":");quote(&b,hash);
        text(&b,",\"format\":{\"outcome\":");quote(&b,successful(formatted)?"ok":"error");text(&b,",\"response\":");text(&b,formatted);text(&b,"},\"check\":");text(&b,checked);text(&b,",\"test\":");text(&b,tested);text(&b,"}");response=finish(&b);free(formatted);free(checked);free(tested);
    }else response=generic_error("unsupported_operation","operation not available in the native backend");
    artifact_free(&a);return response;
}

/* MCP advertises only exercised native operations. Historical schemas and
 * docs remain shared data; no missing feature is marked ready. */
static const char *tool_names[]={"apodexis_capabilities","apodexis_format","apodexis_check","apodexis_explain","apodexis_pseudocode","apodexis_apply_repairs","apodexis_verify","apodexis_test","apodexis_run","apodexis_bundle","apodexis_compile","apodexis_invoke","apodexis_release"};
static const char *resource_uris[]={"apodexis://spec/language","apodexis://spec/types","apodexis://spec/stdlib","apodexis://examples","apodexis://schemas","apodexis://spec/opcodes"};
static int supported_tool(const char *name){for(size_t i=0;i<sizeof tool_names/sizeof tool_names[0];i++)if(!strcmp(name,tool_names[i]))return 1;return 0;}
static ApxValue *catalog(void){char file[PATH_LIMIT];path(file,root,"resources/schemas/mcp-agent-catalog.v1.json");char *data=read_path(file,FRAME_LIMIT);if(!data)return NULL;ApxValue *out=apx_json_parse(data,strlen(data));free(data);return out;}
static char *tool_list(void){ApxValue *c=catalog(),*items=apx_json_get(c,"tools");struct Buffer b={0};text(&b,"{\"tools\":[");int first=1;
    for(size_t i=0;i<apx_json_length(items);i++){ApxValue *item=apx_json_item(items,i);const char *name=apx_json_string(apx_json_get(item,"name"));if(!name||!supported_tool(name))continue;if(!first)text(&b,",");first=0;text(&b,"{\"name\":");quote(&b,name);text(&b,",\"description\":");quote(&b,apx_json_string(apx_json_get(item,"description")));
        char *input=apx_json_encode(apx_json_get(item,"input_schema")),*output=apx_json_encode(apx_json_get(item,"output_schema"));if(!input||!output)die("cannot encode tool schemas");text(&b,",\"inputSchema\":");text(&b,input);text(&b,",\"outputSchema\":");text(&b,output);text(&b,"}");free(input);free(output);
    }text(&b,"]}");return finish(&b);
}
static char *resource_content(const char *uri){
    const char *names[]={"language.md","types.md","stdlib.md","examples.json"};for(size_t i=0;i<4;i++)if(!strcmp(uri,resource_uris[i])){char file[PATH_LIMIT];path(file,root,"resources");char target[PATH_LIMIT];path(target,file,names[i]);return read_path(target,FRAME_LIMIT);}
    ApxValue *c=catalog();if(!strcmp(uri,resource_uris[4]))return apx_json_encode(apx_json_get(c,"protocol_schemas"));
    if(!strcmp(uri,resource_uris[5]))return apx_json_encode(apx_json_get(c,"builtins"));
    return NULL;
}
static void compiled_capabilities(struct Buffer *);
static char *capabilities(void){struct Buffer b={0};envelope(&b,"apodexis.native_capabilities.v1",1);text(&b,",\"backend\":\"qbe\",\"target\":\"amd64_sysv\",\"compiler\":\"C\",\"runtime\":\"C\",\"requires_rust\":false,\"requires_python\":false,\"third_party_compiler_backends\":[\"QBE 1.3\"],\"operations\":[\"format\",\"check\",\"explain\",\"contract\",\"pseudocode\",\"apply_repairs\",\"run\",\"test\",\"verify\",\"emit-qbe\",\"build\",\"bundle\",\"pqc-keygen\"],\"platform_tools\":[\"assembler\",\"linker\"],\"sandbox\":\"Linux seccomp\",\"mcp_tools\":[");for(size_t i=0;i<sizeof tool_names/sizeof tool_names[0];i++){if(i)text(&b,",");quote(&b,tool_names[i]);}text(&b,"]");compiled_capabilities(&b);char plain[PATH_LIMIT],tls[PATH_LIMIT];path(plain,root,".build/tools/apx-http");path(tls,root,".build/tools/apx-http-tls");text(&b,",\"http_client\":{\"activation\":\"cli-run-capabilities\",\"http_helper_available\":");text(&b,access(plain,X_OK)?"false":"true");text(&b,",\"tls_helper_available\":");text(&b,access(tls,X_OK)?"false":"true");text(&b,"}");char crypto[PATH_LIMIT];path(crypto,root,".build/tools/apx-pqc");text(&b,",\"post_quantum\":{\"activation\":\"cli-run-capabilities\",\"policy_schema\":\"apodexis.host_policy.v3\",\"tls_group\":\"X25519MLKEM768\",\"tls_cipher\":\"TLS_AES_256_GCM_SHA384\",\"application_profile\":\"cms-authenveloped-mlkem768-aes256gcm-v1\",\"application_helper_available\":");text(&b,access(crypto,X_OK)?"false":"true");text(&b,",\"keygen\":\"operator-cli\"}}");return finish(&b);}
static int strict_object(ApxValue *v,const char *const *keys,size_t count){if(apx_json_kind(v)!=6||apx_json_length(v)!=count)return 0;for(size_t i=0;i<count;i++)if(!apx_json_get(v,keys[i]))return 0;return 1;}
static int limit_value(ApxValue *v,uint64_t *out){return apx_json_unsigned(v,out);}
static int request_limits(ApxValue *v,struct Limits *out){const char *keys[]={"fuel","max_input_bytes","max_output_bytes"};if(!strict_object(v,keys,3)||!limit_value(apx_json_get(v,"fuel"),&out->fuel))return 0;
    ApxValue *in=apx_json_get(v,"max_input_bytes"),*value=apx_json_get(v,"max_output_bytes");out->has_input=apx_json_kind(in)!=0;out->has_output=apx_json_kind(value)!=0;
    return (!out->has_input||limit_value(in,&out->input))&&(!out->has_output||limit_value(value,&out->output));
}
static char *execution_record_identity(const char *source_hash,const char *contract_hash,const char *input,struct Limits limits,const char *result){
    ApxValue *r=apx_json_parse(result,strlen(result));const char *status=apx_json_string(apx_json_get(r,"status"));struct Buffer b={0};envelope(&b,"apodexis.execution_record.v1",status&&!strcmp(status,"ok"));text(&b,",\"protocol_version\":\"1\",\"sdk_version\":\"0.2.0\"");
    if(source_hash&&*source_hash){text(&b,",\"source_hash\":");quote(&b,source_hash);}if(contract_hash){text(&b,",\"contract_hash\":");quote(&b,contract_hash);}
    char hash[80];hash_text("apxi1:",input,hash);text(&b,",\"input_hash\":");quote(&b,hash);char *bounds=limits_json(limits);text(&b,",\"limits\":");text(&b,bounds);free(bounds);text(&b,",\"fuel_used\":");number(&b,(uint64_t)apx_json_integer(apx_json_get(r,"fuel_used")));
    ApxValue *output=apx_json_get(r,"output"),*diag=apx_json_get(r,"diagnostic");if(output){char *json=apx_json_encode(output);if(!json)die("cannot encode native output");hash_text("apxo1:",json,hash);text(&b,",\"output\":");text(&b,json);text(&b,",\"output_hash\":");quote(&b,hash);free(json);}if(diag){char *json=apx_json_encode(diag);text(&b,",\"diagnostic\":");text(&b,json?json:"{}");free(json);}text(&b,"}");return finish(&b);
}
static char *execution_record(const char *source,const char *input,struct Limits limits,const char *result){
    struct Artifact a={0};compile_front(source,&a,0);char *contract=NULL,*diagnostic=NULL;const char *identity=NULL;
    if(!apx_describe_source(source,NULL,&contract,NULL,&diagnostic)){ApxValue *c=apx_json_parse(contract,strlen(contract));identity=apx_json_string(apx_json_get(c,"contract_hash"));}
    char *out=execution_record_identity(a.hash,identity,input,limits,result);free(contract);free(diagnostic);artifact_free(&a);return out;
}
/* Session entries own only fixed identity metadata and an immutable ELF fd.
 * No request arena value, source text, AST, or worker state survives a frame. */
#define ARTIFACT_SLOTS 64u
struct StoredArtifact {int fd;uint64_t charge;char id[39],source_hash[80],contract_hash[80];};
static struct StoredArtifact *artifacts[ARTIFACT_SLOTS],*pending_artifact;
static uint64_t artifact_max_count=8,artifact_max_bytes=64u*1024u*1024u,artifact_count,artifact_bytes;
static void stored_free(struct StoredArtifact *entry){if(entry){close(entry->fd);free(entry);}}
static void discard_pending(void){stored_free(pending_artifact);pending_artifact=NULL;}
static void clear_store(void){discard_pending();for(size_t i=0;i<ARTIFACT_SLOTS;i++){stored_free(artifacts[i]);artifacts[i]=NULL;}artifact_count=artifact_bytes=0;}
static void commit_pending(void){if(!pending_artifact)return;for(size_t i=0;i<ARTIFACT_SLOTS;i++)if(!artifacts[i]){artifacts[i]=pending_artifact;artifact_count++;artifact_bytes+=pending_artifact->charge;pending_artifact=NULL;return;}die("artifact admission invariant failed");}
static void compiled_capabilities(struct Buffer *b){text(b,",\"compiled_artifacts\":{\"scope\":\"session\",\"storage\":\"sealed_memfd\",\"max_artifacts\":");number(b,artifact_max_count);text(b,",\"max_bytes\":");number(b,artifact_max_bytes);text(b,"}");}
static int operator_limit(const char *name,uint64_t maximum,uint64_t *value){const char *s=getenv(name);if(!s)return 1;uint64_t n=0;if(!*s)return 0;for(;*s;s++){if(*s<'0'||*s>'9'||n>(maximum-(unsigned)(*s-'0'))/10)return 0;n=n*10+(unsigned)(*s-'0');}if(!n||n>maximum)return 0;*value=n;return 1;}
static int store_settings(void){if(!operator_limit("APX_MCP_MAX_ARTIFACTS",64,&artifact_max_count)||!operator_limit("APX_MCP_MAX_ARTIFACT_BYTES",256u*1024u*1024u,&artifact_max_bytes)){fputs("apodexis: invalid MCP artifact limits (decimal digits; count 1..64, bytes 1..268435456)\n",stderr);return 0;}return 1;}
static struct StoredArtifact *find_stored(const char *id){for(size_t i=0;i<ARTIFACT_SLOTS;i++)if(artifacts[i]&&!strcmp(artifacts[i]->id,id))return artifacts[i];return NULL;}
static int valid_artifact_id(ApxValue *v){const char *s=apx_json_string(v);if(!s||apx_json_length(v)!=38||strlen(s)!=38||memcmp(s,"apxa1:",6))return 0;for(size_t i=6;i<38;i++)if(!((s[i]>='0'&&s[i]<='9')||(s[i]>='a'&&s[i]<='f')))return 0;return 1;}
static char *artifact_diagnostic(const char *code,const char *kind,const char *message){struct Buffer b={0};text(&b,"{\"schema\":\"apodexis.diagnostic.v1\",\"severity\":\"error\",\"phase\":\"artifact\",\"code\":");quote(&b,code);text(&b,",\"kind\":");quote(&b,kind);text(&b,",\"message\":");quote(&b,message);text(&b,"}");return finish(&b);}
static char *compile_error(const char *diagnostics){struct Buffer b={0};envelope(&b,"apodexis.compile_response.v1",0);text(&b,",\"diagnostics\":");text(&b,diagnostics);text(&b,"}");return finish(&b);}
static char *compile_artifact_error(const char *code,const char *kind,const char *message){char *diag=artifact_diagnostic(code,kind,message);struct Buffer errors={0};text(&errors,"[");text(&errors,diag);text(&errors,"]");char *out=compile_error(finish(&errors));free(errors.text);free(diag);return out;}
static char *store_failure(void){return compile_artifact_error("A205","artifact_store_failed","could not publish sealed executable and owned identity metadata");}
/* Read/hash full binary lengths, including NUL bytes. The running compiler is
 * opened through /proc/self/exe even if its original pathname was replaced. */
static unsigned char *binary_bytes(const char *name,size_t *length){int fd=open(name,O_RDONLY|O_CLOEXEC);if(fd<0)return NULL;struct stat st;if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<=0||(uint64_t)st.st_size>64u*1024u*1024u){close(fd);return NULL;}size_t n=(size_t)st.st_size;unsigned char *data=malloc(n);if(!data){close(fd);return NULL;}size_t offset=0;while(offset<n){ssize_t got=read(fd,data+offset,n-offset);if(got<0&&errno==EINTR)continue;if(got<=0){free(data);close(fd);return NULL;}offset+=(size_t)got;}unsigned char extra;ssize_t tail;do{tail=read(fd,&extra,1);}while(tail<0&&errno==EINTR);close(fd);if(tail!=0){free(data);return NULL;}*length=n;return data;}
static int binary_hash(const char *name,char hex[65]){size_t n;unsigned char *data=binary_bytes(name,&n);if(!data)return 0;apx_sha256(data,n,hex);free(data);return 1;}
static int new_artifact_id(char id[39]){static const char hex[]="0123456789abcdef";for(unsigned attempt=0;attempt<16;attempt++){unsigned char random[16];size_t offset=0;while(offset<sizeof random){ssize_t n=syscall(SYS_getrandom,random+offset,sizeof random-offset,0);if(n<0&&errno==EINTR&&!shutdown_requested)continue;if(n<=0)return 0;offset+=(size_t)n;}memcpy(id,"apxa1:",6);for(size_t i=0;i<sizeof random;i++){id[6+i*2]=hex[random[i]>>4];id[7+i*2]=hex[random[i]&15];}id[38]=0;if(!find_stored(id))return 1;}return 0;}
static int sealed_image(const unsigned char *bytes,size_t length){int fd=(int)syscall(SYS_memfd_create,"apodexis-artifact",MFD_CLOEXEC|MFD_ALLOW_SEALING);if(fd<0)return -1;size_t offset=0;while(offset<length){ssize_t n=write(fd,bytes+offset,length-offset);if(n<0&&errno==EINTR&&!shutdown_requested)continue;if(n<=0){close(fd);return -1;}offset+=(size_t)n;}int seals=F_SEAL_WRITE|F_SEAL_GROW|F_SEAL_SHRINK|F_SEAL_SEAL;if(fcntl(fd,F_ADD_SEALS,seals)||fcntl(fd,F_GET_SEALS)!=seals){close(fd);return -1;}return fd;}
static char *compile_artifact(const char *source){
    struct Artifact a={0};char *result=NULL,*contract=NULL,*diag=NULL,*build_error=NULL;unsigned char *bytes=NULL;struct StoredArtifact *entry=NULL;
    if(compile_front(source,&a,1)){struct Buffer errors={0};errors.maximum=DIAGNOSTIC_TRANSPORT_LIMIT;append_diagnostics(&errors,&a);result=errors.exceeded?store_failure():compile_error(finish(&errors));free(errors.text);goto done;}
    if(apx_describe_source(source,NULL,&contract,NULL,&diag)){struct Buffer errors={0};text(&errors,"[");char *clean=clean_diagnostic(diag?diag:"{}");text(&errors,clean);text(&errors,"]");free(clean);result=compile_error(finish(&errors));free(errors.text);goto done;}
    build_error=build_native(&a);if(build_error){ApxValue *r=apx_json_parse(build_error,strlen(build_error));char *diagnostic=apx_json_encode(apx_json_get(r,"diagnostic"));struct Buffer errors={0};text(&errors,"[");text(&errors,diagnostic?diagnostic:"{}");text(&errors,"]");result=compile_error(finish(&errors));free(errors.text);free(diagnostic);goto done;}
    size_t length=0;bytes=binary_bytes(a.program,&length);if(!bytes)goto failed;
    uint64_t charge=(uint64_t)length+sizeof(struct StoredArtifact);
    if(artifact_count>=artifact_max_count||charge>artifact_max_bytes||artifact_bytes>artifact_max_bytes-charge){result=compile_artifact_error("A204","artifact_limit_exceeded","session artifact count or retained-byte limit exceeded");goto done;}
    entry=calloc(1,sizeof *entry);if(!entry)goto failed;entry->fd=-1;entry->charge=charge;
    ApxValue *c=apx_json_parse(contract,strlen(contract));const char *hash=apx_json_string(apx_json_get(c,"contract_hash"));if(!hash||strlen(hash)>=sizeof entry->contract_hash||!*a.hash)goto failed;
    memcpy(entry->source_hash,a.hash,strlen(a.hash)+1);memcpy(entry->contract_hash,hash,strlen(hash)+1);
    char compiler_hash[65],qbe_hash[65],runtime_hash[65],start_hash[65],program_hash[65],source_text_hash[80],file[PATH_LIMIT];
    if(!binary_hash("/proc/self/exe",compiler_hash))goto failed;
    path(file,root,".build/tools/qbe");if(!binary_hash(file,qbe_hash))goto failed;
    path(file,root,".build/apx_runtime.o");if(!binary_hash(file,runtime_hash))goto failed;
    path(file,root,".build/apx_start.o");if(!binary_hash(file,start_hash))goto failed;
    apx_sha256(bytes,length,program_hash);hash_text("apxs1:",source,source_text_hash);
    if(!new_artifact_id(entry->id)||(entry->fd=sealed_image(bytes,length))<0||shutdown_requested)goto failed;
    struct Buffer b={0};b.maximum=DIAGNOSTIC_TRANSPORT_LIMIT;envelope(&b,"apodexis.compile_response.v1",1);text(&b,",\"artifact_id\":");quote(&b,entry->id);text(&b,",\"source_hash\":");quote(&b,entry->source_hash);text(&b,",\"source_text_hash\":");quote(&b,source_text_hash);text(&b,",\"contract_hash\":");quote(&b,entry->contract_hash);text(&b,",\"program_sha256\":");quote(&b,program_hash);text(&b,",\"stored_bytes\":");number(&b,charge);text(&b,",\"toolchain\":{\"backend\":\"qbe\",\"target\":\"amd64_sysv\",\"compiler_sha256\":");quote(&b,compiler_hash);text(&b,",\"qbe_sha256\":");quote(&b,qbe_hash);text(&b,",\"runtime_sha256\":");quote(&b,runtime_hash);text(&b,",\"start_sha256\":");quote(&b,start_hash);text(&b,"},\"contract\":");text(&b,contract);text(&b,",\"diagnostics\":[]}");
    if(b.exceeded){free(b.text);goto failed;}result=finish(&b);pending_artifact=entry;entry=NULL;goto done;
failed: result=store_failure();
done: stored_free(entry);free(bytes);free(contract);free(diag);free(build_error);artifact_free(&a);return result;
}
static char *invoke_artifact(struct StoredArtifact *entry,const char *input,struct Limits limits){
    char *result=NULL;
    if(!entry){char *diag=artifact_diagnostic("A203","artifact_not_found","artifact is not active in this session");struct Buffer b={0};text(&b,"{\"status\":\"error\",\"fuel_used\":0,\"diagnostic\":");text(&b,diag);text(&b,"}");free(diag);result=finish(&b);}
    else{struct Artifact a={0};char build[PATH_LIMIT];path(build,root,".build");mkdir(build,0700);path(a.directory,build,"native-XXXXXX");if(!mkdtemp(a.directory)){a.directory[0]=0;result=generic_error("native_worker_failed","cannot create bounded native worker channels");}else result=run_native_image(&a,input,limits,0,entry->fd);artifact_free(&a);}
    char *record=execution_record_identity(entry?entry->source_hash:NULL,entry?entry->contract_hash:NULL,input,limits,result);free(result);return record;
}
static char *release_artifact(const char *id){struct StoredArtifact *entry=find_stored(id);struct Buffer b={0};envelope(&b,"apodexis.release_response.v1",entry!=NULL);text(&b,",\"artifact_id\":");quote(&b,id);text(&b,",\"released\":");text(&b,entry?"true":"false");text(&b,",\"diagnostics\":[");if(entry){for(size_t i=0;i<ARTIFACT_SLOTS;i++)if(artifacts[i]==entry){artifacts[i]=NULL;break;}artifact_count--;artifact_bytes-=entry->charge;stored_free(entry);}else{char *diag=artifact_diagnostic("A203","artifact_not_found","artifact is not active in this session");text(&b,diag);free(diag);}text(&b,"]}");return finish(&b);}
/* Minimal schema interpreter for the bundled protocol catalog (never remote
 * schemas). Enforce input/output contracts recursively before publication. */
static int matches_type(const char *type,ApxValue *value){int kind=apx_json_kind(value);uint64_t u;
    if(!strcmp(type,"object"))return kind==6;
    if(!strcmp(type,"array"))return kind==5;
    if(!strcmp(type,"string"))return kind==4;
    if(!strcmp(type,"null"))return kind==0;
    if(!strcmp(type,"boolean"))return kind==1;
    if(!strcmp(type,"integer"))return kind==2||apx_json_unsigned(value,&u);
    if(!strcmp(type,"number"))return kind==2||kind==3;
    return 0;
}
static int json_equal(ApxValue *left,ApxValue *right){char *a=apx_json_encode(left),*b=apx_json_encode(right);int same=a&&b&&!strcmp(a,b);free(a);free(b);return same;}
static int validate_schema(ApxValue *schema,ApxValue *value,ApxValue *origin,unsigned depth){
    if(depth>512||!schema)return 0;
    if(apx_json_kind(schema)==1)return apx_json_integer(schema)!=0;
    const char *ref=apx_json_string(apx_json_get(schema,"$ref"));
    if(ref){if(strncmp(ref,"#/",2))return 0;char *copy=duplicate(ref+2),*save=NULL,*part=strtok_r(copy,"/",&save);ApxValue *target=origin;while(part&&target){target=apx_json_get(target,part);part=strtok_r(NULL,"/",&save);}free(copy);return validate_schema(target,value,origin,depth+1);}
    const char *combinations[]={"allOf","anyOf","oneOf"};
    for(size_t k=0;k<3;k++){ApxValue *options=apx_json_get(schema,combinations[k]);if(options){size_t accepted=0,n=apx_json_length(options);for(size_t i=0;i<n;i++)accepted+=(size_t)validate_schema(apx_json_item(options,i),value,origin,depth+1);if((k==0&&accepted!=n)||(k==1&&!accepted)||(k==2&&accepted!=1))return 0;}}
    ApxValue *constant=apx_json_get(schema,"const"),*allowed=apx_json_get(schema,"enum");if(constant&&!json_equal(constant,value))return 0;
    if(allowed){int found=0;for(size_t i=0;i<apx_json_length(allowed);i++)if(json_equal(apx_json_item(allowed,i),value))found=1;if(!found)return 0;}
    ApxValue *type=apx_json_get(schema,"type");if(type){const char *name=apx_json_string(type);if(name){if(!matches_type(name,value))return 0;}else{int found=0;for(size_t i=0;i<apx_json_length(type);i++){const char *n=apx_json_string(apx_json_item(type,i));if(n&&matches_type(n,value))found=1;}if(!found)return 0;}}
    int kind=apx_json_kind(value);
    if(kind==6){ApxValue *required=apx_json_get(schema,"required"),*properties=apx_json_get(schema,"properties");for(size_t i=0;i<apx_json_length(required);i++){const char *key=apx_json_string(apx_json_item(required,i));if(!key||!apx_json_get(value,key))return 0;}
        size_t known=0;for(size_t i=0;i<apx_json_length(properties);i++){const char *key=apx_json_key(properties,i);ApxValue *item=key?apx_json_get(value,key):NULL;if(item){known++;if(!validate_schema(apx_json_get(properties,key),item,origin,depth+1))return 0;}}
        ApxValue *additional=apx_json_get(schema,"additionalProperties");if(additional&&apx_json_kind(additional)==1&&!apx_json_integer(additional)&&known!=apx_json_length(value))return 0;
    }
    if(kind==5){ApxValue *items=apx_json_get(schema,"items");for(size_t i=0;items&&i<apx_json_length(value);i++)if(!validate_schema(items,apx_json_item(value,i),origin,depth+1))return 0;ApxValue *minimum=apx_json_get(schema,"minItems"),*maximum=apx_json_get(schema,"maxItems");if((minimum&&apx_json_length(value)<(uint64_t)apx_json_integer(minimum))||(maximum&&apx_json_length(value)>(uint64_t)apx_json_integer(maximum)))return 0;}
    if(kind==4){ApxValue *minimum=apx_json_get(schema,"minLength"),*maximum=apx_json_get(schema,"maxLength");if((minimum&&apx_json_length(value)<(uint64_t)apx_json_integer(minimum))||(maximum&&apx_json_length(value)>(uint64_t)apx_json_integer(maximum)))return 0;}
    if(kind==2||kind==3){ApxValue *minimum=apx_json_get(schema,"minimum"),*maximum=apx_json_get(schema,"maximum");uint64_t n,bound;if(apx_json_unsigned(value,&n)){if(minimum&&apx_json_unsigned(minimum,&bound)&&n<bound)return 0;if(maximum&&apx_json_unsigned(maximum,&bound)&&n>bound)return 0;}else if(minimum&&apx_json_integer(minimum)>=0&&kind==2&&apx_json_integer(value)<0)return 0;}
    return 1;
}
static ApxValue *tool_descriptor(const char *name){ApxValue *c=catalog(),*all=apx_json_get(c,"tools");for(size_t i=0;i<apx_json_length(all);i++){ApxValue *t=apx_json_item(all,i);const char *n=apx_json_string(apx_json_get(t,"name"));if(n&&!strcmp(n,name))return t;}return NULL;}
static char *validated_result(ApxValue *descriptor,char *result){
    ApxValue *schema=apx_json_get(descriptor,"output_schema"),*value=apx_json_parse(result,strlen(result));
    if(!validate_schema(schema,value,schema,0)){free(result);return NULL;}
    return result;
}
static char *tool_call(const char *name,ApxValue *arguments){
    if(!supported_tool(name))return NULL;
    ApxValue *descriptor=tool_descriptor(name),*schema=apx_json_get(descriptor,"input_schema");
    if(!validate_schema(schema,arguments,schema,0))return NULL;
    if(!strcmp(name,"apodexis_capabilities")){if(apx_json_kind(arguments)!=6||apx_json_length(arguments))return NULL;return validated_result(descriptor,capabilities());}
    if(!strcmp(name,"apodexis_invoke")||!strcmp(name,"apodexis_release")){
        ApxValue *id=apx_json_get(arguments,"artifact_id");if(!valid_artifact_id(id))return NULL;const char *identity=apx_json_string(id);
        if(!strcmp(name,"apodexis_release"))return validated_result(descriptor,release_artifact(identity));
        struct Limits limits=defaults();if(!request_limits(apx_json_get(arguments,"limits"),&limits))return NULL;char *input=apx_json_encode(apx_json_get(arguments,"input"));if(!input)return NULL;
        char *result=invoke_artifact(find_stored(identity),input,limits);free(input);return validated_result(descriptor,result);
    }
    if(!strcmp(name,"apodexis_compile")){
        const char *source=apx_json_string(apx_json_get(arguments,"source"));if(!source||apx_json_length(apx_json_get(arguments,"source"))!=strlen(source))return NULL;
        char *result=validated_result(descriptor,compile_artifact(source));if(!result){discard_pending();result=validated_result(descriptor,store_failure());}return result;
    }
    if(!strcmp(name,"apodexis_bundle")){
        const char *entry=apx_json_string(apx_json_get(arguments,"source"));
        ApxValue *items=apx_json_get(arguments,"modules");size_t count=apx_json_length(items);
        ApxModuleSource modules[64];if(!entry||apx_json_length(apx_json_get(arguments,"source"))!=strlen(entry)||apx_json_kind(items)!=6||count>=64)return NULL;
        for(size_t i=0;i<count;i++){modules[i].name=apx_json_key(items,i);ApxValue *body=apx_json_get(items,modules[i].name);modules[i].source=apx_json_string(body);if(!modules[i].source||apx_json_length(body)!=strlen(modules[i].source))return NULL;}
        char *source=NULL,*diag=NULL;int failed=apx_modules_resolve(entry,modules,count,&source,&diag);struct Buffer b={0};envelope(&b,"apodexis.bundle_response.v1",!failed);
        if(source){text(&b,",\"source\":");quote(&b,source);}
        text(&b,",\"diagnostics\":[");if(diag)text(&b,diag);text(&b,"]}");free(source);free(diag);return validated_result(descriptor,finish(&b));
    }
    const char *source=apx_json_string(apx_json_get(arguments,"source"));if(!source||apx_json_length(apx_json_get(arguments,"source"))!=strlen(source))return NULL;
    if(!strcmp(name,"apodexis_apply_repairs"))return validated_result(descriptor,apply_repairs(source,apx_json_get(arguments,"request")));
    struct Limits limits=defaults();char *input=duplicate("null");int run=!strcmp(name,"apodexis_run");const char *keys[]={"source","input","limits"};
    if(!strict_object(arguments,keys,run?3:1)){free(input);return NULL;}
    if(run){if(!request_limits(apx_json_get(arguments,"limits"),&limits)){free(input);return NULL;}free(input);input=apx_json_encode(apx_json_get(arguments,"input"));if(!input)return NULL;}
    char *source_copy=duplicate(source);const char *op=!strcmp(name,"apodexis_format")?"fmt":!strcmp(name,"apodexis_check")?"repairs":!strcmp(name,"apodexis_explain")?"context":!strcmp(name,"apodexis_pseudocode")?"pseudocode":!strcmp(name,"apodexis_verify")?"verify":!strcmp(name,"apodexis_test")?"test":"run";
    char *result=operation(op,source_copy,input,limits);if(run){char *record=execution_record(source_copy,input,limits,result);free(result);result=record;}
    free(source_copy);free(input);return validated_result(descriptor,result);
}
static int artifact_tool(const char *name){return name&&(!strcmp(name,"apodexis_compile")||!strcmp(name,"apodexis_invoke")||!strcmp(name,"apodexis_release"));}
static char *mcp_tool_result(const char *answer){struct Buffer b={0};if(pending_artifact)b.maximum=FRAME_LIMIT;text(&b,"{\"structuredContent\":");text(&b,answer);text(&b,",\"content\":[{\"type\":\"text\",\"text\":");quote(&b,answer);text(&b,"}],\"isError\":");text(&b,successful(answer)?"false":"true");text(&b,"}");if(b.exceeded){free(b.text);return NULL;}return finish(&b);}
static struct Buffer mcp_frame(const char *id,const char *result,int error,size_t maximum){struct Buffer b={0};b.maximum=maximum;text(&b,"{\"jsonrpc\":\"2.0\",\"id\":");text(&b,id);if(error){char value[32];snprintf(value,sizeof value,"%d",error);text(&b,",\"error\":{\"code\":");text(&b,value);text(&b,",\"message\":\"invalid or unavailable request\"}");}else{text(&b,",\"result\":");text(&b,result?result:"null");}text(&b,"}\n");return b;}
/* Preserve legacy streaming of independently bounded components. Only pending
 * handle publication needs a complete outer-frame admission preflight. */
static int mcp_stream_frame(const char *id,const char *result,int error){
    if(fputs("{\"jsonrpc\":\"2.0\",\"id\":",stdout)<0||fputs(id,stdout)<0)return 0;
    int sent=error?fprintf(stdout,",\"error\":{\"code\":%d,\"message\":\"invalid or unavailable request\"}}\n",error):fprintf(stdout,",\"result\":%s}\n",result?result:"null");
    return sent>=0&&fflush(stdout)==0;
}
static int mcp(void){if(!store_settings())return 64;struct sigaction action={0};action.sa_handler=shutdown_signal;sigemptyset(&action.sa_mask);sigaction(SIGTERM,&action,NULL);sigaction(SIGINT,&action,NULL);signal(SIGPIPE,SIG_IGN);int initialized=0,notified=0,code=0;char *line=memory(FRAME_LIMIT+2);
    while(!shutdown_requested&&fgets(line,FRAME_LIMIT+2,stdin)){
        size_t length=strlen(line);if(length>FRAME_LIMIT||!length||line[length-1]!='\n'){code=2;break;}apx_json_reset();ApxValue *message=apx_json_parse(line,length);
        ApxValue *id=apx_json_get(message,"id"),*params=apx_json_get(message,"params");char *id_json=id?apx_json_encode(id):NULL;
        const char *method=apx_json_string(apx_json_get(message,"method")),*rpc=apx_json_string(apx_json_get(message,"jsonrpc"));char *method_copy=method?duplicate(method):NULL;char *result=NULL;int error=0;
        if(!rpc||strcmp(rpc,"2.0")||apx_json_length(apx_json_get(message,"jsonrpc"))!=strlen(rpc)||!method_copy||apx_json_length(apx_json_get(message,"method"))!=strlen(method_copy))error=-32600;
        else if(!strcmp(method_copy,"initialize")){
            if(initialized)error=-32602;else{initialized=1;result=duplicate("{\"protocolVersion\":\"2025-11-25\",\"capabilities\":{\"tools\":{\"listChanged\":false},\"resources\":{\"subscribe\":false,\"listChanged\":false}},\"serverInfo\":{\"name\":\"apodexis-qbe\",\"version\":\"0.2.0\"},\"instructions\":\"Native C compiler with QBE-only backend; no Python/Rust dependency or AST interpreter. Read Apodexis language/types/stdlib resources before authoring. Two-space indentation and explicit statements. Use row.id and rows[index] for checked reads, P out value for builder writeback, and inferred locals when initializers determine the type. Keep function signatures, buf item types and context-free empty/variant locals typed. Short forms normalize to existing prefix canonical source; no methods or general mutation. Strings/conversions, typed string-keyed map T and immutable array/with updates are available. For imports use apodexis_bundle with inline source/modules, then verify/run its assembled source. Host operations return denied results in MCP; only an operator-owned CLI run policy grants effects. Run/verify execute compiled native programs under fuel/memory/deadline and kernel syscall restrictions.\"}");}
        }else if(!strcmp(method_copy,"notifications/initialized")){notified=initialized;free(method_copy);free(id_json);continue;}
        else if(!strcmp(method_copy,"ping"))result=duplicate("{}");
        else if(!initialized||!notified)error=-32602;
        else if(!strcmp(method_copy,"tools/list"))result=tool_list();
        else if(!strcmp(method_copy,"tools/call")){
            ApxValue *name_value=apx_json_get(params,"name");const char *name=apx_json_string(name_value);
            int new_tool=artifact_tool(name);int request_id=id&&(apx_json_kind(id)==4||apx_json_kind(id)==2||apx_json_kind(id)==3);
            char *answer=name&&apx_json_length(name_value)==strlen(name)&&(!new_tool||request_id)?tool_call(name,apx_json_get(params,"arguments")):NULL;
            if(!answer)error=-32602;else{result=mcp_tool_result(answer);free(answer);if(!result){discard_pending();answer=store_failure();result=mcp_tool_result(answer);free(answer);}}
        }else if(!strcmp(method_copy,"resources/read")){
            const char *uri=apx_json_string(apx_json_get(params,"uri"));char *uri_copy=uri?duplicate(uri):NULL,*content=uri_copy?resource_content(uri_copy):NULL;
            if(!content)error=-32602;else{struct Buffer b={0};text(&b,"{\"contents\":[{\"uri\":");quote(&b,uri_copy);text(&b,",\"mimeType\":");quote(&b,strstr(uri_copy,"schemas")||strstr(uri_copy,"examples")||strstr(uri_copy,"opcodes")?"application/json":"text/markdown");text(&b,",\"text\":");quote(&b,content);text(&b,"}]}");result=finish(&b);}free(uri_copy);free(content);
        }else if(!strcmp(method_copy,"resources/list")){
            struct Buffer b={0};text(&b,"{\"resources\":[");for(size_t i=0;i<sizeof resource_uris/sizeof resource_uris[0];i++){if(i)text(&b,",");text(&b,"{\"uri\":");quote(&b,resource_uris[i]);text(&b,",\"name\":");quote(&b,resource_uris[i]);text(&b,",\"mimeType\":");quote(&b,i<3?"text/markdown":"application/json");text(&b,"}");}text(&b,"]}");result=finish(&b);
        }else if(!strncmp(method_copy,"notifications/",14)){free(method_copy);free(id_json);continue;}
        else error=-32601;
        int sent=1;
        if(id_json&&!shutdown_requested){
            if(!pending_artifact)sent=mcp_stream_frame(id_json,result,error);
            else{struct Buffer frame=mcp_frame(id_json,result,error,FRAME_LIMIT);
                if(frame.exceeded){discard_pending();free(frame.text);char *answer=store_failure(),*failure=mcp_tool_result(answer);free(answer);frame=mcp_frame(id_json,failure,0,FRAME_LIMIT);free(failure);
                    /* A near-limit request ID may not leave room even for a tool
                     * error envelope. Return the smaller protocol error instead. */
                    if(frame.exceeded){free(frame.text);frame=mcp_frame(id_json,NULL,-32602,FRAME_LIMIT);}
                }
                sent=!frame.exceeded&&fwrite(frame.text,1,frame.length,stdout)==frame.length&&fflush(stdout)==0;free(frame.text);if(sent)commit_pending();
            }
        }
        discard_pending();free(result);free(method_copy);free(id_json);if(!sent){code=2;break;}
    }free(line);clear_store();apx_json_reset();return shutdown_requested?128+shutdown_requested:code;
}
/* Serving admission owns the manifest and DTO projection. Driver callbacks own
 * only startup compilation/sealing and an isolated native broker invocation. */
static int serve_compile(const char *file,ApxServeImage *image){
    struct Artifact a={0};char *source=read_path(file,SOURCE_LIMIT),*diagnostic=NULL,*error=NULL;unsigned char *bytes=NULL;int ok=0;
    if(!source)goto done;
    if(strstr(source,"\nI ")||strstr(source,"\nV ")){char *assembled=NULL;if(apx_modules_load_file(file,&assembled,&diagnostic))goto done;free(source);source=assembled;}
    if(compile_front(source,&a,1)||apx_describe_source(source,NULL,&image->contract,NULL,&diagnostic))goto done;
    error=build_native(&a);if(error)goto done;
    size_t length=0;bytes=binary_bytes(a.program,&length);if(!bytes)goto done;
    ApxValue *contract=apx_json_parse(image->contract,strlen(image->contract));const char *hash=apx_json_string(apx_json_get(contract,"contract_hash"));
    if(!hash||strlen(hash)>=sizeof image->contract_hash||!*a.hash)goto done;
    image->fd=sealed_image(bytes,length);if(image->fd<0)goto done;
    image->bytes=length;strcpy(image->source_hash,a.hash);strcpy(image->contract_hash,hash);ok=1;
done:
    free(source);free(diagnostic);free(error);free(bytes);artifact_free(&a);return ok;
}
static char *serve_invoke(const ApxServeImage *image,ApxHostPolicy *policy,const char *input,ApxServeLimits bounds,const char *directory,int64_t until){
    struct Artifact a={0};if(strlen(directory)>=sizeof a.directory)return NULL;strcpy(a.directory,directory);
    struct Limits limits={bounds.fuel,bounds.input,bounds.output,1,1};
    serving_invocation=1;serving_deadline=until;host_policy=policy;timed_out=shutdown_requested=0;active_process=0;
    return run_native_image(&a,input,limits,0,image->fd);
}
static void usage(void){fputs("Usage: apodexis {fmt|check|explain|contract|pseudocode|run|test|verify|emit-qbe|build|bundle} source.apx [--json] [--input file.json] [--fuel n] [--max-input-bytes n] [--max-output-bytes n] [--capabilities policy.json] [-o path]\n       apodexis pqc-keygen ABSOLUTE_NEW_DIRECTORY --json\n       apodexis capabilities\n       apodexis mcp\n       apodexis serve --manifest FILE\n",stderr);}
static int unsigned_argument(const char *s,uint64_t *value){if(!s||!*s||*s=='-')return 0;errno=0;char *end;unsigned long long n=strtoull(s,&end,10);if(errno||*end)return 0;*value=(uint64_t)n;return 1;}
int main(int argc,char **argv){find_root();struct sigaction action={0};action.sa_handler=deadline;sigemptyset(&action.sa_mask);sigaction(SIGALRM,&action,NULL);action.sa_handler=shutdown_signal;sigaction(SIGTERM,&action,NULL);sigaction(SIGINT,&action,NULL);apx_host_cancel_signals(&timed_out,&shutdown_requested);
    const char *basename=strrchr(argv[0],'/');basename=basename?basename+1:argv[0];if(!strcmp(basename,"apx-mcp")||(argc>1&&!strcmp(argv[1],"mcp")))return mcp();
    if(argc>1&&!strcmp(argv[1],"serve")){if(argc!=4||strcmp(argv[2],"--manifest")){usage();return 64;}ApxServeDriver driver={serve_compile,serve_invoke};char build[PATH_LIMIT];path(build,root,".build");return apx_serve(argv[3],build,&driver);}
    if(argc>1&&!strcmp(argv[1],"pqc-keygen")){if(argc!=4||strcmp(argv[3],"--json")){usage();return 64;}char *out=apx_host_pqc_keygen(argv[2]);int ok=out&&successful(out);if(out){puts(out);free(out);}apx_json_reset();return ok?0:1;}
    if(argc>1&&!strcmp(argv[1],"capabilities")){char *out=capabilities();puts(out);free(out);return 0;}if(argc<3){usage();return 64;}
    const char *command=argv[1];const char *commands[]={"fmt","check","explain","contract","pseudocode","run","test","verify","emit-qbe","build","bundle"};int known=0;for(size_t i=0;i<sizeof commands/sizeof commands[0];i++)if(!strcmp(command,commands[i]))known=1;if(!known){usage();return 64;}
    struct Limits limits=defaults();const char *input_name=NULL,*output=NULL;int json_mode=0;
    for(int i=3;i<argc;i++){
        if(!strcmp(argv[i],"--json")){json_mode=1;continue;}
        if(i+1<argc&&(!strcmp(argv[i],"-o")||!strcmp(argv[i],"--output"))){output=argv[++i];continue;}
        if(i+1<argc&&!strcmp(argv[i],"--input")){input_name=argv[++i];continue;}
        if(i+1<argc&&!strcmp(argv[i],"--capabilities")){if(strcmp(command,"run")||host_policy){fputs("capabilities are supported only for one CLI run policy\n",stderr);return 64;}char *error=NULL;host_policy=apx_host_policy_load(argv[++i],&error);if(!host_policy){fprintf(stderr,"apodexis: %s\n",error?error:"invalid host policy");free(error);return 64;}continue;}
        uint64_t *value=NULL;if(!strcmp(argv[i],"--fuel"))value=&limits.fuel;else if(!strcmp(argv[i],"--max-input-bytes")){value=&limits.input;limits.has_input=1;}else if(!strcmp(argv[i],"--max-output-bytes")){value=&limits.output;limits.has_output=1;}
        if(!value||i+1>=argc||!unsigned_argument(argv[++i],value)){usage();return 64;}
    }
    char *source=!strcmp(argv[2],"-")?read_stream(stdin,SOURCE_LIMIT):read_path(argv[2],SOURCE_LIMIT);if(!source){fputs("source unreadable or exceeds 65536 bytes\n",stderr);return 66;}
    if(strcmp(argv[2],"-")&&(strstr(source,"\nI ")||strstr(source,"\nV "))){char *assembled=NULL,*diag=NULL;if(apx_modules_load_file(argv[2],&assembled,&diag)){struct Buffer b={0};envelope(&b,"apodexis.check_response.v1",0);text(&b,",\"diagnostics\":[");text(&b,diag?diag:"{}");text(&b,"]}");puts(finish(&b));free(b.text);free(source);free(diag);return 2;}free(source);source=assembled;}
    if(!strcmp(command,"bundle")){if(json_mode){struct Buffer b={0};envelope(&b,"apodexis.bundle_response.v1",1);text(&b,",\"source\":");quote(&b,source);text(&b,",\"diagnostics\":[]}");puts(finish(&b));free(b.text);}else fputs(source,stdout);free(source);return 0;}
    if(!strcmp(command,"emit-qbe")||!strcmp(command,"build")){
        struct Artifact a={0};int front=compile_front(source,&a,1);if(front){char *out=basic_check_response(&a,0);puts(out);free(out);artifact_free(&a);free(source);return 2;}
        int code=0;if(!strcmp(command,"emit-qbe")){if(output){if(write_new(output,a.il,strlen(a.il),0644))code=73;}else fputs(a.il,stdout);}
        else if(!output){usage();code=64;}else{
            char *error=build_native(&a);if(error){puts(error);free(error);code=70;}else{
                FILE *file=fopen(a.program,"rb");char *bytes=file?read_stream(file,PROCESS_LIMIT):NULL;if(file)fclose(file);
                /* ELF contains NUL bytes: determine its actual size separately. */
                struct stat statbuf;if(!bytes||stat(a.program,&statbuf)||write_new(output,bytes,(size_t)statbuf.st_size,0755)){fputs("output must be a new writable artifact path\n",stderr);code=73;}
                else{char hash[65];apx_sha256(bytes,(size_t)statbuf.st_size,hash);struct Buffer b={0};text(&b,"{\"schema\":\"apodexis.native_build.v1\",\"status\":\"ok\",\"backend\":\"qbe\",\"target\":\"amd64_sysv\",\"source_hash\":");quote(&b,a.hash);text(&b,",\"output\":");quote(&b,output);text(&b,",\"program_sha256\":");quote(&b,hash);text(&b,",\"requires_rust\":false,\"requires_python\":false}");puts(finish(&b));free(b.text);}free(bytes);
            }
        }artifact_free(&a);free(source);return code;
    }
    char *input=input_name?read_path(input_name,FRAME_LIMIT):duplicate("null");if(!input){free(source);return 66;}
    if(!strcmp(command,"fmt")){char *diag=NULL,*formatted=apx_format_source(source,&diag);if(formatted){fputs(formatted,stdout);free(formatted);free(diag);free(source);free(input);return 0;}free(diag);}
    char *response=operation(command,source,input,limits);int ok=successful(response);
    if(!strcmp(command,"pseudocode")&&ok&&!json_mode){ApxValue *value=apx_json_parse(response,strlen(response));const char *projection=apx_json_string(apx_json_get(value,"text"));fputs(projection,stdout);}else puts(response);free(response);free(source);free(input);apx_host_policy_free(host_policy);apx_json_reset();return ok?0:!strcmp(command,"test")?1:2;
}
