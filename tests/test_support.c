/* Self-contained developer acceptance; no Python/Rust runner needed. */
#include "../compiler/apx_hash.h"
#include "../runtime/apx_runtime.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void expect_json(ApxValue *value,const char *expected){
    assert(value);char *json=apx_json_encode(value);
    assert(json&&!strcmp(json,expected));free(json);
}
static void json_ownership(void){
    enum { GROWTHS=6 };
    const size_t sizes[GROWTHS]={0,1,31,1024,8192,65536};
    const char *mixed="{\"empty\":[],\"flag\":true,\"none\":null,\"object\":{},\"text\":\"é🙂\",\"values\":[false,-7,1.5,{\"tag\":\"some\",\"value\":[1,2]}]}";
    const char *prefix="{\"mix\":[null,true,false,-7,1.5,[],{},{\"tag\":\"some\",\"value\":\"é🙂\"}],\"payload\":\"";
    const char *suffix="\"}";
    ApxValue *initial=apx_json_parse(mixed,strlen(mixed));assert(initial);
    char *owned=apx_json_encode(initial);assert(owned&&!strcmp(owned,mixed));
    apx_json_reset();assert(!strcmp(owned,mixed));
    for(unsigned cycle=0;cycle<4;cycle++){
        ApxValue *early=apx_json_parse(mixed,strlen(mixed));assert(early);
        const char *early_text=apx_json_string(apx_json_get(early,"text"));assert(early_text);
        ApxValue *roots[GROWTHS];char *expected[GROWTHS],*encoded[GROWTHS];
        for(size_t i=0;i<GROWTHS;i++){
            size_t start=strlen(prefix),length=start+sizes[i]+strlen(suffix);
            expected[i]=malloc(length+1);assert(expected[i]);
            memcpy(expected[i],prefix,start);
            memset(expected[i]+start,'a'+(int)cycle+(int)i,sizes[i]);
            memcpy(expected[i]+start+sizes[i],suffix,strlen(suffix)+1);
            roots[i]=apx_json_parse(expected[i],length);assert(roots[i]);
            assert(apx_json_length(apx_json_get(roots[i],"payload"))==sizes[i]);
            encoded[i]=apx_json_encode(roots[i]);assert(encoded[i]&&!strcmp(encoded[i],expected[i]));
            /* Later growth must preserve every earlier DOM and borrowed string. */
            for(size_t j=0;j<=i;j++)expect_json(roots[j],expected[j]);
            expect_json(early,mixed);assert(!strcmp(early_text,"é🙂"));
            assert(!strcmp(owned,mixed));
        }
        apx_json_reset();
        /* Only caller-owned encodings, never DOM pointers, are used after reset. */
        for(size_t i=0;i<GROWTHS;i++){
            assert(!strcmp(encoded[i],expected[i]));free(encoded[i]);free(expected[i]);
        }
        assert(!strcmp(owned,mixed));
    }
    free(owned);
}
int main(void){
    char hash[65];
    apx_sha256("",0,hash);assert(!strcmp(hash,"e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    apx_sha256("abc",3,hash);assert(!strcmp(hash,"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    const char *cases[]={"null","true","false","[1,2,3]","\"é🙂\"","{\"b\":2,\"a\":1}","18446744073709551615"};
    const char *expected[]={"null","true","false","[1,2,3]","\"é🙂\"","{\"a\":1,\"b\":2}","18446744073709551615"};
    for(size_t i=0;i<sizeof cases/sizeof cases[0];i++){
        ApxValue *value=apx_json_parse(cases[i],strlen(cases[i]));assert(value);char *json=apx_json_encode(value);assert(json&&!strcmp(json,expected[i]));apx_json_reset();assert(!strcmp(json,expected[i]));free(json);
    }
    const char *bad[]={"{\"a\":1,\"a\":2}","[1,]","01","\"\\ud800\"","\"\xc0\x80\"","1e9999","true false","{\"partial\":[{\"text\":\"allocated\",\"values\":[1,true,null]},"};
    const char *recovery="{\"ok\":[null,true,3,\"recovered\",[],{}]}";
    for(size_t i=0;i<sizeof bad/sizeof bad[0];i++){
        ApxValue *earlier=apx_json_parse(recovery,strlen(recovery));assert(earlier);
        assert(!apx_json_parse(bad[i],strlen(bad[i])));
        expect_json(earlier,recovery);
        expect_json(apx_json_parse(recovery,strlen(recovery)),recovery);
        apx_json_reset();
        expect_json(apx_json_parse(recovery,strlen(recovery)),recovery);
        apx_json_reset();
    }
    const char *maximum="18446744073709551615";ApxValue *value=apx_json_parse(maximum,strlen(maximum));uint64_t number=0;
    assert(apx_json_unsigned(value,&number)&&number==UINT64_MAX);apx_json_reset();
    value=apx_json_parse("1.0",3);assert(!apx_json_unsigned(value,&number));apx_json_reset();
    const char *subnormal[]={"5e-324","-5e-324","2.225073858507201e-308","1e-10000"};
    for(size_t i=0;i<sizeof subnormal/sizeof subnormal[0];i++){
        ApxValue *number_value=apx_json_parse(subnormal[i],strlen(subnormal[i]));assert(number_value&&apx_json_kind(number_value)==3);
        char *json=apx_json_encode(number_value);assert(json);free(json);apx_json_reset();
    }
    json_ownership();
    puts("native C support: SHA-256 vectors, strict JSON, exact u64 limits, subnormal/underflow finite floats, JSON ownership/growth/reset/recovery PASS");
    return 0;
}
