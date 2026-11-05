/* Native execution/fuel/error-order parity for compact authoring forms. */
#define _POSIX_C_SOURCE 200809L
#include "../runtime/apx_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
static unsigned checks;
static void require(int ok,const char *message){checks++;if(!ok){fprintf(stderr,"native authoring FAIL: %s\n",message);exit(1);}}
static void save(const char *path,const char *text){FILE *f=fopen(path,"wb");require(f!=NULL,"open fixture");require(fwrite(text,1,strlen(text),f)==strlen(text)&&!fclose(f),"save fixture");}
static char *readall(const char *path){FILE *f=fopen(path,"rb");require(f!=NULL,"open native response");require(!fseek(f,0,SEEK_END),"seek native response");long n=ftell(f);require(n>=0&&n<12*1024*1024,"bounded native response");rewind(f);char *text=calloc(1,(size_t)n+1);require(text&&fread(text,1,(size_t)n,f)==(size_t)n&&!fclose(f),"read native response");return text;}
static char *run(const char *source,const char *input,const char *fuel,const char *directory,int *code){char file[4096],in[4096],out[4096];snprintf(file,sizeof file,"%s/source.apx",directory);snprintf(in,sizeof in,"%s/input.json",directory);snprintf(out,sizeof out,"%s/output",directory);save(file,source);save(in,input);fflush(NULL);pid_t pid=fork();require(pid>=0,"fork native compiler");if(!pid){if(!freopen(out,"wb",stdout))_exit(126);execl("bin/apodexis","bin/apodexis","run",file,"--input",in,"--fuel",fuel,"--json",(char*)NULL);_exit(127);}int status;require(waitpid(pid,&status,0)==pid&&WIFEXITED(status),"wait native compiler");*code=WEXITSTATUS(status);return readall(out);}
static void same(const char *short_source,const char *prefix_source,const char *input,const char *fuel,const char *expected,int success,const char *directory){int short_code,prefix_code;char *a=run(short_source,input,fuel,directory,&short_code),*b=run(prefix_source,input,fuel,directory,&prefix_code);ApxValue *one=apx_json_parse(a,strlen(a)),*two=apx_json_parse(b,strlen(b));require(one&&two,"valid native envelopes");const char *keys[]={"status","source_hash","fuel_used","output","diagnostic"};for(size_t i=0;i<5;i++){ApxValue *x=apx_json_get(one,keys[i]),*y=apx_json_get(two,keys[i]);require((x!=NULL)==(y!=NULL),"matching native result fields");if(x){char *xx=apx_json_encode(x),*yy=apx_json_encode(y);if(!xx||!yy||strcmp(xx,yy))fprintf(stderr,"%s differs:\n%s\n%s\n",keys[i],a,b);require(xx&&yy&&!strcmp(xx,yy),"matching output/source/fuel/error envelope");free(xx);free(yy);}}
    if(short_code!=prefix_code||short_code!=(success?0:2))fprintf(stderr,"native parity fuel=%s expected_success=%d exit_short=%d exit_prefix=%d\nSOURCE:\n%s\nSHORT:\n%s\nPREFIX:\n%s\n",fuel,success,short_code,prefix_code,short_source,a,b);
    require(short_code==prefix_code&&short_code==(success?0:2),"matching native success/failure exits");if(success){ApxValue *want=apx_json_parse(expected,strlen(expected));char *x=apx_json_encode(apx_json_get(one,"output")),*y=apx_json_encode(want);require(x&&y&&!strcmp(x,y),"correct native output");free(x);free(y);}else require(!strcmp(apx_json_string(apx_json_get(apx_json_get(one,"diagnostic"),"kind")),expected),"expected native failure");free(a);free(b);apx_json_reset();}
static void postfix(const char *directory){
    const char *short_source="M reads\nT Row struct\n  id:str\nF make rows:[Row]->[Row]\n  R rows\nF index->i64\n  R 0\nF main rows:[Row]->str\n  R (make rows)[(index)].id\n";
    const char *prefix="M reads\nT Row struct\n  id:str\nF make rows:[Row]->[Row]\n  R rows\nF index->i64\n  R 0\nF main rows:[Row]->str\n  R (. (@ (make rows) (index)) id)\n";
    same(short_source,prefix,"[{\"id\":\"once\"}]","1000000","\"once\"",1,directory);
    for(unsigned fuel=0;fuel<3;fuel++){char limit[32];snprintf(limit,sizeof limit,"%u",fuel);same(short_source,prefix,"[{\"id\":\"once\"}]",limit,"fuel_exhausted",0,directory);}
    same(short_source,prefix,"[]","1000000","index_out_of_bounds",0,directory);
    same("M reads\nF main rows:[i64]->i64\n  R rows[-1]\n","M reads\nF main rows:[i64]->i64\n  R (@ rows -1)\n","[1]","1000000","index_out_of_bounds",0,directory);
    same("M reads\nF main rows:[i64]->i64\n  R rows[(+ 9223372036854775807 1)]\n","M reads\nF main rows:[i64]->i64\n  R (@ rows (+ 9223372036854775807 1))\n","[1]","1000000","integer_overflow",0,directory);
    same("M reads\nF base->[i64]\n  Z \"base-first\"\nF index->i64\n  Z \"index-second\"\nF main->i64\n  R (base)[(index)]\n","M reads\nF base->[i64]\n  Z \"base-first\"\nF index->i64\n  Z \"index-second\"\nF main->i64\n  R (@ (base) (index))\n","null","1000000","trap",0,directory);
    same("M reads\nF main->i64\n  R [[1 2] [3 4]][1][0]\n","M reads\nF main->i64\n  R (@ (@ [[1 2] [3 4]] 1) 0)\n","null","1000000","3",1,directory);
}
static void push(const char *directory){
    const char *short_source="M push\nF main values:[i64]->[i64]\n  L out (buf i64 (# values))\n  E value values\n    P out (* value 2)\n  R (finish out)\n";
    const char *prefix="M push\nF main values:[i64]->[i64]\n  L out (buf i64 (# values))\n  E value values\n    S out (push out (* value 2))\n  R (finish out)\n";
    same(short_source,prefix,"[1,2]","1000000","[2,4]",1,directory);
    same(short_source,prefix,"[]","1000000","[]",1,directory);
    for(unsigned fuel=0;fuel<9;fuel++){char limit[32];snprintf(limit,sizeof limit,"%u",fuel);same(short_source,prefix,"[1,2]",limit,"fuel_exhausted",0,directory);}
    same("M alias\nF main->[[i64]]\n  L out (buf i64 3)\n  L alias out\n  P out 1\n  P out (count out)\n  L snapshot (finish out)\n  P out 3\n  R [(finish alias) snapshot (finish out)]\n","M alias\nF main->[[i64]]\n  L out (buf i64 3)\n  L alias out\n  S out (push out 1)\n  S out (push out (count out))\n  L snapshot (finish out)\n  S out (push out 3)\n  R [(finish alias) snapshot (finish out)]\n","null","1000000","[[],[1,1],[1,1,3]]",1,directory);
    same("M cap\nF main->[i64]\n  L out (buf i64 0)\n  P out 1\n  R (finish out)\n","M cap\nF main->[i64]\n  L out (buf i64 0)\n  S out (push out 1)\n  R (finish out)\n","null","1000000","builder_capacity_exceeded",0,directory);
    same("M eager\nF value->i64\n  Z \"payload-before-capacity\"\nF main->[i64]\n  L out (buf i64 0)\n  P out (value)\n  R (finish out)\n","M eager\nF value->i64\n  Z \"payload-before-capacity\"\nF main->[i64]\n  L out (buf i64 0)\n  S out (push out (value))\n  R (finish out)\n","null","1000000","trap",0,directory);
    same("M variants\nF main->[option i64]\n  L out (buf option i64 2)\n  P out (some 7)\n  P out (none)\n  R (finish out)\n","M variants\nF main->[option i64]\n  L out (buf option i64 2)\n  S out (push out (some 7))\n  S out (push out (none))\n  R (finish out)\n","null","1000000","[{\"tag\":\"some\",\"value\":7},{\"tag\":\"none\"}]",1,directory);
}
static void builder_snapshots(const char *directory){
    const char *short_source=
        "M snapshots\nF main->[[i64]]\n"
        "  L prefix (buf i64 5)\n  P prefix 10\n  P prefix 20\n"
        "  L left prefix\n  L right prefix\n"
        "  L before (finish prefix)\n  L again (finish prefix)\n"
        "  P left 30\n  P right 40\n  P left 31\n"
        "  L left_before (finish left)\n  P left 32\n  P prefix 50\n"
        "  A (= (count prefix) 3)\n  A (= (count left) 5)\n"
        "  A (= (count right) 3)\n  A (= (cap left) 5)\n"
        "  R [before again (finish prefix) left_before (finish left) (finish right)]\n";
    const char *prefix=
        "M snapshots\nF main->[[i64]]\n"
        "  L prefix (buf i64 5)\n  S prefix (push prefix 10)\n  S prefix (push prefix 20)\n"
        "  L left prefix\n  L right prefix\n"
        "  L before (finish prefix)\n  L again (finish prefix)\n"
        "  S left (push left 30)\n  S right (push right 40)\n  S left (push left 31)\n"
        "  L left_before (finish left)\n  S left (push left 32)\n  S prefix (push prefix 50)\n"
        "  A (= (count prefix) 3)\n  A (= (count left) 5)\n"
        "  A (= (count right) 3)\n  A (= (cap left) 5)\n"
        "  R [before again (finish prefix) left_before (finish left) (finish right)]\n";
    /* Both branches share a nonempty prefix; finishes must not consume or mutate it. */
    same(short_source,prefix,"null","1000000","[[10,20],[10,20],[10,20,50],[10,20,30,31],[10,20,30,31,32],[10,20,40]]",1,directory);
    same(short_source,prefix,"null","8","fuel_exhausted",0,directory);
    same(short_source,prefix,"null","16","fuel_exhausted",0,directory);
}
static void builder_bounds(const char *directory){
    const char *short_source=
        "M bounds\nF main capacity:i64->[[i64]]\n"
        "  L out (buf i64 capacity)\n  A (= (count out) 0)\n  A (= (cap out) capacity)\n"
        "  L empty (finish out)\n  ? (= capacity 0)\n    R [empty (finish out)]\n"
        "  P out 7\n  A (= (count out) 1)\n  A (= (cap out) capacity)\n"
        "  P out 8\n  A (= (count out) 2)\n  A (= (cap out) capacity)\n"
        "  R [empty (finish out)]\n";
    const char *prefix=
        "M bounds\nF main capacity:i64->[[i64]]\n"
        "  L out (buf i64 capacity)\n  A (= (count out) 0)\n  A (= (cap out) capacity)\n"
        "  L empty (finish out)\n  ? (= capacity 0)\n    R [empty (finish out)]\n"
        "  S out (push out 7)\n  A (= (count out) 1)\n  A (= (cap out) capacity)\n"
        "  S out (push out 8)\n  A (= (count out) 2)\n  A (= (cap out) capacity)\n"
        "  R [empty (finish out)]\n";
    same(short_source,prefix,"0","1000000","[[],[]]",1,directory);
    same(short_source,prefix,"2","1000000","[[],[7,8]]",1,directory);
    same(short_source,prefix,"1","1000000","builder_capacity_exceeded",0,directory);
    same(short_source,prefix,"-1","1000000","builder_capacity_exceeded",0,directory);
    /* Declared capacity is metadata, not an eager allocation request. */
    same(short_source,prefix,"9223372036854775807","1000000","[[],[7,8]]",1,directory);
}
static void builder_nested(const char *directory){
    const char *short_source=
        "M nested\nT Row struct\n  label:str\n  cells:[[i64]]\n  choice:option [str]\n"
        "F main->[[result Row str]]\n"
        "  L inner (buf [i64] 2)\n  P inner [1 2]\n  L first (finish inner)\n  P inner []\n"
        "  L rows (buf result Row str 3)\n"
        "  P rows (ok (Row label=\"é🙂\" cells=first choice=(some [\"kept\" \"\"])))\n"
        "  L saved (finish rows)\n  P rows (err \"later\")\n"
        "  P rows (ok (Row label=\"\" cells=(finish inner) choice=(none)))\n"
        "  R [saved (finish rows)]\n";
    const char *prefix=
        "M nested\nT Row struct\n  label:str\n  cells:[[i64]]\n  choice:option [str]\n"
        "F main->[[result Row str]]\n"
        "  L inner (buf [i64] 2)\n  S inner (push inner [1 2])\n  L first (finish inner)\n  S inner (push inner [])\n"
        "  L rows (buf result Row str 3)\n"
        "  S rows (push rows (ok (Row label=\"é🙂\" cells=first choice=(some [\"kept\" \"\"]))))\n"
        "  L saved (finish rows)\n  S rows (push rows (err \"later\"))\n"
        "  S rows (push rows (ok (Row label=\"\" cells=(finish inner) choice=(none))))\n"
        "  R [saved (finish rows)]\n";
    const char *expected=
        "[[{\"tag\":\"ok\",\"value\":{\"label\":\"é🙂\",\"cells\":[[1,2]],\"choice\":{\"tag\":\"some\",\"value\":[\"kept\",\"\"]}}}],"
        "[{\"tag\":\"ok\",\"value\":{\"label\":\"é🙂\",\"cells\":[[1,2]],\"choice\":{\"tag\":\"some\",\"value\":[\"kept\",\"\"]}}},"
        "{\"tag\":\"err\",\"error\":\"later\"},"
        "{\"tag\":\"ok\",\"value\":{\"label\":\"\",\"cells\":[[1,2],[]],\"choice\":{\"tag\":\"none\"}}}]]";
    same(short_source,prefix,"null","1000000",expected,1,directory);
}
int main(void){char directory[]=".build/authoring-native-XXXXXX";require(mkdtemp(directory)!=NULL,"native scratch directory");postfix(directory);push(directory);builder_snapshots(directory);builder_bounds(directory);builder_nested(directory);char path[4096];const char *files[]={"source.apx","input.json","output"};for(size_t i=0;i<3;i++){snprintf(path,sizeof path,"%s/%s",directory,files[i]);unlink(path);}require(!rmdir(directory),"native fixture cleanup");printf("native authoring steps 1-2: %u checks; postfix/P output/fuel/snapshot/nesting/evaluation-order/bounds parity PASS\n",checks);return 0;}
