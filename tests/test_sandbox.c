/* Hostile ABI smoke proves kernel restrictions on a generated native worker.
 * Compiled only as a test entry; Apodexis source cannot express these syscalls. */
#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "../runtime/apx_runtime.h"
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <unistd.h>
const char apx_source_hash[]="apx1:sandbox-test";
const char apx_language_version[]="0.1.11";
const char apx_schema_json[]="null";
int64_t apx_test_count(void){return 0;}
const char *apx_test_name(int64_t i){(void)i;return "sandbox";}
ApxValue *apx_test_run(int64_t i){(void)i;return NULL;}
ApxValue *apx_entry(ApxValue *input){(void)input;int checks=0;
    errno=0;if(open("/etc/passwd",O_RDONLY)==-1&&errno==EPERM)checks++;
    errno=0;if(socket(AF_INET,SOCK_STREAM,0)==-1&&errno==EPERM)checks++;
    errno=0;if(fork()==-1&&errno==EPERM)checks++;
    errno=0;if(mmap(NULL,4096,PROT_READ|PROT_EXEC,MAP_PRIVATE|MAP_ANONYMOUS,-1,0)==MAP_FAILED&&errno==EPERM)checks++;
    errno=0;if(mmap(NULL,4096,PROT_READ,MAP_PRIVATE,0,0)==MAP_FAILED&&errno==EPERM)checks++;
    errno=0;if(write(0,"bad",3)==-1&&errno==EPERM)checks++;
    errno=0;char byte;if(read(1,&byte,1)==-1&&errno==EPERM)checks++;
    void *data=mmap(NULL,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
    if(data!=MAP_FAILED){checks++;munmap(data,4096);}
    if(checks!=8)apx_trap("native kernel restriction regression");
    return apx_int(checks);
}
