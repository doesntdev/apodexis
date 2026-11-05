/* Optional native measurement harness, not linked into installed Apodexis.
 * Compile the same TU against each selected runtime source. JSON setup,
 * encoding and reset are included in pipeline mode; core mode separately
 * records only generated entrypoint work and cleanup. No source evaluator. */
#define _POSIX_C_SOURCE 200809L
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/resource.h>

#ifdef APX_BENCH_COUNTERS
static uint64_t bench_calloc_calls, bench_calloc_bytes, bench_free_calls;
static uint64_t bench_copy_bytes, bench_push_copy_bytes;
static int bench_in_push;
static void *bench_calloc(size_t n,size_t size){
    bench_calloc_calls++;bench_calloc_bytes+=(uint64_t)n*size;return calloc(n,size);
}
static void bench_free(void *p){if(p)bench_free_calls++;free(p);}
static void *bench_memcpy(void *dst,const void *src,size_t n){
    bench_copy_bytes+=n;if(bench_in_push)bench_push_copy_bytes+=n;
    return memcpy(dst,src,n);
}
#define calloc bench_calloc
#define free bench_free
#define memcpy bench_memcpy
#define apx_builtin bench_builtin_inner
#endif
#define APX_LIBRARY_ONLY
#ifndef APX_RUNTIME_SOURCE
#error "Select the runtime source explicitly with APX_RUNTIME_SOURCE"
#endif
#include APX_RUNTIME_SOURCE
#ifdef APX_BENCH_COUNTERS
#undef calloc
#undef free
#undef memcpy
#undef apx_builtin
ApxValue *apx_builtin(int32_t op,ApxValue *a,ApxValue *b){
    int previous=bench_in_push;bench_in_push=op==20;
    ApxValue *out=bench_builtin_inner(op,a,b);bench_in_push=previous;return out;
}
#endif

static uint64_t core_ns, cleanup_ns, expected_fuel, peak_payload;
static int have_fuel;
static uint64_t now_ns(void){
    struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t)){perror("clock_gettime");exit(2);}
    return (uint64_t)t.tv_sec*UINT64_C(1000000000)+(uint64_t)t.tv_nsec;
}
static char *load(const char *path){
    FILE *f=fopen(path,"rb");if(!f){perror(path);exit(2);}
    if(fseek(f,0,SEEK_END)){perror("fseek");exit(2);}long n=ftell(f);
    if(n<0||(unsigned long)n>APX_OUTPUT){fputs("invalid measurement file size\n",stderr);exit(2);}
    rewind(f);char *out=malloc((size_t)n+1);
    if(!out||fread(out,1,(size_t)n,f)!=(size_t)n||fclose(f)){fputs("measurement read failed\n",stderr);exit(2);}
    out[n]=0;return out;
}
static int invoke(const char *input,const char *expected,int core){
    fuel_used=0;fuel_limit=10000000;trap_target=&escape;
    if(setjmp(escape)){
        fprintf(stderr,"measurement trap %s/%s: %s\n",error_code,error_kind,error_message);
        reset_memory();return 0;
    }
    ApxValue *in=parse_complete(input,strlen(input));
    uint64_t start=core?now_ns():0;
    ApxValue *out=apx_entry(in);
    if(core)core_ns+=now_ns()-start;
    char *encoded=apx_json_encode(out);
    if(!encoded||strcmp(encoded,expected)){
        fprintf(stderr,"measurement output mismatch\nexpected: %s\nactual: %s\n",expected,encoded?encoded:"<encoding failed>");
        free(encoded);reset_memory();return 0;
    }
    free(encoded);
    if(have_fuel&&fuel_used!=expected_fuel){fputs("measurement fuel drift\n",stderr);reset_memory();return 0;}
    expected_fuel=fuel_used;have_fuel=1;
    if(memory_used>peak_payload)peak_payload=memory_used;
    start=core?now_ns():0;reset_memory();
    if(core)cleanup_ns+=now_ns()-start;
    return 1;
}
int main(int argc,char **argv){
    if(argc!=5){fputs("usage: repeat input.json expected.json repetitions {pipeline|core}\n",stderr);return 64;}
    char *end;errno=0;unsigned long reps=strtoul(argv[3],&end,10);
    if(errno||*end||!reps||reps>1000000)return 64;
    int core=!strcmp(argv[4],"core");if(!core&&strcmp(argv[4],"pipeline"))return 64;
    char *input=load(argv[1]),*expected=load(argv[2]);
    if(!invoke(input,expected,core)){free(input);free(expected);return 1;}
    core_ns=cleanup_ns=peak_payload=0;
#ifdef APX_BENCH_COUNTERS
    bench_calloc_calls=bench_calloc_bytes=bench_free_calls=0;
    bench_copy_bytes=bench_push_copy_bytes=0;
#endif
    uint64_t start=now_ns();
    for(unsigned long i=0;i<reps;i++)if(!invoke(input,expected,core)){free(input);free(expected);return 1;}
    uint64_t elapsed=now_ns()-start;
    struct rusage usage;if(getrusage(RUSAGE_SELF,&usage)){perror("getrusage");return 2;}
    printf("{\"schema\":\"apodexis.runtime_memory_sample.v1\",\"mode\":\"%s\",\"repetitions\":%lu,\"elapsed_ns\":%"PRIu64",\"core_ns\":%"PRIu64",\"cleanup_ns\":%"PRIu64",\"fuel_used\":%"PRIu64",\"peak_requested_payload_bytes\":%"PRIu64",\"peak_rss_kib\":%ld,\"source_hash\":\"%s\"",argv[4],reps,elapsed,core_ns,cleanup_ns,expected_fuel,peak_payload,usage.ru_maxrss,apx_source_hash);
#ifdef APX_BENCH_COUNTERS
    printf(",\"instrumented\":true,\"calloc_calls\":%"PRIu64",\"calloc_requested_bytes\":%"PRIu64",\"free_calls\":%"PRIu64",\"memcpy_bytes\":%"PRIu64",\"builder_prefix_memcpy_bytes\":%"PRIu64,bench_calloc_calls,bench_calloc_bytes,bench_free_calls,bench_copy_bytes,bench_push_copy_bytes);
#else
    fputs(",\"instrumented\":false",stdout);
#endif
    puts("}");free(input);free(expected);return 0;
}
