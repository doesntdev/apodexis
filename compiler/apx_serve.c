#define _GNU_SOURCE
/* Operator-only, immutable compiled-tool MCP. The parent never executes a
 * program or consumes policy counters. All broker state changes are fork/COW. */
#include "apx_serve.h"
#include "apx_http.h"
#include "../runtime/apx_runtime.h"
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define FRAME (4u*1048576u)
#define REPLY (2u*1048576u)
#define RETAINED (64u*1048576u)
#define SCHEMA 262144u
#define MAX_TOOLS 32u
#define MAX_JOBS 4u
#define ID_BYTES 256u
/* Serving operational limits, not language/JSON ABI or schema semantics. */
#define JSON_MEMBERS 256u
#define JSON_VALUES 16384u
#define JSON_DEPTH 64u
#define JSON_WORK 1048576u
struct Buf { char *s; size_t n,cap,max; int bad; };
struct Tool {
    char *name,*descriptor;
    ApxServeImage image;
    ApxServeLimits limits;
    ApxHostPolicy *policy;
};
struct Job {
    pid_t pid;
    int fd,reaped,cancelled,expired,failed;
    int64_t deadline;
    char *id;
    char directory[4096];
    struct Tool *tool;
    struct Buf reply;
};
struct Server {
    struct Tool tools[MAX_TOOLS];
    struct Job jobs[MAX_JOBS];
    size_t count,concurrency;
    char *catalog;
    const char *build;
    const ApxServeDriver *driver;
    struct Buf outgoing;
    size_t sent;
    int initialized,notified,failed,signal_fd,mask_set;
    sigset_t previous_mask;
};
static volatile sig_atomic_t stopped;
static void stop_signal(int sig){stopped=sig;}
static void child_notifications(int fd){struct signalfd_siginfo info[4];while(read(fd,info,sizeof info)>0){}}
static int wait_timeout(struct Server *s){
    int64_t current=apx_http_now(),nearest=INT64_MAX;
    for(size_t i=0;i<s->concurrency;i++){struct Job *j=&s->jobs[i];if(j->pid&&!j->cancelled&&!j->expired&&j->deadline<nearest)nearest=j->deadline;}
    return nearest==INT64_MAX?-1:nearest<=current?0:(int)(nearest-current);
}
static void *alloc(size_t n){void *p=calloc(1,n?n:1);if(!p){fputs("apodexis: serving allocation failed\n",stderr);exit(70);}return p;}
static char *copy(const char *s){char *p=alloc(strlen(s)+1);strcpy(p,s);return p;}
static void putn(struct Buf *b,const char *s,size_t n){
    if(b->bad)return;
    if(n>b->max||b->n>b->max-n){b->bad=1;return;}
    if(b->n+n+1>b->cap){size_t next=b->cap?b->cap:256;while(next<b->n+n+1)next*=2;if(next>b->max+1)next=b->max+1;char *p=realloc(b->s,next);if(!p){b->bad=1;return;}b->s=p;b->cap=next;}
    memcpy(b->s+b->n,s,n);b->n+=n;b->s[b->n]=0;
}
static void put(struct Buf *b,const char *s){putn(b,s,strlen(s));}
static void quoted(struct Buf *b,const char *s){char *q=apx_json_encode(apx_string(s,(int64_t)strlen(s)));if(!q)b->bad=1;else{put(b,q);free(q);}}
static const char *string(ApxValue *v){const char *s=apx_json_string(v);return s&&strlen(s)==apx_json_length(v)?s:NULL;}
static int is(ApxValue *v,const char *s){const char *t=string(v);return t&&!strcmp(t,s);}
static int fields(ApxValue *v,const char *const *keys,size_t count){
    if(apx_json_kind(v)!=6)return 0;
    for(size_t i=0;i<apx_json_length(v);i++){const char *k=apx_json_key(v,i);if(!k||strlen(k)!=apx_json_key_length(v,i))return 0;int found=0;for(size_t j=0;k&&j<count;j++)if(!strcmp(k,keys[j])&&apx_json_get(v,k))found=1;if(!found)return 0;}
    return 1;
}
static int integer(ApxValue *v,uint64_t min,uint64_t max,uint64_t *out){return v&&apx_json_kind(v)==2&&apx_json_unsigned(v,out)&&*out>=min&&*out<=max;}
static int join(char out[4096],const char *dir,const char *name){int n=name[0]=='/'?snprintf(out,4096,"%s",name):snprintf(out,4096,"%s/%s",dir,name);return n>0&&n<4096;}
static char *load(const char *path,size_t max){
    int fd=open(path,O_RDONLY|O_CLOEXEC|O_NONBLOCK);struct stat st;
    if(fd<0)return NULL;
    if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<0||(uint64_t)st.st_size>max){close(fd);return NULL;}
    struct Buf b={.max=max};char block[4096];ssize_t n;
    while((n=read(fd,block,sizeof block))!=0){if(n<0){if(errno==EINTR&&!stopped)continue;b.bad=1;break;}if(memchr(block,0,(size_t)n)){b.bad=1;break;}putn(&b,block,(size_t)n);if(b.bad)break;}
    close(fd);if(b.bad){free(b.s);return NULL;}return b.s?b.s:copy("");
}
/* Lexical work admission only: no DOM, allocation, recursive descent, decoded
 * values, duplicate detection or alternate JSON semantics. For valid JSON each
 * non-key token is one value; each colon adds one object member. Adding 2*m-1
 * charges exactly sum(object_members^2), bounding the generic parser's linear
 * duplicate checks. Malformed admitted syntax still belongs to that parser.
 * After a limit, request scanning does only bounded scalar-ID recovery, even
 * when ID follows the offending subtree. Strings/escapes are skipped as units.
 * Every scan loop observes SIGTERM; reply scans can stop immediately on limit. */
static int whitespace(char c){return c==' '||c=='\t'||c=='\r'||c=='\n';}
static int id_key(const char *s,size_t n){
    const char *want="id";size_t at=1;
    for(unsigned i=0;i<2;i++){
        if(at+1<n&&s[at]==want[i])at++;
        else if(at+6<n&&s[at]=='\\'&&s[at+1]=='u'&&s[at+2]=='0'&&s[at+3]=='0'&&s[at+4]=='6'&&s[at+5]==(i?'4':'9'))at+=6;
        else return 0;
    }
    return at+1==n&&s[at]=='"';
}
static int json_preflight(const char *s,size_t n,char recovered[6*ID_BYTES+3]){
    unsigned members[JSON_DEPTH]={0};char kinds[JSON_DEPTH]={0};
    size_t depth=0,values=0,work=0,at=0;unsigned ids=0;
    int limited=0,pending_id=0,root_object=0,root_done=0;
    if(recovered)recovered[0]=0;
    while(at<n){
        if(stopped)return -1;
        char c=s[at];if(whitespace(c)){at++;continue;}
        if(!at||(!depth&&!root_done))root_object=c=='{';
        if(c=='}'||c==']'){if(depth)depth--;if(!depth)root_done=1;at++;continue;}
        if(c==','||c==':'){
            if(c==':'&&!limited&&depth&&depth<=JSON_DEPTH&&kinds[depth-1]=='{'){
                unsigned m=++members[depth-1];work+=2u*m-1u;
                if(m>JSON_MEMBERS||work>JSON_WORK)limited=1;
            }
            at++;if(limited&&!recovered)return 0;continue;
        }
        if(c=='{'||c=='['){
            if(!limited&&++values>JSON_VALUES)limited=1;
            if(depth<JSON_DEPTH){members[depth]=0;kinds[depth]=c;}
            if(++depth>JSON_DEPTH)limited=1;
            pending_id=0;at++;if(limited&&!recovered)return 0;continue;
        }
        size_t start=at;
        if(c=='"'){
            at++;
            while(at<n){if(stopped)return -1;char q=s[at++];if(q=='"')break;if(q=='\\'&&at<n)at++;}
        }else{
            while(at<n&&!whitespace(s[at])&&!strchr("{}[],:\"",s[at])){if(stopped)return -1;at++;}
            if(at==start)at++; /* Malformed bytes must still make progress. */
        }
        size_t end=at,next=at;
        while(next<n&&whitespace(s[next])){if(stopped)return -1;next++;}
        int key=c=='"'&&next<n&&s[next]==':';
        if(recovered&&root_object&&!root_done&&depth==1){
            if(key){pending_id=id_key(s+start,end-start);if(pending_id){ids++;recovered[0]=0;}}
            else if(pending_id){if(ids==1&&end-start<=6*ID_BYTES+2){memcpy(recovered,s+start,end-start);recovered[end-start]=0;}pending_id=0;}
        }
        if(!key&&!limited&&++values>JSON_VALUES)limited=1;
        if(limited&&!recovered)return 0;
    }
    if(recovered&&ids!=1)recovered[0]=0;
    return limited?0:1;
}
/* Validation separately meters actual descriptor/value node visits and an
 * upper bound on linear object lookup comparisons, including schema lookups.
 * This also bounds oneOf branching; preflight alone is not a schema work cap. */
struct Work {size_t left;int exhausted;};
static int spend(struct Work *w,size_t n){if(w->exhausted||stopped||n>w->left){w->exhausted=1;return 0;}w->left-=n;return 1;}
static ApxValue *lookup(struct Work *w,ApxValue *v,const char *key){return spend(w,1+apx_json_length(v))?apx_json_get(v,key):NULL;}
/* Project only checked contract DTOs. Recursive definitions, enum/builder and
 * other unsupported ABI types fail startup, never become permissive schemas. */
static void project(struct Buf *b,ApxValue *type,ApxValue *defs,unsigned depth){
    if(b->bad)return; /* Stop traversal as well as writes at the schema budget. */
    const char *kind=string(apx_json_get(type,"kind"));if(!kind||depth>48){b->bad=1;return;}
    if(!strcmp(kind,"i64"))put(b,"{\"type\":\"integer\",\"minimum\":-9223372036854775808,\"maximum\":9223372036854775807}");
    else if(!strcmp(kind,"f64"))put(b,"{\"type\":\"number\"}");
    else if(!strcmp(kind,"bool"))put(b,"{\"type\":\"boolean\"}");
    else if(!strcmp(kind,"str"))put(b,"{\"type\":\"string\"}");
    else if(!strcmp(kind,"array")||!strcmp(kind,"map")){
        int map=!strcmp(kind,"map");put(b,map?"{\"type\":\"object\",\"propertyNames\":{\"pattern\":\"^[^\\u0000]*$\"},\"additionalProperties\":" : "{\"type\":\"array\",\"items\":");project(b,apx_json_get(type,map?"values":"items"),defs,depth+1);put(b,"}");
    }else if(!strcmp(kind,"option")||!strcmp(kind,"result")){
        int option=!strcmp(kind,"option");put(b,"{\"oneOf\":[{\"type\":\"object\",\"additionalProperties\":false,\"required\":[\"tag\",\"value\"],\"properties\":{\"tag\":{\"const\":");quoted(b,option?"some":"ok");put(b,"},\"value\":");project(b,apx_json_get(type,option?"item":"ok"),defs,depth+1);put(b,"}},{\"type\":\"object\",\"additionalProperties\":false,\"required\":[\"tag\"");if(!option)put(b,",\"error\"");put(b,"],\"properties\":{\"tag\":{\"const\":");quoted(b,option?"none":"err");put(b,"}");if(!option){put(b,",\"error\":");project(b,apx_json_get(type,"error"),defs,depth+1);}put(b,"}}]}");
    }else if(!strcmp(kind,"named")){
        const char *name=string(apx_json_get(type,"name"));ApxValue *def=NULL;
        for(size_t i=0;name&&i<apx_json_length(defs);i++){ApxValue *d=apx_json_item(defs,i);if(is(apx_json_get(d,"name"),name)){def=d;break;}}
        if(!def||!is(apx_json_get(def,"kind"),"struct")){b->bad=1;return;}
        ApxValue *fs=apx_json_get(def,"fields");if(apx_json_length(fs)>JSON_MEMBERS){b->bad=1;return;}put(b,"{\"type\":\"object\",\"additionalProperties\":false,\"required\":[");
        for(size_t i=0;i<apx_json_length(fs);i++){const char *n=string(apx_json_get(apx_json_item(fs,i),"name"));if(!n){b->bad=1;return;}if(i)put(b,",");quoted(b,n);}put(b,"],\"properties\":{");
        for(size_t i=0;i<apx_json_length(fs);i++){ApxValue *f=apx_json_item(fs,i);if(i)put(b,",");quoted(b,string(apx_json_get(f,"name")));put(b,":");project(b,apx_json_get(f,"type"),defs,depth+1);}put(b,"}}");
    }else b->bad=1;
}
static const char error_schema[]="{\"type\":\"object\",\"additionalProperties\":false,\"required\":[\"status\",\"error\"],\"properties\":{\"status\":{\"const\":\"error\"},\"error\":{\"type\":\"object\",\"additionalProperties\":false,\"required\":[\"code\",\"message\"],\"properties\":{\"code\":{\"type\":\"string\"},\"message\":{\"type\":\"string\"}}}}}";
static char *descriptor(struct Tool *t,const char *description){
    ApxValue *root=apx_json_parse(t->image.contract,strlen(t->image.contract)),*c=apx_json_get(root,"contract"),*params=apx_json_get(c,"parameters"),*defs=apx_json_get(c,"definitions");
    if(!is(apx_json_get(root,"status"),"ok")||!is(apx_json_get(c,"name"),"main")||apx_json_kind(params)!=5||apx_json_length(params)>1)return NULL;
    struct Buf b={.max=SCHEMA};put(&b,"{\"name\":");quoted(&b,t->name);put(&b,",\"description\":");quoted(&b,description);
    put(&b,",\"inputSchema\":{\"type\":\"object\",\"additionalProperties\":false,\"required\":[\"input\"],\"properties\":{\"input\":");
    if(apx_json_length(params))project(&b,apx_json_get(apx_json_item(params,0),"type"),defs,0);else put(&b,"{\"type\":\"null\"}");
    put(&b,"}},\"outputSchema\":{\"type\":\"object\",\"oneOf\":[{\"type\":\"object\",\"additionalProperties\":false,\"required\":[\"status\",\"output\",\"fuel_used\",\"source_hash\",\"contract_hash\"],\"properties\":{\"status\":{\"const\":\"ok\"},\"output\":");project(&b,apx_json_get(c,"return_type"),defs,0);
    put(&b,",\"fuel_used\":{\"type\":\"integer\",\"minimum\":0},\"source_hash\":{\"const\":");quoted(&b,t->image.source_hash);put(&b,"},\"contract_hash\":{\"const\":");quoted(&b,t->image.contract_hash);put(&b,"}}},");put(&b,error_schema);put(&b,"]}}");if(b.bad){free(b.s);return NULL;}return b.s;
}
/* Interpreter restricted to the exact locally generated descriptor vocabulary.
 * This single descriptor validates both calls and structured results. */
static int validate(ApxValue *s,ApxValue *v,unsigned depth,struct Work *w){
    if(!spend(w,1)||!s||!v||depth>128)return 0;
    ApxValue *choices=lookup(w,s,"oneOf");if(choices){unsigned accepted=0;for(size_t i=0;i<apx_json_length(choices);i++)accepted+=(unsigned)validate(apx_json_item(choices,i),v,depth+1,w);if(accepted!=1)return 0;}
    ApxValue *constant=lookup(w,s,"const");if(constant){if(apx_json_kind(constant)!=apx_json_kind(v))return 0;char *a=apx_json_encode(constant),*b=apx_json_encode(v);int ok=a&&b&&!strcmp(a,b);free(a);free(b);if(!ok)return 0;}
    const char *type=string(lookup(w,s,"type"));int k=apx_json_kind(v);
    if(type){if(!strcmp(type,"object")){if(k!=6)return 0;}else if(!strcmp(type,"array")){if(k!=5)return 0;}else if(!strcmp(type,"integer")){if(k!=2)return 0;uint64_t u;if(apx_json_unsigned(v,&u)&&u>INT64_MAX)return 0;if(lookup(w,s,"minimum")&&apx_json_integer(lookup(w,s,"minimum"))==0&&apx_json_integer(v)<0)return 0;}else if(!strcmp(type,"number")){if(k!=2&&k!=3)return 0;}else if(!strcmp(type,"string")){if(k!=4)return 0;}else if(!strcmp(type,"boolean")){if(k!=1)return 0;}else if(!strcmp(type,"null")){if(k!=0)return 0;}else return 0;}
    if(k==6){
        ApxValue *required=lookup(w,s,"required"),*props=lookup(w,s,"properties"),*additional=lookup(w,s,"additionalProperties");
        for(size_t i=0;i<apx_json_length(required);i++){const char *key=string(apx_json_item(required,i));if(!key||!lookup(w,v,key))return 0;}
        for(size_t i=0;i<apx_json_length(v);i++){const char *key=apx_json_key(v,i);if(!key||strlen(key)!=apx_json_key_length(v,i))return 0;ApxValue *value=key?lookup(w,v,key):NULL,*child=key?lookup(w,props,key):NULL;if(!value)return 0;if(child){if(!validate(child,value,depth+1,w))return 0;}else if(additional){if(apx_json_kind(additional)==1){if(!apx_json_integer(additional))return 0;}else if(!validate(additional,value,depth+1,w))return 0;}}
    }else if(k==5){ApxValue *items=lookup(w,s,"items");for(size_t i=0;items&&i<apx_json_length(v);i++)if(!validate(items,apx_json_item(v,i),depth+1,w))return 0;}
    return !w->exhausted;
}
static int name_ok(const char *s){size_t n=s?strlen(s):0;if(!n||n>64)return 0;for(size_t i=0;i<n;i++)if(!((s[i]>='A'&&s[i]<='Z')||(s[i]>='a'&&s[i]<='z')||(s[i]>='0'&&s[i]<='9')||s[i]=='_'||s[i]=='-'))return 0;return 1;}
static int configure(struct Server *s,const char *manifest){
    char *raw=load(manifest,1048576);if(!raw)return 0;
    ApxValue *m=apx_json_parse(raw,strlen(raw));free(raw);
    const char *const top[]={"schema","max_concurrent_calls","tools"};uint64_t concurrent;
    if(!fields(m,top,3)||apx_json_length(m)!=3||!is(apx_json_get(m,"schema"),"apodexis.tool_server.v1")||!integer(apx_json_get(m,"max_concurrent_calls"),1,MAX_JOBS,&concurrent))return 0;
    ApxValue *tools=apx_json_get(m,"tools");size_t count=apx_json_length(tools);
    if(apx_json_kind(tools)!=5||!count||count>MAX_TOOLS)return 0;
    char *absolute=realpath(manifest,NULL);if(!absolute)return 0;char *slash=strrchr(absolute,'/');if(!slash){free(absolute);return 0;}*slash=0;
    s->concurrency=(size_t)concurrent;size_t bytes=0;int ok=0;
    for(size_t i=0;i<count;i++){
        struct Tool *t=&s->tools[i];t->image.fd=-1;s->count=i+1;
        ApxValue *v=apx_json_item(tools,i),*limits=apx_json_get(v,"limits");
        const char *const keys[]={"name","description","source","limits","capabilities","credentials"};const char *const bound_keys[]={"fuel","max_input_bytes","max_output_bytes","timeout_ms"};
        const char *name=string(apx_json_get(v,"name")),*description=string(apx_json_get(v,"description")),*source=string(apx_json_get(v,"source")),*policy=string(apx_json_get(v,"capabilities"));ApxValue *credentials=apx_json_get(v,"credentials");
        if(!fields(v,keys,6)||!name_ok(name)||!description||!*description||strlen(description)>4096||!source||!*source||strlen(source)>=4096||!fields(limits,bound_keys,4)||apx_json_length(limits)!=4||!integer(apx_json_get(limits,"fuel"),0,1000000000,&t->limits.fuel)||!integer(apx_json_get(limits,"max_input_bytes"),0,1048576,&t->limits.input)||!integer(apx_json_get(limits,"max_output_bytes"),0,1048576,&t->limits.output)||!integer(apx_json_get(limits,"timeout_ms"),1,30000,&t->limits.timeout_ms))goto done;
        for(size_t j=0;j<i;j++)if(!strcmp(name,s->tools[j].name))goto done;
        t->name=copy(name);char file[4096];
        if(apx_json_get(v,"capabilities")){if(!policy||!*policy||!join(file,absolute,policy))goto done;char *error=NULL,*encoded=credentials?apx_json_encode(credentials):NULL;t->policy=apx_host_policy_load_serving(file,encoded,&error);free(encoded);if(!t->policy){fprintf(stderr,"apodexis: serving policy: %s\n",error?error:"invalid operator policy");free(error);goto done;}free(error);}else if(credentials)goto done;
        if(!join(file,absolute,source)||!s->driver->compile(file,&t->image)||t->image.bytes>RETAINED||bytes>RETAINED-t->image.bytes)goto done;
        bytes+=t->image.bytes;t->descriptor=descriptor(t,description);if(!t->descriptor)goto done;
        free(t->image.contract);t->image.contract=NULL;
    }
    {struct Buf b={.max=FRAME-1024};put(&b,"{\"tools\":[");for(size_t i=0;i<s->count;i++){if(i)put(&b,",");put(&b,s->tools[i].descriptor);}put(&b,"]}");if(b.bad){free(b.s);goto done;}s->catalog=b.s;}
    ok=1;
done: free(absolute);return ok;
}
static char *failure(const char *code){struct Buf b={.max=4096};put(&b,"{\"status\":\"error\",\"error\":{\"code\":");quoted(&b,code);put(&b,",\"message\":\"Bounded tool invocation did not complete successfully\"}}");return b.s;}
static char *tool_result(struct Tool *t,char *answer){
    if(answer&&json_preflight(answer,strlen(answer),NULL)!=1){free(answer);answer=failure("resource_limit");}
    ApxValue *d=apx_json_parse(t->descriptor,strlen(t->descriptor)),*schema=apx_json_get(d,"outputSchema"),*v=answer?apx_json_parse(answer,strlen(answer)):NULL;
    struct Work work={JSON_WORK,0};
    if(!validate(schema,v,0,&work)){free(answer);answer=failure(work.exhausted?"resource_limit":"invalid_result");v=apx_json_parse(answer,strlen(answer));}
    struct Buf b={.max=FRAME-1024};put(&b,"{\"structuredContent\":");put(&b,answer);put(&b,",\"content\":[{\"type\":\"text\",\"text\":");quoted(&b,answer);put(&b,"}],\"isError\":");put(&b,is(apx_json_get(v,"status"),"ok")?"false":"true");put(&b,"}");free(answer);
    if(b.bad){free(b.s);return tool_result(t,failure("output_limit"));}return b.s;
}
static int frame(struct Server *s,const char *id,const char *result,int error){
    struct Buf b={.max=FRAME};put(&b,"{\"jsonrpc\":\"2.0\",\"id\":");put(&b,id?id:"null");
    if(error){char n[32];snprintf(n,sizeof n,"%d",error);put(&b,",\"error\":{\"code\":");put(&b,n);put(&b,",\"message\":");quoted(&b,error==-32000?"Serving JSON resource limit exceeded":"Invalid or unavailable request");put(&b,"}");}else{put(&b,",\"result\":");put(&b,result?result:"null");}put(&b,"}\n");
    if(s->sent){memmove(s->outgoing.s,s->outgoing.s+s->sent,s->outgoing.n-s->sent);s->outgoing.n-=s->sent;s->sent=0;}
    if(b.bad)s->failed=1;else putn(&s->outgoing,b.s,b.n);free(b.s);if(s->outgoing.bad)s->failed=1;return !s->failed;
}
static void tool_failure(struct Server *s,const char *id,struct Tool *t,const char *code){char *result=tool_result(t,failure(code));frame(s,id,result,0);free(result);}
static int id_ok(ApxValue *v){int k=apx_json_kind(v);if(!v||(k!=2&&k!=4))return 0;if(k==4){const char *str=string(v);if(!str||apx_json_length(v)>ID_BYTES)return 0;}char *encoded=apx_json_encode(v);int ok=encoded&&strlen(encoded)<=ID_BYTES;free(encoded);return ok;}
static struct Job *active(struct Server *s,const char *id){for(size_t i=0;i<s->concurrency;i++)if(s->jobs[i].pid&&!strcmp(s->jobs[i].id,id))return &s->jobs[i];return NULL;}
static void kill_job(struct Job *j){if(j->pid>0){kill(-j->pid,SIGKILL);if(!j->reaped)kill(j->pid,SIGKILL);}}
static int write_all(int fd,const char *s){size_t n=strlen(s),at=0;while(at<n){ssize_t k=write(fd,s+at,n-at);if(k<0&&errno==EINTR)continue;if(k<=0)return 0;at+=(size_t)k;}return 1;}
static void admit(struct Server *s,const char *id,struct Tool *t,const char *input){
    struct Job *j=NULL;for(size_t i=0;i<s->concurrency;i++)if(!s->jobs[i].pid){j=&s->jobs[i];break;}
    if(!j){tool_failure(s,id,t,"busy");return;}
    int64_t until=apx_http_now()+(int64_t)t->limits.timeout_ms;
    if(!join(j->directory,s->build,"serve-job-XXXXXX")||!mkdtemp(j->directory)){j->directory[0]=0;tool_failure(s,id,t,"service_unavailable");return;}
    int pipefd[2];if(pipe2(pipefd,O_CLOEXEC)){rmdir(j->directory);j->directory[0]=0;tool_failure(s,id,t,"service_unavailable");return;}
    pid_t parent=getpid();pid_t pid=fork();
    if(!pid){
        signal(SIGTERM,SIG_DFL);signal(SIGINT,SIG_DFL);signal(SIGCHLD,SIG_DFL);
        if(s->mask_set)sigprocmask(SIG_SETMASK,&s->previous_mask,NULL);
        if(s->signal_fd>=0)close(s->signal_fd);
        if(prctl(PR_SET_PDEATHSIG,SIGKILL)||getppid()!=parent||setpgid(0,0))_exit(125);
        close(pipefd[0]);for(size_t i=0;i<s->concurrency;i++)if(s->jobs[i].pid&&s->jobs[i].fd>=0)close(s->jobs[i].fd);
        int dev=open("/dev/null",O_RDWR|O_CLOEXEC);if(dev<0)_exit(125);for(int i=0;i<3;i++)if(dup2(dev,i)<0)_exit(125);if(dev>2)close(dev);
        char *raw=s->driver->invoke(&t->image,t->policy,input,t->limits,j->directory,until);
        int ok=raw&&strlen(raw)<=REPLY&&write_all(pipefd[1],raw);free(raw);close(pipefd[1]);_exit(ok?0:1);
    }
    close(pipefd[1]);if(pid<0){close(pipefd[0]);rmdir(j->directory);j->directory[0]=0;tool_failure(s,id,t,"service_unavailable");return;}
    setpgid(pid,pid);j->pid=pid;j->fd=pipefd[0];fcntl(j->fd,F_SETFL,O_NONBLOCK);j->id=copy(id);j->deadline=until;j->tool=t;j->reply.max=REPLY;
}
static int empty(ApxValue *v){return !v||(apx_json_kind(v)==6&&apx_json_length(v)==0);}
static void request(struct Server *s,const char *raw,size_t n){
    apx_json_reset();char recovered[6*ID_BYTES+3];int admitted=json_preflight(raw,n,recovered);
    if(admitted<0)return;
    if(!admitted){ApxValue *scalar=*recovered?apx_json_parse(recovered,strlen(recovered)):NULL;char *id=id_ok(scalar)?apx_json_encode(scalar):NULL;frame(s,id,NULL,-32000);free(id);return;}
    ApxValue *m=apx_json_parse(raw,n),*iv=apx_json_get(m,"id"),*p=apx_json_get(m,"params");
    const char *method=string(apx_json_get(m,"method"));char *id=id_ok(iv)?apx_json_encode(iv):NULL;
    const char *const keys[]={"jsonrpc","id","method","params"};int error=0;const char *result=NULL;char initialization[1024];
    if(!m){frame(s,NULL,NULL,-32700);return;}
    if(!fields(m,keys,4)||!is(apx_json_get(m,"jsonrpc"),"2.0")||!method||(iv&&!id)){frame(s,id,NULL,-32600);goto done;}
    if(id&&active(s,id)){frame(s,id,NULL,-32600);goto done;}
    if(!strcmp(method,"notifications/cancelled")){
        const char *const ck[]={"requestId","reason"};ApxValue *ci=apx_json_get(p,"requestId"),*reason=apx_json_get(p,"reason");
        if(!id&&s->notified&&fields(p,ck,2)&&id_ok(ci)&&(!reason||(string(reason)&&apx_json_length(reason)<=1024))){char *cancel=apx_json_encode(ci);struct Job *j=active(s,cancel);free(cancel);if(j){j->cancelled=1;kill_job(j);}}
        goto done;
    }
    if(!strcmp(method,"notifications/initialized")){if(!id&&s->initialized&&empty(p))s->notified=1;goto done;}
    if(!id)goto done; /* Calls without IDs never execute. */
    if(!strcmp(method,"initialize")){if(s->initialized||apx_json_kind(p)!=6)error=-32602;else{
        s->initialized=1;
        snprintf(initialization,sizeof initialization,"{\"protocolVersion\":\"2025-11-25\",\"capabilities\":{\"tools\":{\"listChanged\":false},\"experimental\":{\"apodexisJsonLimits\":{\"maxRequestBytes\":%u,\"maxWorkerReplyBytes\":%u,\"maxObjectMembers\":%u,\"maxValues\":%u,\"maxDepth\":%u,\"maxObjectWork\":%u,\"maxValidationWork\":%u}}},\"serverInfo\":{\"name\":\"apodexis-tools\",\"version\":\"0.2.0\"},\"instructions\":\"Immutable operator-configured native tools. Calls accept only input. Excess concurrent calls return busy; no queue.\"}",FRAME,REPLY,JSON_MEMBERS,JSON_VALUES,JSON_DEPTH,JSON_WORK,JSON_WORK);
        result=initialization;
    }}
    else if(!s->notified)error=-32602;
    else if(!strcmp(method,"ping")){if(!empty(p))error=-32602;else result="{}";}
    else if(!strcmp(method,"tools/list")){if(!empty(p))error=-32602;else result=s->catalog;}
    else if(!strcmp(method,"tools/call")){
        const char *const call_keys[]={"name","arguments"};const char *name=string(apx_json_get(p,"name"));struct Tool *t=NULL;
        for(size_t i=0;name&&i<s->count;i++)if(!strcmp(name,s->tools[i].name))t=&s->tools[i];
        ApxValue *args=apx_json_get(p,"arguments"),*d=t?apx_json_parse(t->descriptor,strlen(t->descriptor)):NULL;
        struct Work work={JSON_WORK,0};
        if(!fields(p,call_keys,2)||apx_json_length(p)!=2||!t||!validate(apx_json_get(d,"inputSchema"),args,0,&work))error=work.exhausted?-32000:-32602;
        else{char *input=apx_json_encode(apx_json_get(args,"input"));if(!input)error=-32602;else if(strlen(input)>t->limits.input)tool_failure(s,id,t,"input_limit");else admit(s,id,t,input);free(input);goto done;}
    }else error=-32601;
    frame(s,id,result,error);
done: free(id);
}
static char *checked_answer(struct Job *j){
    if(j->expired)return failure("deadline");
    if(j->failed||j->reply.bad||!j->reply.s)return failure("worker_failed");
    if(json_preflight(j->reply.s,j->reply.n,NULL)!=1)return failure("resource_limit");
    ApxValue *r=apx_json_parse(j->reply.s,j->reply.n),*out=apx_json_get(r,"output");
    if(!is(apx_json_get(r,"status"),"ok")||!out)return failure("runtime_failed");
    char *encoded=apx_json_encode(out);if(!encoded||strlen(encoded)>j->tool->limits.output){free(encoded);return failure("output_limit");}
    struct Buf b={.max=REPLY};put(&b,"{\"status\":\"ok\",\"output\":");put(&b,encoded);free(encoded);put(&b,",\"fuel_used\":");uint64_t fuel;
    if(!integer(apx_json_get(r,"fuel_used"),0,j->tool->limits.fuel,&fuel)){free(b.s);return failure("invalid_result");}
    char number[32];snprintf(number,sizeof number,"%"PRIu64,fuel);put(&b,number);put(&b,",\"source_hash\":");quoted(&b,j->tool->image.source_hash);put(&b,",\"contract_hash\":");quoted(&b,j->tool->image.contract_hash);put(&b,"}");if(b.bad){free(b.s);return failure("output_limit");}return b.s;
}
static void clean_directory(struct Job *j){if(!*j->directory)return;const char *files[]={"stdin","stdout","stderr"};for(size_t i=0;i<3;i++){char path[4096];if(join(path,j->directory,files[i]))unlink(path);}rmdir(j->directory);}
static void harvest(struct Server *s,int shutdown){
    for(size_t i=0;i<s->concurrency;i++){
        struct Job *j=&s->jobs[i];if(!j->pid)continue;
        if(shutdown){j->cancelled=1;kill_job(j);}else if(!j->cancelled&&!j->expired&&apx_http_now()>=j->deadline){j->expired=1;kill_job(j);}
        if(j->fd>=0){char block[8192];for(;;){ssize_t n=read(j->fd,block,sizeof block);if(n>0){putn(&j->reply,block,(size_t)n);if(j->reply.bad){j->failed=1;kill_job(j);break;}}else if(!n){close(j->fd);j->fd=-1;break;}else if(errno==EINTR)continue;else if(errno==EAGAIN)break;else{j->failed=1;close(j->fd);j->fd=-1;break;}}}
        int status;pid_t pid;
        /* Reap the leader by PID too: an early prctl/setpgid failure must not
         * strand a slot in a process group which was never established. */
        if(!j->reaped&&waitpid(j->pid,&status,WNOHANG)==j->pid){j->reaped=1;if(!WIFEXITED(status)||WEXITSTATUS(status))j->failed=1;}
        while((pid=waitpid(-j->pid,&status,WNOHANG))>0){if(pid==j->pid){j->reaped=1;if(!WIFEXITED(status)||WEXITSTATUS(status))j->failed=1;}}
        if(j->reaped){kill_job(j);if(kill(-j->pid,0)==0||errno!=ESRCH||j->fd>=0)continue;
            if(!j->cancelled&&!shutdown){apx_json_reset();char *result=tool_result(j->tool,checked_answer(j));frame(s,j->id,result,0);free(result);}
            clean_directory(j);free(j->id);free(j->reply.s);memset(j,0,sizeof *j);j->fd=-1;
        }
    }
}
static int running(struct Server *s){for(size_t i=0;i<s->concurrency;i++)if(s->jobs[i].pid)return 1;return 0;}
static void release(struct Server *s){for(size_t i=0;i<s->count;i++){struct Tool *t=&s->tools[i];free(t->name);free(t->descriptor);free(t->image.contract);if(t->image.fd>=0)close(t->image.fd);apx_host_policy_free(t->policy);}free(s->catalog);free(s->outgoing.s);if(s->signal_fd>=0)close(s->signal_fd);if(s->mask_set)sigprocmask(SIG_SETMASK,&s->previous_mask,NULL);}
int apx_serve(const char *manifest,const char *build_directory,const ApxServeDriver *driver){
    struct Server *s=alloc(sizeof *s);s->driver=driver;s->build=build_directory;s->outgoing.max=2u*FRAME;s->signal_fd=-1;
    for(size_t i=0;i<MAX_TOOLS;i++)s->tools[i].image.fd=-1;
    for(size_t i=0;i<MAX_JOBS;i++)s->jobs[i].fd=-1;
    if(!configure(s,manifest)){fputs("apodexis: invalid serving configuration or unsupported checked contract\n",stderr);release(s);free(s);apx_json_reset();return 64;}
    apx_json_reset();struct sigaction action={0};sigemptyset(&action.sa_mask);action.sa_handler=stop_signal;sigaction(SIGTERM,&action,NULL);sigaction(SIGINT,&action,NULL);signal(SIGPIPE,SIG_IGN);
    sigset_t child_mask;sigemptyset(&child_mask);sigaddset(&child_mask,SIGCHLD);
    if(sigprocmask(SIG_BLOCK,&child_mask,&s->previous_mask)){release(s);free(s);return 70;}s->mask_set=1;
    s->signal_fd=signalfd(-1,&child_mask,SFD_CLOEXEC|SFD_NONBLOCK);if(s->signal_fd<0){release(s);free(s);return 70;}
    if(prctl(PR_SET_CHILD_SUBREAPER,1)){release(s);free(s);return 70;}
    int in_flags=fcntl(0,F_GETFL),out_flags=fcntl(1,F_GETFL);if(in_flags<0||out_flags<0||fcntl(0,F_SETFL,in_flags|O_NONBLOCK)||fcntl(1,F_SETFL,out_flags|O_NONBLOCK)){release(s);free(s);return 70;}
    char *incoming=alloc(FRAME+1);size_t used=0;int done=0,code=0;
    while(!done&&!stopped&&!s->failed){
        harvest(s,0);
        if(s->failed)break;
        struct pollfd watch[3+MAX_JOBS];watch[0]=(struct pollfd){0,POLLIN,0};watch[1]=(struct pollfd){1,s->outgoing.n>s->sent?POLLOUT:0,0};watch[2]=(struct pollfd){s->signal_fd,POLLIN,0};
        for(size_t i=0;i<s->concurrency;i++)watch[i+3]=(struct pollfd){s->jobs[i].pid?s->jobs[i].fd:-1,POLLIN,0};
        int ready=poll(watch,(nfds_t)(3+s->concurrency),wait_timeout(s));if(ready<0){if(errno==EINTR)continue;code=2;break;}
        if(watch[2].revents&POLLIN)child_notifications(s->signal_fd);
        if(watch[1].revents&(POLLHUP|POLLERR|POLLNVAL)){code=2;break;}
        if(watch[1].revents&POLLOUT){ssize_t n=write(1,s->outgoing.s+s->sent,s->outgoing.n-s->sent);if(n>0){s->sent+=(size_t)n;if(s->sent==s->outgoing.n)s->sent=s->outgoing.n=0;}else if(n<0&&errno!=EINTR&&errno!=EAGAIN){code=2;break;}}
        if(watch[0].revents&(POLLIN|POLLHUP|POLLERR)){
            char block[8192];ssize_t n=read(0,block,sizeof block);
            if(!n){done=1;if(used)code=2;}else if(n<0){if(errno!=EAGAIN&&errno!=EINTR){done=1;code=2;}}else for(ssize_t i=0;i<n;i++){
                unsigned char ch=(unsigned char)block[i];if(!ch){done=1;code=2;break;}
                if(ch=='\n'){incoming[used]=0;request(s,incoming,used);used=0;if(s->failed||stopped)break;}
                else if(used==FRAME){done=1;code=2;break;}else incoming[used++]=(char)ch;
            }
        }
    }
    if(s->failed)code=2;
    while(running(s)){harvest(s,1);if(running(s)){struct pollfd p={s->signal_fd,POLLIN,0};if(poll(&p,1,-1)>0)child_notifications(s->signal_fd);}}
    /* EOF is not an invitation to execute queued work. Already-built protocol
     * replies may drain, bounded by one second even for a nonreading peer. */
    int64_t flush_until=apx_http_now()+1000;
    while(!stopped&&!code&&s->outgoing.n>s->sent&&apx_http_now()<flush_until){struct pollfd p={1,POLLOUT,0};if(poll(&p,1,10)>0&&(p.revents&POLLOUT)){ssize_t n=write(1,s->outgoing.s+s->sent,s->outgoing.n-s->sent);if(n>0)s->sent+=(size_t)n;else if(n<0&&errno!=EAGAIN&&errno!=EINTR){code=2;break;}}}
    if(!stopped&&s->outgoing.n>s->sent)code=2;
    fcntl(0,F_SETFL,in_flags);fcntl(1,F_SETFL,out_flags);free(incoming);release(s);free(s);apx_json_reset();return stopped?128+stopped:code;
}
