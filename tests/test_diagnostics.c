/* Public C checker regression seam; JSON assertions use the native C library.
 * Run from the repository root. No compiler subprocess or source execution. */
#include "apx_frontend.h"
#include "../runtime/apx_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned tests;
static char *canonical, *text;
static ApxValue *diagnostics;
static void require(int ok, const char *name) {
    ++tests;
    if (!ok) { fprintf(stderr, "FAIL %s: %s\n", name, text ? text : "NULL diagnostics"); exit(1); }
}
static const char *string_field(ApxValue *v, const char *key) {
    const char *s = apx_json_string(apx_json_get(v, key)); return s ? s : "<null>";
}
static void checked(const char *source, size_t count, int parsed) {
    int status;
    free(canonical); free(text); canonical = NULL; text = NULL; apx_json_reset();
    status = apx_check_source(source, &canonical, &text);
    require(status == (count != 0), "checker status");
    require((canonical != NULL) == parsed, "parse identity availability");
    require(text != NULL, "JSON array output always supplied");
    diagnostics = apx_json_parse(text, strlen(text));
    require(diagnostics && apx_json_kind(diagnostics) == 5 && apx_json_length(diagnostics) == count,
            "exact diagnostic array length");
    if (!count) require(!strcmp(text, "[]"), "success is empty array");
}
static ApxValue *diag(size_t index, const char *code) {
    ApxValue *v = apx_json_item(diagnostics, index);
    require(v && !strcmp(string_field(v, "code"), code), "diagnostic code and order");
    return v;
}
static void span(ApxValue *v, int64_t start, int64_t end) {
    ApxValue *s = apx_json_get(v, "span");
    require(apx_json_integer(apx_json_get(s, "start_byte")) == start &&
            apx_json_integer(apx_json_get(s, "end_byte")) == end, "exact diagnostic bytes");
}
static void hole(size_t index, const char *expected, const char *const *names, size_t count) {
    ApxValue *v = diag(index, "H001"), *scope = apx_json_get(v, "in_scope");
    ApxValue *type = apx_json_get(v, "expected_type");
    require(expected ? !strcmp(string_field(v, "expected_type"), expected) : apx_json_kind(type) == 0,
            "hole expected type");
    require(scope && apx_json_length(scope) == count, "exact hole lexical scope");
    for (size_t i = 0; i < count; ++i)
        require(!strcmp(string_field(apx_json_item(scope, i), "name"), names[i]), "hole binding snapshot order");
}
static char *readall(const char *path) {
    FILE *file = fopen(path, "rb"); long n; char *data;
    if (!file) { fprintf(stderr, "cannot open fixture %s\n", path); exit(1); }
    require(!fseek(file, 0, SEEK_END), "fixture seek"); n = ftell(file); rewind(file);
    require(n >= 0 && n <= (long)APX_MAX_SOURCE, "bounded fixture");
    data = malloc((size_t)n + 1); require(data != NULL, "fixture allocation");
    require(fread(data, 1, (size_t)n, file) == (size_t)n && !fclose(file), "fixture read");
    data[n] = 0; return data;
}
static void first_error_apis(const char *source) {
    char *first = NULL, *il = NULL, *formatted = NULL, *explain = NULL, *contract = NULL, *pseudo = NULL;
    require(apx_compile_source(source, &il, &formatted, &first) == 1 && !il && formatted && first && first[0] == '{',
            "compile fail closed with original diagnostic object");
    char *encoded = apx_json_encode(apx_json_item(diagnostics, 0));
    ApxValue *first_value = apx_json_parse(first, strlen(first));
    char *first_encoded = first_value ? apx_json_encode(first_value) : NULL;
    require(encoded && first_encoded && !strcmp(first_encoded, encoded), "compile preserves exact first diagnostic");
    free(first_encoded); free(encoded); free(formatted); free(first); first = NULL;
    require(apx_compile_tests(source, &il, NULL, &first) == 1 && !il && first && first[0] == '{',
            "native source test compiler remains fail closed");
    free(first); first = NULL;
    require(apx_describe_source(source, &explain, &contract, &pseudo, &first) == 1 &&
            !explain && !contract && !pseudo && first && first[0] == '{', "describe remains first-error fail closed");
    free(first);
}
static void fixture_checks(void) {
    const char *const fixtures[] = {"bad-assignment-type", "bad-return-type", "builder-escaped", "duplicate-type",
        "missing-return", "typed-hole", "unknown-field", "unknown-symbol", "wrong-arg-count", "wrong-arg-type"};
    const char *const positions[] = {"start_byte", "end_byte", "start_line", "start_column", "end_line", "end_column"};
    for (size_t i = 0; i < sizeof fixtures / sizeof fixtures[0]; ++i) {
        char path[256]; snprintf(path, sizeof path, "fixtures/check/%s.apx", fixtures[i]);
        char *source = readall(path); checked(source, 1, 1);
        snprintf(path, sizeof path, "fixtures/check/%s.expected.json", fixtures[i]);
        char *wanted_text = readall(path); ApxValue *wanted = apx_json_parse(wanted_text, strlen(wanted_text));
        require(wanted != NULL, "expected fixture JSON");
        ApxValue *expected = apx_json_item(apx_json_get(wanted, "diagnostics"), 0);
        ApxValue *actual = apx_json_item(diagnostics, 0);
        require(!strcmp(string_field(actual, "code"), string_field(expected, "code")) &&
                !strcmp(string_field(actual, "kind"), string_field(expected, "kind")), "fixture code and kind unchanged");
        for (size_t j = 0; j < sizeof positions / sizeof positions[0]; ++j)
            require(apx_json_integer(apx_json_get(apx_json_get(actual, "span"), positions[j])) ==
                    apx_json_integer(apx_json_get(apx_json_get(expected, "span"), positions[j])), "fixture source locations unchanged");
        first_error_apis(source);
        free(wanted_text); free(source);
    }
    puts("native diagnostics: all 10 negative fixtures preserve code, kind, six locations and first-error APIs PASS");
}
int main(void) {
    const char *const seed_scope[] = {"seed"}, *const inner_scope[] = {"inner", "seed"};
    checked("M library\nF helper->i64\n  R 0\n", 0, 1);
    require(!strcmp(canonical, "M library\n\nF helper->i64\n  R 0\n"), "canonical success without main ABI");
    const char *statements = "M errors\nF main->i64\n  L one absent_one\n  L two absent_two\n  R 0\n";
    checked(statements, 2, 1); span(diag(0, "N101"), 29, 39); span(diag(1, "N101"), 48, 58);
    first_error_apis(statements);
    char *repeat = NULL; require(apx_check_source(statements, NULL, &repeat) == 1 && repeat && !strcmp(text, repeat), "deterministic diagnostic ordering"); free(repeat);
    checked("M x\nF main->i64\n  R (+ absent_one absent_two)\n", 2, 1);
    span(diag(0, "N101"), 23, 33); span(diag(1, "N101"), 34, 44);
    checked("M x\nF add a:i64 b:i64->i64\n  R (+ a b)\nF main->i64\n  R (add _ _)\n", 2, 1);
    hole(0, "i64", NULL, 0); hole(1, "i64", NULL, 0);
    checked("M x\nF main->i64\n  R (+ _ _)\n", 2, 1);
    hole(0, "i64", NULL, 0); hole(1, "i64", NULL, 0);
    checked("M x\nF main->[i64]\n  R [absent_one absent_two]\n", 2, 1); diag(0, "N101"); diag(1, "N101");
    checked("M x\nF main->bool\n  R (& absent _)\n", 2, 1); diag(0, "N101"); hole(1, "bool", NULL, 0);
    checked("M x\nF main->i64\n  R (absent absent_one _)\n", 3, 1); diag(0, "N101"); diag(1, "N101"); hole(2, NULL, NULL, 0);
    checked("M x\nF main->i64\n  R (+ named=absent)\n", 3, 1); diag(0, "T101"); diag(1, "N104"); diag(2, "N101");
    checked("M x\nF add a:i64 b:i64->i64\n  R (+ a b)\nF main->i64\n  R (add a=absent)\n", 3, 1); diag(0, "N104"); diag(1, "T101"); diag(2, "N101");
    checked("M x\nF main->i64\n  R (+ absent_one)\n", 2, 1); diag(0, "T101"); diag(1, "N101");
    checked("M x\nF add a:i64 b:i64->i64\n  R (+ a b)\nF main->i64\n  R (add a=absent b=_)\n", 3, 1);
    diag(0, "N104"); diag(1, "N101"); hole(2, "i64", NULL, 0); first_error_apis("M x\nF add a:i64 b:i64->i64\n  R (+ a b)\nF main->i64\n  R (add a=absent b=_)\n");
    checked("M x\nT Box struct\n  a:i64\n  b:bool\nF main->Box\n  R (Box a=absent b=_)\n", 2, 1); diag(0, "N101"); hole(1, "bool", NULL, 0);
    checked("M x\nT Box struct\n  a:i64\n  b:bool\nF main->Box\n  R (Box bad=absent worse=_)\n", 6, 1);
    diag(0, "N101"); diag(1, "N101"); diag(2, "N101"); hole(3, NULL, NULL, 0); diag(4, "A101"); diag(5, "A101");
    checked("M x\nF main->i64\n  S absent_target absent_value\n  R 0\n", 2, 1); diag(0, "N101"); diag(1, "N101");
    checked("M x\nF one a:MissingA->MissingR\n  R absent_one\nF two->i64\n  R absent_two\n", 4, 1);
    for (size_t i = 0; i < 4; ++i) diag(i, "N101");
    checked("M x\nT Bad struct\n  a:MissingA\n  b:MissingB\nF main bad:Bad->i64\n  R _\n", 3, 1);
    diag(0, "N101"); diag(1, "N101"); hole(2, "i64", NULL, 0);
    checked("M x\nT A struct\n  a:A\nT B struct\n  b:B\nF main->i64\n  R absent\n", 3, 1);
    diag(0, "T104"); diag(1, "T104"); diag(2, "N101");
    checked("M x\nF one->i64\n  A absent_one\nF two->i64\n  A absent_two\n", 4, 1);
    diag(0, "N101"); diag(1, "T105"); diag(2, "N101"); diag(3, "T105");
    checked("M x\nF main seed:i64->i64\n  L bad (+ missing seed)\n  ? true\n    L seed 2\n    L inner 3\n    L hole:i64 _\n  E item missing_array\n    L deep:i64 _\n  L outer:i64 _\n  R bad\n", 6, 1);
    diag(0, "N101"); diag(1, "N102"); hole(2, "i64", inner_scope, 2); diag(3, "N101");
    hole(4, "i64", seed_scope, 1); hole(5, "i64", seed_scope, 1);
    checked("M x\nT Box struct\n  v:i64\nF id n:i64->i64\n  R n\nF main seed:i64->i64\n  L bad absent\n  L call (id bad)\n  L box (Box v=bad)\n  L buf (buf i64 bad)\n  L opt:option i64 (some bad)\n  L hole:i64 _\n  R bad\n", 2, 1);
    diag(0, "N101"); hole(1, "i64", seed_scope, 1);
    checked("M x\nF main o:option i64->i64\n  C o\n    some a extra\n      L bad absent_one\n      R _\n    none\n      R absent_two\n  E item 7\n    L bad absent_three\n  R 0\n", 6, 1);
    diag(0, "N104"); diag(1, "N101"); diag(2, "H001"); diag(3, "N101"); diag(4, "T101"); diag(5, "N101");
    checked("M x\nF main->i64\n  C missing\n    any item\n      L one absent_one\n    other\n      L two absent_two\n  R 0\n", 3, 1); for (size_t i = 0; i < 3; ++i) diag(i, "N101");
    checked("M x\nF main seed:i64->i64\n  E seed [1]\n    L inside:i64 _\n  L outside:i64 _\n  R 0\n", 3, 1);
    diag(0, "N102"); hole(1, "i64", seed_scope, 1); hole(2, "i64", seed_scope, 1);
    checked("M x\nF main->i64\n  ? absent\n    L one absent_one\n  W 7\n    L two absent_two\n  R 0\n", 4, 1);
    diag(0, "N101"); diag(1, "N101"); diag(2, "T101"); diag(3, "N101");
    checked("M x\nX one\n  R absent_one\nX two\n  A absent_two\n", 3, 1); diag(0, "T101"); diag(1, "N101"); diag(2, "N101");
    checked("M x\nF main->i64\n  R (@ true absent)\n", 2, 1); diag(0, "N101"); diag(1, "T101");
    checked("M x\nF main->i64\n  S missing (some 1)\n  R 0\n", 1, 1); diag(0, "N101");
    checked("M x\nF main->i64\n  C absent\n    arm\n      L one missing_one\n    arm\n      L two missing_two\n  R 0\n", 4, 1);
    diag(0, "N101"); diag(1, "N101"); diag(2, "N102"); diag(3, "N101");
    checked("M x\nF main->i64\n  L bad (+ absent true)\n  R 0\n", 2, 1); diag(0, "N101"); diag(1, "T101");
    checked("M x\nT A struct\n  a:Missing\nT B struct\n  a:A\nF main b:B->i64\n  L v (. b a)\n  L hole:i64 _\n  R 0\n", 2, 1); diag(0, "N101"); hole(1, "i64", NULL, 0);
    checked("M x\nT A struct\n  a:i64\n  a:Missing\nT A struct\n  b:Unknown\nF one p:Missing p:i64->Unknown\n  R absent_one\nF one->i64\n  R absent_two\n", 10, 1);
    diag(0, "N102"); diag(1, "N101"); diag(2, "N102"); diag(3, "N101");
    diag(4, "N101"); diag(5, "N102"); diag(6, "N101"); diag(7, "N101"); diag(8, "N102"); diag(9, "N101");
    checked("M x\nF main o:option i64->i64\n  C o\n    some n\n      L hole:i64 _\n    none\n      L hole:i64 _\n  R 0\n", 2, 1);
    { const char *const payload_scope[] = {"n", "o"}, *const option_scope[] = {"o"};
      hole(0, "i64", payload_scope, 2); hole(1, "i64", option_scope, 1); }
    checked("M x\nF main seed:i64->i64\n  L hole:i64 _\n  L later 1\n  R _\n", 2, 1);
    hole(0, "i64", seed_scope, 1);
    { const char *const later_scope[] = {"later", "seed"}; hole(1, "i64", later_scope, 2); }
    checked("M x\nF main seed:i64->i64\n  ? true\n    L local 1\n    L local 2\n    L hole:i64 _\n  R _\n", 3, 1);
    diag(0, "N102"); { const char *const local_scope[] = {"local", "seed"}; hole(1, "i64", local_scope, 2); }
    hole(2, "i64", seed_scope, 1);
    checked("M x\nF main->i64\n  R absent\n  L bad (\n", 1, 0); diag(0, "P001");
    checked(NULL, 1, 0); diag(0, "P001");
    checked("M x\nF main->i64\n   R absent\n", 1, 0); diag(0, "P003");
    char *large = malloc(APX_MAX_SOURCE + 2); require(large != NULL, "bounded source allocation");
    const char *valid = "M x\nF main->i64\n  R 0\n;";
    memset(large, ' ', APX_MAX_SOURCE + 1); memcpy(large, valid, strlen(valid)); large[APX_MAX_SOURCE] = 0;
    checked(large, 0, 1);
    const char *malformed = "M x\nF main->i64\n  R (\n;";
    memset(large, ' ', APX_MAX_SOURCE + 1); memcpy(large, malformed, strlen(malformed)); large[APX_MAX_SOURCE] = 0;
    checked(large, 1, 0); diag(0, "P001");
    large[APX_MAX_SOURCE] = ' '; large[APX_MAX_SOURCE + 1] = 0;
    checked(large, 1, 0); require(!strcmp(string_field(diag(0, "P006"), "kind"), "source_limit_exceeded"), "source bound is fatal");
    size_t n = (size_t)snprintf(large, APX_MAX_SOURCE, "M x\nF main->i64\n  L node0 0\n");
    for (unsigned i = 1; i <= 66; ++i) n += (size_t)snprintf(large + n, APX_MAX_SOURCE - n, "  L node%u [node%u]\n", i, i - 1);
    snprintf(large + n, APX_MAX_SOURCE - n, "  R absent\n");
    checked(large, 2, 1); diag(0, "T106"); diag(1, "N101");
    n = (size_t)snprintf(large, APX_MAX_SOURCE, "M x\n");
    for (unsigned i = 0; i < 65; ++i) n += (size_t)snprintf(large + n, APX_MAX_SOURCE - n, "T Box%u struct\n  value:Box%u\n", i, i + 1);
    snprintf(large + n, APX_MAX_SOURCE - n, "T Box65 struct\n  value:i64\nF main box:Box0->i64\n  R absent\n");
    checked(large, 2, 1); diag(0, "T106"); diag(1, "N101");
    n = (size_t)snprintf(large, APX_MAX_SOURCE, "M x\nF main->i64\n");
    for (unsigned i = 0; i < 4096; ++i) { memcpy(large + n, "  A _\n", 6); n += 6; }
    memcpy(large + n, "  R 0\n", 7);
    checked(large, 4096, 1); diag(0, "H001"); diag(4095, "H001");
    memcpy(large + n, "  A _\n  R 0\n", 13);
    checked(large, 1, 1); require(!strcmp(string_field(diag(0, "P006"), "kind"), "compiler_resource_limit") &&
                                strstr(string_field(apx_json_item(diagnostics, 0), "message"), "incomplete"), "diagnostic cap never pretends completion");
    free(large);
    fixture_checks();
    require(apx_check_source("M x\nF helper->i64\n  R 0\n", NULL, NULL) == 0, "optional outputs");
    free(canonical); free(text); apx_json_reset();
    printf("native multi-diagnostic API: %u assertions PASS\n", tests);
    return 0;
}
