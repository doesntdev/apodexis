#ifndef APX_QBE_H
#define APX_QBE_H
#include "apx_frontend.h"
/* Arena-owned IL. Consumes only an already checked AST; may report ABI errors. */
char *apx_qbe_emit(ApxContext *context, ApxProgram *program);
char *apx_qbe_emit_tests(ApxContext *context, ApxProgram *program);
#endif
