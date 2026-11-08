#define _GNU_SOURCE
#include "apx_http.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#ifdef APX_HTTP_OPENSSL
#include <openssl/ssl.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/x509v3.h>
#endif
int64_t apx_http_now(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return INT64_MAX;return (int64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
int apx_http_wait(int fd,short events,int64_t deadline){for(;;){int64_t left=deadline-apx_http_now();if(left<=0)return 0;struct pollfd p={fd,events,0};int k=poll(&p,1,left>INT_MAX?INT_MAX:(int)left);if(k<0&&errno==EINTR)continue;return k>0&&!(p.revents&POLLNVAL)&&(p.revents&(events|POLLERR|POLLHUP));}}
const char *apx_http_string(ApxValue *v){const char *s=apx_json_string(v);return s&&strlen(s)==apx_json_length(v)?s:NULL;}
ApxValue *apx_http_parse_request(const char *text,size_t length){
 /* A valid Request has at most 128 five-value Header records and fixed leaves.
  * Bound structural work without parsing string data as JSON or changing the
  * general runtime parser. Oversized invalid trees cannot poison its arena and
  * leave error construction without storage. The strict parser remains final. */
 if(!text||length>APX_HTTP_FRAME)return NULL;
 size_t at=0;while(at<length&&(text[at]==' '||text[at]=='\t'||text[at]=='\r'||text[at]=='\n'))at++;
 if(at==length||text[at]!='{')return NULL;
 unsigned punctuation=0;int quoted=0,escaped=0;
 for(;at<length;at++){unsigned char c=(unsigned char)text[at];if(!c)return NULL;
  if(quoted){if(escaped)escaped=0;else if(c=='\\')escaped=1;else if(c=='"')quoted=0;}
  else if(c=='"')quoted=1;
  else if(strchr("{}[],:",c)&&++punctuation>16u*APX_HTTP_HEADERS+32u)return NULL;
 }
 if(quoted)return NULL;
 return apx_json_parse(text,length);
}
int apx_http_object(ApxValue *v,const char *const *keys,size_t n){if(apx_json_kind(v)!=6||apx_json_length(v)!=n)return 0;for(size_t i=0;i<n;i++)if(!apx_json_get(v,keys[i]))return 0;return 1;}
static int ascii_alnum(unsigned c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9');}
int apx_http_token(const char *s){if(!s||!*s)return 0;for(;*s;s++)if(!ascii_alnum((unsigned char)*s)&&!strchr("!#$%&'*+-.^_`|~",*s))return 0;return 1;}
int apx_http_method(const char *s){const char *all[]={"GET","HEAD","POST","PUT","PATCH","DELETE","OPTIONS"};for(size_t i=0;s&&i<7;i++)if(!strcmp(s,all[i]))return 1;return 0;}
int apx_http_reserved(const char *s){const char *all[]={"host","content-length","transfer-encoding","connection","expect","upgrade","te","trailer","accept-encoding","proxy-authorization","proxy-connection"};for(size_t i=0;i<sizeof all/sizeof all[0];i++)if(!strcasecmp(s,all[i]))return 1;return 0;}
int apx_http_utf8(const unsigned char *s,size_t n){for(size_t i=0;i<n;){unsigned c=s[i++],v,min;size_t more;if(c<128)continue;if(c>=0xc2&&c<=0xdf){v=c&31;min=128;more=1;}else if(c>=0xe0&&c<=0xef){v=c&15;min=2048;more=2;}else if(c>=0xf0&&c<=0xf4){v=c&7;min=65536;more=3;}else return 0;if(more>n-i)return 0;while(more--){c=s[i++];if((c&0xc0)!=0x80)return 0;v=(v<<6)|(c&63);}if(v<min||v>0x10ffff||(v>=0xd800&&v<=0xdfff))return 0;}return 1;}
static int hex(unsigned c){if(c>='0'&&c<='9')return c-'0';if(c>='a'&&c<='f')return c-'a'+10;if(c>='A'&&c<='F')return c-'A'+10;return -1;}
static int conservative_path(const char *s,size_t n){size_t start=0;for(size_t i=0;i<=n;i++){if(i==n||s[i]=='/'){size_t k=i-start;if((k==1&&s[start]=='.')||(k==2&&s[start]=='.'&&s[start+1]=='.'))return 0;start=i+1;}if(i<n&&s[i]=='%'){if(i+2>=n||hex(s[i+1])<0||hex(s[i+2])<0)return 0;unsigned c=(unsigned)(hex(s[i+1])*16+hex(s[i+2]));if(c<=32||c==127||c=='.'||c=='/'||c=='\\'||c=='%')return 0;i+=2;}}return 1;}
int apx_http_prefix(const char *s){size_t n=s?strlen(s):0;if(!n||n>8192||s[0]!='/'||s[n-1]!='/'||strpbrk(s,"?#\\"))return 0;for(size_t i=0;i<n;i++)if((unsigned char)s[i]<=32||(unsigned char)s[i]>126)return 0;return conservative_path(s,n);}
int apx_http_url(const char *s,int strict,ApxHttpUrl *u){memset(u,0,sizeof *u);if(!s||strlen(s)>8192)return 0;size_t scheme;if(!strncmp(s,"http://",7)){scheme=7;u->port=80;}else if(!strncmp(s,"https://",8)){scheme=8;u->tls=1;u->port=443;}else return 0;for(const unsigned char *p=(const unsigned char*)s;*p;p++)if(*p<=32||*p>126||*p=='\\')return 0;const char *a=s+scheme,*end=strpbrk(a,"/?#");if(!end)end=s+strlen(s);if(a==end||memchr(a,'@',(size_t)(end-a))||(size_t)(end-a)>=sizeof u->authority)return 0;memcpy(u->authority,a,(size_t)(end-a));const char *h=a,*he=end,*port=NULL;if(*a=='['){he=memchr(a,']',(size_t)(end-a));if(!he||he==a+1)return 0;h=a+1;if(he+1<end){if(he[1]!=':')return 0;port=he+2;}u->ip=1;}else{const char *colon=memchr(a,':',(size_t)(end-a));if(colon){he=colon;port=colon+1;}}
 size_t hn=(size_t)(he-h);if(!hn||hn>=sizeof u->host)return 0;for(size_t i=0;i<hn;i++){unsigned c=(unsigned char)h[i];u->host[i]=(char)(c>='A'&&c<='Z'?c+32:c);}unsigned char addr[16];if(*a=='['){if(inet_pton(AF_INET6,u->host,addr)!=1)return 0;}else if(inet_pton(AF_INET,u->host,addr)==1)u->ip=1;else{size_t label=0;for(size_t i=0;i<hn;i++){unsigned c=(unsigned char)u->host[i];if(c=='.'){if(!label||label>63||u->host[i-1]=='-')return 0;label=0;}else{if(!ascii_alnum(c)&&c!='-')return 0;if(!label&&c=='-')return 0;label++;}}if(!label||label>63||u->host[hn-1]=='-')return 0;}
 if(port){if(port==end)return 0;unsigned p=0;for(const char *q=port;q<end;q++){if(*q<'0'||*q>'9'||p>6553)return 0;p=p*10+(unsigned)(*q-'0');if(p>65535)return 0;}if(!p)return 0;u->port=p;}
 const char *frag=strchr(end,'#');if(strict&&frag)return 0;const char *stop=frag?frag:s+strlen(s);size_t n=(size_t)(stop-end);if(!n)strcpy(u->target,"/");else if(*end=='?'){u->target[0]='/';memcpy(u->target+1,end,n);}else if(*end=='/')memcpy(u->target,end,n);else if(*end=='#')strcpy(u->target,"/");else return 0;const char *query=strchr(u->target,'?');size_t path=query?(size_t)(query-u->target):strlen(u->target);return !strict||conservative_path(u->target,path);}
static const char b64[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static int digit(unsigned c){const char *p=c?strchr(b64,(int)c):NULL;return p?(int)(p-b64):-1;}
static unsigned char *decode_body(ApxValue *v,size_t max,size_t *length){static const char *const keys[]={"encoding","data"};if(!apx_http_object(v,keys,2))return NULL;const char *enc=apx_http_string(apx_json_get(v,"encoding")),*s=apx_json_string(apx_json_get(v,"data"));size_t n=apx_json_length(apx_json_get(v,"data"));if(!enc||!s)return NULL;if(!strcmp(enc,"utf8")){if(n>max||!apx_http_utf8((const unsigned char*)s,n))return NULL;unsigned char *p=malloc(n+1);if(p){memcpy(p,s,n);*length=n;}return p;}if(strcmp(enc,"base64")||n%4||n>4*((max+2)/3))return NULL;size_t pad=n&&s[n-1]=='='?1:0;if(n>1&&s[n-2]=='=')pad++;size_t raw=n/4*3-pad;if(raw>max)return NULL;unsigned char *p=malloc(raw+1);if(!p)return NULL;size_t at=0;for(size_t i=0;i<n;i+=4){int a=digit((unsigned char)s[i]),b=digit((unsigned char)s[i+1]),c=digit((unsigned char)s[i+2]),d=digit((unsigned char)s[i+3]);int last=i+4==n;if(a<0||b<0||(c<0&&!(last&&pad==2&&s[i+2]=='='))||(d<0&&!(last&&pad&&s[i+3]=='='))||(last&&pad==2&&(b&15))||(last&&pad==1&&(c&3))){free(p);return NULL;}p[at++]=(unsigned char)((a<<2)|(b>>4));if(at<raw)p[at++]=(unsigned char)((b<<4)|(c>>2));if(at<raw)p[at++]=(unsigned char)((c<<6)|d);}*length=raw;return p;}
unsigned char *apx_http_decode_body(ApxValue *v,size_t max,size_t *length){return decode_body(v,max,length);}
ApxValue *apx_http_body(const unsigned char *data,size_t n){ApxValue *v=apx_record_new("Body",2);int text=apx_http_utf8(data,n);apx_record_put(v,0,"encoding",apx_string(text?"utf8":"base64",text?4:6));if(text)apx_record_put(v,1,"data",apx_string((const char*)data,(int64_t)n));else{size_t k=4*((n+2)/3);char *s=malloc(k+1);if(!s)return NULL;size_t at=0;for(size_t i=0;i<n;i+=3){unsigned a=data[i],b=i+1<n?data[i+1]:0,c=i+2<n?data[i+2]:0;s[at++]=b64[a>>2];s[at++]=b64[((a&3)<<4)|(b>>4)];s[at++]=i+1<n?b64[((b&15)<<2)|(c>>6)]:'=';s[at++]=i+2<n?b64[c&63]:'=';}apx_record_put(v,1,"data",apx_string(s,(int64_t)k));free(s);}return v;}
static int field_value(const unsigned char *s,size_t n){for(size_t i=0;i<n;i++)if((s[i]<32&&s[i]!='\t')||s[i]==127)return 0;return 1;}
void apx_http_request_free(ApxHttpRequest *r){for(size_t i=0;i<r->count;i++){free(r->headers[i].name);free(r->headers[i].value);}free(r->body);memset(r,0,sizeof *r);}
static int request_head(char *out,size_t size,const ApxHttpRequest *r){return snprintf(out,size,"%s %s HTTP/1.1\r\nHost: %s\r\nContent-Length: %zu\r\nConnection: close\r\nAccept-Encoding: identity\r\n",r->method,r->url.target,r->url.authority,r->length);}
const char *apx_http_request(ApxValue *v,size_t maximum,int legacy,ApxHttpRequest *r){memset(r,0,sizeof *r);static const char *const keys[]={"method","url","headers","body"},*const hk[]={"name","value"};if(!apx_http_object(v,keys,4))return "invalid_http_request";r->method=apx_http_string(apx_json_get(v,"method"));if(!apx_http_method(r->method)||!apx_http_url(apx_http_string(apx_json_get(v,"url")),!legacy,&r->url))return "invalid_http_request";ApxValue *h=apx_json_get(v,"headers");if(apx_json_kind(h)!=5)return "invalid_http_request";if(apx_json_length(h)>APX_HTTP_HEADERS-4)return "http_header_limit";size_t total=0;for(size_t i=0;i<apx_json_length(h);i++){ApxValue *hv=apx_json_item(h,i);if(!apx_http_object(hv,hk,2))return "invalid_http_request";const char *name=apx_http_string(apx_json_get(hv,"name"));if(!apx_http_token(name)||apx_http_reserved(name))return "invalid_http_request";size_t nn=strlen(name),n=0;if(nn>APX_HTTP_LINE)return "http_header_limit";unsigned char *val=decode_body(apx_json_get(hv,"value"),APX_HTTP_LINE,&n);if(!val)return "invalid_http_request";if(!field_value(val,n)){free(val);return "invalid_http_request";}size_t start=0;while(start<n&&(val[start]==' '||val[start]=='\t'))start++;while(n>start&&(val[n-1]==' '||val[n-1]=='\t'))n--;n-=start;memmove(val,val+start,n);if(nn+n+2>APX_HTTP_LINE||total+nn+n+4>APX_HTTP_HEADER_BYTES){free(val);return "http_header_limit";}char *lower=strdup(name);if(!lower){free(val);return "http_transport_failed";}for(size_t j=0;j<nn;j++)if(lower[j]>='A'&&lower[j]<='Z')lower[j]+=32;r->headers[r->count++]=(ApxHttpHeader){lower,val,n};total+=nn+n+4;}
 r->body=decode_body(apx_json_get(v,"body"),maximum,&r->length);if(!r->body){ApxValue *b=apx_json_get(v,"body");const char *enc=apx_http_string(apx_json_get(b,"encoding"));size_t n=apx_json_length(apx_json_get(b,"data"));return enc&&((!strcmp(enc,"utf8")&&n>maximum)||(!strcmp(enc,"base64")&&n>4*((maximum+2)/3)))?"http_byte_limit":"invalid_http_request";}
 char head[9000];int n=request_head(head,sizeof head,r);
 if(strlen(r->method)+strlen(r->url.target)+10>APX_HTTP_LINE||n<0||(size_t)n>=sizeof head||(size_t)n+total+2>APX_HTTP_HEADER_BYTES)return "http_header_limit";
 return NULL;}
char *apx_http_failure(const char *token){ApxValue *v=apx_record_new("host",2);apx_record_put(v,0,"ok",apx_bool(0));apx_record_put(v,1,"error",apx_string(token,(int64_t)strlen(token)));return apx_json_encode(v);}
struct Transport {int fd;int64_t deadline;const char *error;
#ifdef APX_HTTP_OPENSSL
 SSL_CTX *ctx;SSL *ssl;
#endif
};
static void transport_close(struct Transport *t){
#ifdef APX_HTTP_OPENSSL
 if(t->ssl)SSL_free(t->ssl);
 if(t->ctx)SSL_CTX_free(t->ctx);
#endif
 if(t->fd>=0)close(t->fd);}
#ifdef APX_HTTP_OPENSSL
static int add_ca(SSL_CTX *ctx,const char *ca){
 const char *p=ca;unsigned count=0;
 for(;;){while(*p==' '||*p=='\t'||*p=='\r'||*p=='\n')p++;if(!*p)return count>0;
 if(strncmp(p,"-----BEGIN CERTIFICATE-----",27))return 0;
 const char *end=strstr(p,"-----END CERTIFICATE-----");if(!end)return 0;end+=25;
 BIO *bio=BIO_new_mem_buf(p,(int)(end-p));if(!bio)return 0;X509 *x=PEM_read_bio_X509(bio,NULL,NULL,NULL);BIO_free(bio);if(!x){ERR_clear_error();return 0;}int ok=X509_STORE_add_cert(SSL_CTX_get_cert_store(ctx),x);X509_free(x);if(!ok){ERR_clear_error();return 0;}count++;p=end;
 }
}
#endif
int apx_http_ca_valid(const char *ca){
#ifdef APX_HTTP_OPENSSL
 if(!OPENSSL_init_ssl(OPENSSL_INIT_NO_LOAD_CONFIG,NULL))return 0;
 SSL_CTX *ctx=SSL_CTX_new(TLS_client_method());if(!ctx)return 0;int ok=add_ca(ctx,ca);SSL_CTX_free(ctx);return ok;
#else
 (void)ca;return 0;
#endif
}
static int transport_connect(struct Transport *t,ApxHttpUrl *u,const char *ca,int pq_required){
 if(pq_required&&!u->tls){t->error="http_tls_failed";return 0;}
#ifndef APX_HTTP_OPENSSL
 (void)ca;if(u->tls){t->error="http_tls_unavailable";return 0;}
#else
 /* Resolve the fixed required suite before DNS/connect. No provider/config path
  * is accepted. String group lookup works with older OpenSSL 3.x headers too. */
 if(u->tls&&pq_required){
  t->error="http_pq_unavailable";
  if(!OPENSSL_init_ssl(OPENSSL_INIT_NO_LOAD_CONFIG,NULL))return 0;
  t->ctx=SSL_CTX_new(TLS_client_method());if(!t->ctx)return 0;
  if(!SSL_CTX_set_min_proto_version(t->ctx,TLS1_3_VERSION)||!SSL_CTX_set_max_proto_version(t->ctx,TLS1_3_VERSION)||!SSL_CTX_set1_groups_list(t->ctx,"X25519MLKEM768")||!SSL_CTX_set_ciphersuites(t->ctx,"TLS_AES_256_GCM_SHA384"))return 0;
 }
#endif
 char port[8];snprintf(port,sizeof port,"%u",u->port);struct addrinfo hints={0},*list=NULL;hints.ai_family=AF_UNSPEC;hints.ai_socktype=SOCK_STREAM;hints.ai_protocol=IPPROTO_TCP;if(getaddrinfo(u->host,port,&hints,&list)){t->error="http_resolve_failed";return 0;}for(struct addrinfo *a=list;a&&apx_http_now()<t->deadline;a=a->ai_next){t->fd=socket(a->ai_family,SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK,a->ai_protocol);if(t->fd<0)continue;int ok=connect(t->fd,a->ai_addr,a->ai_addrlen)==0;if(!ok&&errno==EINPROGRESS&&apx_http_wait(t->fd,POLLOUT,t->deadline)){int error=0;socklen_t n=sizeof error;ok=!getsockopt(t->fd,SOL_SOCKET,SO_ERROR,&error,&n)&&!error;}if(ok)break;close(t->fd);t->fd=-1;}freeaddrinfo(list);if(t->fd<0){t->error=apx_http_now()>=t->deadline?"http_timeout":"http_connect_failed";return 0;}
#ifdef APX_HTTP_OPENSSL
 if(u->tls){t->error="http_tls_failed";
  /* Keep standard/old-policy connection and trust-failure ordering unchanged. */
  if(!pq_required){if(!OPENSSL_init_ssl(OPENSSL_INIT_NO_LOAD_CONFIG,NULL))return 0;t->ctx=SSL_CTX_new(TLS_client_method());if(!t->ctx||!SSL_CTX_set_min_proto_version(t->ctx,TLS1_2_VERSION)||!SSL_CTX_set_max_proto_version(t->ctx,TLS1_3_VERSION))return 0;}
  SSL_CTX_set_verify(t->ctx,SSL_VERIFY_PEER,NULL);if(ca&&*ca){if(!add_ca(t->ctx,ca))return 0;}else if(!SSL_CTX_set_default_verify_paths(t->ctx))return 0;
  t->ssl=SSL_new(t->ctx);if(!t->ssl||!SSL_set_fd(t->ssl,t->fd))return 0;X509_VERIFY_PARAM *param=SSL_get0_param(t->ssl);X509_VERIFY_PARAM_set_hostflags(param,X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);if(u->ip){if(!X509_VERIFY_PARAM_set1_ip_asc(param,u->host))return 0;}else if(!SSL_set1_host(t->ssl,u->host)||!SSL_set_tlsext_host_name(t->ssl,u->host))return 0;static const unsigned char alpn[]={8,'h','t','t','p','/','1','.','1'};if(SSL_set_alpn_protos(t->ssl,alpn,sizeof alpn))return 0;for(;;){int k=SSL_connect(t->ssl);if(k==1)break;int err=SSL_get_error(t->ssl,k);short wait=err==SSL_ERROR_WANT_READ?POLLIN:err==SSL_ERROR_WANT_WRITE?POLLOUT:0;if(!wait)return 0;if(!apx_http_wait(t->fd,wait,t->deadline)){t->error="http_timeout";return 0;}}const unsigned char *chosen=NULL;unsigned n=0;SSL_get0_alpn_selected(t->ssl,&chosen,&n);if(n&&(n!=8||memcmp(chosen,"http/1.1",8)))return 0;if(SSL_get_verify_result(t->ssl)!=X509_V_OK)return 0;
  if(pq_required){
   /* Independently gate the negotiated result before writing any HTTP bytes.
    * These public group APIs are available since OpenSSL 3.0; no ML-KEM NID
    * or 3.5-only convenience API is required at compile time. */
   const char *group=SSL_group_to_name(t->ssl,SSL_get_negotiated_group(t->ssl));
   const SSL_CIPHER *cipher=SSL_get_current_cipher(t->ssl);
   if(SSL_version(t->ssl)!=TLS1_3_VERSION||!group||strcmp(group,"X25519MLKEM768")||!cipher||strcmp(SSL_CIPHER_get_name(cipher),"TLS_AES_256_GCM_SHA384")||n!=8)return 0;
  }
 }
#endif
 t->error="http_transport_failed";return 1;}
static ssize_t transfer(struct Transport *t,void *data,size_t n,int writing){for(;;){if(apx_http_now()>=t->deadline){t->error="http_timeout";return -1;}
#ifdef APX_HTTP_OPENSSL
 if(t->ssl){int k=writing?SSL_write(t->ssl,data,(int)n):SSL_read(t->ssl,data,(int)n);if(k>0)return k;int err=SSL_get_error(t->ssl,k);if(!writing&&err==SSL_ERROR_ZERO_RETURN)return 0;short wait=err==SSL_ERROR_WANT_READ?POLLIN:err==SSL_ERROR_WANT_WRITE?POLLOUT:0;if(!wait){t->error="http_tls_failed";return -1;}if(!apx_http_wait(t->fd,wait,t->deadline)){t->error="http_timeout";return -1;}continue;}
#endif
 ssize_t k=writing?send(t->fd,data,n,MSG_NOSIGNAL):recv(t->fd,data,n,0);if(k>=0)return k;if(errno==EINTR)continue;if(errno!=EAGAIN&&errno!=EWOULDBLOCK)return -1;if(!apx_http_wait(t->fd,writing?POLLOUT:POLLIN,t->deadline)){t->error="http_timeout";return -1;}}}
static int write_all(struct Transport *t,const void *data,size_t n){size_t at=0;while(at<n){ssize_t k=transfer(t,(char*)data+at,n-at,1);if(k<=0)return 0;at+=(size_t)k;}return 1;}
struct Reader {struct Transport *t;unsigned char data[8192];size_t at,n,wire,header_bytes,header_count;};
static int byte(struct Reader *r){if(r->at==r->n){ssize_t n=transfer(r->t,r->data,sizeof r->data,0);if(n<=0)return n<0?-2:-1;r->n=(size_t)n;r->at=0;r->wire+=(size_t)n;if(r->wire>APX_HTTP_FRAME){r->t->error="http_byte_limit";return -2;}}return r->data[r->at++];}
static int line(struct Reader *r,char s[APX_HTTP_LINE+1],int header){size_t n=0;for(;;){int c=byte(r);if(c<0)return 0;if(c=='\r'){if(byte(r)!='\n')return 0;break;}if(n>=APX_HTTP_LINE){r->t->error=header?"http_header_limit":"http_protocol_error";return 0;}if(c=='\n'||c==0)return 0;s[n++]=(char)c;}s[n]=0;if(header){r->header_bytes+=n+2;if(r->header_bytes>APX_HTTP_HEADER_BYTES){r->t->error="http_header_limit";return 0;}}return 1;}
static int decimal(const char *s,size_t *out){if(!*s)return 0;size_t n=0;for(;*s;s++){if(*s<'0'||*s>'9'||n>(SIZE_MAX-(unsigned)(*s-'0'))/10)return 0;n=n*10+(unsigned)(*s-'0');}*out=n;return 1;}
static ApxValue *header_value(const char *name,const unsigned char *data,size_t n){ApxValue *v=apx_record_new("Header",2);apx_record_put(v,0,"name",apx_string(name,(int64_t)strlen(name)));apx_record_put(v,1,"value",apx_http_body(data,n));return v;}
struct Headers {ApxValue *values[APX_HTTP_HEADERS];size_t n,length;int cl,te;};
static int connection_upgrade(const unsigned char *value){
 const char *p=(const char*)value;
 for(;;){while(*p==' '||*p=='\t')p++;const char *start=p;
 while(*p&&(ascii_alnum((unsigned char)*p)||strchr("!#$%&'*+-.^_`|~",*p)))p++;
 size_t n=(size_t)(p-start);if(!n)return -1;if(n==7&&!strncasecmp(start,"upgrade",7))return 1;
 while(*p==' '||*p=='\t')p++;
 if(!*p)return 0;
 if(*p++!=',')return -1;
 }
}
static int read_headers(struct Reader *r,struct Headers *h,int trailer){char s[APX_HTTP_LINE+1];for(;;){if(!line(r,s,1))return 0;if(!*s)return 1;if(++r->header_count>APX_HTTP_HEADERS){r->t->error="http_header_limit";return 0;}char *colon=strchr(s,':');if(!colon)return 0;*colon=0;if(!apx_http_token(s))return 0;for(char *p=s;*p;p++)if(*p>='A'&&*p<='Z')*p+=32;unsigned char *value=(unsigned char*)colon+1;size_t n=strlen((char*)value);if(!field_value(value,n))return 0;while(n&&(*value==' '||*value=='\t')){value++;n--;}while(n&&(value[n-1]==' '||value[n-1]=='\t'))n--;value[n]=0;if(!trailer&&(!strcmp(s,"upgrade")||(!strcmp(s,"connection")&&connection_upgrade(value)!=0)))return 0;if(trailer&&(apx_http_reserved(s)||!strcmp(s,"authorization")||!strcmp(s,"content-encoding")||!strcmp(s,"content-type")||!strcmp(s,"content-range")))return 0;if(!strcmp(s,"content-length")){if(h->cl++||h->te||!decimal((char*)value,&h->length))return 0;}if(!strcmp(s,"transfer-encoding")){if(h->te++||h->cl||strcasecmp((char*)value,"chunked"))return 0;}h->values[h->n++]=header_value(s,value,n);}}
static ApxValue *headers_array(struct Headers *h){ApxValue *a=apx_array_new((int64_t)h->n);for(size_t i=0;i<h->n;i++)apx_array_put(a,(int64_t)i,h->values[i]);return a;}
static int chunk_size(const char *s,size_t *size){
 size_t n=0;const char *p=s;
 while(hex((unsigned char)*p)>=0){unsigned d=(unsigned)hex((unsigned char)*p++);if(n>(SIZE_MAX-d)/16)return 0;n=n*16+d;}
 if(p==s)return 0;
 /* RFC 9112 chunk-ext: BWS around delimiters; token or quoted value.
  * BWS does not authorize bare trailing whitespace or control bytes. */
 while(*p){
  while(*p==' '||*p=='\t')p++;
  if(*p++!=';')return 0;
  while(*p==' '||*p=='\t')p++;
  const char *start=p;
  while(*p&&(ascii_alnum((unsigned char)*p)||strchr("!#$%&'*+-.^_`|~",*p)))p++;
  if(p==start)return 0;
  const char *after_name=p;
  while(*p==' '||*p=='\t')p++;
  if(*p=='='){
   p++;while(*p==' '||*p=='\t')p++;
   if(*p=='"'){
    p++;int closed=0;
    while(*p){unsigned c=(unsigned char)*p++;if(c=='"'){closed=1;break;}if(c=='\\'){if(!*p)return 0;c=(unsigned char)*p++;}if((c<32&&c!='\t')||c==127)return 0;}
    if(!closed)return 0;
   }else{
    start=p;while(*p&&(ascii_alnum((unsigned char)*p)||strchr("!#$%&'*+-.^_`|~",*p)))p++;
    if(p==start)return 0;
   }
  }else p=after_name;
 }
 *size=n;return 1;
}
char *apx_http_exchange(ApxHttpRequest *req,size_t maximum,int64_t deadline,const char *ca){struct Transport t={.fd=-1,.deadline=deadline,.error="http_transport_failed"};char *result=NULL;unsigned char *body=NULL;struct Headers headers={0},trailers={0};if(!transport_connect(&t,&req->url,ca,req->pq_required))goto done;char first[9000];int k=request_head(first,sizeof first,req);if(k<0||(size_t)k>=sizeof first||!write_all(&t,first,(size_t)k))goto done;for(size_t i=0;i<req->count;i++){ApxHttpHeader *h=&req->headers[i];if(!write_all(&t,h->name,strlen(h->name))||!write_all(&t,": ",2)||!write_all(&t,h->value,h->length)||!write_all(&t,"\r\n",2))goto done;}if(!write_all(&t,"\r\n",2)||!write_all(&t,req->body,req->length))goto done;
 struct Reader r={.t=&t};int status=0;for(unsigned info=0;;info++){char s[APX_HTTP_LINE+1];t.error="http_protocol_error";if(!line(&r,s,1)||strlen(s)<12||(strncmp(s,"HTTP/1.1 ",9)&&strncmp(s,"HTTP/1.0 ",9))||s[9]<'1'||s[9]>'5'||s[10]<'0'||s[10]>'9'||s[11]<'0'||s[11]>'9'||s[12]!=' ')goto done;for(size_t i=12;s[i];i++)if(((unsigned char)s[i]<32&&s[i]!='\t')||(unsigned char)s[i]==127)goto done;status=(s[9]-'0')*100+(s[10]-'0')*10+s[11]-'0';memset(&headers,0,sizeof headers);if(!read_headers(&r,&headers,0)||(!strncmp(s,"HTTP/1.0 ",9)&&headers.te))goto done;if(status>=200)break;if(status==101||info>=10||headers.te||headers.cl)goto done;}
 body=malloc(maximum+1);if(!body){t.error="http_transport_failed";goto done;}size_t used=0;if(strcmp(req->method,"HEAD")&&status!=204&&status!=304){if(headers.te){for(;;){char s[APX_HTTP_LINE+1];size_t n;if(!line(&r,s,0)||!chunk_size(s,&n))goto done;if(!n){if(!read_headers(&r,&trailers,1))goto done;break;}if(n>maximum-used){t.error="http_byte_limit";goto done;}for(size_t i=0;i<n;i++){int c=byte(&r);if(c<0)goto done;body[used++]=(unsigned char)c;}if(byte(&r)!='\r'||byte(&r)!='\n')goto done;}}else if(headers.cl){if(headers.length>maximum){t.error="http_byte_limit";goto done;}while(used<headers.length){int c=byte(&r);if(c<0)goto done;body[used++]=(unsigned char)c;}}else{for(;;){int c=byte(&r);if(c==-1)break;if(c<0)goto done;if(used==maximum){t.error="http_byte_limit";goto done;}body[used++]=(unsigned char)c;}}}
 {ApxValue *response=apx_record_new("Response",4);apx_record_put(response,0,"status",apx_int(status));apx_record_put(response,1,"headers",headers_array(&headers));apx_record_put(response,2,"trailers",headers_array(&trailers));apx_record_put(response,3,"body",apx_http_body(body,used));char *wire=apx_json_encode(response);if(!wire){t.error="http_byte_limit";goto done;}ApxValue *ok=apx_record_new("host",2);apx_record_put(ok,0,"ok",apx_bool(1));apx_record_put(ok,1,"value",apx_string(wire,(int64_t)strlen(wire)));free(wire);result=apx_json_encode(ok);}
 done:free(body);transport_close(&t);return result?result:apx_http_failure(apx_http_now()>=deadline?"http_timeout":t.error);}
