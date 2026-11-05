/* Optional benchmark harness, not an installed runtime or sandbox.
 * Links real QBE-generated apx_entry; never evaluates source or ASTs. */
#define _POSIX_C_SOURCE 200809L
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define APX_LIBRARY_ONLY
#ifndef APX_RUNTIME_SOURCE
#error "APX_RUNTIME_SOURCE must name the measured runtime"
#endif
#include APX_RUNTIME_SOURCE

static uint64_t core_ns, known_fuel;
static int have_fuel;
static uint64_t now_ns(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) { perror("clock_gettime"); exit(2); }
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}
static char *load_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f || fseek(f, 0, SEEK_END)) { perror(path); exit(2); }
    long n = ftell(f);
    if (n < 0 || (unsigned long)n > APX_INPUT) exit(2);
    rewind(f);
    char *text = malloc((size_t)n + 1);
    if (!text || fread(text, 1, (size_t)n, f) != (size_t)n || fclose(f)) exit(2);
    text[n] = 0;
    return text;
}
static int invoke(const char *input, const char *expected, int timed) {
    fuel_used = 0;
    fuel_limit = 10000000;
    trap_target = &escape;
    if (setjmp(escape)) {
        fprintf(stderr, "Apodexis benchmark trap %s/%s: %s\n", error_code, error_kind, error_message);
        reset_memory();
        return 0;
    }
    ApxValue *in = parse_complete(input, strlen(input));
    uint64_t start = timed ? now_ns() : 0;
    ApxValue *out = apx_entry(in);
    if (timed) core_ns += now_ns() - start;
    char *encoded = apx_json_encode(out);
    int ok = encoded && !strcmp(encoded, expected);
    if (!ok) fputs("Apodexis benchmark output mismatch\n", stderr);
    if (have_fuel && known_fuel != fuel_used) {
        fputs("Apodexis benchmark fuel drift\n", stderr);
        ok = 0;
    }
    known_fuel = fuel_used;
    have_fuel = 1;
    free(encoded);
    reset_memory();
    return ok;
}
static unsigned long count(const char *text, unsigned long maximum, int allow_zero) {
    char *end;
    errno = 0;
    if (!*text || *text == '-') exit(64);
    unsigned long n = strtoul(text, &end, 10);
    if (errno || *end || n > maximum || (!allow_zero && !n)) exit(64);
    return n;
}
int main(int argc, char **argv) {
    if (argc != 7 || strcmp(argv[1], "--sample")) {
        fputs("usage: apx-repeat --sample KERNEL INPUT EXPECTED REPS WARMUPS\n", stderr);
        return 64;
    }
    const char *kernel = argv[2];
    if (strcmp(kernel, "scalar") && strcmp(kernel, "numeric") && strcmp(kernel, "each") &&
        strcmp(kernel, "builder") && strcmp(kernel, "rowdiff")) return 64;
    unsigned long reps = count(argv[5], 1000000, 0);
    unsigned long warmups = count(argv[6], 100000, 1);
    char *input = load_file(argv[3]), *expected = load_file(argv[4]);
    for (unsigned long i = 0; i < warmups; ++i) if (!invoke(input, expected, 0)) return 1;
    core_ns = 0;
    uint64_t start = now_ns();
    for (unsigned long i = 0; i < reps; ++i) if (!invoke(input, expected, 1)) return 1;
    uint64_t pipeline_ns = now_ns() - start;
    printf("{\"schema\":\"apodexis.cross_language_sample.v1\",\"language\":\"Apodexis\","
           "\"kernel\":\"%s\",\"repetitions\":%lu,\"warmups\":%lu,"
           "\"core_ns\":%" PRIu64 ",\"pipeline_ns\":%" PRIu64 ","
           "\"fuel_used\":%" PRIu64 ",\"source_hash\":\"%s\",\"output\":%s}\n",
           kernel, reps, warmups, core_ns, pipeline_ns, known_fuel, apx_source_hash, expected);
    free(input);
    free(expected);
    return 0;
}
