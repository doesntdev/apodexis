#include "apx_frontend.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int probe(const char *name, const char *source, const char *code, int native) {
    char *il=NULL,*canonical=NULL,*diag=NULL; int status=apx_compile_source(source,native?&il:NULL,&canonical,&diag);
    int ok=code ? status && diag && strstr(diag,code) : !status && canonical && (!native || il);
    if (!ok) fprintf(stderr,"FAIL %s: %s\n",name,diag?diag:"unexpected success/failure");
    free(il);free(canonical);free(diag);return ok;
}
static int selftest(void) {
    int total=0,pass=0;
#define TEST(name,source,code,native) do {++total;pass+=probe(name,source,code,native);} while(0)
    TEST("hello","M hello\nF main->i64\n  R 42\n",NULL,1);
    TEST("unknown","M x\nF main->i64\n  R absent\n","N101",1);
    TEST("hole","M x\nF main->i64\n  R _\n","H001",1);
    TEST("annotation","M x\nF main->i64\n  L x:i64 true\n  R 1\n","T101",1);
    TEST("arity","M x\nF main->i64\n  R (+ 1)\n","T101",1);
    TEST("named builtin","M x\nF main->i64\n  R (+ a=1 b=2)\n","N104",1);
    TEST("named function rejected","M named\nF add a:i64 b:i64->i64\n  R (+ a b)\nF main->i64\n  R (add b=2 a=1)\n","N104",1);
    TEST("positional function accepted","M positional\nF add a:i64 b:i64->i64\n  R (+ a b)\nF main->i64\n  R (add 2 1)\n",NULL,1);
    TEST("named constructor retained","M records\nT Pair struct\n  a:i64\n  b:i64\nF main->Pair\n  R (Pair b=2 a=1)\n",NULL,1);
    TEST("outer local shadow rejected","M shadow\nF main->i64\n  L x 1\n  ? true\n    L x 2\n  R x\n","N102",1);
    TEST("parameter shadow rejected","M shadow\nF main x:i64->i64\n  ? true\n    L x 2\n  R x\n","N102",1);
    TEST("loop shadow rejected","M shadow\nF main->i64\n  L x 1\n  E x [2]\n    A true\n  R x\n","N102",1);
    TEST("case payload shadow rejected","M shadow\nF main->i64\n  L x 1\n  L o:option i64 (some 2)\n  C o\n    some x\n      A true\n    none\n      A true\n  R x\n","N102",1);
    TEST("fresh sibling scopes accepted","M sibling\nF main->i64\n  ? true\n    L x 1\n    A (= x 1)\n  ? true\n    L x 2\n    A (= x 2)\n  R 0\n",NULL,1);
    TEST("missing return","M x\nF main->i64\n  A true\n","T105",1);
    TEST("empty array","M x\nF main->i64\n  L x []\n  R 0\n","T101",1);
    TEST("expected array","M x\nF main->[i64]\n  R []\n",NULL,1);
    TEST("duplicate","M x\nF main->i64\n  L x 1\n  L x 2\n  R x\n","N102",1);
    TEST("field","M x\nT Box struct\n  x:i64\nF main->i64\n  R (. (Box x=1) bad)\n","N101",1);
    TEST("missing field","M x\nT Box struct\n  x:i64\nF main->Box\n  R (Box)\n","A101",1);
    TEST("enum constructors unsupported","M x\nT Color enum\n  Red\nF main->Color\n  R (Color.Red)\n","N101",1);
    TEST("input option unsupported","M x\nF main input:option i64->i64\n  R 0\n","A104",1);
    TEST("check no main","M x\nF helper->i64\n  R 0\n",NULL,0);
    TEST("emit no main","M x\nF helper->i64\n  R 0\n","N101",1);
    TEST("builder boundary","M x\nF main input:[builder i64]->i64\n  R 0\n","T101",0);
    TEST("recursive type","M x\nT A struct\n  b:[B]\nT B struct\n  a:A\nF main->i64\n  R 0\n","T104",1);
    TEST("bad indentation","M x\nF main->i64\n   R 1\n","P003",1);
    TEST("bad unicode escape","M x\nF main->str\n  R \"\\u{d800}\"\n","P005",1);
    TEST("overflow","M x\nF main->i64\n  R 9223372036854775808\n","A103",1);
    TEST("i64 minimum","M x\nF main->i64\n  R -9223372036854775808\n",NULL,1);
    TEST("float infinite","M x\nF main->f64\n  R 1e999\n","P005",1);
    TEST("incomplete case","M x\nF main->i64\n  L o:option i64 (none)\n  C o\n    some n\n      R n\n  R 0\n","T103",1);
    TEST("scope","M x\nF main->i64\n  ? true\n    L inner 1\n  R inner\n","N101",1);
    {char *d=NULL,*c=apx_format_source("M hello\r\nF main -> i64\r\n  R 42 ; comment\r\n",&d); ++total; if(c&&!strcmp(c,"M hello\n\nF main->i64\n  R 42\n"))++pass;else fprintf(stderr,"FAIL canonical\n");free(c);free(d);}
    {char *d=NULL,*c=apx_format_source("M floats\nF main->f64\n  R 1e-7\n",&d); ++total;if(c&&!strcmp(c,"M floats\n\nF main->f64\n  R 0.0000001\n"))++pass;else fprintf(stderr,"FAIL float canonical: %s\n",c?c:d);free(c);free(d);}
    printf("C public API: %d/%d PASS\n",pass,total);return pass!=total;
}
int main(int argc,char **argv) {
    FILE *file;char *source,*il=NULL,*canonical=NULL,*diag=NULL;long length;int status,check=0,format=0;
    if(argc==2&&!strcmp(argv[1],"--selftest"))return selftest();
    if(argc==3){check=!strcmp(argv[1],"--check");format=!strcmp(argv[1],"--format");}
    if(argc<2||argc>3)return 64;
    file=fopen(argv[argc-1],"rb");if(!file)return 66;fseek(file,0,SEEK_END);length=ftell(file);rewind(file);
    if(length<0||length>1000000){fclose(file);return 65;}source=malloc((size_t)length+1);if(!source)return 70;
    if(fread(source,1,(size_t)length,file)!=(size_t)length){free(source);fclose(file);return 74;}source[length]=0;fclose(file);
    if(format){canonical=apx_format_source(source,&diag);status=!canonical;}
    else status=apx_compile_source(source,check?NULL:&il,&canonical,&diag);
    if(status){puts(diag?diag:"no diagnostic");}else if(format)fputs(canonical,stdout);else if(!check)fputs(il,stdout);else puts("checked");
    free(source);free(il);free(canonical);free(diag);return status;
}
