/* Public real-process stdio MCP tests. No compiler internals or fake broker.
 * Cancellation uses an accepted, deliberately held loopback HTTP connection as
 * the started signal, not a fast-program race or a sleep-based assertion. */
#define _GNU_SOURCE
#include "../runtime/apx_runtime.h"
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
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#define FRAME (4u*1048576u)
static unsigned checks;
static pid_t server;
static FILE *input,*output,*transcript;
static char directory[4096];
static int listener=-1,peer=-1;
static int64_t now(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (int64_t)t.tv_sec*1000+t.tv_nsec/1000000;}
static void cleanup(void){
    if(input){fclose(input);input=NULL;}if(server){kill(server,SIGTERM);int fd=(int)syscall(SYS_pidfd_open,server,0);struct pollfd p={fd,POLLIN,0};if(fd<0||poll(&p,1,1000)<=0)kill(server,SIGKILL);if(fd>=0)close(fd);while(waitpid(server,NULL,0)<0&&errno==EINTR){}server=0;}if(output){fclose(output);output=NULL;}
    if(peer>=0){close(peer);peer=-1;}if(listener>=0){close(listener);listener=-1;}
    if(*directory){DIR *d=opendir(directory);struct dirent *e;while(d&&(e=readdir(d))){if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;char p[8192];snprintf(p,sizeof p,"%s/%s",directory,e->d_name);unlink(p);}if(d)closedir(d);rmdir(directory);}
}
static void require(int ok,const char *why){checks++;if(!ok){fprintf(stderr,"serving FAIL: %s (check %u)\n",why,checks);cleanup();exit(1);}}
static void save(const char *name,const char *text){char p[8192];snprintf(p,sizeof p,"%s/%s",directory,name);FILE *f=fopen(p,"w");require(f&&fputs(text,f)>=0&&!fclose(f),"write owned fixture");}
static char *read_fixture(const char *name){char p[8192];snprintf(p,sizeof p,"%s/%s",directory,name);FILE *f=fopen(p,"r");require(f!=NULL,"read actual persisted effect");char *b=calloc(1,8192);require(b!=NULL,"read allocation");size_t n=fread(b,1,8191,f);require(!ferror(f)&&feof(f),"bounded persisted effect");b[n]=0;fclose(f);return b;}
static const char *binary(void){const char *b=getenv("APX_TEST_SERVE_BINARY");return b?b:"bin/apodexis";}
static void send_frame(const char *s){if(transcript){if(strlen(s)<65536)fprintf(transcript,"request %s\n",s);else fprintf(transcript,"request bytes=%zu [bounded frame probe]\n",strlen(s));fflush(transcript);}require(fprintf(input,"%s\n",s)>0&&!fflush(input),"write MCP frame");}
static ApxValue *receive_within(int milliseconds){
    struct pollfd p={fileno(output),POLLIN,0};require(poll(&p,1,milliseconds)>0,"bounded MCP response readiness");char *s=NULL;size_t n=0;ssize_t got=getline(&s,&n,output);
    require(got>0,"serve mode must initialize over real stdio (unsupported mode on base)");require((size_t)got<=FRAME,"bounded response frame");
    if(transcript){fprintf(transcript,"response %s",s);fflush(transcript);}apx_json_reset();ApxValue *v=apx_json_parse(s,(size_t)got);free(s);require(v!=NULL,"JSON-only MCP stdout");return v;
}
static ApxValue *receive(void){return receive_within(10000);}
static void launch(void){
    int a[2],b[2];require(!pipe2(a,O_CLOEXEC)&&!pipe2(b,O_CLOEXEC),"server pipes");server=fork();require(server>=0,"server fork");
    if(!server){dup2(a[0],0);dup2(b[1],1);close_range(3,~0u,0);char p[8192];snprintf(p,sizeof p,"%s/manifest.json",directory);execl(binary(),binary(),"serve","--manifest",p,(char*)NULL);_exit(127);}
    close(a[0]);close(b[1]);input=fdopen(a[1],"w");output=fdopen(b[0],"r");require(input&&output,"stdio streams");setvbuf(output,NULL,_IONBF,0);
}
static void start(void){
    launch();send_frame("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}");ApxValue *r=receive();ApxValue *result=apx_json_get(r,"result");require(result!=NULL,"initialize serving MCP");
    if(!getenv("APX_SERVE_WORK_ONLY")){
        ApxValue *limits=apx_json_get(apx_json_get(apx_json_get(result,"capabilities"),"experimental"),"apodexisJsonLimits");
        const char *names[]={"maxRequestBytes","maxWorkerReplyBytes","maxObjectMembers","maxValues","maxDepth","maxObjectWork","maxValidationWork"};
        const int64_t values[]={4194304,2097152,256,16384,64,1048576,1048576};
        for(size_t i=0;i<sizeof values/sizeof values[0];i++)require(apx_json_integer(apx_json_get(limits,names[i]))==values[i],"published serving operational bounds");
    }
    send_frame("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}");
}
static void stop(int expected){if(input){fclose(input);input=NULL;}int status;require(waitpid(server,&status,0)==server&&WIFEXITED(status)&&WEXITSTATUS(status)==expected,"bounded controlled EOF/signal exit");server=0;fclose(output);output=NULL;}
static void request_call(const char *id,const char *name,const char *args){char frame[16384];int n=snprintf(frame,sizeof frame,"{\"jsonrpc\":\"2.0\",\"id\":%s,\"method\":\"tools/call\",\"params\":{\"name\":\"%s\",\"arguments\":%s}}",id,name,args);require(n>0&&(size_t)n<sizeof frame,"bounded test call");send_frame(frame);}
static ApxValue *call_tool(const char *name,const char *args){request_call("3",name,args);return receive();}
static ApxValue *call(const char *args){return call_tool("increment",args);}
static void invalid(ApxValue *r){require(apx_json_integer(apx_json_get(apx_json_get(r,"error"),"code"))==-32602,"strict caller rejection");}
static void error_code(ApxValue *r,int expected){require(apx_json_integer(apx_json_get(apx_json_get(r,"error"),"code"))==expected,"expected protocol error");}
static ApxValue *content(ApxValue *r){
    require(!apx_json_get(r,"error"),"tool error is not a protocol failure");ApxValue *result=apx_json_get(r,"result"),*c=apx_json_get(result,"structuredContent");require(c!=NULL,"structured result");
    const char *text=apx_json_string(apx_json_get(apx_json_item(apx_json_get(result,"content"),0),"text"));char *a=apx_json_encode(c),*b=text?apx_json_encode(apx_json_parse(text,strlen(text))):NULL;
    require(a&&b&&!strcmp(a,b),"parsed compatibility text equals structured content exactly");const char *status=apx_json_string(apx_json_get(c,"status"));require(status&&apx_json_kind(apx_json_get(result,"isError"))==1&&apx_json_integer(apx_json_get(result,"isError"))==(strcmp(status,"ok")!=0),"isError agrees with structured status");free(a);free(b);return c;
}
static void output_is(ApxValue *r,const char *expected){ApxValue *c=content(r);const char *status=apx_json_string(apx_json_get(c,"status"));char *got=apx_json_encode(apx_json_get(c,"output")),*want=apx_json_encode(apx_json_parse(expected,strlen(expected)));if(!got||!want||strcmp(got,want))fprintf(stderr,"serving output got=%s expected=%s\n",got?got:"NULL",expected);require(status&&!strcmp(status,"ok")&&got&&want&&!strcmp(got,want),"typed exact successful output");free(got);free(want);}
static void tool_error(ApxValue *r,const char *expected){ApxValue *c=content(r);const char *code=apx_json_string(apx_json_get(apx_json_get(c,"error"),"code"));require(code&&!strcmp(code,expected),"expected bounded non-leaking tool error");}
static void ping(unsigned id){char frame[256];snprintf(frame,sizeof frame,"{\"jsonrpc\":\"2.0\",\"id\":%u,\"method\":\"ping\",\"params\":{}}",id);send_frame(frame);ApxValue *r=receive();require(apx_json_integer(apx_json_get(r,"id"))==id&&apx_json_get(r,"result"),"ping processed with no cancelled late response");}
static const char increment_source[]="M increment\nF main value:i64->i64\n  R (+ value 1)\n";
static void simple_manifest(void){save("tool.apx",increment_source);save("manifest.json","{\"schema\":\"apodexis.tool_server.v1\",\"max_concurrent_calls\":1,\"tools\":[{\"name\":\"increment\",\"description\":\"Increment a checked integer\",\"source\":\"tool.apx\",\"limits\":{\"fuel\":1000,\"max_input_bytes\":1024,\"max_output_bytes\":1024,\"timeout_ms\":1000}}]}");}
static void first_vertical(void){
    simple_manifest();start();send_frame("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\",\"params\":{}}");ApxValue *r=receive(),*tools=apx_json_get(apx_json_get(r,"result"),"tools");require(apx_json_length(tools)==1,"only operator-selected tools");ApxValue *t=apx_json_item(tools,0);require(!strcmp(apx_json_string(apx_json_get(t,"name")),"increment"),"configured tool name");
    ApxValue *schema=apx_json_get(t,"inputSchema"),*i=apx_json_get(apx_json_get(schema,"properties"),"input");require(!strcmp(apx_json_string(apx_json_get(schema,"type")),"object")&&!strcmp(apx_json_string(apx_json_get(i,"type")),"integer"),"compiler i64 projected into closed MCP object schema");
    output_is(call("{\"input\":4}"),"5");invalid(call("{\"input\":4,\"source\":\"M bad\"}"));invalid(call("{\"input\":4,\"limits\":{}}"));invalid(call("{\"input\":true}"));invalid(call("{}"));
    const char *bad[]={"{\"input\":4,\"capabilities\":{}}","{\"input\":4,\"credentials\":[]}","{\"input\":4,\"manifest\":\"x\"}","{\"input\":4,\"policy\":\"x\"}","{\"input\":4,\"root\":\"/\"}","{\"input\":4,\"fuel\":9}","{\"input\":9223372036854775808}","{\"input\":4.0}"};for(size_t n=0;n<sizeof bad/sizeof bad[0];n++)invalid(call(bad[n]));
    /* Repeated members are a parse error; an unknown key is invalid args. */
    error_code(call("{\"input\":4,\"input\":5}"),-32700);invalid(call("{\"input\\u0000\":4}"));
    /* The real prefix must not hide a distinct NUL-suffixed member. */
    invalid(call("{\"input\":4,\"input\\u0000extra\":\"not an i64\"}"));
    invalid(call("{\"input\\u0000extra\":\"not an i64\",\"input\":4}"));
    invalid(call_tool("apodexis_compile","{\"input\":null}"));invalid(call_tool("not-configured","{\"input\":null}"));
    save("tool.apx","M changed\nF main value:i64->i64\n  R (+ value 99)\n");save("manifest.json","not a manifest anymore");output_is(call("{\"input\":4}"),"5");output_is(call("{\"input\":9}"),"10");output_is(call("{\"input\":-9223372036854775808}"),"-9223372036854775807");tool_error(call("{\"input\":9223372036854775807}"),"runtime_failed");output_is(call("{\"input\":4}"),"5");stop(0);
}
static void manifest_tools(unsigned concurrent,const char *tools){char *m=calloc(1,strlen(tools)+256);require(m!=NULL,"manifest storage");sprintf(m,"{\"schema\":\"apodexis.tool_server.v1\",\"max_concurrent_calls\":%u,\"tools\":[%s]}",concurrent,tools);save("manifest.json",m);free(m);}
static void tool(char out[2048],const char *name,const char *file,unsigned fuel,unsigned in,unsigned bytes,unsigned timeout,const char *policy){int n=snprintf(out,2048,"{\"name\":\"%s\",\"description\":\"Owned native test tool\",\"source\":\"%s\",\"limits\":{\"fuel\":%u,\"max_input_bytes\":%u,\"max_output_bytes\":%u,\"timeout_ms\":%u}%s}",name,file,fuel,in,bytes,timeout,policy?",\"capabilities\":\"policy.json\"":"");require(n>0&&n<2048,"tool fixture bound");}
static void shapes(void){
    /* Option input is supported inside map payloads, not ordinary fields. */
    save("shape.apx","M shapes\nT Payload struct\n  label:str\n  values:[i64]\n  pick:map option bool\n  scores:map f64\nF main value:Payload->result Payload str\n  R (ok value)\n");
    save("unit.apx","M unit\nF main->str\n  R \"ready\"\n");save("trap.apx","M trap\nF main->i64\n  Z \"private operator trap detail\"\n");save("fuel.apx","M fuel\nF main->i64\n  L n 0\n  W true\n    S n (+ n 1)\n  R n\n");
    char a[2048],b[2048],c[2048],d[2048],e[2048],f[2048],all[16384];tool(a,"shape","shape.apx",1000,1024,1024,1000,NULL);tool(b,"unit","unit.apx",1000,4,7,1000,NULL);tool(c,"trap","trap.apx",1000,4,1024,1000,NULL);tool(d,"fuel","fuel.apx",10,4,1024,1000,NULL);tool(e,"noinput","unit.apx",1000,0,1024,1000,NULL);tool(f,"nooutput","unit.apx",1000,4,0,1000,NULL);snprintf(all,sizeof all,"%s,%s,%s,%s,%s,%s",a,b,c,d,e,f);manifest_tools(1,all);start();
    const char *args="{\"input\":{\"label\":\"actual\",\"values\":[1,-2],\"pick\":{\"first\":{\"tag\":\"some\",\"value\":true}},\"scores\":{\"x\":1.5}}}";
    output_is(call_tool("shape",args),"{\"tag\":\"ok\",\"value\":{\"label\":\"actual\",\"values\":[1,-2],\"pick\":{\"first\":{\"tag\":\"some\",\"value\":true}},\"scores\":{\"x\":1.5}}}");
    invalid(call_tool("shape","{\"input\":{\"label\":\"actual\",\"values\":[true],\"pick\":{},\"scores\":{}}}"));
    invalid(call_tool("shape","{\"input\":{\"label\":\"x\",\"values\":[],\"pick\":{\"first\":{\"tag\":\"none\",\"value\":false}},\"scores\":{}}}"));
    invalid(call_tool("shape","{\"input\":{\"label\":\"x\",\"values\":[],\"pick\":{},\"scores\":{},\"extra\":0}}"));
    /* Closed records, map names and tagged payloads reject NUL aliases;
     * string values, Unicode names, empty names and literal backslashes survive. */
    invalid(call_tool("shape","{\"input\":{\"label\":\"x\",\"label\\u0000extra\":999,\"values\":[],\"pick\":{},\"scores\":{}}}"));
    invalid(call_tool("shape","{\"input\":{\"label\":\"x\",\"values\":[],\"pick\":{\"first\":{\"tag\":\"none\"},\"first\\u0000extra\":\"bad\"},\"scores\":{}}}"));
    invalid(call_tool("shape","{\"input\":{\"label\":\"x\",\"values\":[],\"pick\":{\"first\":{\"tag\":\"none\",\"tag\\u0000extra\":\"some\"}},\"scores\":{}}}"));
    output_is(call_tool("shape","{\"input\":{\"label\":\"actual\\u0000data\",\"values\":[],\"pick\":{\"café\":{\"tag\":\"none\"},\"literal\\\\u0000\":{\"tag\":\"some\",\"value\":false}},\"scores\":{\"\":2.5}}}"),"{\"tag\":\"ok\",\"value\":{\"label\":\"actual\\u0000data\",\"values\":[],\"pick\":{\"café\":{\"tag\":\"none\"},\"literal\\\\u0000\":{\"tag\":\"some\",\"value\":false}},\"scores\":{\"\":2.5}}}");
    output_is(call_tool("unit","{\"input\":null}"),"\"ready\"");invalid(call_tool("unit","{\"input\":{}}"));tool_error(call_tool("trap","{\"input\":null}"),"runtime_failed");output_is(call_tool("unit","{\"input\":null}"),"\"ready\"");tool_error(call_tool("fuel","{\"input\":null}"),"runtime_failed");tool_error(call_tool("noinput","{\"input\":null}"),"input_limit");tool_error(call_tool("nooutput","{\"input\":null}"),"runtime_failed");output_is(call_tool("unit","{\"input\":null}"),"\"ready\"");stop(0);
}
static unsigned open_listener(void){listener=socket(AF_INET,SOCK_STREAM|SOCK_CLOEXEC,0);require(listener>=0,"loopback listener");struct sockaddr_in a={0};a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);require(!bind(listener,(struct sockaddr*)&a,sizeof a)&&!listen(listener,8),"owned dynamic loopback port");socklen_t n=sizeof a;require(!getsockname(listener,(struct sockaddr*)&a,&n),"listener address");return ntohs(a.sin_port);}
static void started(void){struct pollfd p={listener,POLLIN,0};require(poll(&p,1,5000)>0,"real allowed HTTP request started");peer=accept4(listener,NULL,NULL,SOCK_CLOEXEC);require(peer>=0,"accept actual native HTTP helper");char b[8192]={0};size_t used=0;while(!strstr(b,"\r\n\r\n")){p=(struct pollfd){peer,POLLIN,0};require(poll(&p,1,5000)>0,"started HTTP headers readiness");ssize_t n=read(peer,b+used,sizeof b-1-used);require(n>0,"actual started HTTP headers");used+=(size_t)n;require(used<sizeof b-1,"bounded HTTP fixture request");}require(!strncmp(b,"GET /hold HTTP/1.1",18),"operator-authorized exact HTTP request observed");}
static void closed_peer(void){struct pollfd p={peer,POLLIN,0};require(poll(&p,1,5000)>0,"cancellation/deadline closed actual HTTP channel");char c;require(read(peer,&c,1)==0,"owned helper socket fully closed");close(peer);peer=-1;}
static void respond(void){const char *response="HTTP/1.1 200 OK\r\nContent-Length: 4\r\nConnection: close\r\n\r\nreal";require(write(peer,response,strlen(response))==(ssize_t)strlen(response),"actual HTTP response");close(peer);peer=-1;}
static void async_config(unsigned timeout,unsigned concurrent){unsigned port=open_listener();char policy[4096];snprintf(policy,sizeof policy,"{\"schema\":\"apodexis.host_policy.v1\",\"http_get\":[\"http://127.0.0.1:%u/hold\"],\"max_requests\":1}",port);save("policy.json",policy);char source[1024];snprintf(source,sizeof source,"M hold\nF main->result str str\n  R (http_get \"http://127.0.0.1:%u/hold\")\n",port);save("hold.apx",source);save("tool.apx",increment_source);char a[2048],b[2048],all[8192];tool(a,"hold","hold.apx",1000,4,1024,timeout,"yes");tool(b,"increment","tool.apx",1000,1024,1024,1000,NULL);snprintf(all,sizeof all,"%s,%s",a,b);manifest_tools(concurrent,all);}
static pid_t broker_pid(void){char p[256];snprintf(p,sizeof p,"/proc/%ld/task/%ld/children",(long)server,(long)server);FILE *f=fopen(p,"r");long pid=0;require(f&&fscanf(f,"%ld",&pid)==1&&pid>0,"real owning invocation broker observed");fclose(f);return (pid_t)pid;}
static void recovered(void){int64_t end=now()+5000;for(;;){ApxValue *r=call("{\"input\":4}");ApxValue *c=content(r);const char *status=apx_json_string(apx_json_get(c,"status"));if(status&&!strcmp(status,"ok")){require(apx_json_integer(apx_json_get(c,"output"))==5,"fresh call after reap recovers");break;}const char *code=apx_json_string(apx_json_get(apx_json_get(c,"error"),"code"));require(code&&!strcmp(code,"busy")&&now()<end,"cancelled slot retained only until actual reap");}}
static void async_lifecycle(void){
    async_config(10000,1);start();request_call("40","hold","{\"input\":null}");started();pid_t broker=broker_pid();
    tool_error(call("{\"input\":4}"),"busy");send_frame("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/cancelled\",\"params\":{\"requestId\":\"40\"}}");ping(90);struct pollfd p={peer,POLLIN,0};require(poll(&p,1,0)==0,"string ID cancellation cannot cancel numeric ID");
    send_frame("{\"jsonrpc\":\"2.0\",\"id\":40,\"method\":\"ping\"}");error_code(receive(),-32600);
    send_frame("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/cancelled\",\"params\":{\"requestId\":40}}\n{\"jsonrpc\":\"2.0\",\"id\":40,\"method\":\"ping\"}\n{\"jsonrpc\":\"2.0\",\"id\":91,\"method\":\"ping\"}");error_code(receive(),-32600);ApxValue *r=receive();require(apx_json_integer(apx_json_get(r,"id"))==91,"cancelled response suppressed and live ping processed");closed_peer();recovered();errno=0;require(kill(-broker,0)<0&&errno==ESRCH,"entire cancelled native/broker/helper group reaped");ping(92);
    request_call("41","hold","{\"input\":null}");started();respond();output_is(receive(),"{\"tag\":\"ok\",\"value\":\"real\"}");request_call("42","hold","{\"input\":null}");started();respond();output_is(receive(),"{\"tag\":\"ok\",\"value\":\"real\"}"); /* max_requests=1 resets each call */
    request_call("43","hold","{\"input\":null}");started();broker=broker_pid();require(!kill(broker,SIGKILL),"crash actual invocation broker");closed_peer();tool_error(receive(),"worker_failed");recovered();errno=0;require(kill(-broker,0)<0&&errno==ESRCH,"crashed broker cannot leave native/helper descendants");stop(0);close(listener);listener=-1;
    async_config(1200,2);start();request_call("50","hold","{\"input\":null}");started();output_is(call("{\"input\":4}"),"5");ping(93);tool_error(receive(),"deadline");closed_peer();recovered();stop(0);close(listener);listener=-1;
    for(int mode=0;mode<3;mode++){async_config(10000,1);start();request_call("60","hold","{\"input\":null}");started();broker=broker_pid();if(mode==1)require(!kill(server,SIGTERM),"SIGTERM while actual native work active");if(mode==2){fclose(output);output=NULL;send_frame("{\"jsonrpc\":\"2.0\",\"id\":99,\"method\":\"ping\"}");}if(mode!=2)stop(mode==1?143:0);else{fclose(input);input=NULL;int status;require(waitpid(server,&status,0)==server&&WIFEXITED(status)&&WEXITSTATUS(status)==2,"client write failure cleans owned work");server=0;}closed_peer();errno=0;require(kill(-broker,0)<0&&errno==ESRCH,"shutdown leaves no owned descendants");close(listener);listener=-1;}
}
static void fixed_authority(void){
    save("data.txt","real file bytes");char policy[16384];snprintf(policy,sizeof policy,"{\"schema\":\"apodexis.host_policy.v1\",\"read_root\":\"%s\",\"write_root\":\"%s\",\"max_requests\":1}",directory,directory);save("policy.json",policy);
    save("read.apx","M read\nF main path:str->result str str\n  R (fs_read path)\n");save("write.apx","M write\nF main path:str->result i64 str\n  R (fs_write path \"exact report bytes\\n\")\n");
    char a[2048],b[2048],c[2048],all[8192];tool(a,"read","read.apx",1000,1024,1024,1000,"yes");tool(b,"denied","read.apx",1000,1024,1024,1000,NULL);tool(c,"write","write.apx",1000,1024,1024,1000,"yes");snprintf(all,sizeof all,"%s,%s,%s",a,b,c);manifest_tools(1,all);start();save("policy.json","{}");
    for(int n=0;n<2;n++){output_is(call_tool("read","{\"input\":\"data.txt\"}"),"{\"tag\":\"ok\",\"value\":\"real file bytes\"}");}
    output_is(call_tool("denied","{\"input\":\"data.txt\"}"),"{\"tag\":\"err\",\"error\":\"capability_denied\"}");output_is(call_tool("read","{\"input\":\"../outside\"}"),"{\"tag\":\"err\",\"error\":\"filesystem_read_denied\"}");
    char link[8192];snprintf(link,sizeof link,"%s/link",directory);require(!symlink("data.txt",link),"owned symlink denial fixture");output_is(call_tool("read","{\"input\":\"link\"}"),"{\"tag\":\"err\",\"error\":\"filesystem_read_failed\"}");output_is(call_tool("write","{\"input\":\"report.txt\"}"),"{\"tag\":\"ok\",\"value\":19}");char *data=read_fixture("report.txt");require(!strcmp(data,"exact report bytes\n"),"exact allowed filesystem write readback");free(data);output_is(call_tool("write","{\"input\":\"../outside\"}"),"{\"tag\":\"err\",\"error\":\"filesystem_write_denied\"}");stop(0);
}
static void startup_refused(const char *manifest){
    save("manifest.json",manifest);launch();fclose(input);input=NULL;
    int pidfd=(int)syscall(SYS_pidfd_open,server,0);require(pidfd>=0,"startup process identity");
    struct pollfd watched={pidfd,POLLIN,0};int ready;do{ready=poll(&watched,1,5000);}while(ready<0&&errno==EINTR);
    close(pidfd);if(ready<=0)kill(server,SIGKILL);
    int status;pid_t waited=waitpid(server,&status,0);server=0;
    require(ready>0,"bounded schema/configuration rejection finishes before startup deadline");
    require(waited>0&&WIFEXITED(status)&&WEXITSTATUS(status)==64,"invalid operator configuration fails closed before protocol");
    require(fgetc(output)==EOF,"startup failure emits no MCP/stdout content");fclose(output);output=NULL;
}
static void strict_startup(void){
    const char *bad[]={"{}","{\"schema\":\"apodexis.tool_server.v1\",\"max_concurrent_calls\":0,\"tools\":[]}","{\"schema\":\"apodexis.tool_server.v1\",\"max_concurrent_calls\":5,\"tools\":[]}","{\"schema\":\"apodexis.tool_server.v1\",\"max_concurrent_calls\":1,\"tools\":[],\"unknown\":0}","{\"schema\":\"apodexis.tool_server.v1\",\"schema\":\"apodexis.tool_server.v1\",\"max_concurrent_calls\":1,\"tools\":[]}"};for(size_t i=0;i<sizeof bad/sizeof bad[0];i++)startup_refused(bad[i]);
    char a[2048],all[8192];tool(a,"same","tool.apx",1000,1024,1024,1000,NULL);
    const char *aliases[]={"name","source","description"};
    for(size_t n=0;n<sizeof aliases/sizeof aliases[0];n++){
        char bad_tool[4096];snprintf(bad_tool,sizeof bad_tool,"%.*s,\"%s\\u0000extra\":0}",(int)strlen(a)-1,a,aliases[n]);
        manifest_tools(1,bad_tool);char *bad_manifest=read_fixture("manifest.json");startup_refused(bad_manifest);free(bad_manifest);
    }
snprintf(all,sizeof all,"%s,%s",a,a);manifest_tools(1,all);char *m=read_fixture("manifest.json");startup_refused(m);free(m);
    save("enum.apx","M enum\nT Choice enum\n  A\n  B\nF main choice:Choice->Choice\n  R choice\n");tool(a,"enum","enum.apx",1000,1024,1024,1000,NULL);manifest_tools(1,a);m=read_fixture("manifest.json");startup_refused(m);free(m);
    /* A compact checked return-type DAG can describe an exponentially large
     * schema even though the actual returned array is empty. Refuse as soon as
     * the schema budget is reached, not after traversing the full expansion. */
    char expansion[16000];size_t used=(size_t)snprintf(expansion,sizeof expansion,"M expansion\nT Node0 struct\n  value:i64\n");
    for(unsigned i=1;i<=42;i++)used+=(size_t)snprintf(expansion+used,sizeof expansion-used,"T Node%u struct\n  left:Node%u\n  right:Node%u\n",i,i-1,i-1);
    require(used<sizeof expansion-100,"bounded expansion source");snprintf(expansion+used,sizeof expansion-used,"F main->[Node42]\n  R []\n");save("expansion.apx",expansion);
    tool(a,"expansion","expansion.apx",1000,1024,1024,1000,NULL);manifest_tools(1,a);m=read_fixture("manifest.json");startup_refused(m);free(m);
    tool(a,"missing","missing.apx",1000,1024,1024,1000,NULL);manifest_tools(1,a);m=read_fixture("manifest.json");startup_refused(m);free(m);
}
/* QLT-001: a byte-small but quadratic object must not own the control plane.
 * Every hostile case starts a real HTTP worker, then proves cancellation,
 * recovery, no late reply, and a second started worker's SIGTERM/group reap. */
static char *object_json(unsigned members){
    char *s=calloc(1,(size_t)members*32+3);require(s!=NULL,"object probe allocation");size_t n=0;s[n++]='{';
    for(unsigned i=0;i<members;i++)n+=(size_t)sprintf(s+n,"%s\"k%06u\":0",i?",":"",i);
    s[n++]='}';s[n]=0;return s;
}
static char *rows_json(unsigned rows,unsigned members){
    char *object=object_json(members);size_t len=strlen(object);char *s=calloc(1,(len+1)*rows+3);require(s!=NULL,"nested work probe allocation");size_t n=0;s[n++]='[';
    for(unsigned i=0;i<rows;i++){if(i)s[n++]=',';memcpy(s+n,object,len);n+=len;}s[n++]=']';s[n]=0;free(object);return s;
}
static void large_call(const char *id,const char *name,const char *value){
    char *s=calloc(1,strlen(value)+512);require(s!=NULL,"large typed call storage");sprintf(s,"{\"jsonrpc\":\"2.0\",\"method\":\"tools/call\",\"params\":{\"name\":\"%s\",\"arguments\":{\"input\":%s}},\"id\":%s}",name,value,id);send_frame(s);free(s);
}
static void prompt_shutdown(pid_t broker){
    int fd=(int)syscall(SYS_pidfd_open,server,0);require(fd>=0,"shutdown process identity");require(!kill(server,SIGTERM),"SIGTERM after hostile request");struct pollfd p={fd,POLLIN,0};int ready=poll(&p,1,1500);close(fd);require(ready>0,"SIGTERM exits promptly after hostile JSON");stop(143);closed_peer();errno=0;require(kill(-broker,0)<0&&errno==ESRCH,"hostile JSON shutdown reaps whole owned group");
}
static void json_work_lifecycle(void){
    for(unsigned mode=0;mode<6;mode++){
        async_config(10000,1);start();request_call("20","hold","{\"input\":null}");started();pid_t broker=broker_pid();int64_t begin=now();
        char *object=object_json(mode<2?70000:257),*frame=NULL;
        if(mode==0){require(strlen(object)==840001,"exact rejected 70000-member 840001-byte reproduction");send_frame(object);}
        else if(mode==1){frame=calloc(1,strlen(object)+128);require(frame!=NULL,"late ID storage");sprintf(frame,"{\"params\":%s,\"id\":71,\"jsonrpc\":\"2.0\",\"method\":\"ping\"}",object);send_frame(frame);}
        else if(mode==2)large_call("71","increment",object);
        else if(mode==3){char *rows=rows_json(16,256);large_call("71","increment",rows);free(rows);}
        else if(mode==4){char deep[512];size_t n=0;for(unsigned i=0;i<65;i++)deep[n++]='[';deep[n++]='0';for(unsigned i=0;i<65;i++)deep[n++]=']';deep[n]=0;large_call("71","increment",deep);}
        else{frame=calloc(1,strlen(object)+128);require(frame!=NULL,"escaped late ID storage");sprintf(frame,"{\"params\":%s,\"\\u0069d\":\"bounded\\u0020ID\\\\u0000\",\"jsonrpc\":\"2.0\",\"method\":\"ping\"}",object);send_frame(frame);}
        free(frame);free(object);
        send_frame("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/cancelled\",\"params\":{\"requestId\":20}}\n{\"jsonrpc\":\"2.0\",\"id\":99,\"method\":\"ping\"}");
        ApxValue *r=receive_within(1500);error_code(r,-32000);require(mode==5?(apx_json_string(apx_json_get(r,"id"))&&!strcmp(apx_json_string(apx_json_get(r,"id")),"bounded ID\\u0000")):mode?apx_json_integer(apx_json_get(r,"id"))==71:apx_json_kind(apx_json_get(r,"id"))==0,"bounded ID recovered even after hostile input");
        r=receive_within(1500);require(apx_json_integer(apx_json_get(r,"id"))==99&&apx_json_get(r,"result"),"ping survives hostile work without cancellation reply");
        struct pollfd p={peer,POLLIN,0};require(poll(&p,1,1500)>0,"hostile work cannot defer started cancellation");closed_peer();recovered();errno=0;require(kill(-broker,0)<0&&errno==ESRCH,"hostile cancellation group reaped");ping(100);
        printf("json-work started case %u: rejection/cancel/ping/recovery %lld ms\n",mode,(long long)(now()-begin));
        request_call("21","hold","{\"input\":null}");started();broker=broker_pid();prompt_shutdown(broker);close(listener);listener=-1;
    }
}
static void record_fixture(const char *file,int output_record){
    char s[24000];size_t n=(size_t)sprintf(s,"M records\nT Wide struct\n");
    for(unsigned i=0;i<256;i++)n+=(size_t)sprintf(s+n,"  k%06u:i64\n",i);
    if(!output_record)sprintf(s+n,"F main x:[Wide]->[Wide]\n  R x\n");
    else{n+=(size_t)sprintf(s+n,"F main n:i64->[Wide]\n  L row (Wide");for(unsigned i=0;i<256;i++)n+=(size_t)sprintf(s+n," k%06u=0",i);sprintf(s+n,")\n  L b (buf Wide n)\n  L i 0\n  W (< i n)\n    P b row\n    S i (+ i 1)\n  R (finish b)\n");}
    save(file,s);
}
static void json_work_values(void){
    save("maps.apx","M maps\nF main x:[map i64]->[map i64]\n  R x\n");
    save("strings.apx","M strings\nF main x:[str]->[str]\n  R x\n");
    save("out.apx","M out\nF main n:i64->map i64\n  L m (map i64)\n  L i 0\n  W (< i n)\n    S m (map_put m (str_i64 i) i)\n    S i (+ i 1)\n  R m\n");
    save("outrows.apx","M outrows\nF main n:i64->[map i64]\n  L m (map i64)\n  L i 0\n  W (< i 256)\n    S m (map_put m (str_i64 i) i)\n    S i (+ i 1)\n  L b (buf map i64 n)\n  S i 0\n  W (< i n)\n    P b m\n    S i (+ i 1)\n  R (finish b)\n");
    save("outvalues.apx","M outvalues\nF main n:i64->[i64]\n  L b (buf i64 n)\n  L i 0\n  W (< i n)\n    P b i\n    S i (+ i 1)\n  R (finish b)\n");
    record_fixture("records.apx",0);record_fixture("outrecords.apx",1);
    char a[2048],b[2048],c[2048],d[2048],e[2048],f[2048],g[2048],all[16000];tool(a,"maps","maps.apx",1000000,1048576,1048576,5000,NULL);tool(b,"strings","strings.apx",1000000,1048576,1048576,5000,NULL);tool(c,"out","out.apx",1000000,1024,1048576,5000,NULL);tool(d,"outrows","outrows.apx",1000000,1024,1048576,5000,NULL);tool(e,"outvalues","outvalues.apx",1000000,1024,1048576,5000,NULL);tool(f,"records","records.apx",1000000,1048576,1048576,5000,NULL);tool(g,"outrecords","outrecords.apx",1000000,1024,1048576,5000,NULL);snprintf(all,sizeof all,"%s,%s,%s,%s,%s,%s,%s",a,b,c,d,e,f,g);manifest_tools(1,all);start();
    send_frame("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"initialize\",\"params\":{}}");error_code(receive(),-32602);
    char *rows=rows_json(1,256);large_call("31","maps",rows);output_is(receive(),rows);free(rows);
    rows=rows_json(10,200);large_call("31","maps",rows);output_is(receive(),rows);free(rows);
    rows=rows_json(14,256);large_call("31","maps",rows);output_is(receive(),rows);free(rows);
    rows=rows_json(4,256);large_call("31","records",rows);output_is(receive(),rows);free(rows);
    rows=rows_json(6,256);large_call("31","records",rows);error_code(receive(),-32000);free(rows);
    tool_error(call_tool("outrecords","{\"input\":6}"),"resource_limit");
    rows=rows_json(16,256);large_call("32","maps",rows);error_code(receive(),-32000);free(rows);
    rows=rows_json(1,257);large_call("33","maps",rows);error_code(receive(),-32000);free(rows);
    const char *special="[\"braces{}[],:\\\"quote\\\\backslash\\u0000data\",\"literal\\\\u0000\"]";large_call("34","strings",special);output_is(receive(),special);
    /* Quoted punctuation at scale must not inflate member/value/depth work. */
    char *text=calloc(1,200000);require(text!=NULL,"quoted punctuation storage");size_t used=0;text[used++]='[';text[used++]='\"';for(unsigned i=0;i<10000;i++){memcpy(text+used,"{}[],:\\\\",8);used+=8;}text[used++]='\"';text[used++]=']';text[used]=0;large_call("35","strings",text);output_is(receive(),text);free(text);
    char *values=calloc(1,70000);require(values!=NULL,"value count storage");used=0;values[used++]='[';for(unsigned i=0;i<16384;i++){if(i)values[used++]=',';values[used++]='{';values[used++]='}';}values[used++]=']';values[used]=0;large_call("36","maps",values);error_code(receive(),-32000);free(values);
    const char *malformed[]={"{\"x\":0,}","[1,,2]","{\"x\":1,\"x\":2}","\"bad\\q\"","[true false]"};for(size_t i=0;i<sizeof malformed/sizeof malformed[0];i++){large_call("37","maps",malformed[i]);error_code(receive(),-32700);}
    tool_error(call_tool("out","{\"input\":257}"),"resource_limit");
    ApxValue *r=content(call_tool("out","{\"input\":256}"));require(apx_json_length(apx_json_get(r,"output"))==256,"exact member-cap worker output admitted");
    tool_error(call_tool("outrows","{\"input\":16}"),"resource_limit");
    tool_error(call_tool("outvalues","{\"input\":16384}"),"resource_limit");
    r=content(call_tool("outvalues","{\"input\":16000}"));require(apx_json_length(apx_json_get(r,"output"))==16000,"near value-cap worker output admitted");ping(110);stop(0);
    /* A second native result must not defer the already-started call's control
     * plane. Exercise raw-reply shape work and later descriptor work separately. */
    async_config(10000,2);tool(a,"hold","hold.apx",1000,4,1024,10000,"yes");tool(b,"increment","tool.apx",1000,1024,1024,1000,NULL);tool(c,"outrows","outrows.apx",1000000,1024,1048576,5000,NULL);tool(d,"outrecords","outrecords.apx",1000000,1024,1048576,5000,NULL);snprintf(all,sizeof all,"%s,%s,%s,%s",a,b,c,d);manifest_tools(2,all);start();
    for(unsigned mode=0;mode<2;mode++){
        request_call("20","hold","{\"input\":null}");started();pid_t broker=broker_pid();
        request_call("22",mode?"outrecords":"outrows",mode?"{\"input\":6}":"{\"input\":16}");tool_error(receive_within(1500),"resource_limit");
        send_frame("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/cancelled\",\"params\":{\"requestId\":20}}");ping(111);struct pollfd p={peer,POLLIN,0};require(poll(&p,1,1500)>0,"result work cannot defer actual started cancellation");closed_peer();recovered();
        int64_t until=now()+1500;while(kill(-broker,0)==0&&now()<until)ping(112);errno=0;require(kill(-broker,0)<0&&errno==ESRCH,"result-work cancellation reaps owned group");
    }
    request_call("21","hold","{\"input\":null}");started();prompt_shutdown(broker_pid());close(listener);listener=-1;
    /* Checked records exceeding the operational cap are not advertised. */
    char wide[12000];used=(size_t)sprintf(wide,"M wide\nT Wide struct\n");for(unsigned i=0;i<257;i++)used+=(size_t)sprintf(wide+used,"  f%u:i64\n",i);sprintf(wide+used,"F main->[Wide]\n  R []\n");save("wide.apx",wide);tool(a,"wide","wide.apx",1000,1024,1024,1000,NULL);manifest_tools(1,a);char *m=read_fixture("manifest.json");startup_refused(m);free(m);
}
static void frame_lifecycle(void){
    simple_manifest();launch();send_frame("{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"tools/list\"}");error_code(receive(),-32602);send_frame("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}");receive();send_frame("{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"tools/list\"}");error_code(receive(),-32602);send_frame("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}");
    send_frame("{\"jsonrpc\":\"2.0\",\"method\":\"tools/call\",\"params\":{\"name\":\"increment\",\"arguments\":{\"input\":4}}}");ping(100);send_frame("{bad json}");error_code(receive(),-32700);
    char *frame=calloc(1,FRAME+2);require(frame!=NULL,"exact receive storage fixture");const char *prefix="{\"jsonrpc\":\"2.0\",\"id\":101,\"method\":\"ping\"}";strcpy(frame,prefix);memset(frame+strlen(prefix),' ',FRAME-strlen(prefix));frame[FRAME]=0;send_frame(frame);ApxValue *r=receive();require(apx_json_integer(apx_json_get(r,"id"))==101,"exact maximum raw frame admitted");frame[FRAME]=' ';frame[FRAME+1]=0;send_frame(frame);free(frame);stop(2);
    simple_manifest();start();char id[400];memset(id,'x',sizeof id);id[sizeof id-1]=0;char request[1024];snprintf(request,sizeof request,"{\"jsonrpc\":\"2.0\",\"id\":\"%s\",\"method\":\"ping\"}",id);send_frame(request);error_code(receive(),-32600);ping(102);require(fputs("partial",input)>=0,"partial frame fixture");stop(2);
}
int main(void){
    signal(SIGPIPE,SIG_IGN);alarm(150);atexit(cleanup);const char *log=getenv("APX_SERVE_TRANSCRIPT");if(log){transcript=fopen(log,"wx");require(transcript!=NULL,"exclusive transcript");}
    const char *tmp=getenv("TMPDIR");require(tmp!=NULL,"owned scratch");snprintf(directory,sizeof directory,"%s/apx-serving-XXXXXX",tmp);require(mkdtemp(directory)!=NULL,"fixture directory");
    const char *key_fixture="{\"a\\u0000extra\":0,\"a\":1,\"\":2}";
    ApxValue *keys=apx_json_parse(key_fixture,strlen(key_fixture));
    require(keys&&apx_json_key_length(keys,0)==7&&strlen(apx_json_key(keys,0))==1,"readonly accessor preserves embedded NUL key byte length");
    require(apx_json_key_length(keys,1)==1&&apx_json_key(keys,2)&&apx_json_key_length(keys,2)==0,"ordinary and empty key byte lengths");
    require(!apx_json_key(keys,3)&&apx_json_key_length(keys,3)==0&&apx_json_key_length(NULL,0)==0,"key length kind and bounds safety");
    if(getenv("APX_SERVE_WORK_ONLY")){json_work_lifecycle();}else{first_vertical();shapes();fixed_authority();async_lifecycle();strict_startup();frame_lifecycle();json_work_lifecycle();json_work_values();}cleanup();apx_json_reset();if(transcript)fclose(transcript);printf("serving: %u checks PASS; immutable typed discovery/calls, strict caller authority, fresh brokers, actual HTTP/file effects, started cancellation/deadline/crash, bounded lifecycle and frames/JSON work\n",checks);return 0;
}
