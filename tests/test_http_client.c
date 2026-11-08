#define _GNU_SOURCE
#ifdef APX_HTTP_RESOLVER_WRAPPER
#include <netdb.h>
#include <unistd.h>
int __wrap_getaddrinfo(const char *a,const char *b,const struct addrinfo *c,struct addrinfo **d){(void)a;(void)b;(void)c;(void)d;sleep(20);return EAI_AGAIN;}
#elif defined(APX_HTTP_PQ_UNAVAILABLE_WRAPPER)
#include <openssl/ssl.h>
#include <string.h>
long __real_SSL_CTX_ctrl(SSL_CTX *,int,long,void *);
/* Only the test-linked helper loses this fixed group; no production switch. */
long __wrap_SSL_CTX_ctrl(SSL_CTX *ctx,int cmd,long arg,void *data){if(cmd==SSL_CTRL_SET_GROUPS_LIST&&data&&!strcmp(data,"X25519MLKEM768"))return 0;return __real_SSL_CTX_ctrl(ctx,cmd,arg,data);}
#else
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
#ifdef APX_HTTP_OPENSSL
#include <openssl/ssl.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>
#include <openssl/err.h>
#endif
static unsigned cases,checks;static char directory[4096],root[4096];
#ifdef APX_HTTP_PQ
static int policy_rejection,peer_version,old_schema;static const char *peer_group,*peer_cipher;
static char cli_binary[8192];
#endif
static void require(int ok,const char *what){checks++;if(!ok){fprintf(stderr,"HTTP FAIL: %s\n",what);exit(1);}}
static void path(char out[8192],const char *name){require(snprintf(out,8192,"%s/%s",directory,name)<8192,"fixture path");}
static void save(const char *name,const char *data){char file[8192];path(file,name);FILE *f=fopen(file,"wb");require(f&&fwrite(data,1,strlen(data),f)==strlen(data)&&!fclose(f),"fixture save");}
static char *readall(const char *name){char file[8192];path(file,name);FILE *f=fopen(file,"rb");require(f!=NULL,"read fixture");require(!fseek(f,0,SEEK_END),"seek fixture");long n=ftell(f);require(n>=0&&n<=APX_HTTP_FRAME,"bounded result");rewind(f);char *s=calloc(1,(size_t)n+1);require(s&&fread(s,1,(size_t)n,f)==(size_t)n&&!fclose(f),"fixture read");return s;}
static void passed(const char *name){printf("case %03u %s PASS\n",++cases,name);fflush(stdout);}
/* The host's independent localops monitor probes newly opened loopback ports.
 * Classify only its exact non-application GET / fingerprint, never an owned
 * /v1/ request, as a foreign observation. Preserve counts and raw bytes. */
/* Test-only TLS ClientHello metadata classifier. Production always offers
 * HTTP/1.1 ALPN; a well-formed hello without ALPN cannot be an owned replay.
 * This parses lengths/extensions only; it performs no TLS or cryptography. */
static int foreign_tls_probe(const unsigned char *s,size_t n){
 if(n<9||s[0]!=22||s[1]!=3||s[5]!=1)return 0;
 size_t record=((size_t)s[3]<<8)|s[4],hello=((size_t)s[6]<<16)|((size_t)s[7]<<8)|s[8];if(record+5!=n||hello+4!=record)return 0;
 size_t at=9;if(n-at<35)return 0;at+=34;size_t bytes=s[at++];if(bytes>n-at)return 0;at+=bytes;if(n-at<2)return 0;bytes=((size_t)s[at]<<8)|s[at+1];at+=2;if(!bytes||bytes%2||bytes>n-at)return 0;at+=bytes;if(at>=n)return 0;bytes=s[at++];if(!bytes||bytes>n-at)return 0;at+=bytes;if(n-at<2)return 0;bytes=((size_t)s[at]<<8)|s[at+1];at+=2;if(bytes!=n-at)return 0;
 while(at<n){if(n-at<4)return 0;unsigned type=((unsigned)s[at]<<8)|s[at+1];bytes=((size_t)s[at+2]<<8)|s[at+3];at+=4;if(bytes>n-at||type==16)return 0;at+=bytes;}
 return at==n;
}
static unsigned foreign_monitor_probes;
static int pending(int s){for(unsigned attempt=0;attempt<8;attempt++){
 struct pollfd f={s,POLLIN,0};int k;do{k=poll(&f,1,0);}while(k<0&&errno==EINTR);if(!k)return 0;if(k<0)return k;
 struct sockaddr_in a;socklen_t n=sizeof a;if(getsockname(s,(struct sockaddr*)&a,&n))return 1;
 int c=accept4(s,NULL,NULL,SOCK_CLOEXEC|SOCK_NONBLOCK);if(c<0)return 1;char bytes[16384];size_t used=0;int64_t until=apx_http_now()+100;
 while(used<sizeof bytes-1){ssize_t got=recv(c,bytes+used,sizeof bytes-1-used,MSG_DONTWAIT);if(got<0&&(errno==EAGAIN||errno==EWOULDBLOCK)){if(!apx_http_wait(c,POLLIN,until))break;continue;}if(got<=0)break;used+=(size_t)got;bytes[used]=0;if(used>=5&&(unsigned char)bytes[0]==22&&(unsigned char)bytes[1]==3){size_t record=((size_t)(unsigned char)bytes[3]<<8)|(unsigned char)bytes[4];if(used>=record+5)break;}else if(strstr(bytes,"\r\n\r\n"))break;}
 close(c);bytes[used]=0;char expected[256];int length=snprintf(expected,sizeof expected,"GET / HTTP/1.1\r\nAccept-Encoding: identity\r\nHost: 127.0.0.1:%u\r\nUser-Agent: localops/0.1\r\nConnection: close\r\n\r\n",ntohs(a.sin_port));
 if((length>0&&used==(size_t)length&&!memcmp(bytes,expected,used))||foreign_tls_probe((const unsigned char*)bytes,used)){foreign_monitor_probes++;printf("foreign_monitor_probe port=%u bytes=%zu hex=",ntohs(a.sin_port),used);for(size_t i=0;i<used;i++)printf("%02x",(unsigned char)bytes[i]);putchar('\n');continue;}
 fprintf(stderr,"unexpected owned/unclassified accept port=%u bytes=%zu hex=",ntohs(a.sin_port),used);for(size_t i=0;i<used;i++)fprintf(stderr,"%02x",(unsigned char)bytes[i]);fputc('\n',stderr);return 1;
 }return 1;}
static int listener(unsigned *port){int s=socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC,0);require(s>=0,"listener");struct sockaddr_in a={.sin_family=AF_INET,.sin_addr.s_addr=htonl(INADDR_LOOPBACK)};require(!bind(s,(struct sockaddr*)&a,sizeof a)&&!listen(s,8),"owned loopback bind");socklen_t n=sizeof a;require(!getsockname(s,(struct sockaddr*)&a,&n),"ephemeral port");*port=ntohs(a.sin_port);return s;}
static void policy(unsigned port,int tls,const char *extra){char s[20000];snprintf(s,sizeof s,"{\"schema\":\"apodexis.host_policy.v2\",\"http_timeout_ms\":500,\"http\":[{\"origin\":\"%s://127.0.0.1:%u\",\"path_prefix\":\"/v1/\",\"methods\":[\"GET\",\"HEAD\",\"POST\",\"PUT\",\"PATCH\",\"DELETE\",\"OPTIONS\"],\"headers\":[\"authorization\",\"content-type\",\"x-tag\"]}]%s}",tls?"https":"http",port,extra?extra:"");save("policy.json",s);}
static char *request_json(unsigned port,int tls,const char *method,const char *target,const char *enc,const char *data,int headers){char url[9000];snprintf(url,sizeof url,"%s://127.0.0.1:%u%s",tls?"https":"http",port,target);ApxValue *v=apx_record_new("Request",4);apx_record_put(v,0,"method",apx_string(method,(int64_t)strlen(method)));apx_record_put(v,1,"url",apx_string(url,(int64_t)strlen(url)));ApxValue *h=apx_array_new(headers?4:0);const char *names[]={"Authorization","content-type","X-Tag","x-tag"},*values[]={"Bearer synthetic-fixture","application/json","first","second"};for(int i=0;headers&&i<4;i++){ApxValue *item=apx_record_new("Header",2);apx_record_put(item,0,"name",apx_string(names[i],(int64_t)strlen(names[i])));apx_record_put(item,1,"value",apx_http_body((const unsigned char*)values[i],strlen(values[i])));apx_array_put(h,i,item);}apx_record_put(v,2,"headers",h);ApxValue *body=apx_record_new("Body",2);apx_record_put(body,0,"encoding",apx_string(enc,(int64_t)strlen(enc)));apx_record_put(body,1,"data",apx_string(data,(int64_t)strlen(data)));apx_record_put(v,3,"body",body);char *s=apx_json_encode(v);require(s!=NULL,"request JSON");return s;}
static char *invoke(const char *request,int granted,int raw){save("input.json",request);char source[8192],input[8192],pol[8192],output[8192],error[8192],binary[8192];path(source,raw?"raw.apx":"main.apx");path(input,"input.json");path(pol,"policy.json");path(output,"output");path(error,"error");snprintf(binary,sizeof binary,"%s/bin/apodexis",root);
#ifdef APX_HTTP_PQ
 if(*cli_binary)snprintf(binary,sizeof binary,"%s",cli_binary);
#endif
 fflush(NULL);pid_t p=fork();require(p>=0,"fork actual CLI");if(!p){alarm(10);if(!freopen(output,"wb",stdout)||!freopen(error,"wb",stderr))_exit(126);char *args[]={binary,"run",source,"--input",input,"--json",granted?"--capabilities":NULL,granted?pol:NULL,NULL};execv(binary,args);_exit(127);}int status;require(waitpid(p,&status,0)==p&&WIFEXITED(status),"actual CLI bounded exit");char *s=readall("output"),*err=readall("error");
#ifdef APX_HTTP_PQ
 if(policy_rejection){require(WEXITSTATUS(status)==64&&!*s&&!strncmp(err,"apodexis: ",10),"policy rejected by actual CLI before execution");printf("policy_refused exit=64 stderr=%s",err);free(err);free(s);return NULL;}
#endif
 if(WEXITSTATUS(status)){fprintf(stderr,"CLI exit %d result %s\nstderr %s\n",WEXITSTATUS(status),s,err);require(0,"CLI native success");}free(err);printf("result %s",s);ApxValue *v=apx_json_parse(s,strlen(s));const char *state=apx_http_string(apx_json_get(v,"status"));require(state&&!strcmp(state,"ok"),"native envelope");char *out=apx_json_encode(apx_json_get(v,"output"));free(s);require(out!=NULL,"language output");return out;}
static void expect_error(const char *out,const char *token){ApxValue *v=apx_json_parse(out,strlen(out));const char *tag=apx_http_string(apx_json_get(v,"tag")),*error=apx_http_string(apx_json_get(v,"error"));if(!tag||!error||strcmp(tag,"err")||strcmp(error,token))fprintf(stderr,"GOT %s WANT %s\n",out,token);require(tag&&error&&!strcmp(tag,"err")&&!strcmp(error,token),"payload-free exact error");}
#ifdef APX_HTTP_OPENSSL
static SSL_CTX *server_ctx;static int expect_dns_sni;
static int alpn(SSL *ssl,const unsigned char **out,unsigned char *outlen,const unsigned char *in,unsigned inlen,void *ctx){(void)ssl;(void)ctx;if(inlen!=9||in[0]!=8||memcmp(in+1,"http/1.1",8))return SSL_TLSEXT_ERR_ALERT_FATAL;*out=in+1;*outlen=8;return SSL_TLSEXT_ERR_OK;}
static void certificate(const char *san,int expired){
 EVP_PKEY *key=EVP_PKEY_Q_keygen(NULL,NULL,"RSA",2048);require(key!=NULL,"synthetic key API");X509 *cert=X509_new();require(cert!=NULL,"certificate API");require(X509_set_version(cert,2)&&ASN1_INTEGER_set(X509_get_serialNumber(cert),7)&&X509_gmtime_adj(X509_getm_notBefore(cert),-3600)&&X509_gmtime_adj(X509_getm_notAfter(cert),expired?-60:3600)&&X509_set_pubkey(cert,key),"certificate times/key");X509_NAME *name=X509_get_subject_name(cert);require(X509_NAME_add_entry_by_txt(name,"CN",MBSTRING_ASC,(const unsigned char*)"fixture",-1,-1,0)&&X509_set_issuer_name(cert,name),"synthetic issuer");X509V3_CTX ctx;X509V3_set_ctx(&ctx,cert,cert,NULL,NULL,0);X509_EXTENSION *ext=X509V3_EXT_conf_nid(NULL,&ctx,NID_subject_alt_name,(char*)san);require(ext&&X509_add_ext(cert,ext,-1),"SAN");X509_EXTENSION_free(ext);ext=X509V3_EXT_conf_nid(NULL,&ctx,NID_basic_constraints,"critical,CA:FALSE");require(ext&&X509_add_ext(cert,ext,-1),"CA constraint");X509_EXTENSION_free(ext);EVP_PKEY *ca_key=EVP_PKEY_Q_keygen(NULL,NULL,"RSA",2048);X509 *ca_cert=X509_new();require(ca_key&&ca_cert,"synthetic distinct CA key/cert");require(X509_set_version(ca_cert,2)&&ASN1_INTEGER_set(X509_get_serialNumber(ca_cert),42)&&X509_gmtime_adj(X509_getm_notBefore(ca_cert),-7200)&&X509_gmtime_adj(X509_getm_notAfter(ca_cert),7200)&&X509_set_pubkey(ca_cert,ca_key),"CA lifetime/key");X509_NAME *ca_name=X509_get_subject_name(ca_cert);require(X509_NAME_add_entry_by_txt(ca_name,"CN",MBSTRING_ASC,(const unsigned char*)"owned fixture CA",-1,-1,0)&&X509_set_issuer_name(ca_cert,ca_name),"CA identity");X509V3_set_ctx(&ctx,ca_cert,ca_cert,NULL,NULL,0);ext=X509V3_EXT_conf_nid(NULL,&ctx,NID_basic_constraints,"critical,CA:TRUE");require(ext&&X509_add_ext(ca_cert,ext,-1),"root CA constraint");X509_EXTENSION_free(ext);require(X509_sign(ca_cert,ca_key,EVP_sha256())>0&&X509_set_issuer_name(cert,ca_name)&&X509_sign(cert,ca_key,EVP_sha256())>0,"distinct CA -> leaf signature chain");char file[8192];path(file,"ca.pem");FILE *f=fopen(file,"wb");require(f&&PEM_write_X509(f,ca_cert)&&!fclose(f),"CA fixture snapshot");require(!chmod(file,0600),"operator-owned CA mode");if(server_ctx)SSL_CTX_free(server_ctx);server_ctx=SSL_CTX_new(TLS_server_method());require(server_ctx&&SSL_CTX_set_min_proto_version(server_ctx,TLS1_2_VERSION)&&SSL_CTX_use_certificate(server_ctx,cert)&&SSL_CTX_use_PrivateKey(server_ctx,key),"TLS fixture context");SSL_CTX_set_alpn_select_cb(server_ctx,alpn,NULL);X509_free(cert);EVP_PKEY_free(key);X509_free(ca_cert);EVP_PKEY_free(ca_key);
}
#endif
static pid_t serve(int s,const char *method,const unsigned char *body,size_t body_n,int headers,const char *reply,size_t reply_n,int tls,int stall,int handshake_bad){fflush(NULL);pid_t p=fork();require(p>=0,"fixture server fork");if(!p){alarm(8);int c=accept4(s,NULL,NULL,SOCK_CLOEXEC);if(c<0)_exit(21);close(s);
#ifdef APX_HTTP_OPENSSL
 SSL *ssl=NULL;if(tls&&!stall){ssl=SSL_new(server_ctx);if(!ssl||!SSL_set_fd(ssl,c))_exit(22);int ok=SSL_accept(ssl);
#ifdef APX_HTTP_PQ
 if(handshake_bad){
  int why=ok==1?0:SSL_get_error(ssl,ok);unsigned long reason=ERR_peek_last_error();unsigned char unexpected[32];int bytes=SSL_read(ssl,unexpected,sizeof unexpected);if(bytes>0)_exit(35);
  printf("peer_refusal handshake=%d ssl_error=%d reason=%s http_payload_bytes=0\n",ok,why,reason?ERR_reason_error_string(reason):"none");fflush(stdout);SSL_free(ssl);close(c);_exit(0);
 }
#endif
 if(ok!=1){SSL_free(ssl);close(c);_exit(handshake_bad?0:23);}if(SSL_version(ssl)<TLS1_2_VERSION)_exit(31);
#ifdef APX_HTTP_PQ
 if(peer_version){const char *group=SSL_group_to_name(ssl,SSL_get_negotiated_group(ssl));const char *cipher=SSL_get_cipher_name(ssl);const unsigned char *selected=NULL;unsigned selected_n=0;SSL_get0_alpn_selected(ssl,&selected,&selected_n);
  if(SSL_version(ssl)!=peer_version||!group||strcmp(group,peer_group)||strcmp(cipher,peer_cipher)||selected_n!=8||memcmp(selected,"http/1.1",8))_exit(34);
  printf("peer_negotiated version=%s group=%s cipher=%s alpn=http/1.1\n",SSL_get_version(ssl),group,cipher);fflush(stdout);
 }
#endif
 if(expect_dns_sni){const char *name=SSL_get_servername(ssl,TLSEXT_NAMETYPE_host_name);if(!name||strcmp(name,"localhost"))_exit(32);}}
#else
 (void)tls;(void)handshake_bad;
#endif
 if(stall==2){sleep(2);close(c);_exit(0);}size_t capacity=APX_HTTP_BODY+131072;unsigned char *request=malloc(capacity);if(!request)_exit(26);size_t n=0,head=0,wanted=0;for(;;){ssize_t k;
#ifdef APX_HTTP_OPENSSL
 if(ssl)k=SSL_read(ssl,request+n,(int)(capacity-n-1));else
#endif
 {k=read(c,request+n,capacity-n-1);}
 if(k<=0){_exit(24);}
 n+=(size_t)k;request[n]=0;if(!head){char *end=strstr((char*)request,"\r\n\r\n");if(end){head=(size_t)(end-(char*)request)+4;char *len=strstr((char*)request,"\r\nContent-Length: ");if(!len)_exit(25);wanted=(size_t)strtoul(len+18,NULL,10);}}if(head&&n>=head+wanted)break;if(n>=capacity-1)_exit(26);}
 char first[128];snprintf(first,sizeof first,"%s /v1/item?q=%%2F HTTP/1.1\r\n",method);if(strncmp((char*)request,first,strlen(first))||wanted!=body_n||memcmp(request+head,body,body_n)||n!=head+body_n)_exit(27);
 if(!strstr((char*)request,"\r\nConnection: close\r\n")||!strstr((char*)request,"\r\nAccept-Encoding: identity\r\n")||strstr((char*)request,"Cookie:")||strstr((char*)request,"Proxy-Authorization:"))_exit(28);
 if(headers&&(!strstr((char*)request,"authorization: Bearer synthetic-fixture\r\n")||!strstr((char*)request,"content-type: application/json\r\n")||!strstr((char*)request,"x-tag: first\r\nx-tag: second\r\n")))_exit(29);
 if(!headers&&strstr((char*)request,"authorization:"))_exit(30);
 if(stall){save("effect.log","received exactly once");sleep(2);}else {size_t at=0;while(at<reply_n){ssize_t k;
#ifdef APX_HTTP_OPENSSL
 if(ssl)k=SSL_write(ssl,reply+at,(int)(reply_n-at));else
#endif
 {k=send(c,reply+at,reply_n-at,MSG_NOSIGNAL);}
 if(k<=0){break;}
 at+=(size_t)k;}}
#ifdef APX_HTTP_OPENSSL
 if(ssl){SSL_shutdown(ssl);SSL_free(ssl);}
#endif
 close(c);_exit(0);}return p;}
static void reap(pid_t p){int status;require(waitpid(p,&status,0)==p&&WIFEXITED(status),"fixture reaped");if(WEXITSTATUS(status))fprintf(stderr,"fixture exit %d\n",WEXITSTATUS(status));require(!WEXITSTATUS(status),"server observed exact single request");}
static void exchange(const char *name,const char *method,const char *reply,const char *error,int status,const char *encoding,const char *data,int tls){unsigned port;int s=listener(&port);char ca[8500]="";
#ifdef APX_HTTP_OPENSSL
 if(tls){char file[8192];path(file,"ca.pem");snprintf(ca,sizeof ca,",\"tls_ca_file\":\"%s\"",file);}
#endif
 policy(port,tls,ca);const unsigned char bytes[]={'a',0,0xff};char *req=request_json(port,tls,method,"/v1/item?q=%2F","base64","YQD/",1);pid_t p=serve(s,method,bytes,sizeof bytes,1,reply,strlen(reply),tls,0,0);char *out=invoke(req,1,0);if(error)expect_error(out,error);else{ApxValue *v=apx_json_parse(out,strlen(out)),*value=apx_json_get(v,"value"),*body=apx_json_get(value,"body");const char *enc=apx_http_string(apx_json_get(body,"encoding")),*text=apx_json_string(apx_json_get(body,"data"));require(apx_json_integer(apx_json_get(value,"status"))==status,"exact HTTP status");require(enc&&text&&!strcmp(enc,encoding)&&!strcmp(text,data),"reversible response body");if(strstr(reply,"X-Order: one")){ApxValue *h=apx_json_get(value,"headers");require(apx_json_length(h)>=2,"duplicate headers retained");require(!strcmp(apx_http_string(apx_json_get(apx_json_item(h,0),"name")),"x-order"),"lowercase response header");require(!strcmp(apx_http_string(apx_json_get(apx_json_get(apx_json_item(h,1),"value"),"data")),"two"),"duplicate order");}if(strstr(reply,"X-Trail: end"))require(apx_json_length(apx_json_get(value,"trailers"))==1,"separate trailer");}
 free(out);free(req);reap(p);require(pending(s)==0,"no retry or redirect connection");close(s);apx_json_reset();passed(name);}
static void plain_cases(void){const char *methods[]={"GET","HEAD","POST","PUT","PATCH","DELETE","OPTIONS"};for(size_t i=0;i<7;i++)exchange(methods[i],methods[i],"HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok",NULL,200,"utf8",i==1?"":"ok",0);
 const int statuses[]={201,204,302,401,404,500,304};for(size_t i=0;i<7;i++){char name[32],reply[512];snprintf(name,sizeof name,"status-%d",statuses[i]);snprintf(reply,sizeof reply,"HTTP/1.0 %d Reply\r\nContent-Length: 2\r\nLocation: /never\r\n\r\nok",statuses[i]);exchange(name,"GET",reply,NULL,statuses[i],"utf8",statuses[i]==204||statuses[i]==304?"":"ok",0);}
 exchange("chunked-duplicates-trailer","POST","HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 200 OK\r\nX-Order: one\r\nX-Order: two\r\nTransfer-Encoding: chunked\r\n\r\n2;foo=bar\r\nok\r\n0\r\nX-Trail: end\r\n\r\n",NULL,200,"utf8","ok",0);
 exchange("chunk-extension-BWS-and-HTAB","GET","HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n2 \t; foo = \"a\tb\"\r\nok\r\n0\r\n\r\n",NULL,200,"utf8","ok",0);
 exchange("close-body-binary","GET","HTTP/1.0 200 OK\r\nX-Bytes: \xff\r\n\r\n\xff",NULL,200,"base64","/w==",0);
 exchange("utf8-no-conversion","GET","HTTP/1.1 200 OK\r\nContent-Length: 2\r\nContent-Encoding: gzip\r\n\r\né",NULL,200,"utf8","é",0);
 const char *bad[]={"HTTP/1.1 200 OK\r\nUpgrade: websocket\r\nContent-Length: 0\r\n\r\n","HTTP/1.1 200 OK\r\nConnection: keep-alive, Upgrade\r\nContent-Length: 0\r\n\r\n","HTTP/1.0 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n","HTTP/2 200 OK\r\n\r\n","HTTP/1.1 101 Switch\r\n\r\n","HTTP/1.1 200 OK\r\nContent-Length: 0\r\nContent-Length: 0\r\n\r\n","HTTP/1.1 200 OK\r\nContent-Length: 0\r\nTransfer-Encoding: chunked\r\n\r\n","HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nok","HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip\r\n\r\n","HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nz\r\n","HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1\r\naX\n0\r\n\r\n","HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\nContent-Length: 0\r\n\r\n","HTTP/1.1 200 OK\r\n folded: value\r\n\r\n","HTTP/1.1 200 OK\n\n","HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n1;bad=\"\r\na\r\n0\r\n\r\n"};for(size_t i=0;i<sizeof bad/sizeof bad[0];i++){char name[64];snprintf(name,sizeof name,"malformed-protocol-%zu",i);exchange(name,"GET",bad[i],"http_protocol_error",0,"","",0);}}
static void denials(void){unsigned port;int s=listener(&port);policy(port,0,NULL);const char *paths[]={"/v1evil/","/v1/../secret","/v1/%2e/x","/v1/%2f/x","/v1/%5c/x","/v1/%25/x","/v1/%00/x","/v1/%GG/x","/v1/a#fragment"};for(size_t i=0;i<sizeof paths/sizeof paths[0];i++){char *req=request_json(port,0,"GET",paths[i],"utf8","",0),*out=invoke(req,1,0);expect_error(out,i?"invalid_http_request":"http_request_denied");free(req);free(out);apx_json_reset();passed(paths[i]);}
 char *req=request_json(port,0,"GET","/v1/item?q=%2F","utf8","",0),*out=invoke(req,0,0);expect_error(out,"capability_denied");free(out);save("policy.json","{\"schema\":\"apodexis.host_policy.v1\",\"http_get\":[]}");out=invoke(req,1,0);expect_error(out,"http_request_denied");free(out);free(req);passed("default-and-v1-do-not-grant-new-api");policy(port,0,NULL);
 const char *raw[]={"{}","{\"method\":\"GET\",\"url\":\"http://127.0.0.1/\",\"headers\":[],\"body\":{\"encoding\":\"base64\",\"data\":\"/x==\"}}"};for(size_t i=0;i<2;i++){char *input=apx_json_encode(apx_string(raw[i],(int64_t)strlen(raw[i])));out=invoke(input,1,1);expect_error(out,"invalid_http_request");free(input);free(out);apx_json_reset();passed("strict-wire-invalid-or-noncanonical-base64");}
 const char *base64[]={"YQ=","YQ===","YQ==\n","Y===","YQ=A","YR=="};for(size_t i=0;i<6;i++){req=request_json(port,0,"GET","/v1/item?q=%2F","base64",base64[i],0);out=invoke(req,1,0);expect_error(out,"invalid_http_request");free(req);free(out);apx_json_reset();passed("invalid-base64");}
 require(pending(s)==0,"all denied/invalid requests made zero socket accepts");close(s);}
static void stalled(int tls,int handshake){unsigned port;int s=listener(&port);char extra[8500]="";
#ifdef APX_HTTP_OPENSSL
 if(tls){char file[8192];path(file,"ca.pem");snprintf(extra,sizeof extra,",\"tls_ca_file\":\"%s\"",file);}
#endif
 policy(port,tls,extra);const unsigned char empty[]={0};pid_t p=serve(s,"POST",empty,0,0,"",0,tls,handshake?2:1,0);char *req=request_json(port,tls,"POST","/v1/item?q=%2F","utf8","",0);int64_t start=apx_http_now();char *out=invoke(req,1,0);require(apx_http_now()-start<1800,"absolute request timeout");expect_error(out,"http_timeout");free(out);free(req);reap(p);if(!handshake){char *log=readall("effect.log");require(!strcmp(log,"received exactly once"),"post-send effect recorded before timeout");free(log);}require(pending(s)==0,"post-send timeout never replayed");close(s);passed(handshake?"TLS-handshake-deadline":"post-send-read-timeout-effect-uncertain");}
#ifdef APX_HTTP_OPENSSL
static void tls_negative(const char *name,const char *san,int expired,int trusted){certificate(san,expired);unsigned port;int s=listener(&port);char extra[8500]="";if(trusted){char file[8192];path(file,"ca.pem");snprintf(extra,sizeof extra,",\"tls_ca_file\":\"%s\"",file);}policy(port,1,extra);char ca_env[8192];path(ca_env,"ca.pem");if(!trusted)require(!setenv("SSL_CERT_FILE",ca_env,1),"untrusted CA env sentinel");pid_t p=serve(s,"GET",(const unsigned char*)"",0,0,"",0,1,0,1);char *req=request_json(port,1,"GET","/v1/item?q=%2F","utf8","",0),*out=invoke(req,1,0);expect_error(out,"http_tls_failed");free(out);free(req);reap(p);close(s);if(!trusted)require(!unsetenv("SSL_CERT_FILE"),"remove owned CA env sentinel");passed(name);}
static void tls_dns(const char *san,int failure){certificate(san,0);unsigned port;int s=listener(&port);char file[8192],pol[16000];path(file,"ca.pem");snprintf(pol,sizeof pol,"{\"schema\":\"apodexis.host_policy.v2\",\"tls_ca_file\":\"%s\",\"http_timeout_ms\":500,\"http\":[{\"origin\":\"https://localhost:%u\",\"path_prefix\":\"/v1/\",\"methods\":[\"GET\"],\"headers\":[]}]}",file,port);save("policy.json",pol);char input[1024];snprintf(input,sizeof input,"{\"method\":\"GET\",\"url\":\"https://localhost:%u/v1/item?q=%%2F\",\"headers\":[],\"body\":{\"encoding\":\"utf8\",\"data\":\"\"}}",port);expect_dns_sni=1;const char *reply="HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";pid_t p=serve(s,"GET",(const unsigned char*)"",0,0,reply,strlen(reply),1,0,failure);char *out=invoke(input,1,0);if(failure)expect_error(out,"http_tls_failed");else {ApxValue *v=apx_json_parse(out,strlen(out));require(apx_json_integer(apx_json_get(apx_json_get(v,"value"),"status"))==200,"DNS verified TLS with SNI/HTTP1.1 ALPN");}free(out);reap(p);close(s);expect_dns_sni=0;passed(failure?"TLS-wrong-hostname":"TLS-DNS-hostname-SNI-ALPN-positive");}
static void tls_plaintext(void){unsigned port;int s=listener(&port);policy(port,1,NULL);pid_t p=fork();require(p>=0,"plaintext-on-HTTPS fixture");if(!p){alarm(5);int c=accept4(s,NULL,NULL,SOCK_CLOEXEC);if(c<0)_exit(1);unsigned char b[1024];ssize_t n=read(c,b,sizeof b);if(n<=0||b[0]!=22)_exit(2);const char reply[]="HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";send(c,reply,sizeof reply-1,MSG_NOSIGNAL);close(c);close(s);_exit(0);}char *req=request_json(port,1,"GET","/v1/item?q=%2F","utf8","",0),*out=invoke(req,1,0);expect_error(out,"http_tls_failed");free(req);free(out);reap(p);require(pending(s)==0,"HTTPS never downgraded");close(s);passed("TLS-plaintext-peer-rejected-no-downgrade");}
#ifdef APX_HTTP_PQ
/* This mode requires real hybrid TLS; the ordinary TLS target remains 3.x-only. */
static void pq_policy(unsigned port,const char *mode,int legacy,int dns,int trusted){
 char file[8192],ca[8500]="",grant[2048],text[18000];path(file,"ca.pem");
 if(trusted)snprintf(ca,sizeof ca,",\"tls_ca_file\":\"%s\"",file);
 const char *host=dns?"localhost":"127.0.0.1";
 if(legacy)snprintf(grant,sizeof grant,"\"http_get\":[\"https://%s:%u/v1/item?q=%%2F\"]",host,port);
 else snprintf(grant,sizeof grant,"\"http\":[{\"origin\":\"https://%s:%u\",\"path_prefix\":\"/v1/\",\"methods\":[\"GET\"],\"headers\":[]}]",host,port);
 snprintf(text,sizeof text,"{\"schema\":\"apodexis.host_policy.v%u\",\"http_timeout_ms\":1000,%s%s%s}",old_schema?2u:3u,grant,ca,mode?mode:"");save("policy.json",text);
}
static void pq_exchange(const char *name,const char *mode,int version,const char *groups,const char *suites,int rejected,int legacy,int dns,int trusted,int status){
 require(SSL_CTX_set_min_proto_version(server_ctx,version)&&SSL_CTX_set_max_proto_version(server_ctx,version)&&SSL_CTX_set1_groups_list(server_ctx,groups)&&SSL_CTX_set_ciphersuites(server_ctx,suites)&&SSL_CTX_set_cipher_list(server_ctx,"ECDHE-RSA-AES128-GCM-SHA256"),"PQ peer configuration available, no skips");
 int standard=old_schema||(mode&&strstr(mode,"standard"));peer_version=version;peer_group=standard?"x25519":"X25519MLKEM768";peer_cipher=version==TLS1_2_VERSION?"ECDHE-RSA-AES128-GCM-SHA256":standard?"TLS_AES_128_GCM_SHA256":"TLS_AES_256_GCM_SHA384";expect_dns_sni=dns;
 unsigned port;int s=listener(&port);pq_policy(port,mode,legacy,dns,trusted);char reply[512];snprintf(reply,sizeof reply,"HTTP/1.1 %d Reply\r\nContent-Length: 2\r\nLocation: https://127.0.0.1:%u/never\r\n\r\nok",status,port);
 char *req=request_json(port,1,"GET","/v1/item?q=%2F","utf8","",0);
 if(dns){ApxValue *v=apx_json_parse(req,strlen(req));char url[512];snprintf(url,sizeof url,"https://localhost:%u/v1/item?q=%%2F",port);ApxValue *changed=apx_record_new("Request",4);apx_record_put(changed,0,"method",apx_json_get(v,"method"));apx_record_put(changed,1,"url",apx_string(url,(int64_t)strlen(url)));apx_record_put(changed,2,"headers",apx_json_get(v,"headers"));apx_record_put(changed,3,"body",apx_json_get(v,"body"));free(req);req=apx_json_encode(changed);require(req!=NULL,"DNS request JSON");}
 if(legacy){char url[512];snprintf(url,sizeof url,"https://127.0.0.1:%u/v1/item?q=%%2F",port);free(req);req=apx_json_encode(apx_string(url,(int64_t)strlen(url)));save("main.apx","M legacy\nF main url:str->result str str\n  R (http_get url)\n");}
 pid_t p=serve(s,"GET",(const unsigned char*)"",0,0,reply,strlen(reply),1,0,rejected);char *out=invoke(req,1,0);
 if(rejected)expect_error(out,legacy?"http_get_failed":"http_tls_failed");
 else {ApxValue *v=apx_json_parse(out,strlen(out));if(legacy)require(!strcmp(apx_http_string(apx_json_get(v,"value")),"ok"),"legacy PQ body");else require(apx_json_integer(apx_json_get(apx_json_get(v,"value"),"status"))==status,"actual CLI status over independently verified TLS");}
 free(req);free(out);reap(p);require(pending(s)==0,"no downgrade, redirect or retry connection");close(s);peer_version=0;expect_dns_sni=0;apx_json_reset();if(legacy)save("main.apx","M fixture\nI h \"http\"\nF main req:h::Request->result h::Response str\n  R (h::request req)\n");passed(name);
}
static void pq_policy_cases(void){
 unsigned port;int s=listener(&port);char *req=request_json(port,0,"GET","/v1/item?q=%2F","utf8","",0),text[4096];
 const char *bad[]={"\"tls_security\":\"preferred\"","\"tls_security\":true","\"tls_security\":null","\"tls_security\":1","\"tls_security\":[]","\"tls_security\":{}","\"tls_security\":\"standard\",\"tls_security\":\"pq_required\"","\"tls_security\":\"pq_required\\u0000hidden\"","\"tls_security\\u0000hidden\":\"standard\"","\"unknown\":true","\"schema\":\"apodexis.host_policy.v3\"","\"clock\":1","\"application_keys\":[]"};
 for(size_t i=0;i<sizeof bad/sizeof bad[0];i++){snprintf(text,sizeof text,"{\"schema\":\"apodexis.host_policy.v3\",%s}",bad[i]);save("policy.json",text);policy_rejection=1;free(invoke(req,1,0));policy_rejection=0;require(pending(s)==0,"invalid v3 policy made no connection");passed("PQ-invalid-policy-CLI");}
 for(unsigned v=1;v<=2;v++){snprintf(text,sizeof text,"{\"schema\":\"apodexis.host_policy.v%u\",\"tls_security\":\"standard\"}",v);save("policy.json",text);policy_rejection=1;free(invoke(req,1,0));policy_rejection=0;passed("old-policy-rejects-new-mode");}
 for(int legacy=0;legacy<2;legacy++){if(legacy)snprintf(text,sizeof text,"{\"schema\":\"apodexis.host_policy.v3\",\"http_get\":[\"http://127.0.0.1:%u/v1/item?q=%%2F\"]}",port);else snprintf(text,sizeof text,"{\"schema\":\"apodexis.host_policy.v3\",\"http\":[{\"origin\":\"http://127.0.0.1:%u\",\"path_prefix\":\"/v1/\",\"methods\":[\"GET\"],\"headers\":[]}]}",port);save("policy.json",text);policy_rejection=1;free(invoke(req,1,0));policy_rejection=0;require(pending(s)==0,"plaintext grants rejected before socket effects");passed(legacy?"PQ-legacy-plaintext-grant-refused":"PQ-plaintext-grant-refused");}
 save("policy.json","{\"schema\":\"apodexis.host_policy.v3\"}");char *out=invoke(req,1,0);expect_error(out,"http_request_denied");free(out);passed("v3-no-network-policy-accepted-denied");
 snprintf(text,sizeof text,"{\"schema\":\"apodexis.host_policy.v3\",\"tls_security\":\"standard\",\"http\":[{\"origin\":\"http://127.0.0.1:%u\",\"path_prefix\":\"/v1/\",\"methods\":[\"GET\"],\"headers\":[]}]}",port);save("policy.json",text);const char reply[]="HTTP/1.1 200 OK\r\nContent-Length: 2\r\n\r\nok";pid_t p=serve(s,"GET",(const unsigned char*)"",0,0,reply,sizeof reply-1,0,0,0);out=invoke(req,1,0);ApxValue *v=apx_json_parse(out,strlen(out));require(apx_json_integer(apx_json_get(apx_json_get(v,"value"),"status"))==200,"standard explicitly permits plaintext");free(out);reap(p);require(pending(s)==0,"standard single plaintext send");passed("v3-standard-plaintext-positive");free(req);
 /* Guest JSON cannot set either a boolean or string security override. */
 pq_policy(port,NULL,0,0,1);req=request_json(port,1,"GET","/v1/item?q=%2F","utf8","",0);const char *fields[]={"\"tls_security\":\"standard\"","\"pq_required\":false","\"tls_security\":\"pq_required\"","\"pq_required\":true"};
 for(size_t i=0;i<sizeof fields/sizeof fields[0];i++){snprintf(text,sizeof text,"{%s,%s",fields[i],req+1);char *input=apx_json_encode(apx_string(text,(int64_t)strlen(text)));out=invoke(input,1,1);expect_error(out,"invalid_http_request");free(out);free(input);require(pending(s)==0,"guest security override never connects");passed("guest-security-override-refused");}free(req);close(s);apx_json_reset();
}
static void pq_missing_helper(void){
 /* Hardlink only the current executable; share QBE/runtime through symlinks.
  * Never rename the live helper or copy a build tree to test absence. */
 const char *dirs[]={"missing","missing/bin","missing/.build","missing/.build/tools"};char p[8192],target[8192];for(size_t i=0;i<4;i++){path(p,dirs[i]);require(!mkdir(p,0700),"minimal missing-helper layout");}
 snprintf(target,sizeof target,"%s/bin/apodexis",root);path(cli_binary,"missing/bin/apodexis");require(!link(target,cli_binary),"same current CLI hardlink");
 const char *links[]={".build/tools/qbe",".build/apx_runtime.o",".build/apx_start.o","resources"};for(size_t i=0;i<4;i++){char relative[128];snprintf(relative,sizeof relative,"missing/%s",links[i]);path(p,relative);snprintf(target,sizeof target,"%s/%s",root,links[i]);require(!symlink(target,p),"shared compilation inputs");}
 unsigned port;int s=listener(&port);char pol[2048];snprintf(pol,sizeof pol,"{\"schema\":\"apodexis.host_policy.v3\",\"http\":[{\"origin\":\"https://127.0.0.1:%u\",\"path_prefix\":\"/v1/\",\"methods\":[\"GET\"],\"headers\":[]}]}",port);save("policy.json",pol);char *req=request_json(port,1,"GET","/v1/item?q=%2F","utf8","",0),*out=invoke(req,1,0);expect_error(out,"http_tls_unavailable");free(req);free(out);require(pending(s)==0,"missing helper never downgraded or connected");close(s);passed("PQ-missing-helper-actual-CLI-refused");
 path(p,"missing/.build/tools/apx-http-tls");snprintf(target,sizeof target,"%s/.build/tools/apx-http-pq-unavailable",root);require(!symlink(target,p),"test-only group-unavailable helper");
 s=listener(&port);snprintf(pol,sizeof pol,"{\"schema\":\"apodexis.host_policy.v3\",\"http\":[{\"origin\":\"https://127.0.0.1:%u\",\"path_prefix\":\"/v1/\",\"methods\":[\"GET\"],\"headers\":[]}]}",port);save("policy.json",pol);req=request_json(port,1,"GET","/v1/item?q=%2F","utf8","",0);out=invoke(req,1,0);expect_error(out,"http_pq_unavailable");free(req);free(out);require(pending(s)==0,"unsupported fixed group rejected before connect");close(s);require(!unlink(p),"remove test-linked helper reference");passed("PQ-unavailable-fails-before-connect");
 require(!unlink(cli_binary),"remove hardlinked CLI");*cli_binary=0;
 for(size_t i=0;i<4;i++){char relative[128];snprintf(relative,sizeof relative,"missing/%s",links[i]);path(p,relative);require(!unlink(p),"remove fixture symlink");}for(size_t i=4;i>0;i--){path(p,dirs[i-1]);require(!rmdir(p),"remove exact missing-helper directory");}
}
static void pq_cases(void){
 const char *pq="X25519MLKEM768",*aes="TLS_AES_256_GCM_SHA384",*standard=",\"tls_security\":\"standard\"";certificate("IP:127.0.0.1",0);
 pq_exchange("PQ-required-actual-CLI-positive",",\"tls_security\":\"pq_required\"",TLS1_3_VERSION,pq,aes,0,0,0,1,200);
 pq_exchange("PQ-v3-default-positive",NULL,TLS1_3_VERSION,pq,aes,0,0,0,1,200);
 pq_exchange("PQ-mixed-peer-still-strict",NULL,TLS1_3_VERSION,"X25519:X25519MLKEM768","TLS_AES_128_GCM_SHA256:TLS_AES_256_GCM_SHA384",0,0,0,1,200);
 pq_exchange("v3-standard-classical-TLS13-positive",standard,TLS1_3_VERSION,"X25519","TLS_AES_128_GCM_SHA256",0,0,0,1,200);
 pq_exchange("v3-standard-TLS12-positive",standard,TLS1_2_VERSION,"X25519",aes,0,0,0,1,200);
 old_schema=2;pq_exchange("v2-classical-TLS13-compatible",NULL,TLS1_3_VERSION,"X25519","TLS_AES_128_GCM_SHA256",0,0,0,1,200);pq_exchange("v2-TLS12-compatible",NULL,TLS1_2_VERSION,"X25519",aes,0,0,0,1,200);old_schema=0;
 pq_exchange("PQ-classical-only-TLS13-refused",",\"tls_security\":\"pq_required\"",TLS1_3_VERSION,"X25519",aes,1,0,0,1,200);
 pq_exchange("PQ-default-classical-only-refused",NULL,TLS1_3_VERSION,"X25519",aes,1,0,0,1,200);
 pq_exchange("PQ-TLS12-only-refused",NULL,TLS1_2_VERSION,"X25519",aes,1,0,0,1,200);
 pq_exchange("PQ-AES128-only-refused",NULL,TLS1_3_VERSION,pq,"TLS_AES_128_GCM_SHA256",1,0,0,1,200);
 SSL_CTX_set_alpn_select_cb(server_ctx,NULL,NULL);pq_exchange("PQ-no-ALPN-refused-before-HTTP",NULL,TLS1_3_VERSION,pq,aes,1,0,0,1,200);SSL_CTX_set_alpn_select_cb(server_ctx,alpn,NULL);
 pq_exchange("PQ-redirect-not-followed",NULL,TLS1_3_VERSION,pq,aes,0,0,0,1,302);
 pq_exchange("legacy-HTTPS-PQ-positive",NULL,TLS1_3_VERSION,pq,aes,0,1,0,1,200);
 pq_exchange("legacy-HTTPS-PQ-classical-refused",NULL,TLS1_3_VERSION,"X25519",aes,1,1,0,1,200);
 certificate("IP:127.0.0.2",0);pq_exchange("PQ-wrong-IP-refused",NULL,TLS1_3_VERSION,pq,aes,1,0,0,1,200);
 certificate("DNS:localhost",0);pq_exchange("PQ-DNS-SNI-ALPN-positive",NULL,TLS1_3_VERSION,pq,aes,0,0,1,1,200);
 certificate("DNS:other.invalid",0);pq_exchange("PQ-wrong-hostname-refused",NULL,TLS1_3_VERSION,pq,aes,1,0,1,1,200);
 certificate("IP:127.0.0.1",1);pq_exchange("PQ-expired-refused",NULL,TLS1_3_VERSION,pq,aes,1,0,0,1,200);
 certificate("IP:127.0.0.1",0);pq_exchange("PQ-untrusted-chain-refused",NULL,TLS1_3_VERSION,pq,aes,1,0,0,0,200);
 pq_policy_cases();pq_missing_helper();SSL_CTX_free(server_ctx);server_ctx=NULL;
}
#endif
static void tls_cases(void){certificate("IP:127.0.0.1",0);exchange("TLS-trusted-IP-positive","POST","HTTP/1.1 201 Created\r\nContent-Length: 2\r\n\r\nok",NULL,201,"utf8","ok",1);stalled(1,1);tls_negative("TLS-untrusted","IP:127.0.0.1",0,0);tls_negative("TLS-wrong-IP","IP:127.0.0.2",0,1);tls_negative("TLS-expired","IP:127.0.0.1",1,1);tls_dns("DNS:localhost",0);tls_dns("DNS:other.invalid",1);tls_plaintext();SSL_CTX_free(server_ctx);server_ctx=NULL;}
#endif
static void raw_error(const char *raw,const char *token){char *input=apx_json_encode(apx_string(raw,(int64_t)strlen(raw)));require(input!=NULL,"raw request input");char *out=invoke(input,1,1);expect_error(out,token);free(input);free(out);apx_json_reset();}
static void authority_headers_bounds(void){unsigned port;int s=listener(&port);policy(port,0,NULL);char url[512],raw[20000];snprintf(url,sizeof url,"http://127.0.0.1:%u/v1/item?q=%%2F",port);
 const char *names[]={"Host","Content-Length","Transfer-Encoding","Connection","Expect","Upgrade","TE","Trailer","Accept-Encoding","Proxy-Authorization","Proxy-Connection","x-ungranted"};
 for(size_t i=0;i<sizeof names/sizeof names[0];i++){snprintf(raw,sizeof raw,"{\"method\":\"GET\",\"url\":\"%s\",\"headers\":[{\"name\":\"%s\",\"value\":{\"encoding\":\"utf8\",\"data\":\"x\"}}],\"body\":{\"encoding\":\"utf8\",\"data\":\"\"}}",url,names[i]);raw_error(raw,i==11?"http_request_denied":"invalid_http_request");passed(names[i]);}
 const char *values[]={"\\r\\nInjected: yes","\\u0000","\\u0001","\\u007f"};for(size_t i=0;i<4;i++){snprintf(raw,sizeof raw,"{\"method\":\"GET\",\"url\":\"%s\",\"headers\":[{\"name\":\"x-tag\",\"value\":{\"encoding\":\"utf8\",\"data\":\"%s\"}}],\"body\":{\"encoding\":\"utf8\",\"data\":\"\"}}",url,values[i]);raw_error(raw,"invalid_http_request");passed("header-control-injection-denied");}
 const char *targets[]={"http://localhost:1/v1/","https://127.0.0.1:1/v1/","http://127.0.0.1:1/v1/"};for(size_t i=0;i<3;i++){snprintf(raw,sizeof raw,"{\"method\":\"GET\",\"url\":\"%s\",\"headers\":[],\"body\":{\"encoding\":\"utf8\",\"data\":\"\"}}",targets[i]);raw_error(raw,"http_request_denied");passed("wrong-host-scheme-or-port-denied");}
 snprintf(raw,sizeof raw,"{\"method\":\"TRACE\",\"url\":\"%s\",\"headers\":[],\"body\":{\"encoding\":\"utf8\",\"data\":\"\"}}",url);raw_error(raw,"invalid_http_request");passed("unsupported-method");
 policy(port,0,",\"max_bytes\":3");char *req=request_json(port,0,"POST","/v1/item?q=%2F","utf8","four",0),*out=invoke(req,1,0);expect_error(out,"http_byte_limit");free(req);free(out);require(pending(s)==0,"invalid and denied requests did not connect");close(s);passed("request-byte-over-limit");
 unsigned pport;s=listener(&pport);policy(pport,0,",\"max_bytes\":3");req=request_json(pport,0,"POST","/v1/item?q=%2F","base64","YQD/",1);const unsigned char body[]={'a',0,255};const char *reply="HTTP/1.1 200 OK\r\nContent-Length: 4\r\n\r\nfour";pid_t p=serve(s,"POST",body,3,1,reply,strlen(reply),0,0,0);out=invoke(req,1,0);expect_error(out,"http_byte_limit");free(out);free(req);reap(p);close(s);passed("response-byte-over-limit");
 char *many=malloc(20000);require(many!=NULL,"header limit fixture");size_t at=(size_t)sprintf(many,"HTTP/1.1 200 OK\r\n");for(size_t i=0;i<129;i++)at+=(size_t)sprintf(many+at,"X: a\r\n");strcpy(many+at,"\r\n");exchange("aggregate-header-count-limit","GET",many,"http_header_limit",0,"","",0);free(many);
 char *info=malloc(1024);require(info!=NULL,"informational limit fixture");info[0]=0;for(int i=0;i<11;i++)strcat(info,"HTTP/1.1 100 Continue\r\n\r\n");strcat(info,"HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n");exchange("informational-block-limit","GET",info,"http_protocol_error",0,"","",0);free(info);
}
static void typed_documents(void){
 save("main.apx","M typed_http\nI h \"http\"\nT Payload struct\n  id:i64\nT Reply struct\n  answer:i64\nF main url:str->result i64 str\n  L req (h::Request method=\"POST\" url=url headers=[] body=(h::text (json_encode (Payload id=7))))\n  C (h::request req)\n    ok response\n      C (h::body_text response.body)\n        ok text\n          C (json_decode Reply text)\n            ok reply\n              R (ok reply.answer)\n            err problem\n              R (err problem)\n        err problem\n          R (err problem)\n    err problem\n      R (err problem)\n");
 unsigned port;int s=listener(&port);policy(port,0,NULL);const char *reply="HTTP/1.1 200 OK\r\nContent-Length: 13\r\n\r\n{\"answer\":42}";pid_t p=serve(s,"POST",(const unsigned char*)"{\"id\":7}",8,0,reply,strlen(reply),0,0,0);char url[512];snprintf(url,sizeof url,"http://127.0.0.1:%u/v1/item?q=%%2F",port);char *input=apx_json_encode(apx_string(url,(int64_t)strlen(url))),*out=invoke(input,1,0);ApxValue *v=apx_json_parse(out,strlen(out));require(apx_json_integer(apx_json_get(v,"value"))==42,"typed JSON encode -> HTTP -> typed decode");free(out);free(input);reap(p);close(s);save("main.apx","M fixture\nI h \"http\"\nF main req:h::Request->result h::Response str\n  R (h::request req)\n");passed("typed-application-document-roundtrip");
}
static void worst_case_controls(void){
 save("main.apx","M controls\nI h \"http\"\nF main req:h::Request->result h::Response str\n  L data \"\\0\"\n  L i 0\n  W (< i 20)\n    S data (cat data data)\n    S i (+ i 1)\n  R (h::request (with req body=(h::text data)))\n");
 unsigned port;int s=listener(&port);policy(port,0,NULL);size_t n=APX_HTTP_BODY;unsigned char *data=calloc(1,n);require(data!=NULL,"worst-case control body");char *input=request_json(port,0,"POST","/v1/item?q=%2F","utf8","",0);const char *reply="HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";pid_t p=serve(s,"POST",data,n,0,reply,strlen(reply),0,0,0);char *out=invoke(input,1,0);ApxValue *v=apx_json_parse(out,strlen(out));require(apx_json_integer(apx_json_get(apx_json_get(v,"value"),"status"))==200,"max request NUL body passed full escaping/frame limits");free(input);free(out);free(data);reap(p);close(s);apx_json_reset();save("main.apx","M fixture\nI h \"http\"\nF main req:h::Request->result h::Response str\n  R (h::request req)\n");passed("max-body-utf8-NUL-worst-case-escaping");}
static void cancellation(int worker_death){
 unsigned port;int s=listener(&port);policy(port,0,",\"max_bytes\":1024");char log[8192];path(log,"effect.log");unlink(log);pid_t server=serve(s,"POST",(const unsigned char*)"",0,0,"",0,0,1,0);
 char *req=request_json(port,0,"POST","/v1/item?q=%2F","utf8","",0);save("input.json",req);free(req);char source[8192],input[8192],pol[8192],out[8192],err[8192],binary[8192];path(source,"main.apx");path(input,"input.json");path(pol,"policy.json");path(out,"cancel-output");path(err,"cancel-error");snprintf(binary,sizeof binary,"%s/bin/apodexis",root);fflush(NULL);pid_t cli=fork();require(cli>=0,"cancellable CLI fork");if(!cli){alarm(8);if(!freopen(out,"wb",stdout)||!freopen(err,"wb",stderr))_exit(126);execl(binary,binary,"run",source,"--input",input,"--capabilities",pol,"--json",(char*)NULL);_exit(127);}
 int64_t until=apx_http_now()+3000;while(access(log,F_OK)&&apx_http_now()<until)poll(NULL,0,1);require(!access(log,F_OK),"effect observed before cancellation");char proc[128];snprintf(proc,sizeof proc,"/proc/%ld/task/%ld/children",(long)cli,(long)cli);FILE *f=fopen(proc,"rb");require(f!=NULL,"owned CLI children");pid_t helper_pid=0,worker_pid=0;long child;while(fscanf(f,"%ld",&child)==1){char exe[128],target[8192];snprintf(exe,sizeof exe,"/proc/%ld/exe",child);ssize_t n=readlink(exe,target,sizeof target-1);if(n>0){target[n]=0;if(strstr(target,"apx-http"))helper_pid=(pid_t)child;else if(strstr(target,"/program"))worker_pid=(pid_t)child;}}fclose(f);require(helper_pid>0&&worker_pid>0,"exact active helper and language worker identities");int64_t start=apx_http_now();require(!kill(worker_death?worker_pid:cli,worker_death?SIGKILL:SIGTERM),"cancel exact owned process");int status;require(waitpid(cli,&status,0)==cli&&apx_http_now()-start<1200,"CLI/worker cancellation bounded");snprintf(proc,sizeof proc,"/proc/%ld",(long)helper_pid);require(access(proc,F_OK)&&errno==ENOENT,"helper exactly reaped after cancellation");snprintf(proc,sizeof proc,"/proc/%ld",(long)worker_pid);require(access(proc,F_OK)&&errno==ENOENT,"worker exactly reaped after cancellation");reap(server);require(pending(s)==0,"cancelled request not replayed");close(s);passed(worker_death?"worker-death-cancels-and-reaps-helper":"CLI-SIGTERM-cancels-and-reaps-process-group");
}
static ApxValue *mcp_rpc(FILE *in,FILE *out,unsigned *id,const char *method,const char *params){unsigned request=++*id;require(fprintf(in,"{\"jsonrpc\":\"2.0\",\"id\":%u,\"method\":\"%s\",\"params\":%s}\n",request,method,params)>0&&!fflush(in),"MCP send");char *line=NULL;size_t cap=0;ssize_t n=getline(&line,&cap,out);require(n>0,"MCP receive");printf("MCP %s",line);apx_json_reset();ApxValue *v=apx_json_parse(line,(size_t)n);free(line);require(v&&apx_json_integer(apx_json_get(v,"id"))==request,"MCP response ID");return v;}
static ApxValue *mcp_tool(FILE *in,FILE *out,unsigned *id,const char *name,const char *args){char params[16384];require(snprintf(params,sizeof params,"{\"name\":\"%s\",\"arguments\":%s}",name,args)<(int)sizeof params,"MCP params bound");return mcp_rpc(in,out,id,"tools/call",params);}
static void mcp_denial(void){int in[2],out[2];require(!pipe2(in,O_CLOEXEC)&&!pipe2(out,O_CLOEXEC),"MCP channels");char binary[8192];snprintf(binary,sizeof binary,"%s/bin/apodexis",root);pid_t p=fork();require(p>=0,"MCP fork");if(!p){alarm(10);if(dup2(in[0],0)<0||dup2(out[1],1)<0)_exit(126);close(in[0]);close(in[1]);close(out[0]);close(out[1]);execl(binary,binary,"mcp",(char*)NULL);_exit(127);}close(in[0]);close(out[1]);FILE *send=fdopen(in[1],"w"),*receive=fdopen(out[0],"r");require(send&&receive,"MCP streams");unsigned id=0;ApxValue *v=mcp_rpc(send,receive,&id,"initialize","{}");require(!apx_json_get(v,"error"),"MCP initialize");require(fputs("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n",send)>=0&&!fflush(send),"initialized notification");v=mcp_rpc(send,receive,&id,"tools/list","{}");require(apx_json_length(apx_json_get(apx_json_get(v,"result"),"tools"))==13,"unchanged thirteen-tool discovery");
 const char *source="M denied_http\nF main->result str str\n  R (http_request \"{}\")\nX denied\n  A (= (main) (err \"capability_denied\"))\n";char *encoded=apx_json_encode(apx_string(source,(int64_t)strlen(source)));require(encoded!=NULL,"MCP source encode");char args[10000];snprintf(args,sizeof args,"{\"source\":%s,\"input\":null,\"limits\":{\"fuel\":10000,\"max_input_bytes\":null,\"max_output_bytes\":null}}",encoded);v=mcp_tool(send,receive,&id,"apodexis_run",args);ApxValue *result=apx_json_get(apx_json_get(v,"result"),"structuredContent");char *output=apx_json_encode(apx_json_get(result,"output"));require(output!=NULL,"MCP run output");expect_error(output,"capability_denied");free(output);passed("MCP-http-request-default-denial");
 snprintf(args,sizeof args,"{\"source\":%s}",encoded);v=mcp_tool(send,receive,&id,"apodexis_compile",args);result=apx_json_get(apx_json_get(v,"result"),"structuredContent");const char *handle=apx_http_string(apx_json_get(result,"artifact_id"));require(handle&&strlen(handle)==38,"sealed HTTP artifact compiled");char artifact[39];strcpy(artifact,handle);snprintf(args,sizeof args,"{\"artifact_id\":\"%s\",\"input\":null,\"limits\":{\"fuel\":10000,\"max_input_bytes\":null,\"max_output_bytes\":null}}",artifact);v=mcp_tool(send,receive,&id,"apodexis_invoke",args);result=apx_json_get(apx_json_get(v,"result"),"structuredContent");output=apx_json_encode(apx_json_get(result,"output"));require(output!=NULL,"MCP invoke output");expect_error(output,"capability_denied");free(output);passed("MCP-sealed-invoke-http-default-denial");
 const char *tools[]={"apodexis_test","apodexis_verify"};for(size_t i=0;i<2;i++){snprintf(args,sizeof args,"{\"source\":%s}",encoded);v=mcp_tool(send,receive,&id,tools[i],args);result=apx_json_get(apx_json_get(v,"result"),"structuredContent");const char *state=apx_http_string(apx_json_get(result,"status"));require(state&&!strcmp(state,"ok"),"MCP test/verify confirms denied source test");passed(tools[i]);}
 snprintf(args,sizeof args,"{\"source\":%s,\"input\":null,\"limits\":{\"fuel\":10000,\"max_input_bytes\":null,\"max_output_bytes\":null},\"capabilities\":{\"schema\":\"apodexis.host_policy.v2\",\"http\":[]}}",encoded);v=mcp_tool(send,receive,&id,"apodexis_run",args);require(apx_json_get(v,"error")!=NULL,"policy-shaped extra input rejected");free(encoded);snprintf(args,sizeof args,"{\"artifact_id\":\"%s\"}",artifact);v=mcp_tool(send,receive,&id,"apodexis_release",args);require(!apx_json_get(v,"error"),"release owned handle");fclose(send);fclose(receive);int status;require(waitpid(p,&status,0)==p&&WIFEXITED(status)&&!WEXITSTATUS(status),"MCP EOF reclaims resources");passed("MCP-no-policy-shaped-self-grant");}
static void connect_deadline(void){unsigned port;int s=listener(&port);require(!listen(s,0),"zero-backlog owned listener");int fill=socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC,0);require(fill>=0,"backlog filler");struct sockaddr_in a={.sin_family=AF_INET,.sin_addr.s_addr=htonl(INADDR_LOOPBACK),.sin_port=htons((unsigned short)port)};require(!connect(fill,(struct sockaddr*)&a,sizeof a),"fill accept backlog without serving");policy(port,0,NULL);char *req=request_json(port,0,"POST","/v1/item?q=%2F","utf8","effect",0);int64_t start=apx_http_now();char *out=invoke(req,1,0);require(apx_http_now()-start<1500,"connect is within one absolute deadline");expect_error(out,"http_timeout");free(req);free(out);close(fill);close(s);passed("actual-connect-stall-deadline");}
/* Request limits cover the complete emitted head, including managed fields. */
static void request_head_limits(void){
 const char *names[]={"request-field-count-over","request-field-count-exact","request-head-bytes-over","request-head-bytes-exact","request-field-line-over","request-field-line-exact","request-start-line-over","request-start-line-exact"};
 for(unsigned which=0;which<8;which++){
  unsigned port;int s=listener(&port);int allowed=which&1;const char *host=which>=6?"0":"127.0.0.1",*method=which>=6?"OPTIONS":"GET";
  char target[APX_HTTP_LINE+1]="/v1/item?q=%2F",url[9000],pol[2000];
  if(which>=6){size_t n=APX_HTTP_LINE-strlen(method)-10+(allowed?0:1);memset(target,'a',n);memcpy(target,"/v1/",4);target[n]=0;}
  snprintf(url,sizeof url,"http://%s:%u%s",host,port,target);
  snprintf(pol,sizeof pol,"{\"schema\":\"apodexis.host_policy.v2\",\"http_timeout_ms\":500,\"http\":[{\"origin\":\"http://%s:%u\",\"path_prefix\":\"/v1/\",\"methods\":[\"GET\",\"OPTIONS\"],\"headers\":[\"x-tag\"]}]}",host,port);save("policy.json",pol);
  char *expected=malloc(APX_HTTP_HEADER_BYTES+1024);require(expected!=NULL,"bounded request head oracle");
  int prefix=snprintf(expected,APX_HTTP_HEADER_BYTES+1024,"%s %s HTTP/1.1\r\nHost: %s:%u\r\nContent-Length: 0\r\nConnection: close\r\nAccept-Encoding: identity\r\n",method,target,host,port);require(prefix>0,"request head prefix");
  size_t count=which<2?(allowed?124:125):which<4?8:which<6?1:0,lengths[128]={0};
  if(which<2)for(size_t i=0;i<count;i++)lengths[i]=1;
  if(which>=2&&which<4){size_t bytes=APX_HTTP_HEADER_BYTES+(allowed?0:1)-(size_t)prefix-2-count*9;for(size_t i=0;i<count;i++)lengths[i]=bytes/count+(i<bytes%count);}
  if(which>=4&&which<6)lengths[0]=APX_HTTP_LINE-7+(allowed?0:1);
  ApxValue *request=apx_record_new("Request",4),*headers=apx_array_new((int64_t)count);apx_record_put(request,0,"method",apx_string(method,(int64_t)strlen(method)));apx_record_put(request,1,"url",apx_string(url,(int64_t)strlen(url)));
  size_t used=(size_t)prefix;
  for(size_t i=0;i<count;i++){char value[APX_HTTP_LINE+1];require(lengths[i]<=APX_HTTP_LINE,"bounded header oracle value");memset(value,'a',lengths[i]);ApxValue *h=apx_record_new("Header",2);apx_record_put(h,0,"name",apx_string("x-tag",5));apx_record_put(h,1,"value",apx_http_body((unsigned char*)value,lengths[i]));apx_array_put(headers,(int64_t)i,h);memcpy(expected+used,"x-tag: ",7);used+=7;memset(expected+used,'a',lengths[i]);used+=lengths[i];memcpy(expected+used,"\r\n",2);used+=2;}
  memcpy(expected+used,"\r\n",2);used+=2;expected[used]=0;require(used<APX_HTTP_HEADER_BYTES+1024,"bounded expected wire");
  if(which>=2&&which<4)require(used==APX_HTTP_HEADER_BYTES+(allowed?0:1),"exact aggregate head threshold");
  if(which>=6)require((size_t)(strstr(expected,"\r\n")-expected)==APX_HTTP_LINE+(allowed?0:1),"exact request line threshold");
  apx_record_put(request,2,"headers",headers);apx_record_put(request,3,"body",apx_http_body((unsigned char*)"",0));char *input=apx_json_encode(request);require(input!=NULL,"bounded request JSON");pid_t server=0;
  if(allowed){fflush(NULL);server=fork();require(server>=0,"boundary server fork");if(!server){alarm(5);int c=accept4(s,NULL,NULL,SOCK_CLOEXEC);if(c<0)_exit(41);close(s);char observed[APX_HTTP_HEADER_BYTES+2];size_t n=0;while(n<sizeof observed-1){ssize_t k=read(c,observed+n,sizeof observed-1-n);if(k<=0)_exit(42);n+=(size_t)k;observed[n]=0;if(strstr(observed,"\r\n\r\n"))break;}if(n!=used||memcmp(observed,expected,n))_exit(43);const char reply[]="HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";if(send(c,reply,sizeof reply-1,MSG_NOSIGNAL)!=(ssize_t)(sizeof reply-1))_exit(44);close(c);_exit(0);}}
  char *out=invoke(input,1,0);if(allowed){ApxValue *v=apx_json_parse(out,strlen(out));require(apx_json_integer(apx_json_get(apx_json_get(v,"value"),"status"))==200,"exact-limit request succeeds");reap(server);}else expect_error(out,"http_header_limit");
  require(pending(s)==0,"over-limit heads never sent and accepted heads never replayed");close(s);free(input);free(out);free(expected);apx_json_reset();passed(names[which]);
 }
 /* Structural punctuation inside a body string is data, not a JSON node budget. */
 unsigned port;int s=listener(&port);policy(port,0,NULL);char data[16385];const char pattern[]="[]{}:,\\\"";for(size_t i=0;i<sizeof data-1;i++)data[i]=pattern[i%(sizeof pattern-1)];data[sizeof data-1]=0;const char reply[]="HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n";pid_t server=serve(s,"POST",(unsigned char*)data,sizeof data-1,0,reply,sizeof reply-1,0,0,0);char *input=request_json(port,0,"POST","/v1/item?q=%2F","utf8",data,0),*out=invoke(input,1,0);ApxValue *v=apx_json_parse(out,strlen(out));require(apx_json_integer(apx_json_get(apx_json_get(v,"value"),"status"))==200,"quoted punctuation is not structural work");free(input);free(out);reap(server);close(s);apx_json_reset();passed("request-quoted-punctuation-and-escapes");
}
static void framing_byte_limits(void){
 size_t n=APX_HTTP_LINE+100;char *reply=malloc(n+100);require(reply!=NULL,"long header fixture");size_t at=(size_t)sprintf(reply,"HTTP/1.1 200 OK\r\nX: ");memset(reply+at,'a',n);strcpy(reply+at+n,"\r\n\r\n");exchange("header-line-limit","GET",reply,"http_header_limit",0,"","",0);free(reply);
 reply=malloc(80000);require(reply!=NULL,"aggregate header fixture");at=(size_t)sprintf(reply,"HTTP/1.1 200 OK\r\n");for(size_t i=0;i<120;i++){memcpy(reply+at,"X: ",3);at+=3;memset(reply+at,'a',600);at+=600;memcpy(reply+at,"\r\n",2);at+=2;}strcpy(reply+at,"\r\n");exchange("aggregate-header-byte-limit","GET",reply,"http_header_limit",0,"","",0);free(reply);
 reply=malloc(9*1048576);require(reply!=NULL,"wire cap fixture");at=(size_t)sprintf(reply,"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n");for(size_t i=0;i<1100;i++){memcpy(reply+at,"1;x=",4);at+=4;memset(reply+at,'a',8100);at+=8100;memcpy(reply+at,"\r\nx\r\n",5);at+=5;}strcpy(reply+at,"0\r\n\r\n");exchange("response-wire-byte-limit","GET",reply,"http_byte_limit",0,"","",0);free(reply);
}
static void missing_tls(void){unsigned port;int s=listener(&port);policy(port,1,NULL);char *req=request_json(port,1,"GET","/v1/item?q=%2F","utf8","",0),*out=invoke(req,1,0);expect_error(out,"http_tls_unavailable");free(req);free(out);require(pending(s)==0,"missing TLS cannot downgrade to plaintext");close(s);passed("plain-package-missing-TLS-fails-closed");}
static void resolver_deadline(void){
 char file[8192];path(file,"policy.json");save("policy.json","{\"schema\":\"apodexis.host_policy.v2\",\"http_timeout_ms\":100,\"http\":[{\"origin\":\"http://dns-stall.invalid:80\",\"path_prefix\":\"/\",\"methods\":[\"GET\"],\"headers\":[]}]}");
 char plain[8192],tls[8192];snprintf(plain,sizeof plain,"%s/.build/tools/apx-http-dns-stall",root);snprintf(tls,sizeof tls,"%s/.build/tools/apx-http-tls",root);apx_host_helpers(plain,tls);char *error=NULL;ApxHostPolicy *p=apx_host_policy_load(file,&error);require(p&&!error,"DNS deadline policy");
 const char *raw="{\"method\":\"GET\",\"url\":\"http://dns-stall.invalid/\",\"headers\":[],\"body\":{\"encoding\":\"utf8\",\"data\":\"\"}}";
 ApxValue *v=apx_record_new("host",2);apx_record_put(v,0,"op",apx_string("http_request",12));ApxValue *a=apx_array_new(1);apx_array_put(a,0,apx_string(raw,(int64_t)strlen(raw)));apx_record_put(v,1,"args",a);char *wire=apx_json_encode(v);require(wire!=NULL,"DNS request envelope");int64_t start=apx_http_now();char *out=apx_host_dispatch(p,wire,strlen(wire));require(out&&strstr(out,"http_timeout")&&apx_http_now()-start<1000,"broker interrupts blocking resolver");int status;errno=0;require(waitpid(-1,&status,WNOHANG)==-1&&errno==ECHILD,"exact DNS helper reaped");free(out);free(wire);apx_host_policy_free(p);apx_json_reset();snprintf(plain,sizeof plain,"%s/.build/tools/apx-http",root);apx_host_helpers(plain,tls);passed("DNS-stall-broker-deadline-and-reap");
}
static void cleanup(void){DIR *d=opendir(directory);require(d!=NULL,"fixture cleanup");struct dirent *e;while((e=readdir(d))){if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;char p[8192];path(p,e->d_name);require(!unlink(p),"remove exact owned file");}closedir(d);require(!rmdir(directory),"remove owned fixture root");}
int main(void){signal(SIGPIPE,SIG_IGN);const char *r=getenv("APX_TEST_TOOLCHAIN_ROOT");if(r)snprintf(root,sizeof root,"%s",r);else require(getcwd(root,sizeof root)!=NULL,"toolchain root");const char *scratch=getenv("APX_HTTP_FIXTURE_DIR");if(!scratch)scratch=".build";require(snprintf(directory,sizeof directory,"%s/http-client-XXXXXX",scratch)<(int)sizeof directory,"fixture root");require(mkdtemp(directory)!=NULL,"owned fixture directory");char *absolute=realpath(directory,NULL);require(absolute&&strlen(absolute)<sizeof directory,"absolute fixture root");strcpy(directory,absolute);free(absolute);
 char module[8192];snprintf(module,sizeof module,"%s/examples/http_client/http.apx",root);FILE *f=fopen(module,"rb");require(f!=NULL,"shipped ordinary module");char text[8192];size_t n=fread(text,1,sizeof text-1,f);require(!ferror(f)&&!fclose(f)&&n<sizeof text-1,"module read");text[n]=0;save("http.apx",text);save("main.apx","M fixture\nI h \"http\"\nF main req:h::Request->result h::Response str\n  R (h::request req)\n");save("raw.apx","M raw\nF main text:str->result str str\n  R (http_request text)\n");
#ifdef APX_HTTP_PQ
 pq_cases();
#endif
 plain_cases();denials();authority_headers_bounds();typed_documents();worst_case_controls();stalled(0,0);connect_deadline();request_head_limits();framing_byte_limits();cancellation(0);cancellation(1);mcp_denial();if(!getenv("APX_HTTP_PACKAGE"))resolver_deadline();else if(!strcmp(getenv("APX_HTTP_PACKAGE"),"plain"))missing_tls();
#ifdef APX_HTTP_OPENSSL
 tls_cases();
#endif
 cleanup();apx_json_reset();int status;errno=0;require(waitpid(-1,&status,WNOHANG)==-1&&errno==ECHILD,"no unreaped fixture/CLI children");printf("HTTP inventory: %u cases, %u checks, 0 skips PASS; foreign_monitor_probes=%u\n",cases,checks,foreign_monitor_probes);return 0;}
#endif
