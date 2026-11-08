#define _GNU_SOURCE
#include "apx_pqc.h"
#include <stdlib.h>
#include <string.h>

ApxValue *apx_pqc_parse(const char *text,size_t length,unsigned limit){
    /* Fixed-shape messages have few structural tokens. Count only outside
     * strings, before the generic arena parser can allocate attacker nodes. */
    if(!text||length>APX_HTTP_FRAME)return NULL;
    unsigned tokens=0;int quoted=0,escaped=0;
    for(size_t i=0;i<length;i++){
        unsigned char c=(unsigned char)text[i];if(!c)return NULL;
        if(quoted){if(escaped)escaped=0;else if(c=='\\')escaped=1;else if(c=='"')quoted=0;}
        else if(c=='"')quoted=1;
        else if(strchr("{}[],:",c)&&++tokens>limit)return NULL;
    }
    return quoted?NULL:apx_json_parse(text,length);
}
int apx_pqc_slot(const char *s){
    if(!s||!*s||strlen(s)>64)return 0;
    for(;*s;s++)if(!((*s>='a'&&*s<='z')||(*s>='A'&&*s<='Z')||(*s>='0'&&*s<='9')||*s=='_'||*s=='-'))return 0;
    return 1;
}
const char *apx_pqc_request(ApxValue *v,int opening,const char **slot){
    const char *const seal[]={"key","body"},*const open[]={"key","envelope"},*const body[]={"encoding","data"},*const env[]={"format","data"};
    if(!apx_http_object(v,opening?open:seal,2))return "invalid_pqc_request";
    *slot=apx_http_string(apx_json_get(v,"key"));if(!apx_pqc_slot(*slot))return "invalid_pqc_request";
    ApxValue *b=apx_json_get(v,opening?"envelope":"body");
    if(!apx_http_object(b,opening?env:body,2)||!apx_json_string(apx_json_get(b,"data"))||!apx_http_string(apx_json_get(b,opening?"format":"encoding")))return "invalid_pqc_request";
    return NULL;
}
char *apx_pqc_base64(const unsigned char *data,size_t n){
    static const char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t size=4*((n+2)/3),at=0;char *s=malloc(size+1);if(!s)return NULL;
    for(size_t i=0;i<n;i+=3){unsigned a=data[i],b=i+1<n?data[i+1]:0,c=i+2<n?data[i+2]:0;
        s[at++]=alphabet[a>>2];s[at++]=alphabet[((a&3)<<4)|(b>>4)];s[at++]=i+1<n?alphabet[((b&15)<<2)|(c>>6)]:'=';s[at++]=i+2<n?alphabet[c&63]:'=';
    }s[at]=0;return s;
}
#ifdef APX_PQC_OPENSSL
#include "apx_pqc_profile.h"
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
#include <openssl/buffer.h>
#include <openssl/cms.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/provider.h>
#include <openssl/x509v3.h>
#if OPENSSL_VERSION_NUMBER < 0x40100000L
#error "PQC requires OpenSSL 4.1+ CMS KEM public APIs"
#endif
int apx_pqc_init(void){
    return OPENSSL_init_crypto(OPENSSL_INIT_NO_LOAD_CONFIG,NULL);
}
static char *success(ApxValue *v){
    char *doc=apx_json_encode(v);if(!doc)return apx_http_failure("pqc_encrypt_failed");
    ApxValue *out=apx_record_new("host",2);apx_record_put(out,0,"ok",apx_bool(1));apx_record_put(out,1,"value",apx_string(doc,(int64_t)strlen(doc)));free(doc);return apx_json_encode(out);
}
static int no_password(char *s,int n,int rw,void *u){(void)s;(void)n;(void)rw;(void)u;return 0;}
static int whitespace(const unsigned char *p,size_t n){for(size_t i=0;i<n;i++)if(!strchr(" \t\r\n",p[i]))return 0;return 1;}
static EVP_PKEY *snapshot_key(int private_key){
    struct stat st;int seals=fcntl(4,F_GET_SEALS),needed=F_SEAL_SEAL|F_SEAL_WRITE|F_SEAL_GROW|F_SEAL_SHRINK;
    if(seals<0||(seals&needed)!=needed||fstat(4,&st)||!S_ISREG(st.st_mode)||st.st_size<=0||st.st_size>(private_key?32768:16384))return NULL;
    size_t n=(size_t)st.st_size;unsigned char *data=malloc(n+1);if(!data)return NULL;
    size_t at=0;while(at<n){ssize_t k=pread(4,data+at,n-at,(off_t)at);if(k<0&&errno==EINTR)continue;if(k<=0)break;at+=(size_t)k;}data[n]=0;
    EVP_PKEY *key=NULL;BIO *bio=NULL;EVP_PKEY_CTX *ctx=NULL;
    const char *begin=private_key?"-----BEGIN PRIVATE KEY-----":"-----BEGIN PUBLIC KEY-----",*end=private_key?"-----END PRIVATE KEY-----":"-----END PUBLIC KEY-----";
    if(at!=n||memchr(data,0,n))goto done;
    size_t start=0;while(start<n&&strchr(" \t\r\n",data[start]))start++;
    if(strncmp((char*)data+start,begin,strlen(begin)))goto done;
    char *finish=strstr((char*)data+start,end);if(!finish)goto done;
    size_t used=(size_t)(finish-(char*)data)+strlen(end);
    if(!whitespace(data+used,n-used))goto done;
    bio=BIO_new_mem_buf(data+start,(int)(used-start));if(!bio)goto done;
    key=private_key?PEM_read_bio_PrivateKey(bio,NULL,no_password,NULL):PEM_read_bio_PUBKEY(bio,NULL,no_password,NULL);
    if(!key||!EVP_PKEY_is_a(key,"ML-KEM-768"))goto invalid;
    ctx=EVP_PKEY_CTX_new_from_pkey(NULL,key,"provider=default");
    if(!ctx||EVP_PKEY_public_check(ctx)!=1||(private_key&&(EVP_PKEY_private_check(ctx)!=1||EVP_PKEY_pairwise_check(ctx)!=1)))goto invalid;
    goto done;
invalid:EVP_PKEY_free(key);key=NULL;
done:EVP_PKEY_CTX_free(ctx);BIO_free(bio);OPENSSL_cleanse(data,n+1);free(data);ERR_clear_error();return key;
}
static int fingerprint(EVP_PKEY *key,unsigned char id[32]){
    unsigned char *der=NULL;unsigned n=0;int length=i2d_PUBKEY(key,&der);
    int ok=length>0&&EVP_Digest(der,(size_t)length,id,&n,EVP_sha256(),NULL)==1&&n==32;OPENSSL_free(der);return ok;
}
static X509 *carrier(EVP_PKEY *key,const unsigned char id[32]){
    /* Only an in-memory API carrier, never a serialized identity certificate. */
    X509 *x=X509_new();ASN1_OCTET_STRING *ski=ASN1_OCTET_STRING_new();
    int ok=x&&ski&&X509_set_version(x,2)&&ASN1_INTEGER_set(X509_get_serialNumber(x),1)&&X509_set_pubkey(x,key)&&ASN1_OCTET_STRING_set(ski,id,32)&&X509_add1_ext_i2d(x,NID_subject_key_identifier,ski,0,X509V3_ADD_DEFAULT);
    ASN1_OCTET_STRING_free(ski);if(!ok){X509_free(x);return NULL;}return x;
}
static unsigned char *envelope_bytes(ApxValue *env,size_t maximum,size_t *length){
    const char *format=apx_http_string(apx_json_get(env,"format"));if(!format||strcmp(format,APX_PQC_FORMAT))return NULL;
    ApxValue *body=apx_record_new("Body",2);apx_record_put(body,0,"encoding",apx_string("base64",6));apx_record_put(body,1,"data",apx_json_get(env,"data"));return apx_http_decode_body(body,maximum+4096,length);
}
char *apx_pqc_native(const char *mode,ApxValue *request,size_t maximum){
    int opening=!strcmp(mode,"open"),validate=!strcmp(mode,"validate-public")||!strcmp(mode,"validate-private");
    EVP_PKEY *key=snapshot_key(opening||!strcmp(mode,"validate-private"));
    if(!key)return apx_http_failure("pqc_key_unavailable");
    if(validate){EVP_PKEY_free(key);return apx_http_failure("pqc_key_valid");}
    unsigned char id[32],*data=NULL,*der=NULL;size_t n=0;X509 *recipient=NULL;CMS_ContentInfo *cms=NULL;BIO *input=NULL,*output=NULL;STACK_OF(X509) *recipients=NULL;char *result=NULL;
    if(!fingerprint(key,id)||(recipient=carrier(key,id))==NULL)goto done;
    if(opening){
        ApxValue *envelope=apx_json_get(request,"envelope");
        if(apx_json_length(apx_json_get(envelope,"data"))>4*((maximum+4096+2)/3)){result=apx_http_failure("pqc_byte_limit");goto done;}
        data=envelope_bytes(envelope,APX_HTTP_BODY,&n);if(!data)goto done;
        int profile=apx_pqc_profile(data,n,id,maximum);
        if(profile!=1){if(profile<0)result=apx_http_failure("pqc_byte_limit");goto done;}
        const unsigned char *cursor=data;cms=d2i_CMS_ContentInfo(NULL,&cursor,(long)n);output=BIO_new(BIO_s_mem());
        if(!cms||cursor!=data+n||!output||CMS_decrypt(cms,key,recipient,NULL,output,CMS_BINARY)!=1)goto done;
        char *plain=NULL;long size=BIO_get_mem_data(output,&plain);if(size<0||(size_t)size>maximum)goto done;
        result=success(apx_http_body((unsigned char*)plain,(size_t)size));
    }else{
        ApxValue *body=apx_json_get(request,"body");data=apx_http_decode_body(body,APX_HTTP_BODY,&n);
        if(data&&n>maximum){result=apx_http_failure("pqc_byte_limit");goto done;}
        if(!data){const char *enc=apx_http_string(apx_json_get(body,"encoding"));size_t text_n=apx_json_length(apx_json_get(body,"data"));result=apx_http_failure(enc&&((!strcmp(enc,"utf8")&&text_n>maximum)||(!strcmp(enc,"base64")&&text_n>4*((maximum+2)/3)))?"pqc_byte_limit":"invalid_pqc_request");goto done;}
        input=BIO_new_mem_buf(data,(int)n);recipients=sk_X509_new_null();
        if(!input||!recipients||!sk_X509_push(recipients,recipient))goto done;
        cms=CMS_encrypt(recipients,input,EVP_aes_256_gcm(),CMS_BINARY|CMS_USE_KEYID);
        int size=cms?i2d_CMS_ContentInfo(cms,&der):0;
        if(size<=0||apx_pqc_profile(der,(size_t)size,id,maximum)!=1)goto done;
        char *encoded=apx_pqc_base64(der,(size_t)size);if(!encoded)goto done;
        ApxValue *env=apx_record_new("Envelope",2);apx_record_put(env,0,"format",apx_string(APX_PQC_FORMAT,sizeof(APX_PQC_FORMAT)-1));apx_record_put(env,1,"data",apx_string(encoded,(int64_t)strlen(encoded)));free(encoded);result=success(env);
    }
done:
    /* Even if the backend changes its failure behavior, no plaintext is sent
     * until complete authentication. Cleanse every private output BIO. */
    if(output){char *plain=NULL;long size=BIO_get_mem_data(output,&plain);if(plain&&size>0)OPENSSL_cleanse(plain,(size_t)size);}
    if(data){OPENSSL_cleanse(data,n);free(data);}OPENSSL_free(der);BIO_free(input);BIO_free(output);CMS_ContentInfo_free(cms);sk_X509_free(recipients);X509_free(recipient);EVP_PKEY_free(key);ERR_clear_error();
    return result?result:apx_http_failure(opening?"pqc_decrypt_failed":"pqc_encrypt_failed");
}
/* Absolute component-by-component resolution; no dot or symlink components. */
static int new_parent(const char *path,char leaf[NAME_MAX+1]){
    if(!path||path[0]!='/'||strlen(path)>=PATH_MAX)return -1;
    int fd=open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC);if(fd<0)return -1;
    const char *part=path+1;
    for(;;){const char *slash=strchr(part,'/');size_t n=slash?(size_t)(slash-part):strlen(part);
        if(!n||n>NAME_MAX||(n==1&&part[0]=='.')||(n==2&&!memcmp(part,"..",2))){close(fd);return -1;}
        char name[NAME_MAX+1];memcpy(name,part,n);name[n]=0;
        if(!slash){memcpy(leaf,name,n+1);return fd;}
        int next=openat(fd,name,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);close(fd);if(next<0)return -1;fd=next;part=slash+1;
    }
}
static int owned_entry(int dir,const char *name,const struct stat *owned){
    struct stat now;return !fstatat(dir,name,&now,AT_SYMLINK_NOFOLLOW)&&now.st_dev==owned->st_dev&&now.st_ino==owned->st_ino;
}
char *apx_pqc_generate(const char *directory){
    if(!directory||strlen(directory)>PATH_MAX-sizeof("/private.pem"))return NULL;
    /* This isolated operator helper never inherits an unsafe/restrictive mask. */
    umask(077);
    char leaf[NAME_MAX+1];int parent=new_parent(directory,leaf),dir=-1,pub=-1,priv=-1,made=0,pub_made=0,priv_made=0;struct stat dir_stat={0},pub_stat={0},priv_stat={0};EVP_PKEY_CTX *ctx=NULL;EVP_PKEY *key=NULL;BIO *bio=NULL;char *result=NULL;unsigned char id[32];
    if(parent<0||mkdirat(parent,leaf,0700))goto done;
    if(fstatat(parent,leaf,&dir_stat,AT_SYMLINK_NOFOLLOW))goto done;
    made=1;dir=openat(parent,leaf,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(dir<0||!owned_entry(parent,leaf,&dir_stat)||fchmod(dir,0700))goto done;
    ctx=EVP_PKEY_CTX_new_from_name(NULL,"ML-KEM-768","provider=default");if(!ctx||EVP_PKEY_keygen_init(ctx)<=0||EVP_PKEY_generate(ctx,&key)<=0||!fingerprint(key,id))goto done;
    pub=openat(dir,"public.pem",O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0644);if(pub<0||fstat(pub,&pub_stat))goto done;pub_made=1;
    if(fchmod(pub,0644)||(bio=BIO_new_fd(pub,BIO_NOCLOSE))==NULL||!PEM_write_bio_PUBKEY(bio,key)||BIO_flush(bio)!=1||fsync(pub))goto done;
    BIO_free(bio);bio=NULL;
    priv=openat(dir,"private.pem",O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC|O_NOFOLLOW,0600);if(priv<0||fstat(priv,&priv_stat))goto done;priv_made=1;
    if(fchmod(priv,0600)||(bio=BIO_new_fd(priv,BIO_NOCLOSE))==NULL||!PEM_write_bio_PKCS8PrivateKey(bio,key,NULL,NULL,0,NULL,NULL)||BIO_flush(bio)!=1||fsync(priv)||fsync(dir)||fsync(parent))goto done;
    char public_path[PATH_MAX+16],private_path[PATH_MAX+16],hex[65];snprintf(public_path,sizeof public_path,"%s/public.pem",directory);snprintf(private_path,sizeof private_path,"%s/private.pem",directory);for(size_t i=0;i<32;i++)snprintf(hex+i*2,3,"%02x",id[i]);
    ApxValue *v=apx_record_new("keygen",6);apx_record_put(v,0,"schema",apx_string("apodexis.pqc_keygen.v1",sizeof("apodexis.pqc_keygen.v1")-1));apx_record_put(v,1,"status",apx_string("ok",2));apx_record_put(v,2,"algorithm",apx_string("ML-KEM-768",sizeof("ML-KEM-768")-1));apx_record_put(v,3,"public_key_file",apx_string(public_path,(int64_t)strlen(public_path)));apx_record_put(v,4,"private_key_file",apx_string(private_path,(int64_t)strlen(private_path)));apx_record_put(v,5,"key_id_sha256",apx_string(hex,64));result=apx_json_encode(v);
done:
    BIO_free(bio);int close_failed=0;if(pub>=0&&close(pub))close_failed=1;if(priv>=0&&close(priv))close_failed=1;
    if(close_failed){free(result);result=NULL;}EVP_PKEY_free(key);EVP_PKEY_CTX_free(ctx);ERR_clear_error();
    /* A same-operator replacement is not one of our owned outputs. Never
     * delete it or recursively remove unexpected files on an error path. */
    if(!result&&dir>=0){if(pub_made&&owned_entry(dir,"public.pem",&pub_stat))unlinkat(dir,"public.pem",0);if(priv_made&&owned_entry(dir,"private.pem",&priv_stat))unlinkat(dir,"private.pem",0);}
    if(dir>=0)close(dir);
    if(!result&&made&&owned_entry(parent,leaf,&dir_stat))unlinkat(parent,leaf,AT_REMOVEDIR);
    if(parent>=0)close(parent);
    return result;
}
#endif
