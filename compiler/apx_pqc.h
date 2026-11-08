#ifndef APX_PQC_H
#define APX_PQC_H
#include "apx_http.h"
#define APX_PQC_FORMAT "cms-authenveloped-mlkem768-aes256gcm-v1"
/* Trusted internal interfaces; the language sees only the two JSON builtins. */
ApxValue *apx_pqc_parse(const char *text,size_t length,unsigned structure_limit);
int apx_pqc_slot(const char *name);
const char *apx_pqc_request(ApxValue *value,int opening,const char **slot);
char *apx_pqc_base64(const unsigned char *data,size_t length);
#ifdef APX_PQC_OPENSSL
int apx_pqc_init(void);
char *apx_pqc_native(const char *mode,ApxValue *request,size_t maximum);
char *apx_pqc_generate(const char *directory);
#endif
#endif
