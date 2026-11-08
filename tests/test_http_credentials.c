#define _GNU_SOURCE
#include "../compiler/apx_host.h"
#include "../compiler/apx_http.h"
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
/* A weak observation, not a product fallback: permits the same executable to
 * demonstrate missing-feature RED before the new public loader exists. */
extern ApxHostPolicy *apx_host_policy_load_serving(const char *,const char *,char **) __attribute__((weak));
static char directory[4096],policy_path[8192],secret_path[8192];
static const char secret[]="owned-auth-fixture-not-a-real-credential";
static unsigned checks,cases;
static pid_t peer;
static int listener_fd=-1;
static void require(int ok,const char *message){checks++;if(!ok){fprintf(stderr,"credential FAIL: %s\n",message);exit(1);}}
static void passed(const char *message){printf("credential case %u %s PASS\n",++cases,message);fflush(stdout);}
static void cleanup(void){
    if(peer>0){kill(peer,SIGKILL);while(waitpid(peer,NULL,0)<0&&errno==EINTR){}peer=0;}
    if(listener_fd>=0){close(listener_fd);listener_fd=-1;}
    if(*directory){DIR *d=opendir(directory);if(d){struct dirent *e;while((e=readdir(d))){if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;char p[8192];snprintf(p,sizeof p,"%s/%s",directory,e->d_name);unlink(p);}closedir(d);}rmdir(directory);}
}
static void save(const char *path,const char *bytes,mode_t mode){int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,0600);require(fd>=0,"create owned fixture");size_t n=strlen(bytes);require(write(fd,bytes,n)==(ssize_t)n&&!fchmod(fd,mode)&&!close(fd),"save exact fixture bytes/mode");}
static void policy(unsigned port,const char *extra){char raw[20000];snprintf(raw,sizeof raw,"{\"schema\":\"apodexis.host_policy.v2\",\"http_timeout_ms\":1000,\"http\":[{\"origin\":\"http://127.0.0.1:%u\",\"path_prefix\":\"/v1/\",\"methods\":[\"GET\"],\"headers\":[\"authorization\",\"x-pad\"]}]%s}",port,extra?extra:"");save(policy_path,raw,0600);}
static char *bindings(unsigned port,const char *file,const char *extra){char *raw=calloc(1,20000);require(raw!=NULL,"binding storage");snprintf(raw,20000,"[{\"origin\":\"http://127.0.0.1:%u\",\"path_prefix\":\"/v1/\",\"methods\":[\"GET\"],\"header\":\"authorization\",\"secret_file\":\"%s\",\"prefix\":\"Bearer \"%s}]",port,file,extra?extra:"");return raw;}
static ApxHostPolicy *load(const char *raw){char *error=NULL;ApxHostPolicy *p=apx_host_policy_load_serving(policy_path,raw,&error);if(!p)fprintf(stderr,"safe loader diagnostic: %s\n",error?error:"missing");require(p!=NULL,"valid serving policy and private credential accepted");free(error);return p;}
static void refused(const char *raw){char *error=NULL;ApxHostPolicy *p=apx_host_policy_load_serving(policy_path,raw,&error);require(!p&&error&&*error,"invalid operator binding refused with safe error");require(!strstr(error,secret)&&!strstr(error,secret_path),"error omits credential and operator path");free(error);}
static unsigned listener(void){listener_fd=socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC,0);require(listener_fd>=0,"owned HTTP listener");struct sockaddr_in a={.sin_family=AF_INET,.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};require(!bind(listener_fd,(struct sockaddr*)&a,sizeof a)&&!listen(listener_fd,8),"owned loopback bind");socklen_t n=sizeof a;require(!getsockname(listener_fd,(struct sockaddr*)&a,&n),"OS-assigned port");return ntohs(a.sin_port);}
static void begin_peer(size_t expected_bytes){fflush(NULL);peer=fork();require(peer>=0,"owned HTTP peer fork");if(!peer){alarm(5);for(unsigned attempt=0;attempt<16;attempt++){int c=accept4(listener_fd,NULL,NULL,SOCK_CLOEXEC);if(c<0)_exit(21);char *head=calloc(1,APX_HTTP_HEADER_BYTES+2);if(!head)_exit(22);size_t n=0;while(n<APX_HTTP_HEADER_BYTES+1){ssize_t k=read(c,head+n,APX_HTTP_HEADER_BYTES+1-n);if(k<=0)break;n+=(size_t)k;if(strstr(head,"\r\n\r\n"))break;}
        /* Foreign workstation monitor GET / is not an owned API request. */
        if(!strncmp(head,"GET / HTTP/1.1\r\n",16)){close(c);free(head);continue;}
        char wanted[256];snprintf(wanted,sizeof wanted,"\r\nauthorization: Bearer %s\r\n",secret);int good=!strncmp(head,"GET /v1/report HTTP/1.1\r\n",strlen("GET /v1/report HTTP/1.1\r\n"))&&strstr(head,wanted)&&(!expected_bytes||n==expected_bytes);free(head);const char reply[]="HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nok";if(!good){close(c);_exit(23);}if(write(c,reply,sizeof reply-1)!=(ssize_t)sizeof reply-1){close(c);_exit(24);}close(c);_exit(0);}_exit(25);}}
static void end_peer(void){int status;require(waitpid(peer,&status,0)==peer&&WIFEXITED(status)&&WEXITSTATUS(status)==0,"peer independently observed exact credential/request bytes");peer=0;}
static char *request(unsigned port,const char *method,const char *path,int conflict,size_t padding){
    char url[1024];snprintf(url,sizeof url,"http://127.0.0.1:%u%s",port,path);ApxValue *v=apx_record_new("Request",4);apx_record_put(v,0,"method",apx_string(method,(int64_t)strlen(method)));apx_record_put(v,1,"url",apx_string(url,(int64_t)strlen(url)));
    size_t count=conflict?1:padding?8:0;ApxValue *h=apx_array_new((int64_t)count);for(size_t i=0;i<count;i++){size_t n=conflict?8:(padding>8180?8180:padding);padding-=conflict?0:n;char *bytes=malloc(n+1);require(bytes!=NULL,"header fixture allocation");memset(bytes,'a',n);bytes[n]=0;ApxValue *item=apx_record_new("Header",2);const char *name=conflict?"Authorization":"x-pad";apx_record_put(item,0,"name",apx_string(name,(int64_t)strlen(name)));apx_record_put(item,1,"value",apx_http_body((unsigned char*)bytes,n));free(bytes);apx_array_put(h,(int64_t)i,item);}require(!padding,"padding fits bounded field inventory");apx_record_put(v,2,"headers",h);apx_record_put(v,3,"body",apx_http_body((const unsigned char*)"",0));char *s=apx_json_encode(v);require(s!=NULL,"request encoding");return s;
}
static char *dispatch(ApxHostPolicy *p,char *req){ApxValue *args=apx_array_new(1);apx_array_put(args,0,apx_string(req,(int64_t)strlen(req)));ApxValue *v=apx_record_new("host",2);apx_record_put(v,0,"op",apx_string("http_request",12));apx_record_put(v,1,"args",args);char *wire=apx_json_encode(v);require(wire!=NULL,"broker envelope");char *out=apx_host_dispatch(p,wire,strlen(wire));free(wire);free(req);require(out!=NULL,"bounded broker result");require(!strstr(out,secret),"normal result does not contain broker credential");return out;}
static void expect_ok(char *out){ApxValue *v=apx_json_parse(out,strlen(out));require(apx_json_integer(apx_json_get(v,"ok"))==1,"authorized HTTP succeeds");const char *text=apx_http_string(apx_json_get(v,"value"));ApxValue *r=text?apx_json_parse(text,strlen(text)):NULL;require(apx_json_integer(apx_json_get(r,"status"))==200,"actual HTTP status");const char *data=apx_http_string(apx_json_get(apx_json_get(r,"body"),"data"));require(data&&!strcmp(data,"ok"),"actual HTTP response body");free(out);}
static void expect_error(char *out,const char *expected){ApxValue *v=apx_json_parse(out,strlen(out));const char *e=apx_http_string(apx_json_get(v,"error"));if(!e||strcmp(e,expected))fprintf(stderr,"safe broker error mismatch, expected %s\n",expected);require(apx_json_integer(apx_json_get(v,"ok"))==0&&e&&!strcmp(e,expected),"specific no-send broker refusal");free(out);}
static void startup_guards(unsigned port){
    char *good=bindings(port,secret_path,NULL);ApxHostPolicy *p=load(good);apx_host_policy_free(p);passed("strict private snapshot positive control");
    const char *malformed[]={"{}","null","[{}]","[true]","[{\"origin\":false}]"};for(size_t i=0;i<sizeof malformed/sizeof malformed[0];i++)refused(malformed[i]);char *bad=bindings(port,secret_path,",\"unknown\":0");refused(bad);free(bad);passed("recursive malformed/unknown binding rejection");
    require(!chmod(secret_path,0644),"unsafe mode fixture");refused(good);require(!chmod(secret_path,0600),"restore private mode");save(secret_path,"embedded\nnewline",0600);refused(good);save(secret_path," leading-space",0600);refused(good);save(secret_path,"trailing-space ",0600);refused(good);save(secret_path,"",0600);refused(good);save(secret_path,secret,0600);passed("mode and exact header-byte guards");
    char *large=malloc(4098);require(large!=NULL,"private-file boundary fixture");memset(large,'a',4097);large[4096]=0;save(secret_path,large,0400);p=load(good);apx_host_policy_free(p);require(!chmod(secret_path,0600),"restore writable fixture mode");large[4096]='a';large[4097]=0;save(secret_path,large,0600);refused(good);free(large);save(secret_path,secret,0600);passed("0400 and exact/one-over private secret size");
    char alias[8192];snprintf(alias,sizeof alias,"%s/alias",directory);require(!symlink(secret_path,alias),"symlink fixture");bad=bindings(port,alias,NULL);refused(bad);free(bad);require(!unlink(alias)&&!link(secret_path,alias),"hardlink fixture");refused(good);require(!unlink(alias),"remove owned hardlink");passed("symlink and hardlink credential refusal");
    char extra[12000];snprintf(extra,sizeof extra,",\"read_root\":\"%s\"",directory);policy(port,extra);refused(good);snprintf(extra,sizeof extra,",\"write_root\":\"%s\"",directory);policy(port,extra);refused(good);require(!setenv("APX_OWNED_TOKEN",secret,1),"owned environment fixture");policy(port,",\"env\":[\"APX_OWNED_TOKEN\"]");refused(good);unsetenv("APX_OWNED_TOKEN");policy(port,NULL);passed("credential unavailable through file roots or granted environment");
    bad=bindings(port+1,secret_path,NULL);refused(bad);free(bad);char *dupe=calloc(1,strlen(good)*2+4);require(dupe!=NULL,"duplicate binding fixture");snprintf(dupe,strlen(good)*2+4,"[%.*s,%.*s]",(int)strlen(good)-2,good+1,(int)strlen(good)-2,good+1);refused(dupe);free(dupe);passed("wrong origin and duplicate binding refusal");
    save(policy_path,"{\"schema\":\"apodexis.host_policy.v3\",\"crypto_keys\":[]}",0600);refused(NULL);save(policy_path,"{\"schema\":\"apodexis.host_policy.v3\",\"crypto_timeout_ms\":1}",0600);refused(NULL);policy(port,NULL);passed("serving crypto activation stays excluded");free(good);
}
static void wire_guards(unsigned port){char *good=bindings(port,secret_path,NULL);ApxHostPolicy *p=load(good);begin_peer(0);expect_ok(dispatch(p,request(port,"GET","/v1/report",0,0)));end_peer();passed("real authorized HTTP receives broker header");
    save(secret_path,"mutated-after-startup",0600);begin_peer(0);expect_ok(dispatch(p,request(port,"GET","/v1/report",0,0)));end_peer();save(secret_path,secret,0600);passed("credential snapshot ignores subsequent file edits");
    expect_error(dispatch(p,request(port,"GET","/v1/report",1,0)),"http_credential_conflict");expect_error(dispatch(p,request(port,"POST","/v1/report",0,0)),"http_request_denied");expect_error(dispatch(p,request(port,"GET","/v10/report",0,0)),"http_request_denied");expect_error(dispatch(p,request(port+1,"GET","/v1/report",0,0)),"http_request_denied");passed("conflict/method/path/origin denied before transport");
    /* Independent complete wire-head oracle; eight ordinary fields use padding
     * with no trim-sensitive bytes. The managed head and injected field count. */
    char base[2048];int n=snprintf(base,sizeof base,"GET /v1/report HTTP/1.1\r\nHost: 127.0.0.1:%u\r\nContent-Length: 0\r\nConnection: close\r\nAccept-Encoding: identity\r\nauthorization: Bearer %s\r\n",port,secret);require(n>0,"wire budget oracle");size_t padding=APX_HTTP_HEADER_BYTES-(size_t)n-2-8*9;require(padding<=8*8180,"bounded padding fixture");
    begin_peer(APX_HTTP_HEADER_BYTES);expect_ok(dispatch(p,request(port,"GET","/v1/report",0,padding)));end_peer();expect_error(dispatch(p,request(port,"GET","/v1/report",0,padding+1)),"http_header_limit");begin_peer(0);expect_ok(dispatch(p,request(port,"GET","/v1/report",0,0)));end_peer();passed("injected bytes count at exact/over complete wire-head boundary and recover");
    const char *blocked[]={"{\"op\":\"pqc_seal\",\"args\":[\"{}\"]}","{\"op\":\"pqc_open\",\"args\":[\"{}\"]}"};
    for(size_t i=0;i<sizeof blocked/sizeof blocked[0];i++){expect_error(apx_host_dispatch(p,blocked[i],strlen(blocked[i])),"capability_denied");}
    passed("application key operations remain denied in a granted serving broker");
    apx_host_policy_free(p);free(good);
}
int main(void){signal(SIGPIPE,SIG_IGN);alarm(90);atexit(cleanup);require(apx_host_policy_load_serving!=NULL,"serving credential loader is implemented (missing-feature RED)");const char *tmp=getenv("TMPDIR");require(tmp!=NULL,"owned scratch");snprintf(directory,sizeof directory,"%s/apx-auth-XXXXXX",tmp);require(mkdtemp(directory)!=NULL,"owned fixture directory");snprintf(policy_path,sizeof policy_path,"%s/policy.json",directory);snprintf(secret_path,sizeof secret_path,"%s/secret",directory);save(secret_path,secret,0600);unsigned port=listener();policy(port,NULL);startup_guards(port);wire_guards(port);cleanup();apx_json_reset();printf("http credentials: %u case groups / %u checks PASS; private snapshots, exact scope, no caller override, real wire accounting\n",cases,checks);return 0;}
