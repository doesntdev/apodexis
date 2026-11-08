#define _GNU_SOURCE
#include "apx_host.h"
#include "apx_http.h"
#include "apx_pqc.h"
#include <sys/mman.h>
#include <sys/resource.h>
#include <strings.h>
#include <sys/prctl.h>
#include "../runtime/apx_runtime.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#define HOST_BYTES 1048576u
#define HOST_FRAME (8u*HOST_BYTES)
#define HOST_GRANTS 64u
struct HttpGrant {ApxHttpUrl origin;char *prefix;char *methods[7],*headers[APX_HTTP_HEADERS];size_t method_n,header_n;};
struct CryptoKey {char *name;int public_fd,private_fd;};
struct HttpCredential {size_t grant;char *header,*value;};
struct ApxHostPolicy {struct HttpCredential credentials[16];size_t credential_n;int serving;struct CryptoKey keys[16];size_t key_n;unsigned crypto_ms;int read_root,write_root,clock,random,v2,pq_required;unsigned requests,max_requests,timeout_ms;size_t max_bytes;char *env_names[HOST_GRANTS],*env_values[HOST_GRANTS],*urls[HOST_GRANTS];size_t env_n,url_n;struct HttpGrant grants[HOST_GRANTS];size_t grant_n;char *ca;int64_t deadline;pid_t group;int pidfd,client_fd;volatile sig_atomic_t *timeout,*shutdown;};
static volatile sig_atomic_t *cancel_timeout,*cancel_shutdown;
void apx_host_cancel_signals(volatile sig_atomic_t *timeout,volatile sig_atomic_t *shutdown){cancel_timeout=timeout;cancel_shutdown=shutdown;}
static char plain_helper[PATH_MAX]=".build/tools/apx-http",tls_helper[PATH_MAX]=".build/tools/apx-http-tls";
void apx_host_helpers(const char *plain,const char *tls){snprintf(plain_helper,sizeof plain_helper,"%s",plain);snprintf(tls_helper,sizeof tls_helper,"%s",tls);}
void apx_host_execution(ApxHostPolicy *p,int64_t deadline,pid_t group,int pidfd,volatile sig_atomic_t *timeout,volatile sig_atomic_t *shutdown){if(p){p->deadline=deadline;p->group=group;p->pidfd=pidfd;p->timeout=timeout;p->shutdown=shutdown;}}
static char crypto_helper[PATH_MAX]=".build/tools/apx-pqc";
void apx_host_pqc_helper(const char *s){snprintf(crypto_helper,sizeof crypto_helper,"%s",s);}
static char *crypto_call(ApxHostPolicy *,const char *,const char *,int);
static int keys_load(ApxHostPolicy *,ApxValue *);
static char *crypto_request(ApxHostPolicy *,const char *,int);
static char *helper(ApxHostPolicy *p,const char *request,int tls,int legacy,int validate_ca);
static int grants_load(ApxHostPolicy *p,ApxValue *grants);
static int ca_load(ApxHostPolicy *p,const char *path);
static int text_utf8(const char *s,size_t n);
static int credentials_load(ApxHostPolicy *p,const char *raw);
static int same_directory(int a,int b);
static char *dup_text(const char *s){if(!s)return NULL;size_t n=strlen(s)+1;char *p=malloc(n);if(p)memcpy(p,s,n);return p;}
static char *failure(const char *error){ApxValue *v=apx_record_new("host",2);apx_record_put(v,0,"ok",apx_bool(0));apx_record_put(v,1,"error",apx_string(error,(int64_t)strlen(error)));return apx_json_encode(v);}
static char *success(ApxValue *value){ApxValue *v=apx_record_new("host",2);apx_record_put(v,0,"ok",apx_bool(1));apx_record_put(v,1,"value",value);return apx_json_encode(v);}
static const char *string(ApxValue *v){const char *s=apx_json_string(v);return s&&strlen(s)==apx_json_length(v)?s:NULL;}
static int flag(ApxValue *v,int *out){if(!v){*out=0;return 1;}if(apx_json_kind(v)!=1)return 0;*out=apx_json_integer(v)!=0;return 1;}
static int env_name(const char *s){if(!s||!*s||strlen(s)>128)return 0;for(;*s;s++)if(!((*s>='A'&&*s<='Z')||(*s>='a'&&*s<='z')||(*s>='0'&&*s<='9')||*s=='_'))return 0;return 1;}
static int safe_url(const char *s){if(!s||strlen(s)>8192||(strncmp(s,"https://",8)&&strncmp(s,"http://",7)))return 0;const char *authority=(strchr)(s,':')+3,*end=(strpbrk)(authority,"/?#");if(!end)end=s+strlen(s);if(end==authority||(memchr)(authority,'@',(size_t)(end-authority)))return 0;for(const unsigned char *p=(const unsigned char*)s;*p;p++)if(*p<33||*p>126||*p=='\\')return 0;return 1;}
void apx_host_policy_free(ApxHostPolicy *p){if(!p)return;if(p->read_root>=0)close(p->read_root);if(p->write_root>=0)close(p->write_root);for(size_t i=0;i<p->env_n;i++){free(p->env_names[i]);free(p->env_values[i]);}for(size_t i=0;i<p->url_n;i++)free(p->urls[i]);for(size_t i=0;i<p->grant_n;i++){struct HttpGrant *g=&p->grants[i];free(g->prefix);for(size_t j=0;j<g->method_n;j++)free(g->methods[j]);for(size_t j=0;j<g->header_n;j++)free(g->headers[j]);}for(size_t i=0;i<16;i++){free(p->keys[i].name);if(p->keys[i].public_fd>=0)close(p->keys[i].public_fd);if(p->keys[i].private_fd>=0)close(p->keys[i].private_fd);}for(size_t i=0;i<p->credential_n;i++){free(p->credentials[i].header);if(p->credentials[i].value){explicit_bzero(p->credentials[i].value,strlen(p->credentials[i].value));free(p->credentials[i].value);}}free(p->ca);free(p);}
static ApxHostPolicy *policy_load(const char *path,int serving,char **error){*error=NULL;int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);struct stat st;if(fd<0||fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<0||st.st_size>65536){if(fd>=0)close(fd);*error=dup_text("invalid host policy file");return NULL;}size_t n=(size_t)st.st_size;char *raw=calloc(1,n+1);if(!raw){close(fd);*error=dup_text("host policy allocation failed");return NULL;}size_t at=0;while(at<n){ssize_t k=read(fd,raw+at,n-at);if(k<0&&errno==EINTR)continue;if(k<=0)break;at+=(size_t)k;}close(fd);ApxValue *v=at==n&&!(memchr)(raw,0,n)?apx_json_parse(raw,n):NULL;free(raw);ApxHostPolicy *p=calloc(1,sizeof *p);if(!p){*error=dup_text("host policy allocation failed");return NULL;}for(size_t i=0;i<16;i++)p->keys[i].public_fd=p->keys[i].private_fd=-1;p->serving=serving;p->crypto_ms=5000;p->read_root=p->write_root=p->pidfd=p->client_fd=-1;p->deadline=INT64_MAX;p->timeout=cancel_timeout;p->shutdown=cancel_shutdown;p->timeout_ms=5000;p->max_bytes=HOST_BYTES;p->max_requests=64;const char *bad=NULL;
 if(apx_json_kind(v)!=6)bad="host policy must be an object";
 const char *schema=string(apx_json_get(v,"schema"));int v3=schema&&!strcmp(schema,"apodexis.host_policy.v3");p->v2=v3||(schema&&!strcmp(schema,"apodexis.host_policy.v2"));p->pq_required=v3;
 const char *allowed[]={"schema","read_root","write_root","env","clock","random","http_get","max_bytes","max_requests","http","http_timeout_ms","tls_ca_file","tls_security","crypto_keys","crypto_timeout_ms"};
 unsigned seen=0;for(size_t i=0;v&&i<apx_json_length(v)&&!bad;i++){const char *key=apx_json_key(v,i);int known=0;for(size_t j=0;j<(v3?15:p->v2?12:9);j++)if(key&&!strcmp(key,allowed[j])){if(p->v2&&(!apx_json_get(v,allowed[j])||(seen&(1u<<j))))break;seen|=1u<<j;known=1;}if(!known)bad="unknown host policy field";}
 if(!schema||(!p->v2&&strcmp(schema,"apodexis.host_policy.v1")))bad="invalid host policy schema";
 if(serving&&(apx_json_get(v,"crypto_keys")||apx_json_get(v,"crypto_timeout_ms")))bad="application crypto grants unavailable in serving mode";
 if(v3){ApxValue *mode=apx_json_get(v,"tls_security");if(mode){const char *s=string(mode);if(!s||(strcmp(s,"pq_required")&&strcmp(s,"standard")))bad="invalid TLS security mode";else p->pq_required=!strcmp(s,"pq_required");}}
 if(!flag(apx_json_get(v,"clock"),&p->clock)||!flag(apx_json_get(v,"random"),&p->random))bad="host policy flags must be bool";
 const char *rootnames[]={"read_root","write_root"};int *roots[]={&p->read_root,&p->write_root};for(size_t i=0;i<2&&!bad;i++){ApxValue *rv=apx_json_get(v,rootnames[i]);if(!rv||apx_json_kind(rv)==0)continue;const char *name=string(rv);if(!name||name[0]!='/')bad="host roots must be absolute directories";else if((*roots[i]=open(name,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW))<0)bad="host root unavailable or symlink";}
 ApxValue *max=apx_json_get(v,"max_bytes");uint64_t limit;if(max){if(!apx_json_unsigned(max,&limit)||!limit||limit>HOST_BYTES)bad="invalid host max_bytes";else p->max_bytes=(size_t)limit;}max=apx_json_get(v,"max_requests");if(max){if(!apx_json_unsigned(max,&limit)||!limit||limit>1024)bad="invalid host max_requests";else p->max_requests=(unsigned)limit;}
 ApxValue *env=apx_json_get(v,"env"),*urls=apx_json_get(v,"http_get");if(env&&(apx_json_kind(env)!=5||apx_json_length(env)>HOST_GRANTS))bad="invalid environment grants";if(urls&&(apx_json_kind(urls)!=5||apx_json_length(urls)>HOST_GRANTS))bad="invalid HTTP grants";
 for(size_t i=0;env&&!bad&&i<apx_json_length(env);i++){const char *key=string(apx_json_item(env,i));if(!env_name(key)){bad="invalid environment grant name";break;}for(size_t j=0;j<p->env_n;j++)if(!strcmp(p->env_names[j],key))bad="duplicate environment grant";if(bad)break;const char *value=getenv(key);if(value&&(strlen(value)>p->max_bytes||!text_utf8(value,strlen(value)))){bad="environment grant value too large or invalid UTF-8";break;}p->env_names[p->env_n]=dup_text(key);p->env_values[p->env_n]=dup_text(value);if(!p->env_names[p->env_n]||(value&&!p->env_values[p->env_n]))bad="host policy allocation failed";p->env_n++;}
 for(size_t i=0;urls&&!bad&&i<apx_json_length(urls);i++){const char *url=string(apx_json_item(urls,i));if(!safe_url(url)){bad="invalid HTTP grant URL";break;}if(p->pq_required&&strncmp(url,"https://",8)){bad="required PQ forbids plaintext HTTP grants";break;}for(size_t j=0;j<p->url_n;j++)if(!strcmp(p->urls[j],url))bad="duplicate HTTP grant";if(bad)break;p->urls[p->url_n]=dup_text(url);if(!p->urls[p->url_n])bad="host policy allocation failed";p->url_n++;}
 if(p->v2&&!bad){ApxValue *time=apx_json_get(v,"http_timeout_ms");if(time){if(!apx_json_unsigned(time,&limit)||!limit||limit>5000)bad="invalid HTTP timeout";else p->timeout_ms=(unsigned)limit;}
 if(!bad&&!grants_load(p,apx_json_get(v,"http")))bad="invalid HTTP v2 grants";
 ApxValue *ca=apx_json_get(v,"tls_ca_file");if(ca&&!bad){const char *file=string(ca);if(!file||!ca_load(p,file))bad="invalid HTTP CA snapshot";}}
 if(v3&&!bad){ApxValue *time=apx_json_get(v,"crypto_timeout_ms");if(time){if(!apx_json_unsigned(time,&limit)||!limit||limit>5000)bad="invalid crypto timeout";else p->crypto_ms=(unsigned)limit;}if(!bad&&!keys_load(p,apx_json_get(v,"crypto_keys")))bad=access(crypto_helper,X_OK)?"pqc_unavailable":"pqc_key_unavailable";}
 if(bad){*error=dup_text(bad);apx_host_policy_free(p);return NULL;}return p;}
ApxHostPolicy *apx_host_policy_load(const char *path,char **error){return policy_load(path,0,error);}
ApxHostPolicy *apx_host_policy_load_serving(const char *path,const char *raw,char **error){
 ApxHostPolicy *p=policy_load(path,1,error);if(!p)return NULL;
 if(!credentials_load(p,raw)){*error=dup_text("invalid private HTTP credential binding");apx_host_policy_free(p);return NULL;}
 return p;
}
static int parent_dir(int root,const char *path,char leaf[NAME_MAX+1]){if(root<0||!path||!*path||*path=='/'||strlen(path)>4096)return -1;char *copy=dup_text(path);if(!copy)return -1;int fd=dup(root);char *part=copy;
 for(;;){char *slash=(strchr)(part,'/');if(slash)*slash=0;size_t n=strlen(part);if(!n||n>NAME_MAX||!strcmp(part,".")||!strcmp(part,"..")){close(fd);free(copy);return -1;}if(!slash){memcpy(leaf,part,n+1);free(copy);return fd;}int next=openat(fd,part,O_RDONLY|O_DIRECTORY|O_CLOEXEC|O_NOFOLLOW);close(fd);if(next<0){free(copy);return -1;}fd=next;part=slash+1;}}
static int text_utf8(const char *s,size_t n){for(size_t i=0;i<n;){unsigned char c=(unsigned char)s[i++];if(c<128)continue;unsigned value,min;size_t more;if(c>=0xc2&&c<=0xdf){value=c&31;min=128;more=1;}else if(c>=0xe0&&c<=0xef){value=c&15;min=2048;more=2;}else if(c>=0xf0&&c<=0xf4){value=c&7;min=65536;more=3;}else return 0;if(more>n-i)return 0;while(more--){c=(unsigned char)s[i++];if((c&0xc0)!=0x80)return 0;value=(value<<6)|(c&63);}if(value<min||value>0x10ffff||(value>=0xd800&&value<=0xdfff))return 0;}return 1;}
static char *read_file(ApxHostPolicy *p,const char *path){char name[NAME_MAX+1];int dir=parent_dir(p->read_root,path,name);if(dir<0)return failure("filesystem_read_denied");int fd=openat(dir,name,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);close(dir);struct stat st;if(fd<0||fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_nlink!=1||st.st_size<0||(uint64_t)st.st_size>p->max_bytes){if(fd>=0)close(fd);return failure("filesystem_read_failed");}char *buf=malloc(p->max_bytes+1);if(!buf){close(fd);return failure("host_storage_limit");}size_t n=0;while(n<=p->max_bytes){ssize_t got=read(fd,buf+n,p->max_bytes+1-n);if(got<0&&errno==EINTR)continue;if(got<0){n=p->max_bytes+1;break;}if(!got)break;n+=(size_t)got;}close(fd);char *out=n>p->max_bytes?failure("host_byte_limit"):!text_utf8(buf,n)?failure("filesystem_non_utf8"):success(apx_string(buf,(int64_t)n));free(buf);return out;}
static char *write_file(ApxHostPolicy *p,const char *path,ApxValue *data){const char *bytes=apx_json_string(data);size_t n=apx_json_length(data);if(!bytes||n>p->max_bytes)return failure("host_byte_limit");char name[NAME_MAX+1];int dir=parent_dir(p->write_root,path,name);if(dir<0)return failure("filesystem_write_denied");struct stat st;if(!fstatat(dir,name,&st,AT_SYMLINK_NOFOLLOW)&&(!S_ISREG(st.st_mode)||st.st_nlink!=1)){close(dir);return failure("filesystem_write_denied");}char temp[80];int fd=-1;for(unsigned i=0;i<32&&fd<0;i++){snprintf(temp,sizeof temp,".apx-write-%ld-%u-%u",(long)getpid(),p->requests,i);fd=openat(dir,temp,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);}if(fd<0){close(dir);return failure("filesystem_write_failed");}size_t at=0;while(at<n){ssize_t k=write(fd,bytes+at,n-at);if(k<0&&errno==EINTR)continue;if(k<=0)break;at+=(size_t)k;}int ok=at==n&&!fsync(fd);if(close(fd))ok=0;if(ok&&renameat(dir,temp,dir,name))ok=0;if(!ok)unlinkat(dir,temp,0);else if(fsync(dir))ok=0;close(dir);return ok?success(apx_int((int64_t)n)):failure("filesystem_write_failed");}
static int origin_equal(const ApxHttpUrl *a,const ApxHttpUrl *b){return a->tls==b->tls&&a->port==b->port&&!strcasecmp(a->host,b->host);}
static int member(char *const *items,size_t n,const char *s){for(size_t i=0;i<n;i++)if(!strcasecmp(items[i],s))return 1;return 0;}
static int grants_load(ApxHostPolicy *p,ApxValue *v){
 if(!v)return 1;
 if(apx_json_kind(v)!=5||apx_json_length(v)>HOST_GRANTS)return 0;
 static const char *const keys[]={"origin","path_prefix","methods","headers"};
 for(size_t i=0;i<apx_json_length(v);i++){ApxValue *g=apx_json_item(v,i);if(!apx_http_object(g,keys,4))return 0;
 const char *origin=string(apx_json_get(g,"origin")),*prefix=string(apx_json_get(g,"path_prefix"));
 struct HttpGrant *out=&p->grants[p->grant_n++];if(!origin||strpbrk(origin+(!strncmp(origin,"https://",8)?8:!strncmp(origin,"http://",7)?7:0),"/?#")||!apx_http_url(origin,1,&out->origin)||!apx_http_prefix(prefix))return 0;
 if(p->pq_required&&!out->origin.tls)return 0;
 out->prefix=dup_text(prefix);if(!out->prefix)return 0;
 ApxValue *methods=apx_json_get(g,"methods"),*headers=apx_json_get(g,"headers");if(apx_json_kind(methods)!=5||apx_json_length(methods)>7||apx_json_kind(headers)!=5||apx_json_length(headers)>APX_HTTP_HEADERS)return 0;
 for(size_t j=0;j<apx_json_length(methods);j++){const char *s=string(apx_json_item(methods,j));if(!apx_http_method(s)||member(out->methods,out->method_n,s))return 0;char *copy=dup_text(s);if(!copy)return 0;out->methods[out->method_n++]=copy;}
 for(size_t j=0;j<apx_json_length(headers);j++){const char *s=string(apx_json_item(headers,j));if(!apx_http_token(s)||strlen(s)>APX_HTTP_LINE||apx_http_reserved(s)||member(out->headers,out->header_n,s))return 0;char *copy=dup_text(s);if(!copy)return 0;out->headers[out->header_n++]=copy;}
 for(size_t j=0;j<i;j++){struct HttpGrant *prior=&p->grants[j];if(!origin_equal(&prior->origin,&out->origin)||strcmp(prior->prefix,out->prefix)||prior->method_n!=out->method_n||prior->header_n!=out->header_n)continue;int equal=1;for(size_t k=0;k<out->method_n;k++)if(!member(prior->methods,prior->method_n,out->methods[k]))equal=0;for(size_t k=0;k<out->header_n;k++)if(!member(prior->headers,prior->header_n,out->headers[k]))equal=0;if(equal)return 0;}}
 return 1;
}
/* Operator credential intake is independent of application-key support. Only
 * the trusted broker keeps this private snapshot; workers receive no secret fd. */
static char *credential_file(ApxHostPolicy *p,const char *path){
    if(!path||path[0]!='/'||strlen(path)>=PATH_MAX)return NULL;
    int dir=open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC),fd=-1;char *data=NULL;size_t n=0;
    if(dir<0)return NULL;
    const char *part=path+1;
    for(;;){
        if(same_directory(dir,p->read_root)||same_directory(dir,p->write_root))goto invalid;
        const char *slash=strchr(part,'/');size_t length=slash?(size_t)(slash-part):strlen(part);char name[NAME_MAX+1];
        if(!length||length>NAME_MAX||(length==1&&part[0]=='.')||(length==2&&!memcmp(part,"..",2)))goto invalid;
        memcpy(name,part,length);name[length]=0;
        if(!slash){fd=openat(dir,name,O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW);break;}
        int next=openat(dir,name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);close(dir);dir=next;if(dir<0)goto invalid;part=slash+1;
    }
    struct stat st;
    if(fd<0||fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_nlink!=1||st.st_uid!=geteuid()||((st.st_mode&07777)!=0400&&(st.st_mode&07777)!=0600)||st.st_size<=0||st.st_size>4096)goto invalid;
    n=(size_t)st.st_size;data=calloc(1,n+1);if(!data)goto invalid;
    size_t at=0;while(at<n){ssize_t k=read(fd,data+at,n-at);if(k<0&&errno==EINTR)continue;if(k<=0)goto invalid;at+=(size_t)k;}
    unsigned char extra;ssize_t tail;do{tail=read(fd,&extra,1);}while(tail<0&&errno==EINTR);if(tail!=0)goto invalid;
    /* Exact ASCII field bytes; refuse trim-sensitive edges instead of silently
     * changing a token when the ordinary HTTP parser removes optional space. */
    for(size_t i=0;i<n;i++)if((unsigned char)data[i]<32||(unsigned char)data[i]>126)goto invalid;
    if(data[0]==' '||data[n-1]==' ')goto invalid;
    for(size_t i=0;i<p->env_n;i++)if(p->env_values[i]&&strstr(p->env_values[i],data))goto invalid;
    close(fd);close(dir);return data;
invalid:
    if(data){explicit_bzero(data,n+1);free(data);}if(fd>=0)close(fd);if(dir>=0)close(dir);return NULL;
}
static int credentials_load(ApxHostPolicy *p,const char *raw){
    if(!raw)return 1;
    if(strlen(raw)>65536)return 0;
    ApxValue *all=apx_json_parse(raw,strlen(raw));if(apx_json_kind(all)!=5||apx_json_length(all)>16)return 0;
    const char *const fields[]={"origin","path_prefix","methods","header","secret_file","prefix"};
    for(size_t i=0;i<apx_json_length(all);i++){
        ApxValue *v=apx_json_item(all,i);if(!apx_http_object(v,fields,6))return 0;
        const char *origin=string(apx_json_get(v,"origin")),*prefix=string(apx_json_get(v,"path_prefix")),*name=string(apx_json_get(v,"header")),*file=string(apx_json_get(v,"secret_file")),*leader=string(apx_json_get(v,"prefix"));
        ApxHttpUrl url;size_t scheme=origin&&!strncmp(origin,"https://",8)?8:origin&&!strncmp(origin,"http://",7)?7:0;
        if(!scheme||strpbrk(origin+scheme,"/?#")||!apx_http_url(origin,1,&url)||!apx_http_prefix(prefix)||!apx_http_token(name)||strlen(name)>128||apx_http_reserved(name)||!leader||strlen(leader)>128||(leader[0]==' '))return 0;
        if(!url.tls&&strcmp(url.host,"127.0.0.1")&&strcmp(url.host,"::1"))return 0;
        for(size_t j=0;j<strlen(leader);j++)if((unsigned char)leader[j]<32||(unsigned char)leader[j]>126)return 0;
        ApxValue *methods=apx_json_get(v,"methods");size_t count=apx_json_length(methods);if(apx_json_kind(methods)!=5||!count||count>7)return 0;
        for(size_t j=0;j<count;j++){const char *method=string(apx_json_item(methods,j));if(!apx_http_method(method))return 0;for(size_t k=0;k<j;k++)if(!strcmp(method,string(apx_json_item(methods,k))))return 0;}
        size_t match=0,index=0;
        for(size_t j=0;j<p->grant_n;j++){
            struct HttpGrant *g=&p->grants[j];if(!origin_equal(&g->origin,&url)||strcmp(g->prefix,prefix)||g->method_n!=count)continue;
            int equal=1;for(size_t k=0;k<count;k++)if(!member(g->methods,g->method_n,string(apx_json_item(methods,k))))equal=0;
            if(equal){match++;index=j;}
        }
        if(match!=1)return 0;
        struct HttpGrant *g=&p->grants[index];
        for(size_t j=0;j<p->credential_n;j++){
            struct HttpCredential *old=&p->credentials[j];struct HttpGrant *prior=&p->grants[old->grant];
            if(strcasecmp(name,old->header)||!origin_equal(&prior->origin,&g->origin))continue;
            int overlap=!strncmp(prior->prefix,g->prefix,strlen(g->prefix))||!strncmp(g->prefix,prior->prefix,strlen(prior->prefix));
            if(overlap)for(size_t k=0;k<g->method_n;k++)if(member(prior->methods,prior->method_n,g->methods[k]))return 0;
        }
        char *secret=credential_file(p,file);if(!secret)return 0;
        struct HttpCredential *out=&p->credentials[p->credential_n++];out->grant=index;out->header=dup_text(name);
        size_t a=strlen(leader),b=strlen(secret);out->value=malloc(a+b+1);
        if(out->value){memcpy(out->value,leader,a);memcpy(out->value+a,secret,b+1);}
        explicit_bzero(secret,b);free(secret);if(!out->header||!out->value)return 0;
        for(char *c=out->header;*c;c++)if(*c>='A'&&*c<='Z')*c+=32;
    }
    return 1;
}
static int grant_scope(struct HttpGrant *g,const ApxHttpRequest *r){return origin_equal(&g->origin,&r->url)&&!strncmp(r->url.target,g->prefix,strlen(g->prefix))&&member(g->methods,g->method_n,r->method);}
static int grant_allows(struct HttpGrant *g,const ApxHttpRequest *r){if(!grant_scope(g,r))return 0;for(size_t i=0;i<r->count;i++)if(!member(g->headers,g->header_n,r->headers[i].name))return 0;return 1;}
static int host_wait(ApxHostPolicy *p,int fd,short events,int64_t deadline){for(;;){if((p->timeout&&*p->timeout)||(p->shutdown&&*p->shutdown))return 0;int64_t left=deadline-apx_http_now();if(left<=0)return 0;struct pollfd watch[3]={{fd,events,0},{p->pidfd,POLLIN,0},{p->client_fd,0,0}};int k=poll(watch,3,left>50?50:(int)left);if(k<0&&errno==EINTR)continue;if(k<0||watch[1].revents||watch[2].revents&(POLLHUP|POLLERR|POLLNVAL))return 0;if(k&&watch[0].revents&(events|POLLHUP|POLLERR))return 1;}}
static char *helper(ApxHostPolicy *p,const char *request,int tls,int legacy,int validate_ca){
 int64_t until=apx_http_now()+p->timeout_ms;if(p->deadline<until)until=p->deadline;
 const char *binary=tls?tls_helper:plain_helper;if(access(binary,X_OK))return failure(tls?"http_tls_unavailable":"http_transport_failed");
 ApxValue *v=apx_record_new("transport",7);apx_record_put(v,0,"request",apx_string(request,(int64_t)strlen(request)));apx_record_put(v,1,"max_bytes",apx_int((int64_t)p->max_bytes));apx_record_put(v,2,"deadline",apx_int(until));apx_record_put(v,3,"ca",apx_string(p->ca?p->ca:"",p->ca?(int64_t)strlen(p->ca):0));apx_record_put(v,4,"legacy",apx_bool(legacy));apx_record_put(v,5,"validate_ca",apx_bool(validate_ca));const char *security=p->pq_required?"pq_required":"standard";apx_record_put(v,6,"tls_security",apx_string(security,(int64_t)strlen(security)));char *wire=apx_json_encode(v);if(!wire||strlen(wire)+1>HOST_FRAME){free(wire);return failure("http_byte_limit");}
 int channel[2];if(socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,channel)){free(wire);return failure("http_transport_failed");}
 pid_t parent=getpid(),child=fork();if(child<0){close(channel[0]);close(channel[1]);free(wire);return failure("http_transport_failed");}
 if(!child){signal(SIGALRM,SIG_DFL);signal(SIGTERM,SIG_DFL);signal(SIGINT,SIG_DFL);sigset_t empty;sigemptyset(&empty);sigprocmask(SIG_SETMASK,&empty,NULL);if(prctl(PR_SET_PDEATHSIG,SIGKILL)||getppid()!=parent)_exit(125);if(p->group&&setpgid(0,p->group))_exit(125);if(dup2(channel[1],3)<0)_exit(125);if(close_range(4,~0u,0))_exit(125);int dev=open("/dev/null",O_RDWR);if(dev<0)_exit(125);for(int i=0;i<3;i++)if(dup2(dev,i)<0)_exit(125);if(dev>3)close(dev);char parent_text[32];snprintf(parent_text,sizeof parent_text,"%ld",(long)parent);char *const args[]={(char*)binary,parent_text,NULL};char *const env[]={"PATH=/usr/bin:/bin","LC_ALL=C",NULL};execve(binary,args,env);_exit(127);}
 close(channel[1]);if(p->group)setpgid(child,p->group);int flags=fcntl(channel[0],F_GETFL);fcntl(channel[0],F_SETFL,flags|O_NONBLOCK);size_t n=strlen(wire),at=0;int ok=1;
 while(at<n&&ok){if(!host_wait(p,channel[0],POLLOUT,until)){ok=0;break;}ssize_t k=send(channel[0],wire+at,n-at,MSG_NOSIGNAL);if(k<0&&(errno==EINTR||errno==EAGAIN))continue;if(k<=0){ok=0;break;}at+=(size_t)k;}free(wire);
 if(ok){if(!host_wait(p,channel[0],POLLOUT,until)||send(channel[0],"\n",1,MSG_NOSIGNAL)!=1)ok=0;}
 char *out=malloc(HOST_FRAME+1);n=0;int done=0;if(!out)ok=0;
 while(ok&&n<HOST_FRAME){if(!host_wait(p,channel[0],POLLIN,until)){ok=0;break;}ssize_t k=recv(channel[0],out+n,HOST_FRAME-n<4096?HOST_FRAME-n:4096,0);if(k<0&&(errno==EINTR||errno==EAGAIN))continue;if(k<=0)break;size_t end=n+(size_t)k;for(size_t i=n;i<end;i++)if(out[i]=='\n'){if(i+1!=end)ok=0;done=1;}n=end;if(done)break;}
 close(channel[0]);if(!ok||!done)kill(child,SIGKILL);int status=0;pid_t waited;
 for(;;){waited=waitpid(child,&status,WNOHANG);if(waited<0&&errno==EINTR)continue;if(waited!=0)break;if(apx_http_now()>=until||(p->timeout&&*p->timeout)||(p->shutdown&&*p->shutdown)){kill(child,SIGKILL);break;}poll(NULL,0,1);}
 if(!waited){while(waitpid(child,&status,0)<0&&errno==EINTR){}}else if(waited<0)ok=0;
 if(!ok||!done||!WIFEXITED(status)||WEXITSTATUS(status)){free(out);return failure(apx_http_now()>=until||(p->timeout&&*p->timeout)||(p->shutdown&&*p->shutdown)?"http_timeout":"http_transport_failed");}
 out[n-1]=0;ApxValue *response=apx_json_parse(out,n-1);static const char *const success_keys[]={"ok","value"},*const error_keys[]={"ok","error"};int good=apx_json_kind(apx_json_get(response,"ok"))==1&&(apx_json_integer(apx_json_get(response,"ok"))?apx_http_object(response,success_keys,2)&&string(apx_json_get(response,"value")):apx_http_object(response,error_keys,2)&&string(apx_json_get(response,"error")));if(!good){free(out);return failure("invalid_http_response");}return out;
}
static int ca_load(ApxHostPolicy *p,const char *path){if(path[0]!='/')return 0;int fd=open(path,O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);struct stat st;if(fd<0)return 0;if(fstat(fd,&st)||!S_ISREG(st.st_mode)||(st.st_uid!=geteuid()&&st.st_uid!=0)||(st.st_mode&022)||st.st_size<=0||st.st_size>65536){close(fd);return 0;}size_t n=(size_t)st.st_size;p->ca=calloc(1,n+1);if(!p->ca){close(fd);return 0;}size_t at=0;while(at<n){ssize_t k=read(fd,p->ca+at,n-at);if(k<0&&errno==EINTR)continue;if(k<=0)break;at+=(size_t)k;}close(fd);if(at!=n||memchr(p->ca,0,n)||!text_utf8(p->ca,n))return 0;char *r=helper(p,"{}",1,0,1);ApxValue *reply=r?apx_json_parse(r,strlen(r)):NULL;const char *error=string(apx_json_get(reply,"error"));int ok=error&&!strcmp(error,"ca_valid");free(r);return ok;}
static char *http_request(ApxHostPolicy *p,const char *raw){
    ApxValue *v=apx_http_parse_request(raw,strlen(raw));ApxHttpRequest req;
    const char *error=apx_http_request(v,p->max_bytes,0,&req);int allowed=0;
    if(!error&&p->v2)for(size_t i=0;i<p->grant_n;i++)if(grant_allows(&p->grants[i],&req))allowed=1;
    if(error||!allowed){apx_http_request_free(&req);return failure(error?error:"http_request_denied");}
    size_t selected[16],count=0;
    for(size_t i=0;i<p->credential_n;i++){
        struct HttpCredential *credential=&p->credentials[i];struct HttpGrant *grant=&p->grants[credential->grant];
        if(!grant_scope(grant,&req))continue;
        for(size_t j=0;j<req.count;j++)if(!strcasecmp(credential->header,req.headers[j].name)){apx_http_request_free(&req);return failure("http_credential_conflict");}
        if(!grant_allows(grant,&req)){apx_http_request_free(&req);return failure("http_request_denied");}
        selected[count++]=i;
    }
    int tls=req.url.tls;size_t original_count=req.count;apx_http_request_free(&req);
    if(!count)return helper(p,raw,tls,0,0);
    ApxValue *headers=apx_array_new((int64_t)(original_count+count)),*original=apx_json_get(v,"headers");
    for(size_t i=0;i<original_count;i++)apx_array_put(headers,(int64_t)i,apx_json_item(original,i));
    for(size_t i=0;i<count;i++){
        struct HttpCredential *credential=&p->credentials[selected[i]];ApxValue *header=apx_record_new("Header",2);
        apx_record_put(header,0,"name",apx_string(credential->header,(int64_t)strlen(credential->header)));
        apx_record_put(header,1,"value",apx_http_body((const unsigned char*)credential->value,strlen(credential->value)));
        apx_array_put(headers,(int64_t)(original_count+i),header);
    }
    ApxValue *request=apx_record_new("Request",4);apx_record_put(request,0,"method",apx_json_get(v,"method"));apx_record_put(request,1,"url",apx_json_get(v,"url"));apx_record_put(request,2,"headers",headers);apx_record_put(request,3,"body",apx_json_get(v,"body"));
    /* Reuse the production parser/complete emitted-head accounting after
     * injection, before any connection. Secret bytes are not a free quota. */
    error=apx_http_request(request,p->max_bytes,0,&req);apx_http_request_free(&req);if(error)return failure(error);
    char *wire=apx_json_encode(request);if(!wire)return failure("http_byte_limit");
    char *out=helper(p,wire,tls,0,0);explicit_bzero(wire,strlen(wire));free(wire);return out;
}
static char *http_get(ApxHostPolicy *p,const char *url){int allowed=0;for(size_t i=0;i<p->url_n;i++)if(!strcmp(p->urls[i],url))allowed=1;if(!allowed)return failure("http_get_denied");
 ApxHttpUrl parsed;if(!apx_http_url(url,0,&parsed))return failure("http_get_failed");ApxValue *req=apx_record_new("Request",4);apx_record_put(req,0,"method",apx_string("GET",3));apx_record_put(req,1,"url",apx_string(url,(int64_t)strlen(url)));apx_record_put(req,2,"headers",apx_array_new(0));apx_record_put(req,3,"body",apx_http_body((const unsigned char*)"",0));char *wire=apx_json_encode(req);if(!wire)return failure("http_get_failed");char *raw=helper(p,wire,parsed.tls,1,0);free(wire);ApxValue *result=raw?apx_json_parse(raw,strlen(raw)):NULL;const char *doc=string(apx_json_get(result,"value"));ApxValue *response=doc?apx_json_parse(doc,strlen(doc)):NULL;int status=(int)apx_json_integer(apx_json_get(response,"status"));ApxValue *body=apx_json_get(response,"body");const char *enc=string(apx_json_get(body,"encoding"));char *out=status<200||status>=300||!enc?failure("http_get_failed"):strcmp(enc,"utf8")?failure("http_non_utf8"):success(apx_json_get(body,"data"));free(raw);return out;}
char *apx_host_dispatch(ApxHostPolicy *p,const char *request,size_t length){if(!p)return failure("capability_denied");if(++p->requests>p->max_requests)return failure("host_request_limit");if(length>HOST_FRAME)return failure("host_frame_limit");ApxValue *v=apx_json_parse(request,length);const char *op=string(apx_json_get(v,"op"));ApxValue *args=apx_json_get(v,"args");if(apx_json_kind(v)!=6||apx_json_length(v)!=2||!op||apx_json_kind(args)!=5||apx_json_length(args)>2)return failure("invalid_host_request");size_t count=apx_json_length(args);ApxValue *a=apx_json_item(args,0);const char *s=string(a);
 if(!strcmp(op,"fs_read")&&count==1&&s)return read_file(p,s);
 if(!strcmp(op,"fs_write")&&count==2&&s)return write_file(p,s,apx_json_item(args,1));
 if(!strcmp(op,"env_get")&&count==1&&s){for(size_t i=0;i<p->env_n;i++)if(!strcmp(p->env_names[i],s))return p->env_values[i]?success(apx_string(p->env_values[i],(int64_t)strlen(p->env_values[i]))):failure("environment_missing");return failure("environment_denied");}
 if(!strcmp(op,"clock_ms")&&count==0){if(!p->clock)return failure("clock_denied");struct timespec t;if(clock_gettime(CLOCK_REALTIME,&t)||t.tv_sec>INT64_MAX/1000)return failure("clock_failed");return success(apx_int((int64_t)t.tv_sec*1000+t.tv_nsec/1000000));}
 if(!strcmp(op,"random_hex")&&count==1){if(!p->random)return failure("random_denied");uint64_t bytes;if(!apx_json_unsigned(a,&bytes)||bytes>256||bytes*2>p->max_bytes)return failure("random_size_limit");unsigned char data[256];size_t at=0;while(at<bytes){ssize_t got=getrandom(data+at,(size_t)bytes-at,0);if(got<0&&errno==EINTR)continue;if(got<=0)return failure("random_failed");at+=(size_t)got;}char hex[513];const char *digits="0123456789abcdef";for(size_t i=0;i<bytes;i++){hex[i*2]=digits[data[i]>>4];hex[i*2+1]=digits[data[i]&15];}return success(apx_string(hex,(int64_t)(bytes*2)));}
 if(!strcmp(op,"http_get")&&count==1&&s)return http_get(p,s);
 if(!strcmp(op,"http_request")&&count==1&&s)return http_request(p,s);
 if(p->serving&&(!strcmp(op,"pqc_seal")||!strcmp(op,"pqc_open")))return failure("capability_denied");
 if((!strcmp(op,"pqc_seal")||!strcmp(op,"pqc_open"))&&count==1&&s)return crypto_request(p,s,!strcmp(op,"pqc_open"));
 return failure("invalid_host_request");}
int apx_host_serve(ApxHostPolicy *p,int fd){apx_json_reset();char *request=malloc(HOST_FRAME+1);if(!request)return -1;p->client_fd=fd;size_t n=0;int done=0,result=-1;
 while(n<HOST_FRAME){if(!host_wait(p,fd,POLLIN,p->deadline))goto done;ssize_t got=recv(fd,request+n,HOST_FRAME-n<4096?HOST_FRAME-n:4096,MSG_DONTWAIT);if(got<0&&(errno==EINTR||errno==EAGAIN))continue;if(got<=0){result=!got&&!n?1:-1;goto done;}size_t end=n+(size_t)got;for(size_t i=n;i<end;i++)if(request[i]=='\n'){if(i+1!=end)goto done;done=1;}n=end;if(done)break;}
 if(done){char *response=apx_host_dispatch(p,request,n);if(!response)goto done;size_t size=strlen(response),at=0;while(at<size){if(!host_wait(p,fd,POLLOUT,p->deadline))break;ssize_t k=send(fd,response+at,size-at,MSG_NOSIGNAL|MSG_DONTWAIT);if(k<0&&(errno==EINTR||errno==EAGAIN))continue;if(k<=0)break;at+=(size_t)k;}result=at==size&&host_wait(p,fd,POLLOUT,p->deadline)&&send(fd,"\n",1,MSG_NOSIGNAL|MSG_DONTWAIT)==1?0:-1;free(response);}
 done:free(request);p->client_fd=-1;return result;
}
/* Crypto is isolated from the libc-only compiler and generated worker. The
 * broker alone owns key snapshots. One selected sealed fd crosses exec. */
static int same_directory(int a,int b){struct stat x,y;return a>=0&&b>=0&&!fstat(a,&x)&&!fstat(b,&y)&&x.st_dev==y.st_dev&&x.st_ino==y.st_ino;}
static int key_snapshot(ApxHostPolicy *p,const char *path,int private_key){
    if(!path||path[0]!='/'||strlen(path)>=PATH_MAX)return -1;
    int dir=open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(dir<0)return -1;
    const char *part=path+1;int fd=-1,snapshot=-1;unsigned char *data=NULL;size_t n=0;
    for(;;){
        if(private_key&&(same_directory(dir,p->read_root)||same_directory(dir,p->write_root)))goto done;
        const char *slash=strchr(part,'/');size_t len=slash?(size_t)(slash-part):strlen(part);char name[NAME_MAX+1];
        if(!len||len>NAME_MAX||(len==1&&part[0]=='.')||(len==2&&!memcmp(part,"..",2)))goto done;
        memcpy(name,part,len);name[len]=0;
        if(!slash){fd=openat(dir,name,O_RDONLY|O_NONBLOCK|O_CLOEXEC|O_NOFOLLOW);break;}
        int next=openat(dir,name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);close(dir);dir=next;if(dir<0)goto done;part=slash+1;
    }
    struct stat st;if(fd<0||fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_nlink!=1||st.st_size<=0||st.st_size>(private_key?32768:16384))goto done;
    if(private_key?(st.st_uid!=geteuid()||((st.st_mode&07777)!=0400&&(st.st_mode&07777)!=0600)):((st.st_uid!=geteuid()&&st.st_uid!=0)||(st.st_mode&022)))goto done;
    n=(size_t)st.st_size;data=malloc(n+1);if(!data)goto done;
    size_t at=0;while(at<n){ssize_t k=read(fd,data+at,n-at);if(k<0&&errno==EINTR)continue;if(k<=0)goto done;at+=(size_t)k;}
    unsigned char extra;if(read(fd,&extra,1)!=0||memchr(data,0,n))goto done;
    snapshot=memfd_create("apx-crypto-key",MFD_CLOEXEC|MFD_ALLOW_SEALING);if(snapshot<0)goto done;
    at=0;while(at<n){ssize_t k=write(snapshot,data+at,n-at);if(k<0&&errno==EINTR)continue;if(k<=0)goto invalid;at+=(size_t)k;}
    if(fcntl(snapshot,F_ADD_SEALS,F_SEAL_SEAL|F_SEAL_WRITE|F_SEAL_GROW|F_SEAL_SHRINK)<0)goto invalid;
    goto done;
invalid:close(snapshot);snapshot=-1;
done:if(data){explicit_bzero(data,n+1);free(data);}if(fd>=0)close(fd);if(dir>=0)close(dir);return snapshot;
}
static int keys_load(ApxHostPolicy *p,ApxValue *keys){
    if(!keys)return 1;
    if(apx_json_kind(keys)!=5||apx_json_length(keys)>16)return 0;
    for(size_t i=0;i<apx_json_length(keys);i++){
        ApxValue *v=apx_json_item(keys,i);if(apx_json_kind(v)!=6||apx_json_length(v)<2||apx_json_length(v)>3)return 0;
        unsigned seen=0;const char *const fields[]={"name","public_key_file","private_key_file"};
        for(size_t j=0;j<apx_json_length(v);j++){const char *field=apx_json_key(v,j);int found=0;for(unsigned k=0;k<3;k++)if(field&&!strcmp(field,fields[k])&&!(seen&(1u<<k))){seen|=1u<<k;found=1;break;}if(!found)return 0;}
        const char *name=string(apx_json_get(v,"name"));if(!(seen&1)||!apx_pqc_slot(name))return 0;
        for(size_t j=0;j<i;j++)if(!strcmp(name,p->keys[j].name))return 0;
        struct CryptoKey *key=&p->keys[p->key_n++];key->name=dup_text(name);if(!key->name)return 0;
        for(unsigned direction=0;direction<2;direction++){
            if(!(seen&(2u<<direction)))continue;
            int *snapshot=direction?&key->private_fd:&key->public_fd;
            const char *file=string(apx_json_get(v,fields[direction+1]));
            *snapshot=key_snapshot(p,file,(int)direction);if(*snapshot<0)return 0;
            char *out=crypto_call(p,direction?"validate-private":"validate-public","",*snapshot);
            ApxValue *response=out?apx_pqc_parse(out,strlen(out),16):NULL;const char *error=string(apx_json_get(response,"error"));int ok=error&&!strcmp(error,"pqc_key_valid");free(out);if(!ok)return 0;
        }
    }return 1;
}
static int crypto_error(const char *s){
    const char *const errors[]={"invalid_pqc_request","pqc_key_valid","pqc_key_unavailable","pqc_unavailable","pqc_encrypt_failed","pqc_decrypt_failed","pqc_byte_limit","pqc_timeout","pqc_keygen_failed"};
    for(size_t i=0;s&&i<sizeof errors/sizeof errors[0];i++)if(!strcmp(s,errors[i]))return 1;
    return 0;
}
static int crypto_response(ApxValue *v,const char *mode,size_t maximum){
    static const char *const good[]={"ok","value"},*const bad[]={"ok","error"};
    ApxValue *ok=apx_json_get(v,"ok");if(apx_json_kind(ok)!=1)return 0;
    if(!apx_json_integer(ok)){
        const char *error=string(apx_json_get(v,"error"));if(!apx_http_object(v,bad,2)||!crypto_error(error))return 0;
        if(!strcmp(error,"pqc_key_valid"))return !strcmp(mode,"validate-public")||!strcmp(mode,"validate-private");
        if(!strcmp(error,"pqc_keygen_failed"))return !strcmp(mode,"keygen");
        if(!strcmp(error,"pqc_encrypt_failed"))return !strcmp(mode,"seal");
        if(!strcmp(error,"pqc_decrypt_failed"))return !strcmp(mode,"open");
        return 1;
    }
    if(!apx_http_object(v,good,2))return 0;
    const char *doc=string(apx_json_get(v,"value"));ApxValue *value=doc?apx_pqc_parse(doc,strlen(doc),24):NULL;
    if(!strcmp(mode,"keygen")){
        const char *const fields[]={"schema","status","algorithm","public_key_file","private_key_file","key_id_sha256"};
        if(!apx_http_object(value,fields,6))return 0;
        const char *schema=string(apx_json_get(value,"schema")),*status=string(apx_json_get(value,"status")),*alg=string(apx_json_get(value,"algorithm")),*pub=string(apx_json_get(value,"public_key_file")),*priv=string(apx_json_get(value,"private_key_file")),*id=string(apx_json_get(value,"key_id_sha256"));
        if(!schema||strcmp(schema,"apodexis.pqc_keygen.v1")||!status||strcmp(status,"ok")||!alg||strcmp(alg,"ML-KEM-768")||!pub||pub[0]!='/'||!priv||priv[0]!='/'||!id||strlen(id)!=64)return 0;
        for(size_t i=0;i<64;i++)if(!strchr("0123456789abcdef",id[i]))return 0;
        return 1;
    }
    if(!strcmp(mode,"seal")){
        const char *const fields[]={"format","data"};if(!apx_http_object(value,fields,2))return 0;const char *format=string(apx_json_get(value,"format"));if(!format||strcmp(format,APX_PQC_FORMAT))return 0;
        ApxValue *body=apx_record_new("Body",2);apx_record_put(body,0,"encoding",apx_string("base64",6));apx_record_put(body,1,"data",apx_json_get(value,"data"));value=body;maximum+=4096;
    }else if(strcmp(mode,"open"))return 0;
    size_t size=0;unsigned char *bytes=apx_http_decode_body(value,maximum,&size);int valid=bytes!=NULL;if(bytes){explicit_bzero(bytes,size);free(bytes);}return valid;
}
static char *crypto_call(ApxHostPolicy *p,const char *mode,const char *request,int key){
    int64_t until=apx_http_now()+p->crypto_ms;if(p->deadline<until)until=p->deadline;
    if(access(crypto_helper,X_OK))return failure("pqc_unavailable");
    if(until<=apx_http_now())return failure("pqc_timeout");
    ApxValue *v=apx_record_new("crypto",4);apx_record_put(v,0,"mode",apx_string(mode,(int64_t)strlen(mode)));apx_record_put(v,1,"request",apx_string(request,(int64_t)strlen(request)));apx_record_put(v,2,"max_bytes",apx_int((int64_t)p->max_bytes));apx_record_put(v,3,"deadline",apx_int(until));char *wire=apx_json_encode(v);
    if(!wire||strlen(wire)+1>HOST_FRAME){free(wire);return failure("pqc_byte_limit");}
    int channel[2];if(socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,channel)){free(wire);return failure("pqc_unavailable");}
    pid_t parent=getpid(),child=fork();if(child<0){close(channel[0]);close(channel[1]);free(wire);return failure("pqc_unavailable");}
    if(!child){
        signal(SIGALRM,SIG_DFL);signal(SIGTERM,SIG_DFL);signal(SIGINT,SIG_DFL);sigset_t empty;sigemptyset(&empty);sigprocmask(SIG_SETMASK,&empty,NULL);
        if(prctl(PR_SET_PDEATHSIG,SIGKILL)||getppid()!=parent||(p->group&&setpgid(0,p->group)))_exit(125);
        struct rlimit core={0,0};if(setrlimit(RLIMIT_CORE,&core))_exit(125);
        /* Duplicate both above destinations first: either original may alias
         * FD3/FD4, particularly when a caller closed its standard streams. */
        int broker=fcntl(channel[1],F_DUPFD_CLOEXEC,5),selected=key>=0?fcntl(key,F_DUPFD_CLOEXEC,5):-1;
        if(broker<0||(key>=0&&selected<0)||dup2(broker,3)<0||(key>=0&&dup2(selected,4)<0))_exit(125);
        if(key<0)close(4);
        if(close_range(5,~0u,0))_exit(125);
        int dev=open("/dev/null",O_RDWR);if(dev<0)_exit(125);for(int i=0;i<3;i++)if(dup2(dev,i)<0)_exit(125);if(dev>4||(dev==4&&key<0))close(dev);
        char parent_text[32];snprintf(parent_text,sizeof parent_text,"%ld",(long)parent);char *const args[]={crypto_helper,parent_text,NULL};char *const env[]={"PATH=/usr/bin:/bin","LC_ALL=C",NULL};execve(crypto_helper,args,env);_exit(127);
    }
    close(channel[1]);if(p->group)setpgid(child,p->group);int flags=fcntl(channel[0],F_GETFL);fcntl(channel[0],F_SETFL,flags|O_NONBLOCK);
    size_t n=strlen(wire),at=0;int ok=1;
    while(at<n&&ok){if(!host_wait(p,channel[0],POLLOUT,until)){ok=0;break;}ssize_t k=send(channel[0],wire+at,n-at,MSG_NOSIGNAL);if(k<0&&(errno==EINTR||errno==EAGAIN))continue;if(k<=0){ok=0;break;}at+=(size_t)k;}free(wire);
    if(ok&&(!host_wait(p,channel[0],POLLOUT,until)||send(channel[0],"\n",1,MSG_NOSIGNAL)!=1))ok=0;
    char *out=malloc(HOST_FRAME+1);n=0;int done=0;if(!out)ok=0;
    /* Read through EOF, not just newline: reject trailing/split extra frames. */
    while(ok&&n<HOST_FRAME){if(!host_wait(p,channel[0],POLLIN,until)){ok=0;break;}ssize_t k=recv(channel[0],out+n,HOST_FRAME-n<4096?HOST_FRAME-n:4096,0);if(k<0&&(errno==EINTR||errno==EAGAIN))continue;if(k<0){ok=0;break;}if(!k){done=n>0&&out[n-1]=='\n';break;}n+=(size_t)k;}
    close(channel[0]);if(!ok||!done)kill(child,SIGKILL);
    int status=0;pid_t waited;
    for(;;){waited=waitpid(child,&status,WNOHANG);if(waited<0&&errno==EINTR)continue;if(waited)break;if(apx_http_now()>=until||(p->timeout&&*p->timeout)||(p->shutdown&&*p->shutdown)){ok=0;kill(child,SIGKILL);break;}poll(NULL,0,1);}
    if(!waited){while(waitpid(child,&status,0)<0&&errno==EINTR){}}else if(waited<0)ok=0;
    int timed=apx_http_now()>=until||(p->timeout&&*p->timeout)||(p->shutdown&&*p->shutdown);
    if(timed||!ok||!done||!WIFEXITED(status)||WEXITSTATUS(status)){free(out);return failure(timed?"pqc_timeout":"pqc_unavailable");}
    out[n-1]=0;ApxValue *response=apx_pqc_parse(out,n-1,16);
    if(!crypto_response(response,mode,p->max_bytes)){free(out);return failure("pqc_unavailable");}
    if(apx_http_now()>=until||(p->timeout&&*p->timeout)||(p->shutdown&&*p->shutdown)){free(out);return failure("pqc_timeout");}
    return out;
}
static char *crypto_request(ApxHostPolicy *p,const char *raw,int opening){
    int64_t saved=p->deadline,until=apx_http_now()+p->crypto_ms;if(until<p->deadline)p->deadline=until;
    ApxValue *request=apx_pqc_parse(raw,strlen(raw),24);const char *slot=NULL,*error=apx_pqc_request(request,opening,&slot);
    int selected=-1;if(!error)for(size_t i=0;i<p->key_n;i++)if(!strcmp(p->keys[i].name,slot))selected=opening?p->keys[i].private_fd:p->keys[i].public_fd;
    if(apx_http_now()>=p->deadline)error="pqc_timeout";
    char *out=error?failure(error):selected<0?failure("pqc_key_denied"):crypto_call(p,opening?"open":"seal",raw,selected);p->deadline=saved;return out;
}
char *apx_host_pqc_keygen(const char *directory){
    ApxHostPolicy p={0};p.crypto_ms=5000;p.max_bytes=HOST_BYTES;p.deadline=INT64_MAX;p.pidfd=p.client_fd=-1;p.timeout=cancel_timeout;p.shutdown=cancel_shutdown;
    char *raw=crypto_call(&p,"keygen",directory,-1);ApxValue *v=raw?apx_pqc_parse(raw,strlen(raw),16):NULL;const char *doc=string(apx_json_get(v,"value"));
    char *out=doc?dup_text(doc):NULL;
    if(!out){const char *why=string(apx_json_get(v,"error"));ApxValue *failure=apx_record_new("keygen",3);apx_record_put(failure,0,"schema",apx_string("apodexis.pqc_keygen.v1",sizeof("apodexis.pqc_keygen.v1")-1));apx_record_put(failure,1,"status",apx_string("error",5));const char *token=why&&!strcmp(why,"pqc_unavailable")?"pqc_unavailable":"pqc_keygen_failed";apx_record_put(failure,2,"error",apx_string(token,(int64_t)strlen(token)));out=apx_json_encode(failure);}free(raw);return out;
}
