#ifndef APX_HOST_H
#define APX_HOST_H
#include <signal.h>
#include <stdint.h>
#include <sys/types.h>
#include <stddef.h>
typedef struct ApxHostPolicy ApxHostPolicy;
/* Operator-owned, explicit policy; never populated from an authored program.
 * Roots are held as directory descriptors; values returned are malloc-owned. */
ApxHostPolicy *apx_host_policy_load(const char *path,char **error);
/* Serving-only operator snapshot. Credentials are a strict JSON array of
 * HTTP-grant-bound private file references, never guest-supplied arguments.
 * Application crypto grants are deliberately unavailable in this mode. */
ApxHostPolicy *apx_host_policy_load_serving(const char *path,
                                          const char *credentials_json,
                                          char **error);
void apx_host_policy_free(ApxHostPolicy *policy);
char *apx_host_dispatch(ApxHostPolicy *policy,const char *request,size_t length);
/* Serve one bounded, newline-delimited request on an inherited socket. */
/* Trusted CLI wiring only. Paths never come from source or MCP requests. */
void apx_host_helpers(const char *plain,const char *tls);
void apx_host_pqc_helper(const char *path);
char *apx_host_pqc_keygen(const char *directory);
void apx_host_cancel_signals(volatile sig_atomic_t *timeout,volatile sig_atomic_t *shutdown);
void apx_host_execution(ApxHostPolicy *policy,int64_t deadline,pid_t group,int pidfd,
                        volatile sig_atomic_t *timeout,volatile sig_atomic_t *shutdown);
int apx_host_serve(ApxHostPolicy *policy,int socket_fd);
#endif
