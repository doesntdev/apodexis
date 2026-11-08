#define _GNU_SOURCE
#include "apx_http.h"
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <unistd.h>
int main(int argc,char **argv){
    if(argc!=2)return 125;
    char *end=NULL;long parent=strtol(argv[1],&end,10);
    if(!end||*end||parent<=1||prctl(PR_SET_PDEATHSIG,SIGKILL)||getppid()!=parent)return 125;
    signal(SIGPIPE,SIG_IGN);if(close_range(4,~0u,0))return 125;
    char *wire=malloc(APX_HTTP_FRAME+1);if(!wire)return 125;size_t n=0;int done=0;
    /* Parent independently bounds this startup read, including DNS/IPC. */
    while(n<APX_HTTP_FRAME){ssize_t k=read(3,wire+n,APX_HTTP_FRAME-n<4096?APX_HTTP_FRAME-n:4096);if(k<0&&errno==EINTR)continue;if(k<=0)break;n+=(size_t)k;if(wire[n-1]=='\n'){done=1;break;}}
    ApxValue *context=done?apx_json_parse(wire,n):NULL;free(wire);
    static const char *const keys[]={"request","max_bytes","deadline","ca","legacy","validate_ca","tls_security"};
    if(!apx_http_object(context,keys,7)||apx_json_kind(apx_json_get(context,"legacy"))!=1||apx_json_kind(apx_json_get(context,"validate_ca"))!=1)return 125;
    const char *security=apx_http_string(apx_json_get(context,"tls_security"));
    if(!security||(strcmp(security,"standard")&&strcmp(security,"pq_required")))return 125;
    int pq_required=!strcmp(security,"pq_required");
    uint64_t maximum=0,deadline=0;if(!apx_json_unsigned(apx_json_get(context,"max_bytes"),&maximum)||!maximum||maximum>APX_HTTP_BODY||!apx_json_unsigned(apx_json_get(context,"deadline"),&deadline)||deadline>INT64_MAX)return 125;
    const char *ca=apx_http_string(apx_json_get(context,"ca"));if(!ca||!apx_http_string(apx_json_get(context,"request")))return 125;char *response=NULL;
    if(apx_json_integer(apx_json_get(context,"validate_ca")))response=apx_http_failure(apx_http_ca_valid(ca)?"ca_valid":"invalid_ca");
    else {const char *raw=apx_http_string(apx_json_get(context,"request"));ApxValue *v=raw?apx_http_parse_request(raw,strlen(raw)):NULL;ApxHttpRequest req;const char *error=apx_http_request(v,(size_t)maximum,(int)apx_json_integer(apx_json_get(context,"legacy")),&req);/* Security comes only from the trusted context, after strict guest parsing. */req.pq_required=pq_required;response=error?apx_http_failure(error):apx_http_exchange(&req,(size_t)maximum,(int64_t)deadline,ca);apx_http_request_free(&req);}
    if(!response){apx_json_reset();return 125;}size_t length=strlen(response),at=0;
    if(length>=APX_HTTP_FRAME){free(response);apx_json_reset();return 125;}
    while(at<length){if(!apx_http_wait(3,POLLOUT,(int64_t)deadline))break;ssize_t k=send(3,response+at,length-at,MSG_NOSIGNAL);if(k<0&&errno==EINTR)continue;if(k<=0)break;at+=(size_t)k;}
    int ok=at==length&&send(3,"\n",1,MSG_NOSIGNAL)==1;free(response);apx_json_reset();return ok?0:125;
}
