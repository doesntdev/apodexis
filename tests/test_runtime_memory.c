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
#define calloc tracked_calloc
#define free tracked_free
#define APX_LIBRARY_ONLY
#ifndef APX_RUNTIME_SOURCE
#define APX_RUNTIME_SOURCE "../runtime/apx_runtime.c"
#endif
#include APX_RUNTIME_SOURCE
#undef calloc
#undef free

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
int main(int argc,char **argv){
    if(argc==2&&!strcmp(argv[1],"--overrun")){
        volatile unsigned char *p=allocate(1);p[1]=7;return 0;
    }
    if(argc==2&&!strcmp(argv[1],"--after-reset")){
        volatile unsigned char *p=allocate(1);reset_memory();*p=7;return 0;
    }
    require(argc==1,"valid test arguments");storage();limits();
    printf("runtime memory: %u checks; alignment/zeroing/stability/chunking/quota/backing/reset/recovery PASS\n",checks);
    return 0;
}
