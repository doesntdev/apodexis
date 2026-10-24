/* Optional sanitizer/fuzz gate; deterministic, bounded libc-only inputs. */
#include "../compiler/apx_frontend.h"
#include "../runtime/apx_runtime.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t random_state=0x31415926;
static unsigned next(void){random_state=random_state*1664525u+1013904223u;return random_state;}
int main(void){const char alphabet[]="MFTRSLXP?CEWA_ .()[]:=abcdefghijklmnopqrstuvwxyz0123456789-+*\"\\\n\r\t";char source[768];
    for(unsigned i=0;i<5000;i++){
        size_t n=next()%sizeof source;for(size_t j=0;j<n;j++)source[j]=alphabet[next()%(sizeof alphabet-1)];source[n]=0;
        char *il=NULL,*canonical=NULL,*diag=NULL;apx_compile_source(source,&il,&canonical,&diag);free(il);free(canonical);free(diag);
        diag=NULL;canonical=apx_format_source(source,&diag);free(canonical);free(diag);
        diag=NULL;canonical=NULL;apx_check_source(source,&canonical,&diag);free(canonical);free(diag);
        ApxValue *value=apx_json_parse(source,n);if(value){char *encoded=apx_json_encode(value);free(encoded);}apx_json_reset();
    }
    const char *good="M metadata\n\nT Box struct\n  text:str\n\nF main b:Box->str\n  R (. b text)\n\nX native\n  A (= (main (Box text=\"ok\")) \"ok\")\n";
    for(unsigned i=0;i<300;i++){char *e=NULL,*c=NULL,*p=NULL,*d=NULL;if(apx_describe_source(good,&e,&c,&p,&d))abort();free(e);free(c);free(p);free(d);}
    puts("ASan/UBSan-compatible C fuzz: 5000 malformed syntax/check/JSON inputs and 300 full checked metadata render cycles PASS");return 0;
}
