/* Private allocator acceptance. No testing API is exposed by the runtime. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static size_t calloc_calls, free_calls;
static int fail_next_calloc;
static void *tracked_calloc(size_t n,size_t size){
    calloc_calls++;
    if(fail_next_calloc){fail_next_calloc=0;return NULL;}
    return calloc(n,size);
}
static void tracked_free(void *p){if(p)free_calls++;free(p);}
static size_t copied_bytes;
static void *tracked_memcpy(void *dst,const void *src,size_t n){
    copied_bytes+=n;return memcpy(dst,src,n);
}
#define calloc tracked_calloc
#define free tracked_free
#define memcpy tracked_memcpy
#define APX_LIBRARY_ONLY
#ifndef APX_RUNTIME_SOURCE
#define APX_RUNTIME_SOURCE "../runtime/apx_runtime.c"
#endif
#include APX_RUNTIME_SOURCE
#undef calloc
#undef free
#undef memcpy

static unsigned checks;
static void require(int ok,const char *message){
    checks++;if(!ok){fprintf(stderr,"runtime memory FAIL: %s\n",message);exit(1);}
}
static void expect_limit(size_t request){
    jmp_buf local;jmp_buf *previous=trap_target;trap_target=&local;
    if(!setjmp(local)){(void)allocate(request);require(0,"expected structured memory exhaustion");}
    else require(!strcmp(error_code,"R204")&&!strcmp(error_kind,"memory_limit_exceeded"),"structured R204 exhaustion");
    trap_target=previous;
}
static void storage(void){
    enum { COUNT=4096 };
    struct Saved {unsigned char *bytes;size_t length;unsigned char pattern;};
    static struct Saved saved[COUNT];
    struct ActualAlignment {char first;ApxValue value;};
    size_t expected=0;calloc_calls=free_calls=0;fuel_used=91;call_depth=3;
    for(size_t i=0;i<COUNT;i++){
        size_t n=i%79;unsigned char *p=allocate(n);expected+=n;
        require(p!=NULL&&(uintptr_t)p%offsetof(struct ActualAlignment,value)==0,"aligned valid storage");
        for(size_t j=0;j<n;j++)require(p[j]==0,"zero-initialized bytes");
        unsigned char pattern=(unsigned char)(i%251+1);memset(p,pattern,n);
        saved[i].bytes=p;saved[i].length=n;saved[i].pattern=pattern;
#ifdef APX_ARENA_ASAN
        require(__asan_address_is_poisoned(p+(n?n:1)),"logical object boundary poisoned");
#endif
    }
    require(memory_used==expected,"requested payload accounting excludes padding and zero-sized slots");
    for(size_t i=0;i<COUNT;i++)for(size_t j=0;j<saved[i].length;j++)
        require(saved[i].bytes[j]==saved[i].pattern,"earlier bytes stable across growth");
    size_t calls=calloc_calls;reset_memory();
    require(memory_used==0&&call_depth==0&&fuel_used==91,"reset preserves fuel and clears storage/depth");
    require(calloc_calls==free_calls,"all backing allocations freed at reset");
    require(calls<COUNT/4,"chunk-level system allocation, not one calloc per value");
#ifdef APX_RUNTIME_ARENA
    require(!arena_chunks&&!arena_current&&!arena_backing&&arena_next_capacity==APX_ARENA_INITIAL,"arena reset has no retained chunks");
#endif
}
static void limits(void){
    expect_limit(SIZE_MAX);require(memory_used==0,"overflow does not consume quota");
    unsigned char *large=allocate(APX_MEMORY-1);require(large[0]==0&&large[APX_MEMORY-2]==0,"large allocation remains zeroed");
    (void)allocate(1);require(memory_used==APX_MEMORY,"exact logical quota accepted");
    expect_limit(1);reset_memory();
    fail_next_calloc=1;expect_limit(8);
    require(memory_used==0,"failed backing allocation does not consume payload quota");
#ifdef APX_RUNTIME_ARENA
    require(!arena_chunks&&!arena_backing,"failed allocation does not publish a chunk");
    /* Exercise the backing guard directly without allocating the entire budget. */
    arena_backing=APX_ARENA_BACKING;
    size_t before=calloc_calls;expect_limit(8);
    require(calloc_calls==before,"backing limit checked before system allocation");
    arena_backing=0;
#endif
    reset_memory();
    ApxValue *v=apx_int(17);char *encoded=apx_json_encode(v);
    require(encoded&&!strcmp(encoded,"17"),"successful execution after allocation failures");
    reset_memory();require(!strcmp(encoded,"17"),"caller-owned encoding survives reset");free(encoded);
}
static void builder_values(ApxValue *builder,size_t prefix,int64_t suffix,int appended){
    ApxValue *one=apx_builtin(21,builder,NULL),*two=apx_builtin(21,builder,NULL);
    require(one->kind==VARRAY&&one->length==prefix+(size_t)appended,"finished snapshot length");
    require(one->items==two->items,"repeat finish reuses immutable materialization");
    for(size_t i=0;i<prefix;i++)require(integer(apx_at_raw(one,(int64_t)i))==(int64_t)i,"snapshot prefix retains original order");
    if(appended)require(integer(apx_at_raw(one,(int64_t)prefix))==suffix,"branch has its own appended value");
}
static void builder_failure(int32_t op,ApxValue *builder,ApxValue *item){
    jmp_buf local;jmp_buf *previous=trap_target;trap_target=&local;fail_next_calloc=1;
    if(!setjmp(local)){(void)apx_builtin(op,builder,item);require(0,"expected builder allocation failure");}
    else require(!strcmp(error_code,"R204")&&!strcmp(error_kind,"memory_limit_exceeded")&&!fail_next_calloc,
                 "builder fails through the injected backing allocator");
    trap_target=previous;
}
static void builder_recovery(ApxValue *prefix){
    ApxValue *item=apx_int(-3);
    struct BuilderBlock *tail=prefix->members.builder_tail;
    size_t used=tail->used;
    /* Fail the new snapshot allocation before any shared block can be changed. */
    arena_current=NULL;builder_failure(20,prefix,item);
    require(tail->used==used,"failed snapshot allocation leaves shared frontier intact");
    builder_values(prefix,64,0,0);
    /* Let the value wrapper fit, then fail a required new full-size tail block. */
    arena_current=NULL;(void)allocate(0);
    builder_failure(20,prefix,item);
    require(tail->used==used,"failed tail allocation leaves shared frontier intact");
    builder_values(prefix,64,0,0);
    ApxValue *retry=apx_builtin(20,prefix,item);
    require(!retry->items,"new builder materialization is initially absent");
    arena_current=NULL;(void)allocate(0);
    builder_failure(21,retry,NULL);
    require(!retry->items,"failed finish does not publish a partial cache");
    builder_values(retry,64,-3,1);
    builder_values(prefix,64,0,0);
}
static void builders(void){
    /* The representation must not add a word to every scalar/record value. */
    struct OriginalValueLayout {
        enum Kind kind;int64_t integer;double real;const char *text;
        size_t length,capacity;ApxValue **items;const char **names;size_t *key_lengths;
    };
    require(sizeof(ApxValue)==sizeof(struct OriginalValueLayout),"builder storage does not enlarge other values");
    ApxValue *versions[193];versions[0]=apx_builtin(19,apx_int(256),NULL);
    for(size_t i=0;i<192;i++){
        ApxValue *item=apx_int((int64_t)i);size_t before=copied_bytes;
        versions[i+1]=apx_builtin(20,versions[i],item);
        require(copied_bytes==before,"append does not copy previous prefix items");
        if(i%17==0)builder_values(versions[i+1],i+1,0,0);
    }
    /* Exercise both sides of full and partial block boundaries and old aliases. */
    const size_t prefixes[]={0,1,31,32,63,64,65,127,128,129,191,192};
    for(size_t i=0;i<sizeof prefixes/sizeof prefixes[0];i++){
        size_t n=prefixes[i];ApxValue *original=versions[n];
        ApxValue *left=apx_builtin(20,original,apx_int(-1));
        ApxValue *right=apx_builtin(20,original,apx_int(-2));
        builder_values(original,n,0,0);builder_values(left,n,-1,1);builder_values(right,n,-2,1);
        require(integer(apx_builtin(22,original,NULL))==(int64_t)n,"count is snapshot-local");
        require(integer(apx_builtin(23,right,NULL))==256,"capacity survives branches");
        require(apx_truth(apx_builtin(6,original,versions[n])),"generic equality handles builder representation");
        require(!apx_truth(apx_builtin(6,left,right)),"generic equality distinguishes divergent builder values");
    }
    builder_recovery(versions[64]);
    builder_values(versions[192],192,0,0);reset_memory();
}
int main(int argc,char **argv){
    if(argc==2&&!strcmp(argv[1],"--overrun")){
        volatile unsigned char *p=allocate(1);p[1]=7;return 0;
    }
    if(argc==2&&!strcmp(argv[1],"--after-reset")){
        volatile unsigned char *p=allocate(1);reset_memory();*p=7;return 0;
    }
    require(argc==1,"valid test arguments");storage();limits();builders();
    printf("runtime memory: %u checks; alignment/zeroing/stability/chunking/quota/backing/reset/recovery/builder branching PASS\n",checks);
    return 0;
}
