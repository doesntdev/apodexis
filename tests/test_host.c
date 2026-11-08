#define _GNU_SOURCE
#include "../compiler/apx_host.h"
#include "../runtime/apx_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <signal.h>
static unsigned checks;
static void require(int ok,const char *what){checks++;if(!ok){fprintf(stderr,"host FAIL: %s\n",what);exit(1);}}
static void save(const char *p,const char *data){FILE *f=fopen(p,"wb");require(f&&fwrite(data,1,strlen(data),f)==strlen(data)&&!fclose(f),"write owned fixture");}
static char *request(ApxHostPolicy *p,const char *data,int ok,const char *contains){char *result=apx_host_dispatch(p,data,strlen(data));require(result!=NULL,"host response");ApxValue *json=apx_json_parse(result,strlen(result));require(json&&apx_json_integer(apx_json_get(json,"ok"))==ok,"host status");require(!contains||strstr(result,contains)!=NULL,contains?contains:"response");apx_json_reset();return result;}
static void v2_policy(const char *path){
 const char *invalid[]={
 "{\"schema\":\"apodexis.host_policy.v2\",\"http\\u0000hidden\":[]}",
 "{\"schema\":\"apodexis.host_policy.v2\",\"http\":[],\"http\\u0000hidden\":[]}",
 "{\"schema\":\"apodexis.host_policy.v1\",\"http\":[]}",
 "{\"schema\":\"apodexis.host_policy.v2\",\"http_timeout_ms\":5001}",
 "{\"schema\":\"apodexis.host_policy.v2\",\"http_timeout_ms\":0}",
 "{\"schema\":\"apodexis.host_policy.v2\",\"http\":[{\"origin\":\"http://user@localhost\",\"path_prefix\":\"/\",\"methods\":[\"GET\"],\"headers\":[]}]}",
 "{\"schema\":\"apodexis.host_policy.v2\",\"http\":[{\"origin\":\"http://localhost:65536\",\"path_prefix\":\"/\",\"methods\":[\"GET\"],\"headers\":[]}]}",
 "{\"schema\":\"apodexis.host_policy.v2\",\"http\":[{\"origin\":\"http://localhost/\",\"path_prefix\":\"/\",\"methods\":[\"GET\"],\"headers\":[]}]}",
 "{\"schema\":\"apodexis.host_policy.v2\",\"http\":[{\"origin\":\"http://localhost\",\"path_prefix\":\"/v1\",\"methods\":[\"GET\"],\"headers\":[]}]}",
 "{\"schema\":\"apodexis.host_policy.v2\",\"http\":[{\"origin\":\"http://localhost\",\"path_prefix\":\"/../\",\"methods\":[\"GET\"],\"headers\":[]}]}",
 "{\"schema\":\"apodexis.host_policy.v2\",\"http\":[{\"origin\":\"http://localhost\",\"path_prefix\":\"/\",\"methods\":[\"get\"],\"headers\":[]}]}",
 "{\"schema\":\"apodexis.host_policy.v2\",\"http\":[{\"origin\":\"http://localhost\",\"path_prefix\":\"/\",\"methods\":[\"GET\",\"GET\"],\"headers\":[]}]}",
 "{\"schema\":\"apodexis.host_policy.v2\",\"http\":[{\"origin\":\"http://localhost\",\"path_prefix\":\"/\",\"methods\":[\"GET\"],\"headers\":[\"x-test\",\"X-Test\"]}]}",
 "{\"schema\":\"apodexis.host_policy.v2\",\"http\":[{\"origin\":\"http://localhost\",\"path_prefix\":\"/\",\"methods\":[\"GET\"],\"headers\":[\"Host\"]}]}",
 "{\"schema\":\"apodexis.host_policy.v2\",\"tls_ca_file\":\"relative\"}",
 "{\"schema\":\"apodexis.host_policy.v2\",\"unknown\":true}",
 "{\"schema\":\"apodexis.host_policy.v2\",\"http\":null}",
 "{\"schema\":\"apodexis.host_policy.v2\",\"max_bytes\":1,\"max_bytes\":2}"
 };
 for(size_t i=0;i<sizeof invalid/sizeof invalid[0];i++){save(path,invalid[i]);char *error=NULL;ApxHostPolicy *p=apx_host_policy_load(path,&error);require(!p&&error,"strict malformed v1/v2 policy rejected");free(error);apx_json_reset();}
 save(path,"{\"schema\":\"apodexis.host_policy.v2\",\"http\":[{\"origin\":\"http://LOCALHOST:80\",\"path_prefix\":\"/v1/\",\"methods\":[\"GET\"],\"headers\":[]}],\"max_requests\":2}");char *error=NULL;ApxHostPolicy *p=apx_host_policy_load(path,&error);require(p&&!error,"v2 accepted strict grant");
 free(request(p,"{\"op\":\"http_get\",\"args\":[\"http://localhost/v1/\"]}",0,"http_get_denied"));
 free(request(p,"{\"op\":\"http_request\",\"args\":[\"{}\"]}",0,"invalid_http_request"));
 free(request(p,"{\"op\":\"http_request\",\"args\":[\"{}\"]}",0,"host_request_limit"));apx_host_policy_free(p);apx_json_reset();
 printf("host v2 inventory: %zu malformed policies plus positive/legacy/budget controls PASS\n",sizeof invalid/sizeof invalid[0]);
}
/* Malformed HTTP documents must not exhaust the broker while constructing an error. */
static void bounded_http_documents(const char *path){
 save(path,"{\"schema\":\"apodexis.host_policy.v2\"}");char *error=NULL;ApxHostPolicy *policy=apx_host_policy_load(path,&error);require(policy&&!error,"empty HTTP policy for document bounds");
 for(unsigned object=0;object<2;object++){fflush(NULL);pid_t child=fork();require(child>=0,"bounded malformed document child");if(!child){alarm(8);struct rlimit core={0,0};if(setrlimit(RLIMIT_CORE,&core))_exit(30);size_t count=2000000,n=0;char *raw=malloc(count*2+32);if(!raw)_exit(31);if(object){memcpy(raw,"{\"headers\":",11);n=11;}raw[n++]='[';for(size_t i=0;i<count;i++){raw[n++]='0';if(i+1<count)raw[n++]=',';}raw[n++]=']';if(object)raw[n++]='}';raw[n]=0;ApxValue *host=apx_record_new("host",2),*args=apx_array_new(1);apx_record_put(host,0,"op",apx_string("http_request",12));apx_array_put(args,0,apx_string(raw,(int64_t)n));apx_record_put(host,1,"args",args);char *wire=apx_json_encode(host);if(!wire)_exit(32);char *out=apx_host_dispatch(policy,wire,strlen(wire));ApxValue *reply=out?apx_json_parse(out,strlen(out)):NULL;const char *token=apx_json_string(apx_json_get(reply,"error"));int ok=apx_json_kind(apx_json_get(reply,"ok"))==1&&!apx_json_integer(apx_json_get(reply,"ok"))&&token&&!strcmp(token,"invalid_http_request");free(out);free(wire);free(raw);apx_host_policy_free(policy);apx_json_reset();_exit(ok?0:33);}int status;require(waitpid(child,&status,0)==child&&WIFEXITED(status)&&!WEXITSTATUS(status),"large malformed HTTP document returns bounded error without crash");}
 apx_host_policy_free(policy);apx_json_reset();printf("host HTTP document bounds: 2 oversized-shape cases PASS\n");
}
int main(void){char dir[]=".build/host-unit-XXXXXX";require(mkdtemp(dir)!=NULL,"owned root");char *absolute=realpath(dir,NULL);require(absolute!=NULL,"absolute root");char policy[4096],input[4096],output[4096],symlink_path[4096],hardlink_path[4096];snprintf(policy,sizeof policy,"%s/policy.json",dir);snprintf(input,sizeof input,"%s/input.txt",dir);snprintf(output,sizeof output,"%s/output.txt",dir);snprintf(symlink_path,sizeof symlink_path,"%s/link",dir);snprintf(hardlink_path,sizeof hardlink_path,"%s/hard",dir);save(input,"hello");require(!setenv("APX_TEST_HOST_ONLY","fixture-value",1),"fixture env");char raw[10000];snprintf(raw,sizeof raw,"{\"schema\":\"apodexis.host_policy.v1\",\"read_root\":\"%s\",\"write_root\":\"%s\",\"env\":[\"APX_TEST_HOST_ONLY\"],\"clock\":true,\"random\":true,\"max_bytes\":128,\"max_requests\":64}",absolute,absolute);save(policy,raw);char *error=NULL;ApxHostPolicy *p=apx_host_policy_load(policy,&error);require(p&&!error,"valid explicit policy");
 free(request(NULL,"{\"op\":\"fs_read\",\"args\":[\"input.txt\"]}",0,"capability_denied"));
 free(request(p,"{\"op\":\"fs_read\",\"args\":[\"input.txt\"]}",1,"hello"));
 free(request(p,"{\"op\":\"fs_write\",\"args\":[\"output.txt\",\"written\"]}",1,"7"));FILE *f=fopen(output,"rb");char written[32]={0};require(f&&fread(written,1,31,f)==7&&!fclose(f)&&!strcmp(written,"written"),"exact external write readback");
 free(request(p,"{\"op\":\"fs_read\",\"args\":[\"../outside\"]}",0,"filesystem_read_denied"));
 free(request(p,"{\"op\":\"fs_write\",\"args\":[\"/outside\",\"x\"]}",0,"filesystem_write_denied"));
 require(!symlink("input.txt",symlink_path),"symlink fixture");free(request(p,"{\"op\":\"fs_read\",\"args\":[\"link\"]}",0,"filesystem_read_failed"));free(request(p,"{\"op\":\"fs_write\",\"args\":[\"link\",\"x\"]}",0,"filesystem_write_denied"));
 require(!link(input,hardlink_path),"hardlink fixture");free(request(p,"{\"op\":\"fs_write\",\"args\":[\"hard\",\"x\"]}",0,"filesystem_write_denied"));free(request(p,"{\"op\":\"fs_read\",\"args\":[\"hard\"]}",0,"filesystem_read_failed"));require(!unlink(hardlink_path),"hardlink cleanup");
 free(request(p,"{\"op\":\"env_get\",\"args\":[\"APX_TEST_HOST_ONLY\"]}",1,"fixture-value"));free(request(p,"{\"op\":\"env_get\",\"args\":[\"HOME\"]}",0,"environment_denied"));
 char *result=request(p,"{\"op\":\"clock_ms\",\"args\":[]}",1,NULL);ApxValue *json=apx_json_parse(result,strlen(result));require(apx_json_integer(apx_json_get(json,"value"))>0,"real clock result");free(result);apx_json_reset();result=request(p,"{\"op\":\"random_hex\",\"args\":[16]}",1,NULL);json=apx_json_parse(result,strlen(result));require(apx_json_length(apx_json_get(json,"value"))==32,"actual entropy encoded size");free(result);apx_json_reset();free(request(p,"{\"op\":\"random_hex\",\"args\":[257]}",0,"random_size_limit"));
 free(request(p,"{\"op\":\"http_get\",\"args\":[\"http://127.0.0.1:1/\"]}",0,"http_get_denied"));free(request(p,"{\"op\":\"spawn\",\"args\":[\"command\"]}",0,"invalid_host_request"));free(request(p,"{\"op\":\"fs_read\",\"args\":[\"input.txt\"],\"extra\":true}",0,"invalid_host_request"));
 apx_host_policy_free(p);save(policy,"{\"schema\":\"apodexis.host_policy.v1\",\"clock\":false,\"max_requests\":1}");p=apx_host_policy_load(policy,&error);require(p&&!error,"deny policy");free(request(p,"{\"op\":\"clock_ms\",\"args\":[]}",0,"clock_denied"));free(request(p,"{\"op\":\"clock_ms\",\"args\":[]}",0,"host_request_limit"));apx_host_policy_free(p);save(policy,"{\"schema\":\"apodexis.host_policy.v1\",\"shell\":true}");p=apx_host_policy_load(policy,&error);require(!p&&error,"policy fails closed on unknown grants");free(error);v2_policy(policy);bounded_http_documents(policy);unlink(input);unlink(output);unlink(symlink_path);unlink(policy);require(!rmdir(dir),"owned fixtures cleaned");free(absolute);apx_json_reset();printf("native host broker: %u checks; actual file read/write, env, clock, entropy and deny/path/symlink/hardlink/limit controls PASS\n",checks);return 0;}
