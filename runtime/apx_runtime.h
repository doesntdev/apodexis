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
int32_t apx_truth(ApxValue *value);
int64_t apx_length_raw(ApxValue *array);
ApxValue *apx_at_raw(ApxValue *array, int64_t index);
void apx_charge(int64_t amount);
void apx_assert(ApxValue *condition);
void apx_trap(const char *message);
void apx_trap_value(ApxValue *message);
void apx_missing_return(void);
void apx_enter(void);
void apx_leave(void);
ApxValue *apx_validate(const char *schema_json, ApxValue *input);
/* Safe JSON access used by the standalone compiler/MCP transport. Returned
 * JSON values live until apx_json_reset; encoded strings are caller-owned. */
ApxValue *apx_json_parse(const char *text, size_t length);
char *apx_json_encode(ApxValue *value);
ApxValue *apx_json_get(ApxValue *value, const char *name);
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
