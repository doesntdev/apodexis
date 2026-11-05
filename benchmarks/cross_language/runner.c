/* Local C comparison kernels; the APX library is JSON transport ONLY.
 * Build from repo root (output path must be owned scratch):
 * gcc -std=c99 -O3 -DAPX_LIBRARY_ONLY -Iruntime \
 *   benchmarks/cross_language/runner.c runtime/apx_runtime.c -lm -o "$OUT"
 * Usage: runner --once KERNEL < input.json
 *        runner --sample KERNEL INPUT EXPECTED REPS WARMUPS
 * No APX fuel/security/snapshot contract is emulated. */
#define _POSIX_C_SOURCE 200809L
#include "../../runtime/apx_runtime.h"
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct { const char *bytes; size_t length; } Str;
typedef struct { Str id, hash; } Row;
typedef struct { Row *items; size_t length; } Rows;
typedef enum { SCALAR, NUMERIC, EACH, BUILDER, ROWDIFF } Kernel;
typedef struct {
    int64_t n, *integers; size_t length; Rows before, after;
} Input;
typedef struct { Str *items; size_t length; } Strings;
typedef struct {
    int kind; /* 0 integer, 1 integer array, 2 diff, 3 duplicate error */
    int64_t integer, *integers; size_t length;
    Strings added, removed, changed; const char *error;
} Result;

static void die(const char *message) {
    fprintf(stderr, "runner: %s\n", message); exit(1);
}
static uint64_t now_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts)) die("clock_gettime failed");
    return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + (uint64_t)ts.tv_nsec;
}
static void *array_alloc(size_t count, size_t width) {
    if (count > SIZE_MAX / width) die("allocation size overflow");
    if (!count) return NULL;
    void *p = malloc(count * width);
    if (!p) die("allocation failed");
    return p;
}
static char *read_stream(FILE *stream, size_t *length) {
    size_t capacity = 4096, used = 0;
    char *text = array_alloc(capacity, 1);
    for (;;) {
        if (used == capacity - 1) {
            if (capacity > SIZE_MAX / 2) die("input too large");
            capacity *= 2;
            char *next = realloc(text, capacity);
            if (!next) die("allocation failed");
            text = next;
        }
        size_t n = fread(text + used, 1, capacity - used - 1, stream);
        used += n;
        if (!n) { if (ferror(stream)) die("input read failed"); break; }
    }
    text[used] = '\0'; *length = used; return text;
}
static char *read_path(const char *path, size_t *length) {
    FILE *stream = fopen(path, "rb");
    if (!stream) die("cannot open fixture");
    char *text = read_stream(stream, length);
    if (fclose(stream)) die("fixture close failed");
    return text;
}
static ApxValue *parse(const char *source, size_t length) {
    ApxValue *v = apx_json_parse(source, length);
    if (!v) die("invalid JSON or transport resource limit");
    return v;
}
static char *encode(ApxValue *value) {
    char *wire = apx_json_encode(value);
    if (!wire) die("JSON encoding failed or transport resource limit");
    return wire;
}
static int64_t get_integer(ApxValue *v) {
    if (apx_json_kind(v) != 2) die("expected i64 integer");
    return apx_json_integer(v);
}
static Str get_string(ApxValue *v) {
    if (apx_json_kind(v) != 4) die("expected string");
    Str s = { apx_json_string(v), apx_json_length(v) }; return s;
}
static void object2(ApxValue *v, const char *a, const char *b) {
    if (apx_json_kind(v) != 6 || apx_json_length(v) != 2 ||
        !apx_json_get(v, a) || !apx_json_get(v, b)) die("expected two-field object");
}
static Rows get_rows(ApxValue *v) {
    if (apx_json_kind(v) != 5) die("expected row array");
    Rows out = { NULL, apx_json_length(v) };
    out.items = array_alloc(out.length, sizeof *out.items);
    for (size_t i = 0; i < out.length; i++) {
        ApxValue *row = apx_json_item(v, i); object2(row, "id", "hash");
        out.items[i].id = get_string(apx_json_get(row, "id"));
        out.items[i].hash = get_string(apx_json_get(row, "hash"));
    }
    return out;
}
static Input convert(Kernel kernel, ApxValue *v) {
    Input out = {0};
    if (kernel == ROWDIFF) {
        object2(v, "before", "after");
        out.before = get_rows(apx_json_get(v, "before"));
        out.after = get_rows(apx_json_get(v, "after"));
    } else if (kernel == EACH) {
        if (apx_json_kind(v) != 5) die("expected integer array");
        out.length = apx_json_length(v);
        out.integers = array_alloc(out.length, sizeof *out.integers);
        for (size_t i = 0; i < out.length; i++) out.integers[i] = get_integer(apx_json_item(v, i));
    } else {
        out.n = get_integer(v);
        if (kernel == BUILDER && (out.n < 0 || (uint64_t)out.n > SIZE_MAX / sizeof(int64_t)))
            die("invalid builder capacity");
    }
    return out;
}
static int same(Str a, Str b) {
    return a.length == b.length && !memcmp(a.bytes, b.bytes, a.length);
}
static int duplicate(Rows rows) {
    for (size_t i = 0; i < rows.length; i++)
        for (size_t j = i + 1; j < rows.length; j++)
            if (same(rows.items[i].id, rows.items[j].id)) return 1;
    return 0;
}
static int contains(Rows rows, Str id) {
    for (size_t i = 0; i < rows.length; i++) if (same(rows.items[i].id, id)) return 1;
    return 0;
}
static Str hash_of(Rows rows, Str id) {
    for (size_t i = 0; i < rows.length; i++)
        if (same(rows.items[i].id, id)) return rows.items[i].hash;
    Str empty = { "", 0 }; return empty;
}
static int64_t add(int64_t a, int64_t b) {
    int64_t out; if (__builtin_add_overflow(a, b, &out)) die("i64 addition overflow"); return out;
}
static int64_t mul(int64_t a, int64_t b) {
    int64_t out; if (__builtin_mul_overflow(a, b, &out)) die("i64 multiplication overflow"); return out;
}
static Result compute(Kernel kernel, const Input *in) {
    Result out = {0};
    if (kernel == SCALAR) { out.integer = add(in->n, 1); return out; }
    if (kernel == NUMERIC) {
        for (int64_t i = 0; i < in->n; i = add(i, 1)) out.integer = add(out.integer, add(mul(i, 3), 7));
        return out;
    }
    if (kernel == EACH) {
        for (size_t i = 0; i < in->length; i++) out.integer = add(out.integer, in->integers[i]);
        return out;
    }
    if (kernel == BUILDER) {
        out.kind = 1;
        out.integers = array_alloc((size_t)in->n, sizeof *out.integers);
        for (int64_t i = 0; i < in->n; i = add(i, 1)) out.integers[out.length++] = i;
        return out;
    }
    if (duplicate(in->before)) { out.kind = 3; out.error = "duplicate id in before"; return out; }
    if (duplicate(in->after)) { out.kind = 3; out.error = "duplicate id in after"; return out; }
    out.kind = 2;
    out.added.items = array_alloc(in->after.length, sizeof(Str));
    out.changed.items = array_alloc(in->after.length, sizeof(Str));
    out.removed.items = array_alloc(in->before.length, sizeof(Str));
    for (size_t i = 0; i < in->after.length; i++) {
        Row row = in->after.items[i];
        if (contains(in->before, row.id))
            if (!same(hash_of(in->before, row.id), row.hash)) out.changed.items[out.changed.length++] = row.id;
        if (!contains(in->before, row.id)) out.added.items[out.added.length++] = row.id;
    }
    for (size_t i = 0; i < in->before.length; i++) {
        Str id = in->before.items[i].id;
        if (!contains(in->after, id)) out.removed.items[out.removed.length++] = id;
    }
    return out;
}
static ApxValue *strings(Strings values) {
    if (values.length > INT64_MAX) die("array too large");
    ApxValue *out = apx_array_new((int64_t)values.length);
    for (size_t i = 0; i < values.length; i++) {
        Str s = values.items[i];
        if (s.length > INT64_MAX) die("string too large");
        apx_array_put(out, (int64_t)i, apx_string(s.bytes, (int64_t)s.length));
    }
    return out;
}
static ApxValue *transport(const Result *result) {
    if (!result->kind) return apx_int(result->integer);
    if (result->kind == 1) {
        if (result->length > INT64_MAX) die("array too large");
        ApxValue *out = apx_array_new((int64_t)result->length);
        for (size_t i = 0; i < result->length; i++) apx_array_put(out, (int64_t)i, apx_int(result->integers[i]));
        return out;
    }
    ApxValue *out = apx_record_new("Result", 2);
    apx_record_put(out, 0, "tag", apx_string(result->kind == 3 ? "err" : "ok", result->kind == 3 ? 3 : 2));
    ApxValue *payload;
    if (result->kind == 3) payload = apx_string(result->error, (int64_t)strlen(result->error));
    else {
        payload = apx_record_new("Diff", 3);
        apx_record_put(payload, 0, "added", strings(result->added));
        apx_record_put(payload, 1, "removed", strings(result->removed));
        apx_record_put(payload, 2, "changed", strings(result->changed));
    }
    apx_record_put(out, 1, result->kind == 3 ? "error" : "value", payload); return out;
}
static uint64_t invoke(Kernel kernel, const char *source, size_t length,
                       const char *expected, char **last) {
    Input in = convert(kernel, parse(source, length));
    __asm__ __volatile__("" : : "g"(&in) : "memory");
    uint64_t start = now_ns();
    Result result = compute(kernel, &in);
    __asm__ __volatile__("" : : "g"(&result) : "memory");
    uint64_t core = now_ns() - start;
    char *wire = encode(transport(&result));
    if (expected && strcmp(wire, expected)) die("output mismatch");
    free(result.integers); free(result.added.items); free(result.changed.items); free(result.removed.items);
    free(in.integers); free(in.before.items); free(in.after.items);
    apx_json_reset();
    if (last) *last = wire; else free(wire);
    return core;
}
static Kernel kernel_name(const char *text) {
    const char *names[] = {"scalar", "numeric", "each", "builder", "rowdiff"};
    for (int i = 0; i < 5; i++) if (!strcmp(text, names[i])) return (Kernel)i;
    die("unknown kernel"); return SCALAR;
}
static uint64_t count(const char *text, uint64_t minimum, uint64_t maximum) {
    if (!*text) die("empty count");
    uint64_t n = 0;
    for (const char *p = text; *p; p++) {
        if (*p < '0' || *p > '9' || n > maximum / 10) die("invalid count");
        n = n * 10 + (unsigned)(*p - '0');
        if (n > maximum) die("count out of range");
    }
    if (n < minimum) die("count out of range");
    return n;
}
int main(int argc, char **argv) {
    if (argc == 3 && !strcmp(argv[1], "--once")) {
        Kernel kernel = kernel_name(argv[2]); size_t length;
        char *source = read_stream(stdin, &length), *wire = NULL;
        invoke(kernel, source, length, NULL, &wire); puts(wire); free(wire); free(source); return 0;
    }
    if (argc != 7 || strcmp(argv[1], "--sample")) die("usage: --once KERNEL | --sample KERNEL INPUT EXPECTED REPS WARMUPS");
    Kernel kernel = kernel_name(argv[2]);
    uint64_t reps = count(argv[5], 1, 1000000), warmups = count(argv[6], 0, 100000);
    size_t length, expected_length;
    char *source = read_path(argv[3], &length), *expected_source = read_path(argv[4], &expected_length);
    char *expected = encode(parse(expected_source, expected_length));
    apx_json_reset(); free(expected_source);
    for (uint64_t i = 0; i < warmups; i++) invoke(kernel, source, length, expected, NULL);
    char *last = NULL; uint64_t core = 0, start = now_ns();
    for (uint64_t i = 0; i < reps; i++) core += invoke(kernel, source, length, expected, i + 1 == reps ? &last : NULL);
    uint64_t pipeline = now_ns() - start;
    printf("{\"schema\":\"apodexis.cross_language_sample.v1\",\"language\":\"C\",\"kernel\":\"%s\","
           "\"repetitions\":%" PRIu64 ",\"warmups\":%" PRIu64 ",\"core_ns\":%" PRIu64 ","
           "\"pipeline_ns\":%" PRIu64 ",\"output\":%s}\n", argv[2], reps, warmups, core, pipeline, last);
    free(last); free(expected); free(source); return 0;
}
