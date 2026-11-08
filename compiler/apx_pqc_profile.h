#ifndef APX_PQC_PROFILE_H
#define APX_PQC_PROFILE_H
#include <stddef.h>
/* Returns 1 for the profile, 0 malformed/unsupported, -1 content/DER quota.
 * Validation only: RFC5083/9629/9936 restricted standard CMS profile. */
int apx_pqc_profile(const unsigned char *der,size_t length,const unsigned char key_id[32],size_t maximum);
#endif
