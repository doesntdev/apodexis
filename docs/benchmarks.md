# Local performance measurements

Measured 2026-11-06T14:12:54.166061+00:00. Author-produced on one host; not independently reproduced.

**The result is mixed.** Native scalar/collection/schema/inlining and JSON optimizations are now included. Array JSON pipelines are much closer to these C/Rust implementations, but core-only gaps remain substantial. C, Rust and warmed-up JavaScript lead most non-control kernels; Python still wins some record-difference measurements. Apodexis has lower one-shot latency than Python or Node here, not a general compute-speed advantage.

## Method and source

- Nine preregistered cases, five implementations, nine serial rotated rounds: 810 scored rows across steady-state and one-shot modes. Every steady-state row records both core and enclosing pipeline totals.
- Complete outputs checked on every invocation, including warmup. All 55 additional correctness and 50 wrong-output/count rejection probes passed before scoring.
- See [frozen protocol and reproduction](../benchmarks/cross_language/README.md), [all source](../benchmarks/cross_language/) and [sanitized reaggregatable observations](../benchmarks/cross_language/results.json). Native source is pinned at [`515b512857ab`](https://github.com/doesntdev/apodexis/commit/515b512857abdb08b40def66c9caa2c31167d5ac); the receipt also binds every measured source and binary by hash.
- Host: AMD Ryzen 9 7900X, Linux 7.0.0-42-generic, x86-64, glibc 2.43. Pinned logical CPU 2, unchanged powersave governor. This is not an isolated or fixed-frequency machine.
- GCC 15.2, C `-std=c99 -O3`; QBE 1.3 and default `-O2` Apodexis C runtime; Rust 1.95 release opt-level 3 with overflow checks; serde_json 1.0.153; Python 3.14.9 (GIL build, JIT off); Node 24.15.0. No forced GC.
- Fixed 1,000-call warmup policy, pilot-calibrated repetition counts frozen per case/language. Warmup is not proof of JIT-tier stabilization. Raw totals and counts remain available; min/max reveal retained noise rather than selecting the best round.
- C uses native typed kernels plus Apodexis JSON transport; Rust uses owned typed data plus serde_json; Python/JavaScript use ordinary stdlib/builtin JSON. These are representative implementations, not optimized record-holder programs or a best-C-JSON-library contest.
- C/Rust checked-i64 arithmetic is retained. Python arbitrary precision and JavaScript Number are exactly represented on the fixture domain, not equivalent arbitrary-i64 behavior. Baselines do not emulate Apodexis fuel, quotas, immutable snapshots or capability boundaries.

This source also includes the separately tested strict-JSON whitespace repair: raw NUL outside strings is rejected; escaped `\u0000` remains valid. These fresh measurements qualify that exact corrected source, not the predecessor binary. The C implementation reuses the bundled JSON transport, so its pipeline also includes the JSON fast paths.

All tables report **median [minimum–maximum]** of nine per-sample average times. They are not request-latency percentiles. Lower is better. No composite score, subtraction of controls, or cross-boundary speedup is computed.

## In-process JSON pipeline — µs/invocation

Fresh parse/typed conversion, computation, full encoding, byte-for-byte full-result verification and cleanup, including internal core clocks. Excludes imports, fixture reads, process startup, compilation, broker and OS sandbox setup. Normal GC may defer reclamation. No runner reparses encoded output inside this timer just for checking.

| Workload | Apodexis | C | Rust | Python | JavaScript |
| --- | ---: | ---: | ---: | ---: | ---: |
| Scalar control | 2.17 [2.13–2.28] | 2.17 [2.12–2.31] | 2.12 [2.10–2.29] | 4.83 [4.77–6.19] | 2.38 [2.34–2.42] |
| Numeric loop · 256 | 3.68 [3.58–3.77] | 2.29 [2.25–2.35] | 2.27 [2.18–2.33] | 11.92 [11.71–13.02] | 2.72 [2.61–2.98] |
| Numeric loop · 2,048 | 14.71 [14.62–15.22] | 3.03 [3.00–3.11] | 2.62 [2.47–2.70] | 73.61 [71.83–81.78] | 3.56 [3.50–4.56] |
| Array sum · 128 | 4.17 [4.03–4.24] | 3.77 [3.71–4.39] | 3.95 [3.89–4.23] | 15.77 [15.54–16.78] | 4.13 [4.05–4.38] |
| Array sum · 4,096 | 78.69 [76.55–88.13] | 66.77 [65.69–85.13] | 62.57 [60.65–66.35] | 371.10 [367.59–401.75] | 54.47 [52.58–62.39] |
| Build array · 256 | 5.79 [5.66–7.72] | 5.34 [5.21–6.85] | 4.18 [4.10–4.94] | 13.86 [13.74–14.63] | 4.70 [4.53–4.92] |
| Build array · 4,096 | 94.51 [93.83–98.61] | 58.66 [57.01–92.39] | 55.62 [54.82–58.91] | 168.61 [164.84–176.50] | 45.68 [43.04–53.87] |
| Record diff · 32 | 78.97 [78.30–82.32] | 14.38 [14.19–14.83] | 23.78 [23.46–24.47] | 83.95 [82.03–104.92] | 24.88 [23.84–28.40] |
| Record diff · 64 | 304.50 [292.96–314.23] | 41.28 [40.87–42.68] | 59.15 [58.35–61.29] | 258.71 [252.88–380.29] | 40.20 [37.72–41.84] |

## Instrumented core — µs/invocation

Function/entrypoint and output allocation only. Apodexis generated validation and semantic fuel remain inside; typed baseline conversion is outside. Serialization and cleanup are outside. Clock overhead dominates the scalar control (approximately a microsecond here); do not interpret it as isolated arithmetic speed or subtract it from larger workloads.

| Workload | Apodexis | C | Rust | Python | JavaScript |
| --- | ---: | ---: | ---: | ---: | ---: |
| Scalar control | 1.07 [1.04–1.12] | 1.06 [1.04–1.12] | 1.05 [1.03–1.12] | 1.16 [1.14–1.24] | 1.11 [1.09–1.13] |
| Numeric loop · 256 | 2.55 [2.49–2.66] | 1.17 [1.13–1.19] | 1.14 [1.10–1.17] | 8.13 [8.05–8.89] | 1.40 [1.34–1.52] |
| Numeric loop · 2,048 | 13.56 [13.50–14.09] | 1.93 [1.87–1.95] | 1.51 [1.35–1.57] | 69.77 [68.14–77.20] | 2.25 [2.22–2.93] |
| Array sum · 128 | 1.74 [1.69–1.78] | 1.11 [1.08–1.13] | 1.13 [1.09–1.17] | 2.38 [2.33–2.46] | 1.44 [1.41–1.53] |
| Array sum · 4,096 | 25.04 [24.53–27.15] | 1.85 [1.76–2.11] | 2.59 [2.48–2.97] | 44.42 [42.50–48.00] | 2.83 [2.72–3.11] |
| Build array · 256 | 1.94 [1.89–2.71] | 1.12 [1.10–1.26] | 1.20 [1.15–1.25] | 4.88 [4.85–5.15] | 2.21 [2.12–2.24] |
| Build array · 4,096 | 15.03 [14.74–15.86] | 1.44 [1.38–1.82] | 2.69 [2.56–3.04] | 72.39 [70.54–75.54] | 11.39 [11.00–13.42] |
| Record diff · 32 | 68.51 [67.74–71.28] | 6.08 [6.04–6.24] | 5.86 [5.80–6.00] | 58.57 [57.21–69.92] | 8.76 [8.05–11.70] |
| Record diff · 64 | 273.75 [265.27–283.98] | 25.52 [25.42–25.85] | 23.88 [23.56–25.03] | 210.95 [205.63–303.08] | 17.00 [16.01–17.94] |

## One-shot invocation — ms/process

Parent-observed taskset/spawn, stdin transfer, stdout capture and wait on prebuilt programs. Includes imports/startup, JSON and exit, not compilation. Warm OS file caches. Apodexis standard ELF retains runtime restrictions and a source/fuel/status envelope; baseline programs emit bare result values and provide no equivalent sandbox. This is practical invocation latency, not matched security work.

| Workload | Apodexis | C | Rust | Python | JavaScript |
| --- | ---: | ---: | ---: | ---: | ---: |
| Scalar control | 1.63 [1.39–1.92] | 1.08 [0.95–1.15] | 1.22 [1.09–1.37] | 16.87 [16.18–17.37] | 20.26 [18.67–23.44] |
| Numeric loop · 256 | 1.61 [1.48–2.92] | 1.11 [0.99–1.44] | 1.27 [1.14–1.34] | 16.54 [15.92–17.16] | 19.27 [18.46–20.60] |
| Numeric loop · 2,048 | 1.65 [1.38–1.96] | 1.32 [1.09–11.54] | 1.28 [1.11–1.55] | 16.79 [16.19–18.73] | 19.51 [18.81–20.77] |
| Array sum · 128 | 1.57 [1.40–1.90] | 1.21 [1.09–1.35] | 1.32 [1.12–1.70] | 16.42 [16.10–18.39] | 20.25 [19.20–21.68] |
| Array sum · 4,096 | 1.89 [1.82–2.15] | 1.39 [1.30–1.54] | 1.39 [1.36–1.46] | 17.27 [16.72–17.78] | 20.28 [19.79–21.16] |
| Build array · 256 | 1.62 [1.40–1.68] | 1.16 [0.93–1.44] | 1.26 [1.19–1.41] | 16.82 [15.81–21.29] | 21.27 [18.90–25.85] |
| Build array · 4,096 | 1.76 [1.54–2.05] | 1.49 [1.36–1.69] | 1.41 [1.32–1.63] | 16.82 [16.55–18.31] | 20.04 [19.29–21.59] |
| Record diff · 32 | 1.89 [1.73–2.51] | 1.16 [1.03–1.51] | 1.51 [1.34–1.74] | 16.84 [16.44–18.25] | 20.55 [19.76–22.98] |
| Record diff · 64 | 2.72 [2.49–3.46] | 1.32 [1.20–1.47] | 1.40 [1.25–1.68] | 17.71 [17.13–18.98] | 22.66 [20.55–24.90] |

## Previous cross-language snapshot

The [2026-11-05 observations](../benchmarks/cross_language/history/2026-11-05-results.json) are retained byte-for-byte, with their original source and timing identities. They are historical, not measurements of the new implementation; the [original methodology and tables](https://github.com/doesntdev/apodexis/blob/282d69059297f602156381547169d6cbd991a59f/docs/benchmarks.md) remain available. Do not compare absolute medians across campaigns as if they were a paired speedup experiment.

## Separate historical runtime improvement

This earlier before/after study compares Apodexis with its **previous runtime**, not another language. It uses a different harness and CPU affinity (logical CPU 23); absolute numbers must not be mixed with the cross-language campaign. The current implementation uses bounded execution-lifetime arenas and append-only builder blocks while preserving snapshots, output, canonical identity and semantic fuel.

| 4,096-element construction | Previous runtime | Arena + builder runtime |
| --- | ---: | ---: |
| In-process pipeline | 5.112 ms | 0.251 ms |
| Requested allocation | 65.20 MiB | 1.25 MiB |
| Isolated peak RSS | 67.55 MiB | 3.37 MiB |

The builder example is one predefined target among 22 workloads, not a claim of general 20× speed. Timing medians use nine rotated rounds; requested allocation is cumulative payload work, not live heap or physical RAM. RSS is a separate isolated process observation per case/version, not a timing-run high-water field or a latency percentile.

All frozen controls against the original runtime passed. Repeatedly finishing every intermediate prefix still materializes growing outputs and showed a small regression against arena-only; the design is not a universal allocation win.

See [before/after methodology](../benchmarks/runtime_memory/README.md) and [sanitized historical receipts](../benchmarks/runtime_memory/results.json). The original timed RSS observer inherited a launcher high-water floor; those fields are explicitly omitted from the public export and never used for the RSS claim.

## Reaggregate and reproduce

From a source checkout, after installing the optional benchmark-only toolchains:

```sh
python3 benchmarks/cross_language/test_results.py
python3 benchmarks/runtime_memory/test_results.py
```

This offline check reconstructs the cross-language aggregates from the committed observations and tests rejection of incomplete, duplicate, drifted or mismatched measurements. It is not a rerun of the timing campaign. To rerun the actual builds, correctness checks, warmup and measurements, follow the separate `prepare`, `measure`, `verify` commands in the [benchmark README](../benchmarks/cross_language/README.md). Results will vary across machines and system load. No compiler CI or GitHub workflow is added.

No WebAssembly/GPU, HTTP server, model/agent, network, or end-to-end task performance claim follows from these measurements.
