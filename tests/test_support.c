/* Self-contained developer acceptance; no Python/Rust runner needed. */
#include "../compiler/apx_hash.h"
#include "../runtime/apx_runtime.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void){
    char hash[65];
    apx_sha256("",0,hash);assert(!strcmp(hash,"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    apx_sha256("abc",3,hash);assert(!strcmp(hash,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    const char *cases[]={"null","true","false","[1,2,3]","\"é🙂\"","{\"b\":2,\"a\":1}","18446744073709551615"};
    const char *expected[]={"null","true","false","[1,2,3]","\"é🙂\"","{\"a\":1,\"b\":2}","18446744073709551615"};
    for(size_t i=0;i<sizeof cases/sizeof cases[0];i++){
        ApxValue *value=apx_json_parse(cases[i],strlen(cases[i]));assert(value);char *json=apx_json_encode(value);assert(json&&!strcmp(json,expected[i]));free(json);apx_json_reset();
    }
    const char *bad[]={"{\"a\":1,\"a\":2}","[1,]","01","\"\\ud800\"","\"\xc0\x80\"","1e9999","true false"};
    for(size_t i=0;i<sizeof bad/sizeof bad[0];i++){assert(!apx_json_parse(bad[i],strlen(bad[i])));apx_json_reset();}
    const char *maximum="18446744073709551615";ApxValue *value=apx_json_parse(maximum,strlen(maximum));uint64_t number=0;
    assert(apx_json_unsigned(value,&number)&&number==UINT64_MAX);apx_json_reset();
    value=apx_json_parse("1.0",3);assert(!apx_json_unsigned(value,&number));apx_json_reset();
    const char *subnormal[]={"5e-324","-5e-324","2.225073858507201e-308","1e-10000"};
    for(size_t i=0;i<sizeof subnormal/sizeof subnormal[0];i++){
        ApxValue *number_value=apx_json_parse(subnormal[i],strlen(subnormal[i]));assert(number_value&&apx_json_kind(number_value)==3);
        char *json=apx_json_encode(number_value);assert(json);free(json);apx_json_reset();
    }
    puts("native C support: SHA-256 vectors, strict JSON, exact u64 limits, subnormal/underflow finite floats PASS");
    return 0;
}
