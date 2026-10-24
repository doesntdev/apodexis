#ifndef APX_UNICODE_H
#define APX_UNICODE_H
#include <stdint.h>
/* Pinned 0.1.11 canonical string escape classification, independent of locale.
 * Includes short-escape characters; callers select their spelling first.
 * Invalid Unicode scalars conservatively return 1. */
int apx_unicode_escape(uint32_t scalar);
#endif
