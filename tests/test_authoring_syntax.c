/* Agent authoring sugar is checked/compiled by the existing native C frontend. */
#define _POSIX_C_SOURCE 200809L
#include "apx_frontend.h"
#include "../runtime/apx_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
static unsigned assertions;
static void require(int ok,const char *message){assertions++;if(!ok){fprintf(stderr,"authoring syntax FAIL: %s\n",message);exit(1);}}
static void equivalent(const char *short_source,const char *prefix_source){
    char *short_il=NULL,*prefix_il=NULL,*short_canonical=NULL,*prefix_canonical=NULL,*diagnostic=NULL;
    int result=apx_compile_source(short_source,&short_il,&short_canonical,&diagnostic);
    if(result)fprintf(stderr,"%s\n",diagnostic?diagnostic:"no diagnostic");
    require(!result&&short_il&&short_canonical&&!diagnostic,"short form compiles through native emitter");
    require(!apx_compile_source(prefix_source,&prefix_il,&prefix_canonical,&diagnostic)&&prefix_il&&prefix_canonical&&!diagnostic,"expanded prefix form compiles");
    require(!strcmp(short_canonical,prefix_canonical),"canonical identity unchanged by sugar");
    require(!strcmp(short_il,prefix_il),"QBE IL exactly equal after lowering");
    char *again=apx_format_source(short_canonical,&diagnostic);require(again&&!diagnostic&&!strcmp(again,short_canonical),"canonical idempotence");free(again);
    char *a=NULL,*b=NULL,*c=NULL,*d=NULL,*e=NULL,*f=NULL;
    require(!apx_describe_source(short_source,&a,&b,&c,&diagnostic)&&!diagnostic,"short checked metadata renders");
    require(!apx_describe_source(prefix_source,&d,&e,&f,&diagnostic)&&!diagnostic,"prefix checked metadata renders");
    require(!strcmp(a,d)&&!strcmp(b,e)&&!strcmp(c,f),"facts/contracts/pseudocode equal");
    free(a);free(b);free(c);free(d);free(e);free(f);free(short_il);free(prefix_il);free(short_canonical);free(prefix_canonical);
}
static void error(const char *source,const char *code,const char *kind){char *canonical=NULL,*diagnostics=NULL;require(apx_check_source(source,&canonical,&diagnostics)==1&&diagnostics,"invalid syntax fails closed");ApxValue *all=apx_json_parse(diagnostics,strlen(diagnostics));require(all&&apx_json_length(all)>=1,"nonempty structured error");ApxValue *first=apx_json_item(all,0);const char *actual=apx_json_string(apx_json_get(first,"code")),*actual_kind=apx_json_string(apx_json_get(first,"kind"));if(!actual||strcmp(actual,code)||!actual_kind||strcmp(actual_kind,kind))fprintf(stderr,"unexpected error: %s\n",diagnostics);require(actual&&!strcmp(actual,code)&&actual_kind&&!strcmp(actual_kind,kind),"expected error code/kind");free(canonical);free(diagnostics);apx_json_reset();}
static void postfix_tests(void){
    equivalent("M fields\nT Row struct\n  id:str\nF main row:Row->str\n  R row.id\n","M fields\nT Row struct\n  id:str\nF main row:Row->str\n  R (. row id)\n");
    equivalent("M keywords\nT Row struct\n  true:i64\n  false:i64\nF main row:Row->i64\n  R (+ row.true row.false)\n","M keywords\nT Row struct\n  true:i64\n  false:i64\nF main row:Row->i64\n  R (+ (. row \"true\") (. row \"false\"))\n");
    equivalent("M nested\nT Detail struct\n  values:[i64]\nT Input struct\n  detail:Detail\nF main input:Input->i64\n  R input.detail.values[(+ 0 1)]\n","M nested\nT Detail struct\n  values:[i64]\nT Input struct\n  detail:Detail\nF main input:Input->i64\n  R (@ (. (. input detail) values) (+ 0 1))\n");
    equivalent("M chain\nT Row struct\n  id:str\nF main rows:[Row]->str\n  R rows[0].id\n","M chain\nT Row struct\n  id:str\nF main rows:[Row]->str\n  R (. (@ rows 0) id)\n");
    equivalent("M call\nT Row struct\n  id:str\nF rows->[Row]\n  R [(Row id=\"ok\")]\nF index->i64\n  R 0\nF main->str\n  R (rows)[(index)].id\n","M call\nT Row struct\n  id:str\nF rows->[Row]\n  R [(Row id=\"ok\")]\nF index->i64\n  R 0\nF main->str\n  R (. (@ (rows) (index)) id)\n");
    equivalent("M literal\nF main->i64\n  R [[4 5] [6 7]][1][0]\n","M literal\nF main->i64\n  R (@ (@ [[4 5] [6 7]] 1) 0)\n");
    equivalent("M arguments\nF sum a:i64 b:i64->i64\n  R (+ a b)\nF main rows:[i64]->i64\n  R (sum rows[0] rows[1])\n","M arguments\nF sum a:i64 b:i64->i64\n  R (+ a b)\nF main rows:[i64]->i64\n  R (sum (@ rows 0) (@ rows 1))\n");
    equivalent("M array_items\nF main rows:[i64]->[i64]\n  R [rows[0] rows[1]]\n","M array_items\nF main rows:[i64]->[i64]\n  R [(@ rows 0) (@ rows 1)]\n");
    equivalent("M ctor\nT Box struct\n  item:i64\nF main rows:[i64]->Box\n  R (Box item=rows[0])\n","M ctor\nT Box struct\n  item:i64\nF main rows:[i64]->Box\n  R (Box item=(@ rows 0))\n");
    error("M bad\nT Row struct\n  id:str\nF main row:Row->str\n  R row.missing\n","N101","unknown_symbol");
    error("M bad\nF main rows:[i64]->i64\n  R rows[true]\n","T101","type_mismatch");
    error("M bad\nF main text:str->str\n  R text[0]\n","T101","type_mismatch");
    error("M bad\nF main rows:[i64]->i64\n  R rows[]\n","P001","invalid_identifier");
    error("M bad\nF main rows:[i64]->i64\n  R rows[0 1]\n","P001","unexpected_token");
    error("M bad\nF main rows:[i64]->i64\n  R rows [0]\n","P001","unexpected_token");
    error("M bad\nT Row struct\n  id:str\nF main row:Row->str\n  R row. id\n","P001","unexpected_token");
    error("M bad\nT Row struct\n  id:str\nF main row:Row->str\n  S row.id \"bad\"\n  R row.id\n","P001","unexpected_token");
    const char *numbers[]={"0.1","-0.0","1.0","1.",".5","-.5","-.5e-3","-0.5","1e-7","1.5e+20","5e-324"};
    for(size_t i=0;i<sizeof numbers/sizeof numbers[0];i++){char source[128];snprintf(source,sizeof source,"M decimal\nF main->f64\n  R %s\n",numbers[i]);char *d=NULL,*f=apx_format_source(source,&d);require(f&&!d,"decimal/exponent literals preserved");free(f);free(d);}
    char sugar[8192],prefix[8192];for(unsigned n=64;n<=65;n++){
        char type[140];for(unsigned i=0;i<n;i++)type[i]='[';memcpy(type+n,"i64",3);for(unsigned i=0;i<n;i++)type[n+3+i]=']';type[2*n+3]=0;
        size_t a=(size_t)snprintf(sugar,sizeof sugar,"M depth\nF main rows:%s->i64\n  R rows",type);
        for(unsigned i=0;i<n;i++){memcpy(sugar+a,"[0]",3);a+=3;}strcpy(sugar+a,"\n");
        if(n==64){size_t b=(size_t)snprintf(prefix,sizeof prefix,"M depth\nF main rows:%s->i64\n  R ",type);for(unsigned i=0;i<n;i++){memcpy(prefix+b,"(@ ",3);b+=3;}memcpy(prefix+b,"rows",4);b+=4;for(unsigned i=0;i<n;i++){memcpy(prefix+b," 0)",3);b+=3;}strcpy(prefix+b,"\n");equivalent(sugar,prefix);}else error(sugar,"P001","nesting_limit_exceeded");
    }
    {char marker[140],source[512];unsigned depth=63;
        for(unsigned i=0;i<depth;i++)marker[i]='[';
        memcpy(marker+depth,"i64",3);
        for(unsigned i=0;i<depth;i++)marker[depth+3+i]=']';
        marker[2*depth+3]=0;
        snprintf(source,sizeof source,"M depth\nF main->i64\n  L out (buf %s 1)\n  R 0\n",marker);
        char *diagnostic=NULL,*canonical=apx_format_source(source,&diagnostic);require(canonical&&!diagnostic,"deep type marker fits prefix syntax budget");free(canonical);
        snprintf(source,sizeof source,"M depth\nF main->i64\n  L out (buf %s 1)[0]\n  R 0\n",marker);
        error(source,"P001","nesting_limit_exceeded");
    }
    const char *span_source="M bad\r\nT Row struct\r\n  id:str\r\nF main row:Row->str\r\n  R row.missing\r\n";char *c=NULL,*d=NULL;require(apx_check_source(span_source,&c,&d)==1&&d,"postfix field span diagnosed");ApxValue *all=apx_json_parse(d,strlen(d)),*span=apx_json_get(apx_json_item(all,0),"span");const char *field=strstr(span_source,"missing");require(apx_json_integer(apx_json_get(span,"start_byte"))==(int64_t)(field-span_source)&&apx_json_integer(apx_json_get(span,"end_byte"))==(int64_t)(field-span_source+7),"postfix diagnostic refers to original field bytes");free(c);free(d);apx_json_reset();
}
static void push_tests(void){
    equivalent("M push\nF main values:[i64]->[i64]\n  L out (buf i64 (# values))\n  E value values\n    P out (* value 2)\n  R (finish out)\n","M push\nF main values:[i64]->[i64]\n  L out (buf i64 (# values))\n  E value values\n    S out (push out (* value 2))\n  R (finish out)\n");
    equivalent("M aliases\nF main->[[i64]]\n  L out (buf i64 3)\n  L alias out\n  P out 1\n  P out (count out)\n  L snapshot (finish out)\n  P out 3\n  R [(finish alias) snapshot (finish out)]\n","M aliases\nF main->[[i64]]\n  L out (buf i64 3)\n  L alias out\n  S out (push out 1)\n  S out (push out (count out))\n  L snapshot (finish out)\n  S out (push out 3)\n  R [(finish alias) snapshot (finish out)]\n");
    equivalent("M variants\nF main->[option i64]\n  L out (buf option i64 2)\n  P out (some 7)\n  P out (none)\n  R (finish out)\n","M variants\nF main->[option i64]\n  L out (buf option i64 2)\n  S out (push out (some 7))\n  S out (push out (none))\n  R (finish out)\n");
    equivalent("M arrays\nF main->[[i64]]\n  L out (buf [i64] 2)\n  P out []\n  P out [1 2]\n  R (finish out)\n","M arrays\nF main->[[i64]]\n  L out (buf [i64] 2)\n  S out (push out [])\n  S out (push out [1 2])\n  R (finish out)\n");
    error("M bad\nF main->[i64]\n  L true (buf i64 1)\n  P true 2\n  R (finish true)\n","T101","type_mismatch");
    error("M bad\nF main->i64\n  P missing 1\n  R 0\n","N101","unknown_symbol");
    error("M bad\nF main->i64\n  L out 1\n  P out 2\n  R out\n","T101","type_mismatch");
    error("M bad\nF main->[i64]\n  L out (buf i64 1)\n  P out true\n  R (finish out)\n","T101","type_mismatch");
    error("M bad\nF main->i64\n  P out.field 1\n  R 0\n","P001","unexpected_token");
    error("M bad\nF main->i64\n  P out[0] 1\n  R 0\n","P001","unexpected_token");
    error("M bad\nF main->[i64]\n  L out (buf i64 1)\n  (push out 1)\n  R (finish out)\n","P001","unexpected_token");
    char *canonical=NULL,*diagnostics=NULL;const char *source="M holes\nF main seed:i64->[i64]\n  L out (buf i64 1)\n  P out _\n  R (finish out)\n";
    require(apx_check_source(source,&canonical,&diagnostics)==1,"P payload typed hole rejects execution");
    ApxValue *all=apx_json_parse(diagnostics,strlen(diagnostics)),*hole=apx_json_item(all,0);
    require(hole&&!strcmp(apx_json_string(apx_json_get(hole,"code")),"H001")&&!strcmp(apx_json_string(apx_json_get(hole,"expected_type")),"i64"),"P payload learns builder item type");
    free(canonical);free(diagnostics);apx_json_reset();
}
static void inference_tests(void){
    const char *source="M inference\nF main->i64\n  L total 0\n  L seen false\n  L ratio 1.5\n  L label \"ok\"\n  L values [1 2]\n  L out (buf i64 1)\n  P out values[0]\n  A (! seen)\n  A (= ratio 1.5)\n  A (= (# label) 2)\n  R (+ total (# (finish out)))\n";
    char *il=NULL,*canonical=NULL,*diagnostic=NULL;
    require(!apx_compile_source(source,&il,&canonical,&diagnostic)&&il&&canonical&&!diagnostic,"unambiguous scalar/array/builder local inference remains supported");
    free(il);free(canonical);
    error("M ambiguous\nF main->i64\n  L empty []\n  R 0\n","T101","type_mismatch");
    error("M ambiguous\nF main->i64\n  L missing (none)\n  R 0\n","T101","type_mismatch");
    error("M ambiguous\nF main->i64\n  L outcome (ok 7)\n  R 0\n","T101","type_mismatch");
    error("M typed\nF main->i64\n  L total 0\n  S total true\n  R total\n","T101","type_mismatch");
}
int main(void){postfix_tests();push_tests();inference_tests();printf("native compact authoring steps 1-3: %u assertions PASS\n",assertions);return 0;}
