#define _GNU_SOURCE
#include "apx_pqc.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>
int main(int argc,char **argv){
    if(argc!=2)return 125;
    char *end=NULL;long parent=strtol(argv[1],&end,10);
    if(!end||*end||parent<=1||parent>INT_MAX||prctl(PR_SET_PDEATHSIG,SIGKILL)||getppid()!=(pid_t)parent)return 125;
    struct rlimit core={0,0};if(setrlimit(RLIMIT_CORE,&core))return 125;
#ifndef APX_PQC_SANITIZE
    /* ASan reserves a large virtual shadow; only test builds omit AS. */
    struct rlimit memory={256u*1024u*1024u,256u*1024u*1024u};if(setrlimit(RLIMIT_AS,&memory))return 125;
#endif
    int64_t startup=apx_http_now()+5000;char *wire=malloc(APX_HTTP_FRAME+1);if(!wire)return 125;
    size_t n=0;int done=0;
    while(n<APX_HTTP_FRAME){if(!apx_http_wait(3,POLLIN,startup))break;ssize_t k=recv(3,wire+n,APX_HTTP_FRAME-n<4096?APX_HTTP_FRAME-n:4096,0);if(k<0&&errno==EINTR)continue;if(k<=0)break;size_t limit=n+(size_t)k;for(size_t i=n;i<limit;i++)if(wire[i]=='\n'){if(i+1!=limit){free(wire);return 125;}done=1;}n=limit;if(done)break;}
    if(!done){free(wire);return 125;}wire[n-1]=0;
    ApxValue *v=apx_pqc_parse(wire,n-1,24);free(wire);
    static const char *const fields[]={"mode","request","max_bytes","deadline"};
    const char *mode=apx_http_string(apx_json_get(v,"mode")),*raw=apx_http_string(apx_json_get(v,"request"));uint64_t maximum,deadline;
    if(!apx_http_object(v,fields,4)||!mode||!raw||!apx_json_unsigned(apx_json_get(v,"max_bytes"),&maximum)||maximum<1||maximum>APX_HTTP_BODY||!apx_json_unsigned(apx_json_get(v,"deadline"),&deadline)||deadline>INT64_MAX)return 125;
    int generate=!strcmp(mode,"keygen"),validate=!strcmp(mode,"validate-public")||!strcmp(mode,"validate-private"),opening=!strcmp(mode,"open");
    if(!generate&&!validate&&!opening&&strcmp(mode,"seal"))return 125;
    char *response=NULL;
    if(apx_http_now()>=(int64_t)deadline)response=apx_http_failure("pqc_timeout");
    else if(!apx_pqc_init())response=apx_http_failure("pqc_unavailable");
    else if(generate){char *doc=apx_pqc_generate(raw);if(doc){ApxValue *out=apx_record_new("host",2);apx_record_put(out,0,"ok",apx_bool(1));apx_record_put(out,1,"value",apx_string(doc,(int64_t)strlen(doc)));free(doc);response=apx_json_encode(out);}else response=apx_http_failure("pqc_keygen_failed");}
    else {ApxValue *req=validate?NULL:apx_pqc_parse(raw,strlen(raw),24);const char *slot=NULL,*error=validate?NULL:apx_pqc_request(req,opening,&slot);response=error?apx_http_failure(error):apx_pqc_native(mode,req,(size_t)maximum);}
    if(!response)return 125;
    if(apx_http_now()>=(int64_t)deadline){free(response);response=apx_http_failure("pqc_timeout");}
    size_t size=strlen(response),at=0;int ok=size<APX_HTTP_FRAME;
    while(ok&&at<size){if(!apx_http_wait(3,POLLOUT,(int64_t)deadline)){ok=0;break;}ssize_t k=send(3,response+at,size-at,MSG_NOSIGNAL|MSG_DONTWAIT);if(k<0&&(errno==EINTR||errno==EAGAIN))continue;if(k<=0){ok=0;break;}at+=(size_t)k;}
    if(ok)ok=apx_http_wait(3,POLLOUT,(int64_t)deadline)&&send(3,"\n",1,MSG_NOSIGNAL|MSG_DONTWAIT)==1;
    /* Contains only authenticated plaintext, but do not retain it on teardown. */
    explicit_bzero(response,size);free(response);apx_json_reset();return ok?0:125;
}
