#ifndef APX_HTTP_H
#define APX_HTTP_H
#include <stddef.h>
#include <stdint.h>
#include "../runtime/apx_runtime.h"
#define APX_HTTP_FRAME (8u*1048576u)
#define APX_HTTP_BODY 1048576u
#define APX_HTTP_HEADERS 128u
#define APX_HTTP_HEADER_BYTES 65536u
#define APX_HTTP_LINE 8192u
/* Trusted internal transport contract, not an embedding or language API. */
typedef struct {char host[256],authority[300],target[8193];unsigned port;int tls,ip;} ApxHttpUrl;
typedef struct {char *name;unsigned char *value;size_t length;} ApxHttpHeader;
/* pq_required is trusted broker context, set only after guest Request parsing. */
typedef struct {ApxHttpUrl url;const char *method;ApxHttpHeader headers[APX_HTTP_HEADERS];size_t count;unsigned char *body;size_t length;int pq_required;} ApxHttpRequest;
int64_t apx_http_now(void);
int apx_http_wait(int fd,short events,int64_t deadline);
int apx_http_url(const char *text,int conservative,ApxHttpUrl *url);
int apx_http_prefix(const char *text);
int apx_http_token(const char *text);
int apx_http_method(const char *text);
int apx_http_reserved(const char *name);
int apx_http_utf8(const unsigned char *bytes,size_t length);
const char *apx_http_string(ApxValue *value);
/* Reject impossible request shapes before allocating the bounded JSON DOM. */
ApxValue *apx_http_parse_request(const char *text,size_t length);
int apx_http_object(ApxValue *value,const char *const *keys,size_t count);
const char *apx_http_request(ApxValue *value,size_t maximum,int legacy,ApxHttpRequest *request);
void apx_http_request_free(ApxHttpRequest *request);
unsigned char *apx_http_decode_body(ApxValue *value,size_t maximum,size_t *length);
ApxValue *apx_http_body(const unsigned char *data,size_t length);
char *apx_http_failure(const char *token);
/* One connection, no redirects, replay, decompression or inherited credentials. */
char *apx_http_exchange(ApxHttpRequest *request,size_t maximum,int64_t deadline,const char *ca);
int apx_http_ca_valid(const char *ca);
#endif
