#ifndef APX_RUNTIME_H
#define APX_RUNTIME_H
#include <stdint.h>
#include <stddef.h>
typedef struct ApxValue ApxValue;
ApxValue *apx_int(int64_t value);
ApxValue *apx_float(double value);
ApxValue *apx_bool(int32_t value);
ApxValue *apx_string(const char *bytes, int64_t length);
ApxValue *apx_array_new(int64_t count);
/* Generated i64 collection ABI. Put is construction-only, before publication.
 * Both packed and legacy arrays support allocation-free typed reads. */
ApxValue *apx_array_i64_new(int64_t count);
void apx_array_i64_put(ApxValue *array, int64_t index, int64_t value);
int64_t apx_array_i64_at(ApxValue *array, int64_t index);
/* Internal only: whole-binding nonaliasing proof is required for mutation. */
ApxValue *apx_builder_i64_private_new(int64_t capacity);
ApxValue *apx_builder_i64_private_push(ApxValue *builder, int64_t value);
void apx_array_put(ApxValue *array, int64_t index, ApxValue *value);
ApxValue *apx_record_new(const char *name, int64_t count);
void apx_record_put(ApxValue *record, int64_t index, const char *name, ApxValue *value);
ApxValue *apx_variant(const char *tag, ApxValue *payload);
int32_t apx_tag_is(ApxValue *value, const char *tag);
ApxValue *apx_payload(ApxValue *value);
ApxValue *apx_builtin(int32_t opcode, ApxValue *left, ApxValue *right);
ApxValue *apx_builtin3(int32_t opcode, ApxValue *left, ApxValue *right, ApxValue *third);
ApxValue *apx_record_copy(ApxValue *record);
ApxValue *apx_field(ApxValue *value, const char *name);
/* Internal checked-named-record ABI: declaration-order, bounds-checked read. */
ApxValue *apx_field_index(ApxValue *value, int64_t index);
int32_t apx_truth(ApxValue *value);
/* Internal generated scalar ABI. Generic/public entrypoints remain boxed. */
int64_t apx_i64_raw(ApxValue *value);
int64_t apx_i64_checked(int32_t opcode, int64_t left, int64_t right);
void apx_assert_raw(int32_t condition);
int64_t apx_length_raw(ApxValue *array);
ApxValue *apx_at_raw(ApxValue *array, int64_t index);
/* Trusted internal amd64_sysv execution-state ABI shared by C and QBE.
 * Align 8, size 24: u64 limit at 0, u64 used at 8, i32 depth at 16,
 * reserved zero word at 20. One serial execution, not thread-local/reentrant.
 * Memory reset clears depth only; invocation drivers own fuel initialization. */
#define APX_DEPTH 256
#define APX_EXEC_LIMIT_OFFSET 0
#define APX_EXEC_USED_OFFSET 8
#define APX_EXEC_DEPTH_OFFSET 16
typedef struct {
    uint64_t limit, used;
    int32_t depth;
    uint32_t reserved;
} ApxExecutionState;
extern ApxExecutionState apx_execution_state;
struct ApxExecutionAlignment { char byte; ApxExecutionState state; };
typedef char ApxExecutionABI[
    sizeof(ApxExecutionState)==24 && offsetof(struct ApxExecutionAlignment,state)==8 &&
    offsetof(ApxExecutionState,limit)==APX_EXEC_LIMIT_OFFSET &&
    offsetof(ApxExecutionState,used)==APX_EXEC_USED_OFFSET &&
    offsetof(ApxExecutionState,depth)==APX_EXEC_DEPTH_OFFSET &&
    offsetof(ApxExecutionState,reserved)==20 ? 1 : -1];
/* Cold generated-code failures never return, including under trap recovery. */
#if defined(__GNUC__) || defined(__clang__)
#define APX_NORETURN __attribute__((noreturn))
#else
#define APX_NORETURN
#endif
APX_NORETURN void apx_fuel_exhausted(void);
APX_NORETURN void apx_depth_exhausted(void);
void apx_charge(int64_t amount);
void apx_assert(ApxValue *condition);
void apx_trap(const char *message);
void apx_trap_value(ApxValue *message);
void apx_missing_return(void);
void apx_enter(void);
void apx_leave(void);
ApxValue *apx_validate(const char *schema_json, ApxValue *input);
/* Internal readonly C/QBE schema ABI (amd64_sysv, not a serialization format).
 * Six 8-byte slots, align 8, size 48; fields are pairs of pointers, size 16.
 * Generated descriptors and names have artifact lifetime, never arena lifetime.
 * Only trusted, addressable descriptor storage may be passed to this seam.
 * Counts/kinds/missing children and recursive depth are defensively bounded. */
typedef enum {
    APX_SCHEMA_NULL=0, APX_SCHEMA_I64=1, APX_SCHEMA_F64=2,
    APX_SCHEMA_BOOL=3, APX_SCHEMA_STR=4, APX_SCHEMA_ARRAY=5,
    APX_SCHEMA_MAP=6, APX_SCHEMA_OPTION=7, APX_SCHEMA_RESULT=8,
    APX_SCHEMA_STRUCT=9
} ApxSchemaKind;
typedef struct ApxSchema ApxSchema;
typedef struct { const char *name; const ApxSchema *type; } ApxSchemaField;
struct ApxSchema {
    uint64_t kind;
    const char *name;
    uint64_t count;
    const ApxSchema *item, *error;
    const ApxSchemaField *fields;
};
ApxValue *apx_validate_static(const ApxSchema *schema, ApxValue *input);
/* Safe JSON access used by the standalone compiler/MCP transport. Returned
 * JSON values live until apx_json_reset; encoded strings are caller-owned. */
ApxValue *apx_json_parse(const char *text, size_t length);
char *apx_json_encode(ApxValue *value);
ApxValue *apx_json_get(ApxValue *value, const char *name);
/* NULL for wrong kind, bounds or compatibility-box allocation failure.
 * Successful items are stable arena-owned values until apx_json_reset. */
ApxValue *apx_json_item(ApxValue *value, size_t index);
int apx_json_kind(ApxValue *value); /* null=0,bool=1,int=2,float=3,str=4,array=5,object=6 */
const char *apx_json_string(ApxValue *value);
size_t apx_json_length(ApxValue *value);
int64_t apx_json_integer(ApxValue *value);
int apx_json_unsigned(ApxValue *value, uint64_t *out);
const char *apx_json_key(ApxValue *value, size_t index);
void apx_json_reset(void);
/* QBE-generated entrypoints and metadata; no AST interpreter is linked. */
extern ApxValue *apx_entry(ApxValue *input);
extern int64_t apx_test_count(void);
extern const char *apx_test_name(int64_t index);
extern ApxValue *apx_test_run(int64_t index);
extern const char apx_schema_json[];
extern const char apx_source_hash[];
extern const char apx_language_version[];
#endif
