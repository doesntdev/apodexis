#ifndef APX_SERVE_H
#define APX_SERVE_H
#include <stdint.h>
#include <stddef.h>
#include "apx_host.h"
/* Private driver seam. Owned strings/fd, checked contract DTO only; no AST. */
typedef struct {
    int fd;
    size_t bytes;
    char source_hash[80], contract_hash[80];
    char *contract;
} ApxServeImage;
typedef struct { uint64_t fuel, input, output, timeout_ms; } ApxServeLimits;
typedef struct {
    int (*compile)(const char *path, ApxServeImage *image);
    char *(*invoke)(const ApxServeImage *image, ApxHostPolicy *policy,
                    const char *input, ApxServeLimits limits,
                    const char *owned_directory, int64_t deadline);
} ApxServeDriver;
int apx_serve(const char *manifest, const char *build_directory,
              const ApxServeDriver *driver);
#endif
