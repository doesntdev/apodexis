#ifndef APX_HASH_H
#define APX_HASH_H
#include <stddef.h>
void apx_sha256(const void *data, size_t length, char hexadecimal[65]);
#endif
