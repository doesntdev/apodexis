#ifndef APX_HOST_H
#define APX_HOST_H
#include <stddef.h>
typedef struct ApxHostPolicy ApxHostPolicy;
/* Operator-owned, explicit policy; never populated from an authored program.
 * Roots are held as directory descriptors; values returned are malloc-owned. */
ApxHostPolicy *apx_host_policy_load(const char *path,char **error);
void apx_host_policy_free(ApxHostPolicy *policy);
char *apx_host_dispatch(ApxHostPolicy *policy,const char *request,size_t length);
/* Serve one bounded, newline-delimited request on an inherited socket. */
int apx_host_serve(ApxHostPolicy *policy,int socket_fd);
#endif
