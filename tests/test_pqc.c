/* Qualification at the compiled public CLI, standard OpenSSL CLI, and the
 * operator broker C seam. No production keys, network or provider changes. */
#define _GNU_SOURCE
#if defined(APX_PQC_KEYGEN_FAIL)
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
int __real_fsync(int);
int __wrap_fsync(int fd){
    static int calls;
    if(++calls==1){char proc[64],name[8192];snprintf(proc,sizeof proc,"/proc/self/fd/%d",fd);ssize_t n=readlink(proc,name,sizeof name-32);if(n<=0)return -1;name[n]=0;char *slash=strrchr(name,'/');if(!slash)return -1;strcpy(slash+1,"foreign-sentinel");int out=open(name,O_WRONLY|O_CREAT|O_EXCL,0600);if(out<0)return -1;int ok=write(out,"preserve",8)==8&&!close(out);if(!ok)return -1;}
    if(calls==2){errno=EIO;return -1;}return __real_fsync(fd);
}
#elif defined(APX_PQC_FIXTURE)
#include "../compiler/apx_pqc.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>
extern char **environ;
static int emit(const char *s){return send(3,s,strlen(s),MSG_NOSIGNAL)==(ssize_t)strlen(s);}
static void ready(const char *binary){char name[8192];snprintf(name,sizeof name,"%s",binary);char *slash=strrchr(name,'/');if(!slash)_exit(121);strcpy(slash+1,"helper-ready");int fd=open(name,O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600);if(fd<0||write(fd,"ready",5)!=5||close(fd))_exit(121);}
int main(int argc,char **argv){
    if(argc!=2)return 120;
    if(strstr(argv[0],"startup")){ready(argv[0]);sleep(30);return 120;}
    char *wire=malloc(APX_HTTP_FRAME+1);if(!wire)return 120;size_t n=0;
    for(;;){ssize_t k=recv(3,wire+n,APX_HTTP_FRAME-n<512?APX_HTTP_FRAME-n:512,0);if(k<=0)return 120;n+=(size_t)k;wire[n]=0;
        if(strstr(argv[0],"stallread")&&strstr(wire,"\"mode\":\"seal\"")){ready(argv[0]);sleep(30);return 120;}
        if(memchr(wire,'\n',n))break;
        if(n==APX_HTTP_FRAME)return 120;
    }
    if(strstr(argv[0],"unrelated")){for(int fd=4;fd<1024;fd++)if(fcntl(fd,F_GETFD)>=0)return 122;free(wire);return emit("{\"ok\":false,\"error\":\"http_transport_failed\"}\n")?0:120;}
    ApxValue *v=apx_pqc_parse(wire,n,24);const char *mode=apx_http_string(apx_json_get(v,"mode"));if(!mode)return 120;
    if(!strncmp(mode,"validate-",9)){free(wire);apx_json_reset();return emit("{\"ok\":false,\"error\":\"pqc_key_valid\"}\n")?0:120;}
    if(strstr(argv[0],"audit")){
        unsigned env_n=0;for(char **e=environ;*e;e++){if(strcmp(*e,"PATH=/usr/bin:/bin")&&strcmp(*e,"LC_ALL=C"))return 122;env_n++;}if(env_n!=2)return 122;
        DIR *fds=opendir("/proc/self/fd");if(!fds)return 122;struct dirent *e;while((e=readdir(fds))){char *end;long fd=strtol(e->d_name,&end,10);if(!*end&&fd>4&&fd!=dirfd(fds))return 122;}closedir(fds);
        for(int i=0;i<3;i++){char proc[64],target[128];snprintf(proc,sizeof proc,"/proc/self/fd/%d",i);ssize_t n=readlink(proc,target,sizeof target-1);if(n<0)return 122;target[n]=0;if(strcmp(target,"/dev/null"))return 122;}
        struct rlimit core;if(getrlimit(RLIMIT_CORE,&core)||core.rlim_cur!=0)return 122;
        int seals=fcntl(4,F_GET_SEALS),all=F_SEAL_WRITE|F_SEAL_GROW|F_SEAL_SHRINK|F_SEAL_SEAL;if((seals&all)!=all)return 122;
        char key[128]={0};if(pread(4,key,sizeof key-1,0)<=0)return 122;const char *label=!strcmp(mode,"seal")?"-----BEGIN PUBLIC KEY-----":"-----BEGIN PRIVATE KEY-----";if(strncmp(key,label,strlen(label))||pwrite(4,"x",1,0)!=-1||errno!=EPERM)return 122;
        if(strstr(wire,"-----BEGIN "))return 122;
        const char *out=!strcmp(mode,"seal")?"{\"ok\":false,\"error\":\"pqc_encrypt_failed\"}\n":"{\"ok\":false,\"error\":\"pqc_decrypt_failed\"}\n";
        free(wire);apx_json_reset();return emit(out)?0:120;
    }
    if(strstr(argv[0],"badshape")){free(wire);apx_json_reset();return emit("{\"ok\":false,\"error\":\"pqc_encrypt_failed\",\"extra\":1}\n")?0:120;}
    if(strstr(argv[0],"badtoken")){free(wire);apx_json_reset();return emit("{\"ok\":false,\"error\":\"pqc_key_valid\"}\n")?0:120;}
    if(strstr(argv[0],"trailing")){emit("{\"ok\":false,\"error\":\"pqc_encrypt_failed\"}\n");usleep(20000);emit("{}\n");free(wire);apx_json_reset();return 0;}
    if(strstr(argv[0],"exitstall"))emit("{\"ok\":false,\"error\":\"pqc_encrypt_failed\"}\n");
    if(strstr(argv[0],"writestall"))emit("{\"ok\":");
    ready(argv[0]);sleep(30);free(wire);apx_json_reset();return 120;
}
#else
#include "../compiler/apx_host.h"
#include "../compiler/apx_pqc.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <openssl/asn1t.h>
#include <openssl/cms.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>
static unsigned cases,checks;static char directory[4096],root[4096],cli[8192],helper[8192],openssl_cli[8192];
static void require(int ok,const char *message){checks++;if(!ok){fprintf(stderr,"PQC FAIL: %s\n",message);exit(1);}}
static void passed(const char *name){printf("case %04u %s PASS\n",++cases,name);fflush(stdout);}
static void path(char out[8192],const char *name){require(snprintf(out,8192,"%s/%s",directory,name)<8192,"fixture path bound");}
static void remove_tree(const char *dir){DIR *d=opendir(dir);if(!d)return;struct dirent *e;while((e=readdir(d))){if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;char p[8192];if(snprintf(p,sizeof p,"%s/%s",dir,e->d_name)>=(int)sizeof p)abort();struct stat st;if(lstat(p,&st))abort();if(S_ISDIR(st.st_mode))remove_tree(p);else if(unlink(p))abort();}closedir(d);if(rmdir(dir))abort();}
static void cleanup(void){if(*directory){remove_tree(directory);if(!access(directory,F_OK)||errno!=ENOENT)abort();printf("cleanup exact_fixture_root_absent=1\n");}}
static void save_bytes(const char *name,const void *data,size_t n,mode_t mode){char p[8192];path(p,name);struct stat existing;if(!lstat(p,&existing)&&S_ISREG(existing.st_mode)&&existing.st_uid==geteuid())require(!chmod(p,0600),"restore writable exact fixture mode");int fd=open(p,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,mode);require(fd>=0&&!fchmod(fd,mode),"fixture creation/mode");size_t at=0;while(at<n){ssize_t k=write(fd,(const char*)data+at,n-at);require(k>0,"fixture write");at+=(size_t)k;}require(!fchmod(fd,mode)&&!close(fd),"fixture close and final exact mode");}
static void save(const char *name,const char *data){save_bytes(name,data,strlen(data),0600);}
static char *readall(const char *name,size_t *length){char p[8192];path(p,name);FILE *f=fopen(p,"rb");require(f&&!fseek(f,0,SEEK_END),"fixture result open");long n=ftell(f);require(n>=0&&n<=APX_HTTP_FRAME,"result bytes bounded");rewind(f);char *s=calloc(1,(size_t)n+1);require(s&&fread(s,1,(size_t)n,f)==(size_t)n&&!fclose(f),"fixture result read");if(length)*length=(size_t)n;return s;}
static int command(char *const argv[],const char *infile){
    char out[8192],err[8192],in[8192];path(out,"stdout");path(err,"stderr");path(in,infile?infile:"input.json");fflush(NULL);
    pid_t p=fork();require(p>=0,"public process fork");if(!p){alarm(15);if(!freopen(out,"wb",stdout)||!freopen(err,"wb",stderr)||!freopen(in,"rb",stdin))_exit(126);if(fchmod(1,0600)||fchmod(2,0600))_exit(126);execv(argv[0],argv);_exit(127);}
    int status;require(waitpid(p,&status,0)==p&&WIFEXITED(status),"bounded public process and reap");return WEXITSTATUS(status);
}
static char *run(const char *source,const char *input,int granted){
    save("input.json",input);char src[8192],in[8192],pol[8192];path(src,source);path(in,"input.json");path(pol,"policy.json");char *args[]={cli,"run",src,"--input",in,"--json",granted?"--capabilities":NULL,granted?pol:NULL,NULL};int status=command(args,NULL);char *out=readall("stdout",NULL),*err=readall("stderr",NULL);
    if(status||*err){fprintf(stderr,"CLI exit=%d stdout=%s stderr=%s\n",status,out,err);require(0,"compiled public CLI success");}free(err);ApxValue *v=apx_json_parse(out,strlen(out));const char *state=apx_http_string(apx_json_get(v,"status"));require(state&&!strcmp(state,"ok"),"real compiled run envelope");char *result=apx_json_encode(apx_json_get(v,"output"));free(out);require(result!=NULL,"language result");return result;
}
static void error_result(const char *out,const char *want,int host){ApxValue *v=apx_pqc_parse(out,strlen(out),16);const char *e=apx_http_string(apx_json_get(v,"error"));if(!e||strcmp(e,want))fprintf(stderr,"got=%s want=%s\n",out,want);require(apx_json_length(v)==2&&e&&!strcmp(e,want),"exact payload-free error");if(host)require(apx_json_kind(apx_json_get(v,"ok"))==1&&!apx_json_integer(apx_json_get(v,"ok")),"host failure tag");else require(!strcmp(apx_http_string(apx_json_get(v,"tag")),"err"),"language error tag");}
static char *body_request(const char *slot,const unsigned char *data,size_t n){ApxValue *v=apx_record_new("Seal",2);apx_record_put(v,0,"key",apx_string(slot,(int64_t)strlen(slot)));apx_record_put(v,1,"body",apx_http_body(data,n));return apx_json_encode(v);}
static char *open_request(const char *slot,ApxValue *envelope){ApxValue *v=apx_record_new("Open",2);apx_record_put(v,0,"key",apx_string(slot,(int64_t)strlen(slot)));apx_record_put(v,1,"envelope",envelope);return apx_json_encode(v);}
static char *dispatch(ApxHostPolicy *policy,const char *op,const char *request){ApxValue *v=apx_record_new("host",2);apx_record_put(v,0,"op",apx_string(op,(int64_t)strlen(op)));ApxValue *args=apx_array_new(1);apx_array_put(args,0,apx_string(request,(int64_t)strlen(request)));apx_record_put(v,1,"args",args);char *wire=apx_json_encode(v);require(wire!=NULL,"host frame encode");char *out=apx_host_dispatch(policy,wire,strlen(wire));free(wire);require(out!=NULL,"broker returned frame");return out;}
static ApxValue *value(const char *out,int host){ApxValue *v=apx_json_parse(out,strlen(out));require(host?apx_json_integer(apx_json_get(v,"ok")):!strcmp(apx_http_string(apx_json_get(v,"tag")),"ok"),"success tag");ApxValue *result=apx_json_get(v,"value");if(host){const char *doc=apx_http_string(result);require(doc!=NULL,"one JSON document as str");result=apx_pqc_parse(doc,strlen(doc),24);}return result;}
static ApxHostPolicy *load(int good){char p[8192];path(p,"policy.json");char *error=NULL;ApxHostPolicy *policy=apx_host_policy_load(p,&error);if(good&&(!policy||error))fprintf(stderr,"load error=%s\n",error?error:"none");require(good?(policy&&!error):(!policy&&error),"operator policy load expected result");free(error);return policy;}
static void policy(int direction,const char *extra){char pub[8192],priv[8192];path(pub,"keys/public.pem");path(priv,"keys/private.pem");ApxValue *p=apx_record_new("Policy",2),*keys=apx_array_new(1),*key=apx_record_new("Key",direction==3?3:2);apx_record_put(p,0,"schema",apx_string("apodexis.host_policy.v3",sizeof("apodexis.host_policy.v3")-1));apx_record_put(key,0,"name",apx_string("slot",4));size_t at=1;if(direction&1)apx_record_put(key,(int64_t)at++,"public_key_file",apx_string(pub,(int64_t)strlen(pub)));if(direction&2)apx_record_put(key,(int64_t)at,"private_key_file",apx_string(priv,(int64_t)strlen(priv)));apx_array_put(keys,0,key);apx_record_put(p,1,"crypto_keys",keys);char *s=apx_json_encode(p);require(s!=NULL,"policy encode");if(extra){size_t n=strlen(s);s=realloc(s,n+strlen(extra)+2);require(s!=NULL,"policy extra allocation");s[n-1]=0;strcat(s,extra);strcat(s,"}");}save("policy.json",s);free(s);}
static void keygen(void){save("input.json","null");char dir[8192];path(dir,"keys");char *args[]={cli,"pqc-keygen",dir,"--json",NULL};mode_t old=umask(0777);int exit=command(args,NULL);umask(old);require(!exit,"operator keygen through compiled CLI");char *out=readall("stdout",NULL),*err=readall("stderr",NULL);require(!*err,"keygen no stderr");free(err);ApxValue *v=apx_json_parse(out,strlen(out));require(apx_json_length(v)==6&&!strcmp(apx_http_string(apx_json_get(v,"schema")),"apodexis.pqc_keygen.v1")&&!strcmp(apx_http_string(apx_json_get(v,"algorithm")),"ML-KEM-768"),"exact keygen schema and algorithm");char file[8192];struct stat st;require(!stat(dir,&st)&&(st.st_mode&07777)==0700,"key directory explicit 0700 independent umask");path(file,"keys/public.pem");require(!stat(file,&st)&&(st.st_mode&07777)==0644,"public explicit 0644");FILE *f=fopen(file,"rb");EVP_PKEY *public=f?PEM_read_PUBKEY(f,NULL,NULL,NULL):NULL;if(f)fclose(f);unsigned char *der=NULL,digest[32];unsigned digest_n;int n=public?i2d_PUBKEY(public,&der):0;require(n>0&&EVP_Digest(der,(size_t)n,digest,&digest_n,EVP_sha256(),NULL)&&digest_n==32,"canonical SPKI digest independent library");char hex[65];for(size_t i=0;i<32;i++)snprintf(hex+i*2,3,"%02x",digest[i]);require(!strcmp(hex,apx_http_string(apx_json_get(v,"key_id_sha256"))),"keygen fingerprint exact SHA256 SPKI");EVP_PKEY_free(public);OPENSSL_free(der);path(file,"keys/private.pem");require(!stat(file,&st)&&(st.st_mode&07777)==0600,"private explicit 0600");free(out);passed("operator-keygen-new-exclusive-modes-fingerprint");
    require(command(args,NULL)!=0,"existing directory refused");out=readall("stdout",NULL);v=apx_json_parse(out,strlen(out));require(apx_json_length(v)==3&&!strcmp(apx_http_string(apx_json_get(v,"error")),"pqc_keygen_failed"),"existing refusal exact generic object");free(out);
    const char *names[]={"exists","link","keys/public.pem","missing/child","./dot","../dot",""};save("exists","preserve");char link[8192];path(link,"link");require(!symlink(dir,link),"synthetic keygen symlink");for(size_t i=0;i<sizeof names/sizeof names[0];i++){path(dir,names[i]);require(command(args,NULL)!=0,"invalid/existing path refused");out=readall("stdout",NULL);v=apx_json_parse(out,strlen(out));require(!strcmp(apx_http_string(apx_json_get(v,"error")),"pqc_keygen_failed"),"path refusal payload-free");free(out);passed("keygen-existing-file-symlink-dot-missing-refusal");}
}
static void positives(void){
    policy(3,NULL);const unsigned char utf8[]="é😀 quotes \" []{}:,\\",nul[]={'a',0,'z'},binary[]={0,255,128,7};const unsigned char *bodies[]={utf8,nul,binary,(const unsigned char*)""};size_t lengths[]={sizeof utf8-1,sizeof nul,sizeof binary,0};
    for(size_t i=0;i<4;i++){
        char *req=body_request("slot",bodies[i],lengths[i]),*sealed=run("seal.apx",req,1);ApxValue *env=value(sealed,0);require(!strcmp(apx_http_string(apx_json_get(env,"format")),APX_PQC_FORMAT),"typed envelope format");char *open=open_request("slot",env),*opened=run("open.apx",open,1);size_t n=0;unsigned char *plain=apx_http_decode_body(value(opened,0),APX_HTTP_BODY,&n);require(plain&&n==lengths[i]&&!memcmp(plain,bodies[i],n),"compiled typed seal/open byte-exact roundtrip");free(plain);free(opened);free(open);char *fresh=run("seal.apx",req,1);require(strcmp(fresh,sealed),"fresh randomized ciphertext for identical plaintext");free(fresh);free(sealed);free(req);apx_json_reset();passed("typed-UTF8-NUL-binary-empty-fresh-randomness");
    }
    policy(3,",\"max_bytes\":3");char *req=body_request("slot",nul,3),*out=run("seal.apx",req,1);char *open=open_request("slot",value(out,0));free(out);out=run("open.apx",open,1);size_t n=0;unsigned char *plain=apx_http_decode_body(value(out,0),3,&n);require(plain&&n==3&&!memcmp(plain,nul,3),"configured exact plaintext boundary");free(plain);free(out);free(open);free(req);req=body_request("slot",binary,4);out=run("seal.apx",req,1);error_result(out,"pqc_byte_limit",0);free(out);free(req);passed("configured-byte-bound-and-plus-one");
    policy(3,",\"max_bytes\":1");req=body_request("slot",binary,3);out=run("seal.apx",req,1);error_result(out,"pqc_byte_limit",0);free(out);free(req);passed("base64-decoded-plaintext-quota");
}
static unsigned fd_count(void){DIR *d=opendir("/proc/self/fd");require(d!=NULL,"fd inventory");unsigned n=0;struct dirent *e;while((e=readdir(d)))if(strcmp(e->d_name,".")&&strcmp(e->d_name,".."))n++;closedir(d);return n;}
static void one_key_policy(const char *field,const char *file,const char *extra){char text[20000];require(snprintf(text,sizeof text,"{\"schema\":\"apodexis.host_policy.v3\",\"crypto_keys\":[{\"name\":\"slot\",\"%s\":\"%s\"}]%s}",field,file,extra?extra:"")<(int)sizeof text,"policy fixture bound");save("policy.json",text);}
static void key_failures(void){
    size_t pub_n,priv_n;char *pub=readall("keys/public.pem",&pub_n),*priv=readall("keys/private.pem",&priv_n);char file[8192],other[8192];path(file,"candidate.pem");unsigned before=fd_count();
    const mode_t modes[]={0000,0644,0660,0666,0700,04600};
    for(size_t i=0;i<sizeof modes/sizeof modes[0];i++){save_bytes("candidate.pem",priv,priv_n,modes[i]);one_key_policy("private_key_file",file,NULL);load(0);require(fd_count()==before,"refused private mode leaks no snapshots");passed("insecure-private-mode-refused");}
    save_bytes("candidate.pem",pub,pub_n,0664);one_key_policy("public_key_file",file,NULL);load(0);passed("group-writable-public-refused");
    for(int direction=0;direction<2;direction++){
        const char *field=direction?"private_key_file":"public_key_file";size_t n=direction?priv_n:pub_n;const char *source=direction?priv:pub;
        save_bytes("candidate.pem",source,n,direction?0600:0644);one_key_policy(field,file,NULL);ApxHostPolicy *p=load(1);apx_host_policy_free(p);require(fd_count()==before,"valid policy load/free snapshot closure");
        char *doubled=malloc(2*n+1);require(doubled!=NULL,"extra PEM fixture");memcpy(doubled,source,n);memcpy(doubled+n,source,n);save_bytes("candidate.pem",doubled,2*n,0600);one_key_policy(field,file,NULL);load(0);explicit_bzero(doubled,2*n);free(doubled);passed("extra-PEM-block-refused");
        char *nul=malloc(n);require(nul!=NULL,"NUL fixture");memcpy(nul,source,n);nul[n/2]=0;save_bytes("candidate.pem",nul,n,0600);load(0);explicit_bzero(nul,n);free(nul);passed("embedded-NUL-key-refused");
        save_bytes("candidate.pem",source,n/2,0600);load(0);passed("truncated-PEM-refused");
        save_bytes("candidate.pem",direction?pub:priv,direction?pub_n:priv_n,0600);load(0);passed("public-private-direction-format-refused");
        char *large=calloc(1,direction?32769:16385);require(large!=NULL,"oversize key fixture");save_bytes("candidate.pem",large,direction?32769:16385,0600);free(large);load(0);passed("oversize-key-refused");
        require(!unlink(file),"remove candidate before link tests");path(other,direction?"keys/private.pem":"keys/public.pem");require(!symlink(other,file),"synthetic key symlink");load(0);require(!unlink(file)&&!link(other,file),"synthetic hardlink");load(0);require(!unlink(file)&&!mkfifo(file,0600),"synthetic FIFO");int64_t start=apx_http_now();load(0);require(apx_http_now()-start<1000,"FIFO rejected without blocking");require(!unlink(file),"remove FIFO");passed("symlink-hardlink-FIFO-refused");
        char dot[8192];require(snprintf(dot,sizeof dot,"%s/keys/../keys/%s",directory,direction?"private.pem":"public.pem")<(int)sizeof dot,"dot path");one_key_policy(field,dot,NULL);load(0);passed("dot-path-component-refused");
    }
    /* A different valid key type is not an ML-KEM grant. */
    EVP_PKEY *ed=EVP_PKEY_Q_keygen(NULL,NULL,"ED25519");BIO *bio=BIO_new(BIO_s_mem());require(ed&&bio&&PEM_write_bio_PUBKEY(bio,ed),"wrong-algorithm fixture");char *bytes=NULL;long n=BIO_get_mem_data(bio,&bytes);save_bytes("candidate.pem",bytes,(size_t)n,0644);one_key_policy("public_key_file",file,NULL);load(0);BIO_free(bio);bio=BIO_new(BIO_s_mem());require(bio&&PEM_write_bio_PKCS8PrivateKey(bio,ed,NULL,NULL,0,NULL,NULL),"wrong private algorithm fixture");n=BIO_get_mem_data(bio,&bytes);save_bytes("candidate.pem",bytes,(size_t)n,0600);one_key_policy("private_key_file",file,NULL);load(0);OPENSSL_cleanse(bytes,(size_t)n);BIO_free(bio);EVP_PKEY_free(ed);passed("valid-wrong-key-algorithm-refused");
    /* Encrypted PKCS8 must never cause a prompt or passphrase lookup. */
    path(other,"keys/private.pem");FILE *f=fopen(other,"rb");EVP_PKEY *key=f?PEM_read_PrivateKey(f,NULL,NULL,NULL):NULL;if(f)fclose(f);require(key!=NULL,"private fixture decode");bio=BIO_new(BIO_s_mem());require(bio&&PEM_write_bio_PKCS8PrivateKey(bio,key,EVP_aes_256_cbc(),"synthetic",9,NULL,NULL),"encrypted fixture");n=BIO_get_mem_data(bio,&bytes);save_bytes("candidate.pem",bytes,(size_t)n,0600);one_key_policy("private_key_file",file,NULL);int64_t start=apx_http_now();load(0);require(apx_http_now()-start<1000,"encrypted key never prompts");OPENSSL_cleanse(bytes,(size_t)n);BIO_free(bio);EVP_PKEY_free(key);passed("encrypted-private-key-refused-no-prompt");
    /* Compare object ancestry, including a root reached through a symlinked
     * intermediate component, rather than lexical string containment. */
    path(file,"keys/private.pem");const char *roots[]={directory,"/",NULL};char symlink_path[8192],alias_root[8192];path(symlink_path,"alias");require(!symlink(directory,symlink_path),"directory alias fixture");require(snprintf(alias_root,sizeof alias_root,"%s/alias/keys",directory)<(int)sizeof alias_root,"aliased root");roots[2]=alias_root;
    for(size_t i=0;i<3;i++)for(int write=0;write<2;write++){char extra[10000];require(snprintf(extra,sizeof extra,",\"%s_root\":\"%s\"",write?"write":"read",roots[i])<(int)sizeof extra,"overlap fixture bound");one_key_policy("private_key_file",file,extra);load(0);passed("private-key-filesystem-object-ancestry-overlap-refused");}
    one_key_policy("private_key_file","/dev/null",NULL);load(0);passed("device-key-source-refused");
    explicit_bzero(priv,priv_n);free(priv);free(pub);require(fd_count()==before,"all key refusal paths close every snapshot");
}
static void policy_failures(void){
    const char *bad[]={"\"crypto_keys\":null","\"crypto_keys\":{}","\"crypto_keys\":[{}]","\"crypto_keys\":[{\"name\":\"slot\"}]","\"crypto_keys\":[{\"name\":\"*\",\"public_key_file\":\"/x\"}]","\"crypto_keys\":[{\"name\":\"a/b\",\"public_key_file\":\"/x\"}]","\"crypto_keys\":[{\"name\":\"a\\u0000b\",\"public_key_file\":\"/x\"}]","\"crypto_keys\":[{\"name\":\"slot\",\"public_key_file\":null}]","\"crypto_keys\":[{\"name\":\"slot\",\"public_key_file\":\"relative\"}]","\"crypto_keys\":[{\"name\":\"slot\",\"public_key_file\":\"/x\",\"provider\":\"default\"}]","\"crypto_keys\":[{\"name\":\"slot\",\"name\":\"slot\",\"public_key_file\":\"/x\"}]","\"crypto_timeout_ms\":0","\"crypto_timeout_ms\":5001","\"crypto_timeout_ms\":true","\"crypto_timeout_ms\":1.5","\"crypto_timeout_ms\":null","\"crypto_timeout_ms\":1,\"crypto_timeout_ms\":2"};
    unsigned before=fd_count();char text[20000];for(size_t i=0;i<sizeof bad/sizeof bad[0];i++){snprintf(text,sizeof text,"{\"schema\":\"apodexis.host_policy.v3\",%s}",bad[i]);save("policy.json",text);load(0);require(fd_count()==before,"bad policy closes fds");passed("strict-v3-key-time-policy-refused");}
    for(int version=1;version<=2;version++)for(int field=0;field<2;field++){snprintf(text,sizeof text,"{\"schema\":\"apodexis.host_policy.v%d\",%s}",version,field?"\"crypto_timeout_ms\":1":"\"crypto_keys\":[]");save("policy.json",text);load(0);passed("v1-v2-reject-new-crypto-authority");}
    policy(3,NULL);char *p=readall("policy.json",NULL);ApxValue *v=apx_json_parse(p,strlen(p)),*keys=apx_json_get(v,"crypto_keys"),*key=apx_json_item(keys,0),*many=apx_array_new(17),*doc=apx_record_new("Policy",2);for(int i=0;i<17;i++)apx_array_put(many,i,key);apx_record_put(doc,0,"schema",apx_json_get(v,"schema"));apx_record_put(doc,1,"crypto_keys",many);char *raw=apx_json_encode(doc);save("policy.json",raw);load(0);free(raw);free(p);passed("seventeen-slots-refused");
    policy(3,NULL);p=readall("policy.json",NULL);v=apx_json_parse(p,strlen(p));keys=apx_json_get(v,"crypto_keys");key=apx_json_item(keys,0);many=apx_array_new(2);apx_array_put(many,0,key);apx_array_put(many,1,key);doc=apx_record_new("Policy",2);apx_record_put(doc,0,"schema",apx_json_get(v,"schema"));apx_record_put(doc,1,"crypto_keys",many);raw=apx_json_encode(doc);save("policy.json",raw);load(0);free(raw);free(p);require(fd_count()==before,"partial successful slot load closes snapshots on duplicate");passed("duplicate-slot-and-partial-load-cleanup");
}
static void snapshot_and_requests(void){
    policy(3,",\"max_requests\":1024");unsigned before=fd_count();ApxHostPolicy *p=load(1);require(fd_count()==before+2,"only two sealed key snapshots held");
    size_t pub_n,priv_n;char *pub=readall("keys/public.pem",&pub_n),*priv=readall("keys/private.pem",&priv_n);save("keys/public.pem","changed public file");save("keys/private.pem","changed private file");char replacement[8192],target[8192];path(replacement,"replacement.pem");save("replacement.pem","replacement public inode");path(target,"keys/public.pem");require(!rename(replacement,target),"replace public source inode");save("replacement.pem","replacement private inode");path(target,"keys/private.pem");require(!rename(replacement,target),"replace private source inode");
    char *req=body_request("slot",(const unsigned char*)"snapshot",8),*out=dispatch(p,"pqc_seal",req),*open=open_request("slot",value(out,1));free(out);out=dispatch(p,"pqc_open",open);ApxValue *body=value(out,1);require(!strcmp(apx_json_string(apx_json_get(body,"data")),"snapshot"),"source mutation cannot replace public/private snapshots");free(out);free(open);free(req);save_bytes("keys/public.pem",pub,pub_n,0644);save_bytes("keys/private.pem",priv,priv_n,0600);explicit_bzero(priv,priv_n);free(priv);free(pub);passed("immutable-sealed-snapshots-after-source-mutation");
    const char *slots[]={"other","Slot"};for(size_t i=0;i<2;i++){req=body_request(slots[i],(const unsigned char*)"x",1);out=dispatch(p,"pqc_seal",req);error_result(out,"pqc_key_denied",1);free(out);free(req);passed("unknown-case-sensitive-slot-denied");}
    const char *bad[]={"{}","[]","{\"key\":\"slot\"}","{\"key\":\"slot\",\"key\":\"slot\",\"body\":{\"encoding\":\"utf8\",\"data\":\"x\"}}","{\"key\":\"slot\",\"body\":{\"encoding\":\"utf8\",\"data\":\"x\",\"path\":\"/x\"}}","{\"key\":\"slot\",\"body\":{\"encoding\":\"utf8\",\"encoding\":\"utf8\"}}","{\"key\":\"slot\",\"body\":{\"encoding\":\"utf8\",\"data\":4}}","{\"key\":\"slot\",\"body\":{\"encoding\":\"utf8\",\"data\":\"x\"},\"provider\":\"default\"}","{\"key\":\"/private.pem\",\"body\":{\"encoding\":\"utf8\",\"data\":\"x\"}}","{\"key\":\"slot\",\"body\":{\"encoding\":\"unknown\",\"data\":\"x\"}}"};
    for(size_t i=0;i<sizeof bad/sizeof bad[0];i++){out=dispatch(p,"pqc_seal",bad[i]);error_result(out,"invalid_pqc_request",1);free(out);passed("strict-seal-request-shape");}
    const char *b64[]={"YQ=","YQ===","YQ==\n","Y===","YQ=A","YR==","/x=="};for(size_t i=0;i<sizeof b64/sizeof b64[0];i++){ApxValue *request=apx_record_new("Seal",2),*b=apx_record_new("Body",2);apx_record_put(request,0,"key",apx_string("slot",4));apx_record_put(b,0,"encoding",apx_string("base64",6));apx_record_put(b,1,"data",apx_string(b64[i],(int64_t)strlen(b64[i])));apx_record_put(request,1,"body",b);req=apx_json_encode(request);out=dispatch(p,"pqc_seal",req);error_result(out,"invalid_pqc_request",1);free(out);free(req);passed("canonical-Body-base64-only");}
    apx_host_policy_free(p);require(fd_count()==before,"snapshots closed after operations");
    policy(3,",\"max_requests\":2");p=load(1);out=dispatch(p,"pqc_seal","{}");error_result(out,"invalid_pqc_request",1);free(out);req=body_request("slot",(const unsigned char*)"ok",2);out=dispatch(p,"pqc_seal",req);value(out,1);free(out);out=dispatch(p,"pqc_seal",req);error_result(out,"host_request_limit",1);free(out);apx_host_policy_free(p);p=load(1);out=dispatch(p,"pqc_seal",req);value(out,1);free(out);free(req);apx_host_policy_free(p);passed("all-attempts-budget-exhaustion-and-new-policy-recovery");
    /* Millions of nodes are rejected before generic inner DOM allocation. */
    policy(3,NULL);p=load(1);out=apx_host_dispatch(p,"{}",APX_HTTP_FRAME+1);error_result(out,"host_frame_limit",1);free(out);passed("independent-host-frame-limit");size_t count=2000000,n=0;char *raw=malloc(count*2+64);require(raw!=NULL,"large malformed shape fixture");n+=(size_t)sprintf(raw,"{\"key\":\"slot\",\"body\":[");for(size_t i=0;i<count;i++){raw[n++]='0';if(i+1<count)raw[n++]=',';}raw[n++]=']';raw[n++]='}';raw[n]=0;out=dispatch(p,"pqc_seal",raw);error_result(out,"invalid_pqc_request",1);free(out);out=dispatch(p,"pqc_open",raw);error_result(out,"invalid_pqc_request",1);free(out);free(raw);req=body_request("slot",(const unsigned char*)"still works",11);out=dispatch(p,"pqc_seal",req);value(out,1);free(req);free(out);apx_host_policy_free(p);apx_json_reset();passed("impossible-huge-JSON-shape-preflight-and-follow-on-recovery");
}

typedef struct {
    ASN1_INTEGER *version;
    ASN1_OCTET_STRING *key_id;
    X509_ALGOR *kem;
    ASN1_OCTET_STRING *kem_ciphertext;
    X509_ALGOR *kdf;
    ASN1_INTEGER *kek_length;
    ASN1_OCTET_STRING *ukm;
    X509_ALGOR *wrap;
    ASN1_OCTET_STRING *wrapped_key;
} FixtureKEM;
ASN1_SEQUENCE(FixtureKEM) = {
    ASN1_SIMPLE(FixtureKEM, version, ASN1_INTEGER),
    ASN1_IMP(FixtureKEM, key_id, ASN1_OCTET_STRING, 0),
    ASN1_SIMPLE(FixtureKEM, kem, X509_ALGOR),
    ASN1_SIMPLE(FixtureKEM, kem_ciphertext, ASN1_OCTET_STRING),
    ASN1_SIMPLE(FixtureKEM, kdf, X509_ALGOR),
    ASN1_SIMPLE(FixtureKEM, kek_length, ASN1_INTEGER),
    ASN1_EXP_OPT(FixtureKEM, ukm, ASN1_OCTET_STRING, 0),
    ASN1_SIMPLE(FixtureKEM, wrap, X509_ALGOR),
    ASN1_SIMPLE(FixtureKEM, wrapped_key, ASN1_OCTET_STRING)
} ASN1_SEQUENCE_END(FixtureKEM)

typedef struct { ASN1_OBJECT *type; FixtureKEM *kem; } FixtureORI;
ASN1_SEQUENCE(FixtureORI) = {
    ASN1_SIMPLE(FixtureORI, type, ASN1_OBJECT),
    ASN1_SIMPLE(FixtureORI, kem, FixtureKEM)
} ASN1_SEQUENCE_END(FixtureORI)

typedef FixtureORI FixtureRecipient;
ASN1_ITEM_TEMPLATE(FixtureRecipient) =
    ASN1_EX_TEMPLATE_TYPE(ASN1_TFLG_IMPLICIT, 4, FixtureRecipient, FixtureORI)
ASN1_ITEM_TEMPLATE_END(FixtureRecipient)
DEFINE_STACK_OF(FixtureRecipient)

typedef struct {
    ASN1_OBJECT *type;
    X509_ALGOR *algorithm;
    ASN1_OCTET_STRING *ciphertext;
} FixtureEncrypted;
ASN1_SEQUENCE(FixtureEncrypted) = {
    ASN1_SIMPLE(FixtureEncrypted, type, ASN1_OBJECT),
    ASN1_SIMPLE(FixtureEncrypted, algorithm, X509_ALGOR),
    ASN1_IMP_OPT(FixtureEncrypted, ciphertext, ASN1_OCTET_STRING, 0)
} ASN1_SEQUENCE_END(FixtureEncrypted)

typedef struct {
    ASN1_INTEGER *version;
    STACK_OF(ASN1_TYPE) *originator;
    STACK_OF(FixtureRecipient) *recipients;
    FixtureEncrypted *encrypted;
    STACK_OF(X509_ATTRIBUTE) *authenticated;
    ASN1_OCTET_STRING *tag;
    STACK_OF(X509_ATTRIBUTE) *unauthenticated;
} FixtureAuthEnv;
ASN1_SEQUENCE(FixtureAuthEnv) = {
    ASN1_SIMPLE(FixtureAuthEnv, version, ASN1_INTEGER),
    ASN1_IMP_SEQUENCE_OF_OPT(FixtureAuthEnv, originator, ASN1_ANY, 0),
    ASN1_SET_OF(FixtureAuthEnv, recipients, FixtureRecipient),
    ASN1_SIMPLE(FixtureAuthEnv, encrypted, FixtureEncrypted),
    ASN1_IMP_SET_OF_OPT(FixtureAuthEnv, authenticated, X509_ATTRIBUTE, 1),
    ASN1_SIMPLE(FixtureAuthEnv, tag, ASN1_OCTET_STRING),
    ASN1_IMP_SET_OF_OPT(FixtureAuthEnv, unauthenticated, X509_ATTRIBUTE, 2)
} ASN1_SEQUENCE_END(FixtureAuthEnv)

typedef struct { ASN1_OBJECT *type; FixtureAuthEnv *envelope; } FixtureEnvelope;
ASN1_SEQUENCE(FixtureEnvelope) = {
    ASN1_SIMPLE(FixtureEnvelope, type, ASN1_OBJECT),
    ASN1_EXP(FixtureEnvelope, envelope, FixtureAuthEnv, 0)
} ASN1_SEQUENCE_END(FixtureEnvelope)
IMPLEMENT_ASN1_FUNCTIONS(FixtureEnvelope)

typedef struct { ASN1_OCTET_STRING *nonce; ASN1_INTEGER *tag_length; } FixtureGCM;
ASN1_SEQUENCE(FixtureGCM) = {
    ASN1_SIMPLE(FixtureGCM, nonce, ASN1_OCTET_STRING),
    ASN1_SIMPLE(FixtureGCM, tag_length, ASN1_INTEGER)
} ASN1_SEQUENCE_END(FixtureGCM)
IMPLEMENT_ASN1_FUNCTIONS(FixtureGCM)

static EVP_PKEY *fixture_private(void){char file[8192];path(file,"keys/private.pem");FILE *f=fopen(file,"rb");EVP_PKEY *key=f?PEM_read_PrivateKey(f,NULL,NULL,NULL):NULL;if(f)fclose(f);require(key!=NULL,"test-only private key load");return key;}
static X509 *fixture_certificate(EVP_PKEY *key){
    unsigned char *der=NULL,id[32];unsigned id_n;int n=i2d_PUBKEY(key,&der);require(n>0&&EVP_Digest(der,(size_t)n,id,&id_n,EVP_sha256(),NULL)&&id_n==32,"recipient SPKI identity");OPENSSL_free(der);ASN1_OCTET_STRING *ski=ASN1_OCTET_STRING_new();X509 *cert=X509_new();EVP_PKEY *issuer=EVP_PKEY_Q_keygen(NULL,NULL,"ED25519");require(cert&&ski&&issuer&&ASN1_OCTET_STRING_set(ski,id,32)&&X509_set_version(cert,2)&&ASN1_INTEGER_set(X509_get_serialNumber(cert),7)&&X509_set_pubkey(cert,key)&&X509_gmtime_adj(X509_getm_notBefore(cert),-60)&&X509_gmtime_adj(X509_getm_notAfter(cert),3600),"test recipient certificate");X509_NAME *subject=X509_NAME_new(),*name=X509_NAME_new();require(subject&&name&&X509_NAME_add_entry_by_txt(subject,"CN",MBSTRING_ASC,(unsigned char*)"synthetic ML-KEM recipient",-1,-1,0)&&X509_NAME_add_entry_by_txt(name,"CN",MBSTRING_ASC,(unsigned char*)"synthetic Ed25519 issuer",-1,-1,0)&&X509_set_subject_name(cert,subject)&&X509_set_issuer_name(cert,name)&&X509_add1_ext_i2d(cert,NID_subject_key_identifier,ski,0,X509V3_ADD_DEFAULT)&&X509_sign(cert,issuer,NULL)>0&&X509_verify(cert,issuer)==1,"proper distinct-issuer signature, never ML-KEM signing");X509_NAME_free(subject);X509_NAME_free(name);EVP_PKEY_free(issuer);ASN1_OCTET_STRING_free(ski);return cert;
}
static unsigned char *library_envelope(X509 *recipient,const unsigned char *message,size_t n,const EVP_CIPHER *cipher,int *length){STACK_OF(X509) *recipients=sk_X509_new_null();BIO *input=BIO_new_mem_buf(message,(int)n);require(recipients&&input&&sk_X509_push(recipients,recipient),"independent CMS inputs");CMS_ContentInfo *cms=CMS_encrypt(recipients,input,cipher,CMS_BINARY|CMS_USE_KEYID);unsigned char *der=NULL;*length=cms?i2d_CMS_ContentInfo(cms,&der):0;require(*length>0,"independent high-level CMS encryption");CMS_ContentInfo_free(cms);BIO_free(input);sk_X509_free(recipients);return der;}
static int generic_open(const unsigned char *der,size_t n,EVP_PKEY *key,X509 *cert,const unsigned char *want,size_t length){const unsigned char *p=der;CMS_ContentInfo *cms=d2i_CMS_ContentInfo(NULL,&p,(long)n);BIO *out=BIO_new(BIO_s_mem());int ok=cms&&p==der+n&&out&&CMS_decrypt(cms,key,cert,NULL,out,CMS_BINARY)==1;char *plain=NULL;long size=out?BIO_get_mem_data(out,&plain):0;int exact=ok&&size==(long)length&&!memcmp(plain,want,length);if(plain&&size>0)OPENSSL_cleanse(plain,(size_t)size);BIO_free(out);CMS_ContentInfo_free(cms);ERR_clear_error();return exact;}
static char *der_request(const unsigned char *der,size_t n){char *b64=apx_pqc_base64(der,n);require(b64!=NULL,"DER base64 fixture");ApxValue *env=apx_record_new("Envelope",2);apx_record_put(env,0,"format",apx_string(APX_PQC_FORMAT,sizeof(APX_PQC_FORMAT)-1));apx_record_put(env,1,"data",apx_string(b64,(int64_t)strlen(b64)));free(b64);return open_request("slot",env);}
static void refused_der(ApxHostPolicy *p,const unsigned char *der,size_t n){char *req=der_request(der,n),*out=dispatch(p,"pqc_open",req);error_result(out,"pqc_decrypt_failed",1);free(out);free(req);apx_json_reset();}
static void interoperation_and_profile(void){
    EVP_PKEY *key=fixture_private();X509 *cert=fixture_certificate(key);char cert_path[8192],plain_path[8192],der_path[8192],private_path[8192];path(cert_path,"recipient.pem");path(plain_path,"message.bin");path(der_path,"cli.der");path(private_path,"keys/private.pem");FILE *f=fopen(cert_path,"wb");require(f&&PEM_write_X509(f,cert)&&!fclose(f),"test-only certificate export");const unsigned char message[]={'q','u','o','t','e','"',0,255,7};save_bytes("message.bin",message,sizeof message,0600);
    policy(3,",\"max_requests\":1024");char *req=body_request("slot",message,sizeof message),*out=run("seal.apx",req,1);ApxValue *env=value(out,0),*body=apx_record_new("Body",2);apx_record_put(body,0,"encoding",apx_string("base64",6));apx_record_put(body,1,"data",apx_json_get(env,"data"));size_t length=0;unsigned char *der=apx_http_decode_body(body,APX_HTTP_BODY+4096,&length);require(der!=NULL,"product DER decode");save_bytes("product.der",der,length,0600);free(out);free(req);char product_path[8192];path(product_path,"product.der");char *decrypt[]={openssl_cli,"cms","-decrypt","-binary","-inform","DER","-in",product_path,"-inkey",private_path,NULL};require(!command(decrypt,NULL),"OpenSSL CLI decrypts actual compiled product envelope");size_t n;char *decoded=readall("stdout",&n);require(n==sizeof message&&!memcmp(decoded,message,n),"independent OpenSSL CLI exact binary plaintext");free(decoded);passed("compiled-product-seal-to-OpenSSL-CLI-decrypt");
    char *encrypt[]={openssl_cli,"cms","-encrypt","-binary","-aes-256-gcm","-keyid","-in",plain_path,"-outform","DER","-out",der_path,cert_path,NULL};require(!command(encrypt,NULL),"actual independent OpenSSL CLI encrypts selected profile");char *external=readall("cli.der",&n);req=der_request((unsigned char*)external,n);out=run("open.apx",req,1);size_t size=0;unsigned char *got=apx_http_decode_body(value(out,0),APX_HTTP_BODY,&size);require(got&&size==sizeof message&&!memcmp(got,message,size),"compiled typed open interoperates exact OpenSSL CLI binary");free(got);free(out);free(req);free(external);passed("OpenSSL-CLI-encrypt-to-compiled-typed-product-open");
    ApxHostPolicy *p=load(1);
    /* Exhaust every strict-DER truncation point at the authorized broker seam;
     * rotate policy before the unrelated 1024-attempt quota. */
    for(size_t i=0;i<length;i++){if(i&&i%900==0){apx_host_policy_free(p);p=load(1);}refused_der(p,der,i);}printf("profile_truncation_positions=%zu\n",length);passed("every-DER-truncation-zero-plaintext");apx_host_policy_free(p);p=load(1);
    unsigned char *trailing=malloc(length+1);require(trailing!=NULL,"trailing fixture");memcpy(trailing,der,length);trailing[length]=0;refused_der(p,trailing,length+1);free(trailing);passed("trailing-DER-rejected");
    const unsigned char *cursor=der;FixtureEnvelope *parsed=d2i_FixtureEnvelope(NULL,&cursor,(long)length);require(parsed&&cursor==der+length,"independent standard ASN1 fixture projection");FixtureEnvelope_free(parsed);
    const char *names[]={"outer-content-type","outer-version","other-recipient-type","KEM-version","recipient-key-id","KEM-algorithm-mislabeled","KEM-parameters-present","KEM-ciphertext-length","KEM-ciphertext-tamper","KDF-algorithm","KDF-parameters-present","KEK-length","wrap-algorithm","wrap-parameters-present","wrapped-CEK-length","wrapped-CEK-tamper","content-type","content-cipher","nonce-length","nonce-tamper","GCM-tag-length-parameter","MAC-length","MAC-tamper","ciphertext-tamper","UKM-present","multiple-recipients","noncanonical-opaque-GCM-parameters","originator-info-present","authenticated-attributes-present","unauthenticated-attributes-present","detached-content"};
    for(unsigned which=0;which<sizeof names/sizeof names[0];which++){
        cursor=der;parsed=d2i_FixtureEnvelope(NULL,&cursor,(long)length);require(parsed!=NULL,"fresh standard ASN1 mutation fixture");FixtureAuthEnv *a=parsed->envelope;FixtureORI *ori=sk_FixtureRecipient_value(a->recipients,0);FixtureKEM *kem=ori->kem;FixtureEncrypted *encrypted=a->encrypted;const ASN1_OBJECT *oid=NULL;const void *params=NULL;int param_type;X509_ALGOR_get0(&oid,&param_type,&params,encrypted->algorithm);const ASN1_STRING *sequence=params;const unsigned char *start=ASN1_STRING_get0_data(sequence);cursor=start;FixtureGCM *gcm=d2i_FixtureGCM(NULL,&cursor,ASN1_STRING_get_length(sequence));require(gcm!=NULL,"GCM fixture parameters");
        switch(which){
        case 0:ASN1_OBJECT_free(parsed->type);parsed->type=OBJ_dup(OBJ_nid2obj(NID_pkcs7_enveloped));break;
        case 1:ASN1_INTEGER_set(a->version,1);break;
        case 2:ASN1_OBJECT_free(ori->type);ori->type=OBJ_dup(OBJ_nid2obj(NID_pkcs7_data));break;
        case 3:ASN1_INTEGER_set(kem->version,1);break;
        case 4:((unsigned char*)ASN1_STRING_get0_data(kem->key_id))[0]^=1;break;
        case 5:X509_ALGOR_set0(kem->kem,OBJ_dup(OBJ_nid2obj(NID_ML_KEM_512)),V_ASN1_UNDEF,NULL);break;
        case 6:X509_ALGOR_set0(kem->kem,OBJ_dup(OBJ_nid2obj(NID_ML_KEM_768)),V_ASN1_NULL,NULL);break;
        case 7:ASN1_OCTET_STRING_set(kem->kem_ciphertext,(unsigned char*)"x",1);break;
        case 8:((unsigned char*)ASN1_STRING_get0_data(kem->kem_ciphertext))[23]^=1;break;
        case 9:X509_ALGOR_set0(kem->kdf,OBJ_dup(OBJ_nid2obj(NID_sha256)),V_ASN1_UNDEF,NULL);break;
        case 10:X509_ALGOR_set0(kem->kdf,OBJ_dup(OBJ_nid2obj(NID_HKDF_SHA256)),V_ASN1_NULL,NULL);break;
        case 11:ASN1_INTEGER_set(kem->kek_length,16);break;
        case 12:X509_ALGOR_set0(kem->wrap,OBJ_dup(OBJ_nid2obj(NID_id_aes128_wrap)),V_ASN1_UNDEF,NULL);break;
        case 13:X509_ALGOR_set0(kem->wrap,OBJ_dup(OBJ_nid2obj(NID_id_aes256_wrap)),V_ASN1_NULL,NULL);break;
        case 14:ASN1_OCTET_STRING_set(kem->wrapped_key,(unsigned char*)"x",1);break;
        case 15:((unsigned char*)ASN1_STRING_get0_data(kem->wrapped_key))[0]^=1;break;
        case 16:ASN1_OBJECT_free(encrypted->type);encrypted->type=OBJ_dup(OBJ_nid2obj(NID_pkcs7_signed));break;
        case 17:X509_ALGOR_set0(encrypted->algorithm,OBJ_dup(OBJ_nid2obj(NID_aes_128_gcm)),V_ASN1_UNDEF,NULL);break;
        case 18:ASN1_OCTET_STRING_set(gcm->nonce,(unsigned char*)"x",1);break;
        case 19:((unsigned char*)ASN1_STRING_get0_data(gcm->nonce))[0]^=1;break;
        case 20:ASN1_INTEGER_set(gcm->tag_length,12);break;
        case 21:ASN1_OCTET_STRING_set(a->tag,(unsigned char*)"x",1);break;
        case 22:((unsigned char*)ASN1_STRING_get0_data(a->tag))[0]^=1;break;
        case 23:((unsigned char*)ASN1_STRING_get0_data(encrypted->ciphertext))[0]^=1;break;
        case 24:kem->ukm=ASN1_OCTET_STRING_new();require(kem->ukm&&ASN1_OCTET_STRING_set(kem->ukm,(unsigned char*)"ukm",3),"standard UKM fixture");break;
        case 25:{FixtureRecipient *copy=ASN1_item_dup(ASN1_ITEM_rptr(FixtureRecipient),ori);require(copy&&sk_FixtureRecipient_push(a->recipients,copy),"multiple standard recipients");break;}
        case 26:break;
        case 27:a->originator=sk_ASN1_TYPE_new_null();require(a->originator!=NULL,"empty standard OriginatorInfo");break;
        case 28:case 29:{STACK_OF(X509_ATTRIBUTE) **attrs=which==28?&a->authenticated:&a->unauthenticated;*attrs=sk_X509_ATTRIBUTE_new_null();X509_ATTRIBUTE *attr=X509_ATTRIBUTE_create_by_NID(NULL,NID_pkcs9_contentType,V_ASN1_OBJECT,OBJ_nid2obj(NID_pkcs7_data),-1);require(*attrs&&attr&&sk_X509_ATTRIBUTE_push(*attrs,attr),"standard attribute fixture");break;}
        case 30:ASN1_OCTET_STRING_free(encrypted->ciphertext);encrypted->ciphertext=NULL;break;
        }
        if(which==18||which==19||which==20||which==26){unsigned char *canonical=NULL;int len=i2d_FixtureGCM(gcm,&canonical);require(len>2&&canonical[0]==0x30&&canonical[1]<128,"library short GCM DER fixture");ASN1_STRING *param=ASN1_STRING_type_new(V_ASN1_SEQUENCE);require(param!=NULL,"GCM opaque parameter fixture");if(which==26){unsigned char bytes[128];require(len+1<(int)sizeof bytes,"BER fixture bound");bytes[0]=canonical[0];bytes[1]=0x81;bytes[2]=canonical[1];memcpy(bytes+3,canonical+2,(size_t)len-2);require(ASN1_STRING_set1_data(param,bytes,len+1),"deliberate noncanonical BER length inside opaque parameter");}else require(ASN1_STRING_set1_data(param,canonical,len),"mutated GCM params");X509_ALGOR_set0(encrypted->algorithm,OBJ_dup(OBJ_nid2obj(NID_aes_256_gcm)),V_ASN1_SEQUENCE,param);OPENSSL_free(canonical);}
        unsigned char *altered=NULL;int changed=i2d_FixtureEnvelope(parsed,&altered);require(changed>0,"standard ASN1 mutation encoding");if(which==5)require(generic_open(altered,(size_t)changed,key,cert,message,sizeof message),"generic CMS really accepts mislabeled KEM; product guard must discriminate");refused_der(p,altered,(size_t)changed);OPENSSL_free(altered);FixtureGCM_free(gcm);FixtureEnvelope_free(parsed);passed(names[which]);
    }
    int weak_length;unsigned char *weak=library_envelope(cert,message,sizeof message,EVP_aes_128_gcm(),&weak_length);require(generic_open(weak,(size_t)weak_length,key,cert,message,sizeof message),"generic CMS really accepts valid AES128 variant");refused_der(p,weak,(size_t)weak_length);OPENSSL_free(weak);passed("library-valid-weak-AES128-standard-CMS-refused");
    const char *bad64[]={"AA=","AA===","AB==","AA==\n","AA=A","/x==",""};for(size_t i=0;i<sizeof bad64/sizeof bad64[0];i++){ApxValue *e=apx_record_new("Envelope",2);apx_record_put(e,0,"format",apx_string(APX_PQC_FORMAT,sizeof(APX_PQC_FORMAT)-1));apx_record_put(e,1,"data",apx_string(bad64[i],(int64_t)strlen(bad64[i])));req=open_request("slot",e);out=dispatch(p,"pqc_open",req);error_result(out,"pqc_decrypt_failed",1);free(out);free(req);apx_json_reset();passed("open-malformed-base64-uniform-auth-failure");}
    ApxValue *e=apx_record_new("Envelope",2);apx_record_put(e,0,"format",apx_string("unsupported",11));apx_record_put(e,1,"data",apx_string("",0));req=open_request("slot",e);out=dispatch(p,"pqc_open",req);error_result(out,"pqc_decrypt_failed",1);free(out);free(req);passed("open-unsupported-format-uniform-failure");
    /* Valid ciphertext under a completely different ML-KEM key. */
    EVP_PKEY *wrong=EVP_PKEY_Q_keygen(NULL,NULL,"ML-KEM-768");require(wrong!=NULL,"independent wrong key");X509 *wrong_cert=fixture_certificate(wrong);int wrong_length;unsigned char *wrong_der=library_envelope(wrong_cert,message,sizeof message,EVP_aes_256_gcm(),&wrong_length);refused_der(p,wrong_der,(size_t)wrong_length);OPENSSL_free(wrong_der);X509_free(wrong_cert);EVP_PKEY_free(wrong);passed("valid-other-key-recipient-zero-plaintext");
    /* Success after all malformed/authentication failures in same broker. */
    req=der_request(der,length);out=dispatch(p,"pqc_open",req);body=value(out,1);got=apx_http_decode_body(body,APX_HTTP_BODY,&size);require(got&&size==sizeof message&&!memcmp(got,message,size),"good follow-on authentication recovery");free(got);free(out);free(req);apx_host_policy_free(p);free(der);X509_free(cert);EVP_PKEY_free(key);apx_json_reset();passed("authentication-failure-recovery");
}

static void fixture_helper(char out[8192],const char *name){char target[8192];path(out,name);require(snprintf(target,sizeof target,"%s/.build/tools/apx-pqc-fixture",root)<(int)sizeof target&&!symlink(target,out),"test-only native fixture helper link");}
static volatile sig_atomic_t cancellation_flag;
static void cancel_signal(int s){(void)s;cancellation_flag=1;}
static void lifecycle(void){
    policy(3,",\"crypto_timeout_ms\":250");ApxHostPolicy *p=load(1);unsigned before=fd_count();char *req=body_request("slot",(const unsigned char*)"audit",5),*out;char audit[8192];fixture_helper(audit,"audit");
    require(!setenv("OPENSSL_CONF","/untrusted/config",1)&&!setenv("OPENSSL_MODULES","/untrusted/provider",1)&&!setenv("APX_PRIVATE_SENTINEL","must-not-inherit",1),"test ambient authority sentinels");apx_host_pqc_helper(audit);out=dispatch(p,"pqc_seal",req);error_result(out,"pqc_encrypt_failed",1);free(out);ApxValue *e=apx_record_new("Envelope",2);apx_record_put(e,0,"format",apx_string(APX_PQC_FORMAT,sizeof(APX_PQC_FORMAT)-1));apx_record_put(e,1,"data",apx_string("",0));char *opening=open_request("slot",e);out=dispatch(p,"pqc_open",opening);error_result(out,"pqc_decrypt_failed",1);free(out);free(opening);require(!unsetenv("OPENSSL_CONF")&&!unsetenv("OPENSSL_MODULES")&&!unsetenv("APX_PRIVATE_SENTINEL"),"remove own ambient sentinels");require(fd_count()==before,"privacy audit helper leaves no fds");passed("selected-sealed-FD4-only-private-channel-FD3-no-ambient-env");
    /* Force original snapshot fd3 and socket/key descriptor collisions. */
    apx_host_pqc_helper(helper);apx_host_policy_free(p);fflush(NULL);pid_t alias=fork();require(alias>=0,"fd collision child");if(!alias){alarm(8);for(int fd=3;fd<1024;fd++)close(fd);ApxHostPolicy *collision=load(1);apx_host_pqc_helper(audit);char *reply=dispatch(collision,"pqc_seal",req);ApxValue *v=apx_json_parse(reply,strlen(reply));const char *error=apx_http_string(apx_json_get(v,"error"));int ok=error&&!strcmp(error,"pqc_encrypt_failed");free(reply);apx_host_policy_free(collision);apx_json_reset();_exit(ok?0:1);}int status;require(waitpid(alias,&status,0)==alias&&WIFEXITED(status)&&!WEXITSTATUS(status),"FD3/FD4 alias-safe selected snapshot and channel");passed("FD3-FD4-alias-safe-exec");p=load(1);before=fd_count();
    const char *modes[]={"startup","stallread","writestall","exitstall"};char marker[8192];path(marker,"helper-ready");
    for(size_t i=0;i<4;i++){char binary[8192];fixture_helper(binary,modes[i]);apx_host_pqc_helper(binary);size_t n=i==1?1048576:5;unsigned char *data=malloc(n);require(data!=NULL,"write-stall body allocation");memset(data,'a',n);char *large=body_request("slot",data,n);free(data);int64_t start=apx_http_now();out=dispatch(p,"pqc_seal",large);error_result(out,"pqc_timeout",1);require(apx_http_now()-start<1200,"absolute complete IPC deadline");free(out);free(large);errno=0;require(waitpid(-1,&status,WNOHANG)==-1&&errno==ECHILD,"timeout exactly reaps helper");require(!access(marker,F_OK)&&!unlink(marker),"fixture reached explicit phase-ready signal");require(fd_count()==before,"timeout no fd leak");passed(modes[i]);}
    const char *malformed[]={"badshape","badtoken","trailing"};for(size_t i=0;i<3;i++){char binary[8192];fixture_helper(binary,malformed[i]);apx_host_pqc_helper(binary);out=dispatch(p,"pqc_seal",req);error_result(out,"pqc_unavailable",1);free(out);passed("complete-helper-shape-token-single-frame-validation");}
    apx_host_pqc_helper(audit);apx_host_execution(p,apx_http_now()-1,0,-1,NULL,NULL);out=dispatch(p,"pqc_seal",req);error_result(out,"pqc_timeout",1);free(out);passed("enclosing-execution-deadline-prevents-startup");
    struct sigaction action={0},previous;action.sa_handler=cancel_signal;sigemptyset(&action.sa_mask);require(!sigaction(SIGALRM,&action,&previous),"owned cancellation handler");cancellation_flag=0;apx_host_execution(p,apx_http_now()+5000,0,-1,NULL,&cancellation_flag);char stall[8192];path(stall,"writestall");apx_host_pqc_helper(stall);ualarm(50000,0);int64_t start=apx_http_now();out=dispatch(p,"pqc_seal",req);ualarm(0,0);error_result(out,"pqc_timeout",1);free(out);require(cancellation_flag&&apx_http_now()-start<1000,"cancellation interrupts and reaps pending IPC");require(!sigaction(SIGALRM,&previous,NULL)&&!unlink(marker),"restore signal state and remove exact phase marker");errno=0;require(waitpid(-1,&status,WNOHANG)==-1&&errno==ECHILD,"cancellation helper exactly reaped");passed("broker-cancellation-reaping");
    apx_host_execution(p,INT64_MAX,0,-1,NULL,NULL);apx_host_pqc_helper("/nonexistent/apx-pqc");out=dispatch(p,"pqc_seal",req);error_result(out,"pqc_unavailable",1);free(out);passed("missing-optional-helper-fails-closed");apx_host_pqc_helper(helper);out=dispatch(p,"pqc_seal",req);value(out,1);free(out);free(req);apx_host_policy_free(p);apx_json_reset();passed("timeout-cancel-failure-follow-on-real-backend-recovery");
}
static void unrelated_helper_privacy(void){
    policy(3,",\"tls_security\":\"standard\",\"http\":[{\"origin\":\"http://127.0.0.1:1\",\"path_prefix\":\"/\",\"methods\":[\"GET\"],\"headers\":[]}]");ApxHostPolicy *p=load(1);char binary[8192];fixture_helper(binary,"unrelated");apx_host_helpers(binary,"/nonexistent/tls-helper");const char *request="{\"method\":\"GET\",\"url\":\"http://127.0.0.1:1/\",\"headers\":[],\"body\":{\"encoding\":\"utf8\",\"data\":\"\"}}";char *out=dispatch(p,"http_request",request);error_result(out,"http_transport_failed",1);free(out);apx_host_policy_free(p);apx_json_reset();passed("unrelated-HTTP-helper-inherits-no-key-snapshots");
}
static void maximum_body(void){
    policy(3,NULL);save("max.apx","M maximum\nI p \"pqc\"\nF main->i64\n  L data \"\\0\"\n  L i 0\n  W (< i 20)\n    S data (cat data data)\n    S i (+ i 1)\n  C (p::seal \"slot\" (p::text data))\n    ok envelope\n      C (p::open \"slot\" envelope)\n        ok body\n          C (p::body_text body)\n            ok plain\n              A (= plain data)\n              R (# plain)\n            err problem\n              Z \"maximum-roundtrip-failed\"\n        err problem\n          Z \"maximum-roundtrip-failed\"\n    err problem\n      Z \"maximum-roundtrip-failed\"\n");
    char *req=body_request("slot",(const unsigned char*)"four",4),*sealed=run("seal.apx",req,1),*opening=open_request("slot",value(sealed,0));free(req);free(sealed);policy(3,",\"max_bytes\":3");char *over=run("open.apx",opening,1);error_result(over,"pqc_byte_limit",0);free(over);free(opening);passed("valid-ciphertext-over-content-quota");policy(3,NULL);
    char *out=run("max.apx","null",1);ApxValue *n=apx_json_parse(out,strlen(out));require(apx_json_integer(n)==1048576,"full one-MiB NUL typed roundtrip independent of JSON/frame quotas");free(out);apx_json_reset();passed("max-plaintext-worst-case-NUL-escaping-public-compiled-roundtrip");
}
static void minimal_layout(void){
    const char *dirs[]={"layout","layout/bin","layout/.build","layout/.build/tools"};char file[8192],target[8192];for(size_t i=0;i<4;i++){path(file,dirs[i]);require(!mkdir(file,0700),"minimal source-free test layout");}
    path(file,"layout/bin/apodexis");require(!link(cli,file),"hardlink only exact current CLI, no source/build copy");const char *links[]={".build/tools/qbe",".build/apx_runtime.o",".build/apx_start.o","resources","examples"};for(size_t i=0;i<5;i++){char name[128];snprintf(name,sizeof name,"layout/%s",links[i]);path(file,name);require(snprintf(target,sizeof target,"%s/%s",root,links[i])<(int)sizeof target&&!symlink(target,file),"share only unchanged current inputs");}
    char live_cli[8192];strcpy(live_cli,cli);path(cli,"layout/bin/apodexis");save("input.json","null");char dest[8192];path(dest,"missing-backend-keys");char *gen[]={cli,"pqc-keygen",dest,"--json",NULL};require(command(gen,NULL)!=0,"missing backend keygen fails");char *out=readall("stdout",NULL);ApxValue *v=apx_json_parse(out,strlen(out));require(apx_json_length(v)==3&&!strcmp(apx_http_string(apx_json_get(v,"error")),"pqc_unavailable")&&access(dest,F_OK),"missing backend generic no directory effect");free(out);passed("operator-keygen-missing-helper-unavailable");
    char fail_helper[8192];path(fail_helper,"layout/.build/tools/apx-pqc");require(snprintf(target,sizeof target,"%s/.build/tools/apx-pqc-keygen-fail",root)<(int)sizeof target&&!symlink(target,fail_helper),"compile-time keygen fsync failure helper");path(dest,"partial-keys");require(command(gen,NULL)!=0,"actual native keygen partial fsync failure");out=readall("stdout",NULL);v=apx_json_parse(out,strlen(out));require(apx_json_length(v)==3&&!strcmp(apx_http_string(apx_json_get(v,"error")),"pqc_keygen_failed"),"partial error payload-free no false success");free(out);path(file,"partial-keys/public.pem");require(access(file,F_OK)&&errno==ENOENT,"only owned public output removed");path(file,"partial-keys/private.pem");require(access(file,F_OK)&&errno==ENOENT,"only owned private output removed");path(file,"partial-keys/foreign-sentinel");struct stat st;require(!stat(file,&st),"foreign sentinel preserved, no recursive removal");path(file,"partial-keys");require(!stat(file,&st)&&(st.st_mode&07777)==0700,"unadvertised partial directory remains protected");require(!unlink(fail_helper),"remove injected helper link");passed("real-keygen-partial-failure-owned-cleanup-preserves-foreign-file");
    /* CLI lifecycle: validation succeeds, then a test helper explicitly signals
     * operation entry. Cancel the actual CLI or its generated worker. */
    require(snprintf(target,sizeof target,"%s/.build/tools/apx-pqc-fixture",root)<(int)sizeof target&&!symlink(target,fail_helper),"lifecycle fixture helper");
    for(int worker=0;worker<2;worker++){
        policy(3,NULL);char *req=body_request("slot",(unsigned char*)"cancel",6);save("input.json",req);free(req);char src[8192],input[8192],pol[8192],stdout_path[8192],stderr_path[8192],marker[8192];path(src,"seal.apx");path(input,"input.json");path(pol,"policy.json");path(stdout_path,"cancel-stdout");path(stderr_path,"cancel-stderr");path(marker,"layout/.build/tools/helper-ready");fflush(NULL);pid_t process=fork();require(process>=0,"real cancellable CLI");if(!process){alarm(10);if(!freopen(stdout_path,"wb",stdout)||!freopen(stderr_path,"wb",stderr))_exit(126);execl(cli,cli,"run",src,"--input",input,"--capabilities",pol,"--json",(char*)NULL);_exit(127);}
        int64_t until=apx_http_now()+4000;while(access(marker,F_OK)&&apx_http_now()<until)poll(NULL,0,1);require(!access(marker,F_OK),"explicit helper operation entry signal");char proc[128];snprintf(proc,sizeof proc,"/proc/%ld/task/%ld/children",(long)process,(long)process);FILE *children=fopen(proc,"rb");require(children!=NULL,"real CLI child inventory");long child;pid_t helper_pid=0,worker_pid=0;while(fscanf(children,"%ld",&child)==1){char link[128],resolved[8192];snprintf(link,sizeof link,"/proc/%ld/exe",child);ssize_t n=readlink(link,resolved,sizeof resolved-1);if(n>0){resolved[n]=0;if(strstr(resolved,"apx-pqc-fixture"))helper_pid=(pid_t)child;else if(strstr(resolved,"/program"))worker_pid=(pid_t)child;}}fclose(children);require(helper_pid>0&&worker_pid>0,"exact live helper and compiled worker");char fddir[128];snprintf(fddir,sizeof fddir,"/proc/%ld/fd",(long)worker_pid);DIR *fds=opendir(fddir);require(fds!=NULL,"generated worker descriptors");struct dirent *entry;while((entry=readdir(fds))){if(entry->d_name[0]=='.')continue;char link[512],resolved[8192];require(snprintf(link,sizeof link,"%s/%s",fddir,entry->d_name)<(int)sizeof link,"worker fd path");ssize_t n=readlink(link,resolved,sizeof resolved-1);require(n>=0,"worker descriptor readback");resolved[n]=0;require(!strstr(resolved,"apx-crypto-key"),"generated worker inherits no key snapshot");}closedir(fds);int64_t start=apx_http_now();require(!kill(worker?worker_pid:process,worker?SIGKILL:SIGTERM),"cancel exact owned process");int status;require(waitpid(process,&status,0)==process&&apx_http_now()-start<1500,"CLI cancellation bounded");snprintf(proc,sizeof proc,"/proc/%ld",(long)helper_pid);require(access(proc,F_OK)&&errno==ENOENT,"helper reaped after CLI/worker cancellation");snprintf(proc,sizeof proc,"/proc/%ld",(long)worker_pid);require(access(proc,F_OK)&&errno==ENOENT,"worker reaped after cancellation");require(!unlink(marker),"remove exact phase marker");passed(worker?"actual-worker-death-cancels-and-reaps-helper":"actual-CLI-SIGTERM-cancels-and-reaps-helper");
    }
    strcpy(cli,live_cli);apx_json_reset();
}

static ApxValue *rpc(FILE *send,FILE *receive,unsigned *id,const char *method,const char *params){unsigned request=++*id;require(fprintf(send,"{\"jsonrpc\":\"2.0\",\"id\":%u,\"method\":\"%s\",\"params\":%s}\n",request,method,params)>0&&!fflush(send),"real MCP request");char *line=NULL;size_t capacity=0;ssize_t n=getline(&line,&capacity,receive);require(n>0,"MCP response");apx_json_reset();ApxValue *v=apx_json_parse(line,(size_t)n);free(line);require(v&&apx_json_integer(apx_json_get(v,"id"))==request,"MCP response correlated by id");return v;}
static ApxValue *tool(FILE *send,FILE *receive,unsigned *id,const char *name,const char *args){char params[16000];require(snprintf(params,sizeof params,"{\"name\":\"%s\",\"arguments\":%s}",name,args)<(int)sizeof params,"MCP arguments bound");return rpc(send,receive,id,"tools/call",params);}
static void denials_and_catalog(void){
    const char *source="M denied\nF main->[result str str]\n  R [(pqc_seal \"{}\") (pqc_open \"{}\")]\nX denied\n  A (= (main) [(err \"capability_denied\") (err \"capability_denied\")])\n";save("denied.apx",source);char *out=run("denied.apx","null",0);ApxValue *v=apx_json_parse(out,strlen(out));require(apx_json_length(v)==2,"both appended builtins compiled and execute");for(size_t i=0;i<2;i++){char *r=apx_json_encode(apx_json_item(v,i));error_result(r,"capability_denied",0);free(r);}free(out);passed("public-compiled-both-builtins-default-denied");
    char src[8192],artifact[8192];path(src,"denied.apx");path(artifact,"standalone");char *build[]={cli,"build",src,"-o",artifact,"--json",NULL};require(!command(build,NULL),"compile standalone denied-host binary");char *invoke[]={artifact,NULL};require(!command(invoke,NULL),"pure standalone executes without helper/policy");out=readall("stdout",NULL);v=apx_json_parse(out,strlen(out));require(apx_json_length(apx_json_get(v,"output"))==2,"standalone output array");for(size_t i=0;i<2;i++){char *r=apx_json_encode(apx_json_item(apx_json_get(v,"output"),i));error_result(r,"capability_denied",0);free(r);}free(out);passed("standalone-compiled-artifact-has-no-key-authority");
    char module_main[8192];require(snprintf(module_main,sizeof module_main,"%s/examples/pqc/main.apx",root)<(int)sizeof module_main,"shipped module test path");char *module_test[]={cli,"test",module_main,"--json",NULL};require(!command(module_test,NULL),"ordinary shipped PQC module tests compiled");char *module_result=readall("stdout",NULL);ApxValue *module_record=apx_json_parse(module_result,strlen(module_result));require(apx_json_length(apx_json_get(module_record,"tests"))==3,"all three text/body_text tests execute");free(module_result);passed("shipped-typed-module-text-and-body-text-contract");
    const char *ops[]={"test","verify"};for(size_t i=0;i<2;i++){char *args[]={cli,(char*)ops[i],src,"--json",NULL};require(!command(args,NULL),"CLI tests/verify assert both default denials");passed(ops[i]);}
    int in[2],response[2];require(!pipe2(in,O_CLOEXEC)&&!pipe2(response,O_CLOEXEC),"MCP owned channels");fflush(NULL);pid_t child=fork();require(child>=0,"real MCP process");if(!child){alarm(20);if(dup2(in[0],0)<0||dup2(response[1],1)<0)_exit(126);close(in[0]);close(in[1]);close(response[0]);close(response[1]);execl(cli,cli,"mcp",(char*)NULL);_exit(127);}close(in[0]);close(response[1]);FILE *send=fdopen(in[1],"w"),*receive=fdopen(response[0],"r");require(send&&receive,"MCP streams");unsigned id=0;v=rpc(send,receive,&id,"initialize","{}");require(!apx_json_get(v,"error"),"MCP initialize");require(fputs("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n",send)>=0&&!fflush(send),"MCP initialized");v=rpc(send,receive,&id,"tools/list","{}");require(apx_json_length(apx_json_get(apx_json_get(v,"result"),"tools"))==13,"strict unchanged thirteen-tool MCP discovery");passed("MCP-exactly-thirteen-tools");
    char *encoded=apx_json_encode(apx_string(source,(int64_t)strlen(source)));require(encoded!=NULL,"MCP authored source encoding");char args[15000];snprintf(args,sizeof args,"{\"source\":%s,\"input\":null,\"limits\":{\"fuel\":10000,\"max_input_bytes\":null,\"max_output_bytes\":null}}",encoded);v=tool(send,receive,&id,"apodexis_run",args);ApxValue *record=apx_json_get(apx_json_get(v,"result"),"structuredContent");ApxValue *output=apx_json_get(record,"output");require(apx_json_length(output)==2,"MCP both default denial outputs");for(size_t i=0;i<2;i++){out=apx_json_encode(apx_json_item(output,i));error_result(out,"capability_denied",0);free(out);}passed("MCP-real-compiled-pqc-run-default-denial");
    snprintf(args,sizeof args,"{\"source\":%s}",encoded);v=tool(send,receive,&id,"apodexis_compile",args);record=apx_json_get(apx_json_get(v,"result"),"structuredContent");const char *handle=apx_http_string(apx_json_get(record,"artifact_id"));require(handle&&strlen(handle)==38,"MCP sealed artifact handle");char saved[39];strcpy(saved,handle);snprintf(args,sizeof args,"{\"artifact_id\":\"%s\",\"input\":null,\"limits\":{\"fuel\":10000,\"max_input_bytes\":null,\"max_output_bytes\":null}}",saved);v=tool(send,receive,&id,"apodexis_invoke",args);record=apx_json_get(apx_json_get(v,"result"),"structuredContent");output=apx_json_get(record,"output");require(apx_json_length(output)==2,"MCP compiled invoke both denials");for(size_t i=0;i<2;i++){out=apx_json_encode(apx_json_item(output,i));error_result(out,"capability_denied",0);free(out);}passed("MCP-sealed-compiled-invoke-default-denial");
    for(size_t i=0;i<2;i++){snprintf(args,sizeof args,"{\"source\":%s}",encoded);v=tool(send,receive,&id,i?"apodexis_verify":"apodexis_test",args);record=apx_json_get(apx_json_get(v,"result"),"structuredContent");require(!strcmp(apx_http_string(apx_json_get(record,"status")),"ok"),"MCP test/verify default denial assertions");passed(i?"MCP-verify-denied-effects":"MCP-test-denied-effects");}
    snprintf(args,sizeof args,"{\"source\":%s,\"input\":null,\"limits\":{\"fuel\":10000,\"max_input_bytes\":null,\"max_output_bytes\":null},\"capabilities\":{\"schema\":\"apodexis.host_policy.v3\",\"crypto_keys\":[]}}",encoded);v=tool(send,receive,&id,"apodexis_run",args);require(apx_json_get(v,"error")!=NULL,"MCP cannot self-grant policy-shaped arguments");free(encoded);passed("MCP-policy-shaped-self-grant-refused");
    v=tool(send,receive,&id,"apodexis_capabilities","{}");record=apx_json_get(apx_json_get(v,"result"),"structuredContent");ApxValue *pq=apx_json_get(record,"post_quantum");require(apx_json_length(pq)==7&&!strcmp(apx_http_string(apx_json_get(pq,"application_profile")),APX_PQC_FORMAT)&&apx_json_kind(apx_json_get(pq,"application_helper_available"))==1&&apx_json_integer(apx_json_get(pq,"application_helper_available")),"schema-validated exact post_quantum capabilities");passed("canonical-capabilities-schema-post-quantum-exact-fields");
    v=rpc(send,receive,&id,"resources/read","{\"uri\":\"apodexis://spec/opcodes\"}");ApxValue *contents=apx_json_get(apx_json_get(v,"result"),"contents");const char *text=apx_http_string(apx_json_get(apx_json_item(contents,0),"text"));require(text!=NULL,"canonical opcodes resource");ApxValue *builtins=apx_json_parse(text,strlen(text));unsigned found=0;for(size_t i=0;i<apx_json_length(builtins);i++){ApxValue *b=apx_json_item(builtins,i);const char *name=apx_http_string(apx_json_get(b,"name"));if(name&&!strcmp(name,"pqc_seal")){require(apx_json_integer(apx_json_get(b,"opcode"))==1008,"seal catalog opcode");found++;}if(name&&!strcmp(name,"pqc_open")){require(apx_json_integer(apx_json_get(b,"opcode"))==1009,"open catalog opcode");found++;}}require(found==2,"canonical builtin catalog append parity");passed("MCP-canonical-builtin-opcode-resource");
    snprintf(args,sizeof args,"{\"artifact_id\":\"%s\"}",saved);v=tool(send,receive,&id,"apodexis_release",args);require(!apx_json_get(v,"error"),"release exact owned artifact");fclose(send);fclose(receive);int status;require(waitpid(child,&status,0)==child&&WIFEXITED(status)&&!WEXITSTATUS(status),"MCP EOF clean teardown");apx_json_reset();
}

int main(void){
    const char *tmp=getenv("TMPDIR");require(tmp&&tmp[0]=='/',"explicit task scratch TMPDIR");require(snprintf(directory,sizeof directory,"%s/apx-pqc-XXXXXX",tmp)<(int)sizeof directory&&mkdtemp(directory),"owned disposable fixture root");atexit(cleanup);printf("fixture_root=%s\n",directory);
    const char *override=getenv("APX_TEST_TOOLCHAIN_ROOT");require(override?snprintf(root,sizeof root,"%s",override)<(int)sizeof root:getcwd(root,sizeof root)!=NULL,"toolchain root");require(snprintf(cli,sizeof cli,"%s/bin/apodexis",root)<(int)sizeof cli&&snprintf(helper,sizeof helper,"%s/.build/tools/apx-pqc",root)<(int)sizeof helper,"tool paths");apx_host_pqc_helper(helper);
    const char *ossl=getenv("APX_TEST_OPENSSL");require(ossl&&snprintf(openssl_cli,sizeof openssl_cli,"%s",ossl)<(int)sizeof openssl_cli,"explicit test OpenSSL CLI");
    char module[8192],module_path[8192];require(snprintf(module_path,sizeof module_path,"%s/examples/pqc/pqc.apx",root)<(int)sizeof module_path,"module path");FILE *f=fopen(module_path,"rb");require(f!=NULL,"ordinary shipped module");size_t n=fread(module,1,sizeof module-1,f);require(n<sizeof module-1&&!ferror(f)&&!fclose(f),"bounded module read");module[n]=0;save("pqc.apx",module);
    save("seal.apx","M seal\nI p \"pqc\"\nT Input struct\n  key:str\n  body:p::Body\nF main input:Input->result p::Envelope str\n  R (p::seal input.key input.body)\n");
    save("open.apx","M decrypt\nI p \"pqc\"\nT Input struct\n  key:str\n  envelope:p::Envelope\nF main input:Input->result p::Body str\n  R (p::open input.key input.envelope)\n");
    save("raw-seal.apx","M raw\nF main text:str->result str str\n  R (pqc_seal text)\n");save("raw-open.apx","M raw\nF main text:str->result str str\n  R (pqc_open text)\n");
    keygen();positives();key_failures();policy_failures();snapshot_and_requests();interoperation_and_profile();lifecycle();unrelated_helper_privacy();maximum_body();minimal_layout();denials_and_catalog();
    policy(1,NULL);ApxHostPolicy *p=load(1);char *req=body_request("slot",(const unsigned char*)"x",1),*sealed=dispatch(p,"pqc_seal",req);ApxValue *env=value(sealed,1);char *opening=open_request("slot",env),*out=dispatch(p,"pqc_open",opening);error_result(out,"pqc_key_denied",1);free(out);apx_host_policy_free(p);
    policy(2,NULL);p=load(1);out=dispatch(p,"pqc_seal",req);error_result(out,"pqc_key_denied",1);free(out);out=dispatch(p,"pqc_open",opening);value(out,1);free(out);apx_host_policy_free(p);free(req);free(opening);free(sealed);passed("operation-specific-seal-only-open-only-grants");
    printf("PQC PASS cases=%u assertions=%u skips=0\n",cases,checks);apx_json_reset();return 0;
}
#endif
