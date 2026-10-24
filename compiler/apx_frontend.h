#ifndef APX_FRONTEND_H
#define APX_FRONTEND_H
#include <stddef.h>
#include <stdint.h>
#include <setjmp.h>

/* Public libc-only API. Outputs are initialized to NULL and are malloc-owned.
 * A NULL il_out requests parse + semantic checking only (no main ABI check).
 * Successful parsing supplies canonical_out even if semantic checking fails.
 * Format is parse-only. No source is executed by either entrypoint. */
int apx_compile_source(const char *source, char **il_out, char **canonical_out,
                       char **diagnostic_json_out);
/* Check all recoverable semantic errors after a valid parse. Returns 0 on
 * success, 1 on errors. Diagnostics is always a malloc-owned JSON array
 * ("[]" on success); canonical is available only after successful parsing.
 * No code is emitted or executed. Compile/describe retain their first error.
 * More than 4096 diagnostics is an explicit P006 resource failure, not a
 * truncated collection presented as complete. */
int apx_check_source(const char *source, char **canonical_out,
                     char **diagnostics_json_array_out);
/* Native source tests do not depend on a host/JSON-compatible main. */
int apx_compile_tests(const char *source, char **il_out, char **canonical_out,
                       char **diagnostic_json_out);
char *apx_format_source(const char *source, char **diagnostic_json_out);
/* Checked, deterministic metadata. NULL outputs skip unneeded projections. */
int apx_describe_source(const char *source, char **explain_out, char **contract_out,
                        char **pseudocode_out, char **diagnostic_json_out);

/* Internal checked AST shared only with the native QBE emitter. */
#define APX_MAX_SOURCE 65536u
#define APX_MAX_DEPTH 64u
#define APX_MAX_DIAGNOSTICS 4096u
#define APX_LANGUAGE_VERSION "0.1.11"
typedef struct ApxContext ApxContext;
typedef struct ApxType ApxType;
typedef struct ApxExpr ApxExpr;
typedef struct ApxArg ApxArg;
typedef struct ApxField ApxField;
typedef struct ApxDecl ApxDecl;
typedef struct ApxVariant ApxVariant;
typedef struct ApxStmt ApxStmt;
typedef struct ApxArm ApxArm;
typedef struct ApxBinding ApxBinding;
typedef struct ApxFunction ApxFunction;
typedef struct ApxProgram ApxProgram;
typedef struct ApxAllocation ApxAllocation;
typedef struct ApxRecovery ApxRecovery;
typedef struct ApxDiagnostic ApxDiagnostic;
typedef struct { size_t start, end; } ApxSpan;
typedef enum { APX_I64, APX_F64, APX_BOOL, APX_STR, APX_ARRAY,
               APX_OPTION, APX_RESULT, APX_BUILDER, APX_NAMED,
               APX_MAP, APX_ERROR } ApxTypeKind; /* Internal poison; never reaches codegen. */
struct ApxType { ApxTypeKind kind; ApxType *a, *b; char *name; ApxSpan span; };
typedef enum { APX_INT_EXPR, APX_FLOAT_EXPR, APX_BOOL_EXPR, APX_STRING_EXPR,
               APX_VAR_EXPR, APX_ARRAY_EXPR, APX_CALL_EXPR, APX_HOLE_EXPR,
               APX_TYPE_EXPR } ApxExprKind;
struct ApxArg { char *name; ApxExpr *expr; ApxArg *next; };
struct ApxExpr {
    ApxExprKind kind; ApxSpan span; ApxType *type, *marker;
    char *text; size_t length; int64_t integer; double real;
    unsigned syntax_height; /* Includes lowered postfix/sugar nodes in depth budget. */
    ApxArg *args; size_t count; unsigned binding;
    int opcode; ApxFunction *function; ApxDecl *record;
};
struct ApxField { char *name; ApxType *type; ApxSpan span; unsigned binding; ApxField *next; };
struct ApxVariant { char *name; ApxField *fields; ApxSpan span; ApxVariant *next; };
struct ApxDecl {
    char *name; int is_enum; ApxField *fields; ApxVariant *variants;
    ApxSpan span, name_span; int visit; unsigned height; ApxDecl *next;
};
struct ApxBinding { char *name; ApxType *type; unsigned id, depth; ApxBinding *next; };
struct ApxArm { char *name; ApxField *bindings; ApxStmt *body; ApxSpan span; ApxArm *next; };
struct ApxStmt {
    char kind; char *name; ApxType *annotation; ApxExpr *expr;
    ApxStmt *body, *next; ApxArm *arms; ApxSpan span; unsigned binding;
};
struct ApxFunction {
    char *name; ApxField *params; ApxType *returns; ApxStmt *body;
    ApxSpan span, name_span; unsigned index, bindings; int is_test; ApxFunction *next;
};
struct ApxProgram {
    char *module, *canonical; char source_hash[70];
    ApxDecl *types; ApxFunction *functions, *tests;
};
struct ApxContext {
    const char *source; size_t source_length, allocated;
    ApxAllocation *allocations; jmp_buf escape; int failed;
    const char *error_code, *error_phase, *error_kind, *error_message;
    ApxSpan error_span; ApxType *error_expected; ApxBinding *error_scope;
    int collect_diagnostics;
    ApxRecovery *recovery;
    ApxDiagnostic *diagnostics, **diagnostics_tail;
    size_t diagnostic_count;
};
typedef struct { ApxContext *ctx; char *data; size_t length, capacity; } ApxBuffer;
void *apx_alloc(ApxContext *ctx, size_t size);
char *apx_copy(ApxContext *ctx, const char *source, size_t size);
void apx_buf_init(ApxBuffer *buffer, ApxContext *ctx);
void apx_buf_put(ApxBuffer *buffer, const char *text);
void apx_buf_bytes(ApxBuffer *buffer, const char *text, size_t length);
void apx_buf_printf(ApxBuffer *buffer, const char *format, ...);
void apx_json_quote(ApxBuffer *buffer, const char *text, size_t length);
void apx_type_text(ApxBuffer *buffer, const ApxType *type);
void apx_fail(ApxContext *ctx, const char *code, const char *phase,
              const char *kind, ApxSpan span, const char *message);
ApxDecl *apx_find_decl(ApxProgram *program, const char *name);
ApxFunction *apx_find_function(ApxProgram *program, const char *name);
int apx_builtin_opcode(const char *name);
void apx_metadata_render(ApxContext *context, ApxProgram *program,
                          char **explain_out, char **contract_out, char **pseudocode_out);
#endif
