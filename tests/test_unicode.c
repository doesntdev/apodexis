/* Native canonical Unicode regression tests.
 * Verify the pinned scalar mask, focused vectors, canonical identity,
 * locale independence and escaped/UTF-8 full-scalar formatting.
 */
#include "../compiler/apx_frontend.h"
#include "../compiler/apx_unicode.h"
#include "../compiler/apx_hash.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <locale.h>

#define SCALAR_LIMIT 0x110000u
#define BATCH_SIZE 2048u
#define BUFFER_SIZE (APX_MAX_SOURCE + 1u)
static const char mask_digest[] =
    "b0022187037ddc8b2af77c9f0c6fda5d59e4814062d3af6b5401c5029b4141b3";
static char source[BUFFER_SIZE], expected[BUFFER_SIZE];

static void require(int condition, const char *message) {
    if (!condition) { fprintf(stderr, "Unicode test failure: %s\n", message); exit(1); }
}
static int is_scalar(uint32_t scalar) {
    return scalar < SCALAR_LIMIT && (scalar < 0xd800u || scalar > 0xdfffu);
}
static size_t utf8(uint32_t scalar, char text[4]) {
    if (scalar < 0x80u) { text[0] = (char)scalar; return 1; }
    if (scalar < 0x800u) {
        text[0] = (char)(0xc0u | (scalar >> 6));
        text[1] = (char)(0x80u | (scalar & 63u)); return 2;
    }
    if (scalar < 0x10000u) {
        text[0] = (char)(0xe0u | (scalar >> 12));
        text[1] = (char)(0x80u | ((scalar >> 6) & 63u));
        text[2] = (char)(0x80u | (scalar & 63u)); return 3;
    }
    text[0] = (char)(0xf0u | (scalar >> 18));
    text[1] = (char)(0x80u | ((scalar >> 12) & 63u));
    text[2] = (char)(0x80u | ((scalar >> 6) & 63u));
    text[3] = (char)(0x80u | (scalar & 63u)); return 4;
}
static void append(char *buffer, size_t *length, const char *text, size_t n) {
    require(*length + n < BUFFER_SIZE, "source/output batch exceeded buffer");
    memcpy(buffer + *length, text, n); *length += n; buffer[*length] = 0;
}
/* raw mode still protects string syntax and physical CR/LF/NUL bytes. */
static void append_scalar(char *buffer, size_t *length, uint32_t scalar, int raw) {
    char text[32]; size_t n;
    switch (scalar) {
    case 0: strcpy(text, "\\0"); break;
    case 9: strcpy(text, "\\t"); break;
    case 10: strcpy(text, "\\n"); break;
    case 13: strcpy(text, "\\r"); break;
    case 34: strcpy(text, "\\\""); break;
    case 92: strcpy(text, "\\\\"); break;
    default:
        if (!raw && apx_unicode_escape(scalar)) {
            n = (size_t)snprintf(text, sizeof text, "\\u{%x}", (unsigned)scalar);
        } else n = utf8(scalar, text);
        append(buffer, length, text, n); return;
    }
    append(buffer, length, text, strlen(text));
}
static void check_format(const char *input, const char *wanted, const char *label) {
    char *diagnostic = NULL, *actual = apx_format_source(input, &diagnostic);
    if (!actual || diagnostic || strcmp(actual, wanted)) {
        fprintf(stderr, "formatter mismatch (%s): %s\n", label,
                diagnostic ? diagnostic : "canonical bytes differ");
        free(actual); free(diagnostic); exit(1);
    }
    free(actual);
}
static void check_mask(void) {
    unsigned char *mask = calloc(SCALAR_LIMIT, 1); uint32_t scalar;
    unsigned count = 0, ranges = 0; char digest[65];
    require(mask != NULL, "mask allocation failed");
    for (scalar = 0; scalar < SCALAR_LIMIT; ++scalar) {
        if (is_scalar(scalar)) mask[scalar] = (unsigned char)apx_unicode_escape(scalar);
        require(mask[scalar] <= 1, "non-boolean classification");
        count += mask[scalar];
        if (mask[scalar] && (!scalar || !mask[scalar - 1])) ++ranges;
    }
    apx_sha256(mask, SCALAR_LIMIT, digest);
    require(!strcmp(digest, mask_digest), "complete pinned scalar-mask SHA-256 mismatch");
    require(count == 954588u && ranges == 909u, "mask counts/ranges changed");
    require(apx_unicode_escape(0xd800u) && apx_unicode_escape(0xdfffu) &&
            apx_unicode_escape(SCALAR_LIMIT) && apx_unicode_escape(UINT32_MAX),
            "invalid scalars must be classified conservatively");
    /* The predicate must not start using host locale data. */
    (void)setlocale(LC_CTYPE, "");
    for (scalar = 0; scalar < SCALAR_LIMIT; ++scalar)
        if (is_scalar(scalar)) require(mask[scalar] == apx_unicode_escape(scalar), "locale changed classification");
    require(setlocale(LC_CTYPE, "C") != NULL, "C locale unavailable");
    free(mask);
}
static void check_vectors(void) {
    static const struct { const char *input, *wanted; } vectors[] = {
        {"\\u{34f}", "\\u{34f}"}, {"\xcd\x8f", "\\u{34f}"},
        {"a\xcc\x81z", "a\\u{301}z"}, {"\\u{300}a\\u{34f}", "\\u{300}a\\u{34f}"},
        {"\\u{378}", "\\u{378}"}, {"\\u{a0}", "\\u{a0}"},
        {"\\u{e000}", "\\u{e000}"}, {"\\u{fdd0}", "\\u{fdd0}"},
        {"\\u{ffff}", "\\u{ffff}"}, {"\\u{10ffff}", "\\u{10ffff}"},
        {"\\u{fe0f}", "\\u{fe0f}"}, {"\\u{e0100}", "\\u{e0100}"},
        {"\\u{200d}", "\\u{200d}"}, {"\\u{2028}", "\\u{2028}"},
        {"\\u{1f3fb}", "\xf0\x9f\x8f\xbb"},
        {"\\u{e9}\\u{4e2d}\\u{1f642}", "\xc3\xa9\xe4\xb8\xad\xf0\x9f\x99\x82"},
        {"' ~", "' ~"}, {"\\u{0}\\u{9}\\u{a}\\u{d}\\u{22}\\u{5c}", "\\0\\t\\n\\r\\\"\\\\"}
    };
    size_t i; char input[512], wanted[512], digest[65];
    for (i = 0; i < sizeof vectors / sizeof vectors[0]; ++i) {
        snprintf(input, sizeof input, "M unicode_vectors\nF main -> str\n  R \"%s\"\n", vectors[i].input);
        snprintf(wanted, sizeof wanted, "M unicode_vectors\n\nF main->str\n  R \"%s\"\n", vectors[i].wanted);
        check_format(input, wanted, "focused string vector");
        check_format(wanted, wanted, "canonical idempotence");
    }
    check_format("M unicode_vectors\nF main -> str\n  Z \"a\xcd\x8f\"\n",
                 "M unicode_vectors\n\nF main->str\n  Z \"a\\u{34f}\"\n", "trap message");
    check_format("M unicode_minimal\nF main->str\n  R \"\xcd\x8f\"\n",
                 "M unicode_minimal\n\nF main->str\n  R \"\\u{34f}\"\n", "minimal raw U+034F");
    {
        char *diagnostic = NULL;
        char *canonical = apx_format_source("M unicode_minimal\nF main->str\n  R \"\\u{34f}\"\n", &diagnostic);
        require(canonical && !diagnostic, "minimal source failed");
        apx_sha256(canonical, strlen(canonical), digest);
        require(!strcmp(digest, "671d5cf7aa6475e7451128e565b95fa5205cc145f7d7f454fb3977178bd6f169"),
                "0.1.11 minimal U+034F apx1 identity changed");
        free(canonical); free(diagnostic);
    }
}
static void check_all_scalars(void) {
    const char *prefix = "M unicode_scan\n\nF main->str\n  R \"a";
    unsigned batches = 0, scalar_cases = 0; size_t maximum_source = 0; int raw;
    uint32_t start, end, scalar;
    for (raw = 0; raw <= 1; ++raw) {
        for (start = 0; start < SCALAR_LIMIT; start = end) {
            size_t source_length = 0, expected_length = 0; unsigned in_batch = 0;
            end = start + BATCH_SIZE; if (end > SCALAR_LIMIT) end = SCALAR_LIMIT;
            source[0] = expected[0] = 0;
            append(source, &source_length, prefix, strlen(prefix));
            append(expected, &expected_length, prefix, strlen(prefix));
            for (scalar = start; scalar < end; ++scalar) if (is_scalar(scalar)) {
                char escape[32]; size_t n;
                if (raw) append_scalar(source, &source_length, scalar, 1);
                else {
                    n = (size_t)snprintf(escape, sizeof escape, "\\u{%x}", (unsigned)scalar);
                    append(source, &source_length, escape, n);
                }
                append_scalar(expected, &expected_length, scalar, 0); ++in_batch;
            }
            if (!in_batch) continue;
            append(source, &source_length, "z\"\n", 3);
            append(expected, &expected_length, "z\"\n", 3);
            if (source_length > maximum_source) maximum_source = source_length;
            check_format(source, expected, "full scalar range");
            /* Canonical idempotence is checked for every scalar, not just vectors. */
            check_format(expected, expected, "full range idempotence");
            ++batches; scalar_cases += in_batch;
        }
    }
    require(batches == 1086u && scalar_cases == 2224128u, "incomplete scalar coverage");
    printf("native Unicode: mask SHA-256, vectors, hash identity, locale and full-range formatting PASS\n");
    printf("coverage: 1112064 distinct scalars, %u input cases, %u bounded batches, max source %zu bytes\n",
           scalar_cases, batches, maximum_source);
}
int main(void) {
    check_mask(); check_vectors();
    check_all_scalars();
    return 0;
}
