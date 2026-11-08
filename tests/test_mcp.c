/* libc-only end-to-end MCP protocol regression, no Python test dependency. */
#define _POSIX_C_SOURCE 200809L
#include "../runtime/apx_runtime.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
static void write_file(const char *path,const char *text){FILE *f=fopen(path,"wb");assert(f);assert(fwrite(text,1,strlen(text),f)==strlen(text));assert(!fclose(f));}
static char *read_file(const char *path){FILE *f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>=0&&n<16*1024*1024);rewind(f);char *text=calloc(1,(size_t)n+1);assert(text&&fread(text,1,(size_t)n,f)==(size_t)n);fclose(f);return text;}
static char *transcript(const char *payload,const char *directory){char input[4096],output[4096];snprintf(input,sizeof input,"%s/input",directory);snprintf(output,sizeof output,"%s/output",directory);write_file(input,payload);fflush(NULL);pid_t pid=fork();assert(pid>=0);if(!pid){assert(freopen(input,"rb",stdin));assert(freopen(output,"wb",stdout));const char *binary=getenv("APX_TEST_MCP_BINARY");if(!binary)binary="bin/apx-mcp";execl(binary,binary,"mcp",(char*)NULL);_exit(127);}int status;assert(waitpid(pid,&status,0)==pid&&WIFEXITED(status)&&WEXITSTATUS(status)==0);return read_file(output);}
static ApxValue *reply(const char *all,int id){const char *start=all;while(*start){const char *end=strchr(start,'\n');assert(end);ApxValue *value=apx_json_parse(start,(size_t)(end-start));assert(value);if(apx_json_integer(apx_json_get(value,"id"))==id)return value;start=end+1;}assert(!"missing JSON-RPC response");return NULL;}
static ApxValue *structured(const char *all,int id){ApxValue *value=reply(all,id);assert(!apx_json_get(value,"error"));ApxValue *response=apx_json_get(apx_json_get(value,"result"),"structuredContent");assert(response);return response;}
static void ok(ApxValue *value){const char *s=apx_json_string(apx_json_get(value,"status"));assert(s&&!strcmp(s,"ok"));}
/* Legacy result and request-ID components each fit their original bounds,
 * but their complete outer frame exceeds the driver's 12 MiB result buffer. */
static void legacy_frame_boundary(const char *prefix,const char *directory){
    apx_json_reset();char program[2048];size_t used=(size_t)snprintf(program,sizeof program,"M largeframe\nF main->str\n  L value \"x\"\n");
    for(unsigned i=0;i<22;i++){used+=(size_t)snprintf(program+used,sizeof program-used,"  S value (cat value value)\n");if(i==17)used+=(size_t)snprintf(program+used,sizeof program-used,"  L small value\n");}
    used+=(size_t)snprintf(program+used,sizeof program-used,"  R (cat value (cat small (cat small small)))\n");assert(used<sizeof program);
    char *source=apx_json_encode(apx_string(program,(int64_t)used));assert(source);size_t id_length=3u*1024u*1024u,capacity=id_length+strlen(prefix)+strlen(source)+4096;
    char *payload=calloc(1,capacity);assert(payload);size_t n=(size_t)snprintf(payload,capacity,"%s{\"jsonrpc\":\"2.0\",\"id\":\"",prefix);memset(payload+n,'x',id_length);n+=id_length;
    n+=(size_t)snprintf(payload+n,capacity-n,"\",\"method\":\"tools/call\",\"params\":{\"name\":\"apodexis_run\",\"arguments\":{\"source\":%s,\"input\":null,\"limits\":{\"fuel\":100000,\"max_input_bytes\":null,\"max_output_bytes\":null}}}}\n{\"jsonrpc\":\"2.0\",\"id\":19,\"method\":\"ping\"}\n",source);assert(n<capacity&&n<4u*1024u*1024u);
    char *all=transcript(payload,directory);assert(strlen(all)>12u*1024u*1024u);const char *start=strchr(all,'\n');assert(start);start++;const char *end=strchr(start,'\n');assert(end);
    /* The JSON API accepts 8 MiB per value, not an aggregate MCP frame. Lock
     * the outer bytes and parse its independently bounded components. */
    const char *opening="{\"jsonrpc\":\"2.0\",\"id\":\"",*separator="\",\"result\":{\"structuredContent\":",*middle=",\"content\":[{\"type\":\"text\",\"text\":",*ending="}],\"isError\":false}}";
    assert(!strncmp(start,opening,strlen(opening)));const char *id=start+strlen(opening);assert(strspn(id,"x")==id_length);const char *body=id+id_length;assert(!strncmp(body,separator,strlen(separator)));body+=strlen(separator);
    const char *boundary=strstr(body,middle);assert(boundary&&boundary<end);apx_json_reset();ApxValue *record=apx_json_parse(body,(size_t)(boundary-body));assert(record);ok(record);assert(!strcmp(apx_json_string(apx_json_get(record,"schema")),"apodexis.execution_record.v1"));
    ApxValue *output=apx_json_get(record,"output");const char *text=apx_json_string(output);assert(text&&apx_json_length(output)==4980736&&strspn(text,"x")==4980736);
    const char *content_start=boundary+strlen(middle);assert((size_t)(end-content_start)>strlen(ending)&&!memcmp(end-strlen(ending),ending,strlen(ending)));ApxValue *content=apx_json_parse(content_start,(size_t)(end-strlen(ending)-content_start));const char *decoded=apx_json_string(content);assert(decoded&&apx_json_length(content)==(size_t)(boundary-body)&&!memcmp(decoded,body,(size_t)(boundary-body)));
    apx_json_reset();ApxValue *ping=apx_json_parse(end+1,strlen(end+1));assert(ping&&apx_json_integer(apx_json_get(ping,"id"))==19&&apx_json_get(ping,"result")&&!apx_json_get(ping,"error"));apx_json_reset();free(all);free(payload);free(source);
}
int main(void){char directory[]=".build/mcp-XXXXXX";assert(mkdtemp(directory));const char *hello="M hello\n\nF main->str\n  R \"Hello, world!\"\n\nX hello_output\n  A (= (main) \"Hello, world!\")\n";ApxValue *string=apx_string(hello,(int64_t)strlen(hello));char *source=apx_json_encode(string);assert(source);const char *prefix="{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-11-25\",\"capabilities\":{},\"clientInfo\":{\"name\":\"native-test\",\"version\":\"1\"}}}\n{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n";
    char *payload=calloc(1,100000);assert(payload);size_t n=(size_t)snprintf(payload,100000,"%s{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}\n{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"resources/list\"}\n",prefix);
    const char *names[]={"apodexis_capabilities","apodexis_format","apodexis_check","apodexis_explain","apodexis_pseudocode","apodexis_verify","apodexis_test","apodexis_run"};
    for(size_t i=0;i<8;i++){n+=(size_t)snprintf(payload+n,100000-n,"{\"jsonrpc\":\"2.0\",\"id\":%zu,\"method\":\"tools/call\",\"params\":{\"name\":\"%s\",\"arguments\":{",i+4,names[i]);if(i){n+=(size_t)snprintf(payload+n,100000-n,"\"source\":%s",source);if(i==7)n+=(size_t)snprintf(payload+n,100000-n,",\"input\":null,\"limits\":{\"fuel\":18446744073709551615,\"max_input_bytes\":null,\"max_output_bytes\":null}");}n+=(size_t)snprintf(payload+n,100000-n,"}}}\n");}
    n+=(size_t)snprintf(payload+n,100000-n,"{\"jsonrpc\":\"2.0\",\"id\":12,\"method\":\"tools/call\",\"params\":{\"name\":\"apodexis_run\",\"arguments\":{\"source\":%s,\"input\":null,\"limits\":{\"fuel\":true,\"max_input_bytes\":null,\"max_output_bytes\":null}}}}\n",source);
    n+=(size_t)snprintf(payload+n,100000-n,"{\"jsonrpc\":\"2.0\",\"id\":13,\"method\":\"tools/call\",\"params\":{\"name\":\"apodexis_check\",\"arguments\":{\"source\":\"M holes\\nF main input:i64->i64\\n  R _\\n\"}}}\n");
    n+=(size_t)snprintf(payload+n,100000-n,"{\"jsonrpc\":\"2.0\",\"id\":14,\"method\":\"tools/call\",\"params\":{\"name\":\"apodexis_check\",\"arguments\":{\"source\":\"M repairs\\nF main->i64\\n  R 1\\n\"}}}\n");
    assert(n<100000);char *all=transcript(payload,directory);ApxValue *tools=apx_json_get(apx_json_get(reply(all,2),"result"),"tools");assert(apx_json_length(tools)==13);
    /* MCP outputSchema roots are objects even when the body is an object-only union. */
    for(size_t i=0;i<apx_json_length(tools);i++){const char *type=apx_json_string(apx_json_get(apx_json_get(apx_json_item(tools,i),"outputSchema"),"type"));assert(type&&!strcmp(type,"object"));}
    assert(apx_json_length(apx_json_get(apx_json_get(reply(all,3),"result"),"resources"))==6);for(int id=4;id<=11;id++)ok(structured(all,id));
    snprintf(payload+n,100000-n,"{\"jsonrpc\":\"2.0\",\"id\":20,\"method\":\"resources/read\",\"params\":{\"uri\":\"apodexis://spec/opcodes\"}}\n");
    char *discovery=transcript(payload,directory);ApxValue *contents=apx_json_get(apx_json_get(reply(discovery,20),"result"),"contents");
    const char *catalog_text=apx_json_string(apx_json_get(apx_json_item(contents,0),"text"));assert(catalog_text);
    ApxValue *builtins=apx_json_parse(catalog_text,strlen(catalog_text));assert(builtins);unsigned projection_count=0;
    for(size_t i=0;i<apx_json_length(builtins);i++){ApxValue *item=apx_json_item(builtins,i);const char *name=apx_json_string(apx_json_get(item,"name"));if(name&&!strcmp(name,"json_decode_fields")){assert(apx_json_integer(apx_json_get(item,"opcode"))==57);assert(!strcmp(apx_json_string(apx_json_get(item,"category")),"pure-library"));projection_count++;}}
    assert(projection_count==1);free(discovery);
    ApxValue *pq=apx_json_get(structured(all,4),"post_quantum");assert(apx_json_length(pq)==7);assert(!strcmp(apx_json_string(apx_json_get(pq,"policy_schema")),"apodexis.host_policy.v3"));assert(!strcmp(apx_json_string(apx_json_get(pq,"application_profile")),"cms-authenveloped-mlkem768-aes256gcm-v1"));assert(apx_json_kind(apx_json_get(pq,"application_helper_available"))==1);
    ApxValue *execution=structured(all,11);assert(!strcmp(apx_json_string(apx_json_get(execution,"output")),"Hello, world!"));assert(apx_json_integer(apx_json_get(execution,"fuel_used"))==1);assert(apx_json_string(apx_json_get(execution,"contract_hash")));uint64_t fuel;assert(apx_json_unsigned(apx_json_get(apx_json_get(execution,"limits"),"fuel"),&fuel)&&fuel==UINT64_MAX);assert(apx_json_get(reply(all,12),"error"));
    ApxValue *holes=apx_json_get(structured(all,13),"typed_holes");assert(apx_json_length(holes)==1);ApxValue *hole=apx_json_item(holes,0);assert(!strcmp(apx_json_string(apx_json_get(apx_json_get(hole,"expected_type"),"kind")),"i64"));assert(apx_json_length(apx_json_get(hole,"scope"))==1);
    ApxValue *check=structured(all,14);char *hash=apx_json_encode(apx_json_get(check,"source_text_hash")),*repairs=apx_json_encode(apx_json_get(check,"repairs"));assert(hash&&repairs&&apx_json_length(apx_json_get(check,"repairs"))==1);
    snprintf(payload,100000,"%s{\"jsonrpc\":\"2.0\",\"id\":15,\"method\":\"tools/call\",\"params\":{\"name\":\"apodexis_apply_repairs\",\"arguments\":{\"source\":\"M repairs\\nF main->i64\\n  R 1\\n\",\"request\":{\"schema\":\"apodexis.apply_repairs_request.v1\",\"expected_source_text_hash\":%s,\"repairs\":%s,\"allow_suggestive\":false}}}}\n{\"jsonrpc\":\"2.0\",\"id\":16,\"method\":\"tools/call\",\"params\":{\"name\":\"apodexis_apply_repairs\",\"arguments\":{\"source\":\"M repairs\\nF main->i64\\n  R 1\\n\",\"request\":{\"schema\":\"apodexis.apply_repairs_request.v1\",\"expected_source_text_hash\":\"apxs1:stale\",\"repairs\":%s,\"allow_suggestive\":false}}}}\n",prefix,hash,repairs,repairs);
    char *applied=transcript(payload,directory);ok(structured(applied,15));const char *s=apx_json_string(apx_json_get(structured(applied,16),"status"));assert(s&&!strcmp(s,"error"));
    snprintf(payload,100000,"%s{\"jsonrpc\":\"2.0\",\"id\":17,\"method\":\"tools/call\",\"params\":{\"name\":\"apodexis_check\",\"arguments\":{\"source\":\"M errors\\nF main->i64\\n  L one absent_one\\n  L two absent_two\\n  R 0\\n\"}}}\n{\"jsonrpc\":\"2.0\",\"id\":18,\"method\":\"tools/call\",\"params\":{\"name\":\"apodexis_check\",\"arguments\":{\"source\":\"M holes\\nF main input:i64->i64\\n  L first:i64 _\\n  L second:i64 _\\n  R input\\n\"}}}\n",prefix);
    char *multiple=transcript(payload,directory);
    ApxValue *error_result=structured(multiple,17);assert(apx_json_length(apx_json_get(error_result,"diagnostics"))==2);
    assert(!strcmp(apx_json_string(apx_json_get(error_result,"schema")),"apodexis.repair_response.v1"));
    ApxValue *hole_result=structured(multiple,18);assert(apx_json_length(apx_json_get(hole_result,"diagnostics"))==2);
    assert(apx_json_length(apx_json_get(hole_result,"typed_holes"))==2);free(multiple);
    legacy_frame_boundary(prefix,directory);
    free(all);free(applied);free(payload);free(source);free(hash);free(repairs);apx_json_reset();char input[4096],output[4096];snprintf(input,sizeof input,"%s/input",directory);snprintf(output,sizeof output,"%s/output",directory);unlink(input);unlink(output);assert(!rmdir(directory));puts("native stdio MCP: 13 tools, 6 resources, QBE execution/verify, contract, pseudocode, full u64 fuel, typed-hole context, guarded repair, stale/invalid rejection PASS");return 0;
}
