# Local performance measurements

Measured 2026-11-05T05:39:55.855314+00:00. Author-produced on one host; not independently reproduced.

**The result is mixed.** Apodexis starts a one-shot tool much faster than Python or Node on this corpus, but C, Rust and warmed-up JavaScript lead the non-control kernels. Python also wins several workloads. Do not turn startup savings into a compute-speed claim.

## Method and source

- Nine preregistered cases, five implementations, nine serial rotated rounds: 810 scored rows across steady-state and one-shot modes. Every steady-state row records both core and enclosing pipeline totals.
- Complete outputs checked on every invocation, including warmup. Additional correctness and wrong-output/count rejection probes passed before scoring.
- See [frozen protocol and reproduction](../benchmarks/cross_language/README.md), [all source](../benchmarks/cross_language/) and [sanitized reaggregatable observations](../benchmarks/cross_language/results.json). Native source is pinned at [`8ee21c096924`](https://github.com/doesntdev/apodexis/commit/8ee21c0969247a92017d0fa27af9f84bc737df13); the receipt also binds every measured source and binary by hash.
- Host: AMD Ryzen 9 7900X, Linux 7.0.0-42-generic, x86-64, glibc 2.43. Pinned logical CPU 2, unchanged powersave governor. This is not an isolated or fixed-frequency machine.
- GCC 15.2, C `-std=c99 -O3`; QBE 1.3 and default `-O2` Apodexis C runtime; Rust 1.95 release opt-level 3 with overflow checks; serde_json 1.0.153; Python 3.14.9 (GIL build, JIT off); Node 24.15.0. No forced GC.
- Fixed 1,000-call warmup policy, pilot-calibrated repetition counts frozen per case/language. Warmup is not proof of JIT-tier stabilization. Raw totals and counts remain available; min/max reveal retained noise rather than selecting the best round.
- C uses native typed kernels plus Apodexis JSON transport; Rust uses owned typed data plus serde_json; Python/JavaScript use ordinary stdlib/builtin JSON. These are representative implementations, not optimized record-holder programs or a best-C-JSON-library contest.
- C/Rust checked-i64 arithmetic is retained. Python arbitrary precision and JavaScript Number are exactly represented on the fixture domain, not equivalent arbitrary-i64 behavior. Baselines do not emulate Apodexis fuel, quotas, immutable snapshots or capability boundaries.

All tables report **median [minimum–maximum]** of nine per-sample average times. They are not request-latency percentiles. Lower is better. No composite score, subtraction of controls, or cross-boundary speedup is computed.

## In-process JSON pipeline — µs/invocation

Fresh parse/typed conversion, computation, full encoding, byte-for-byte full-result verification and cleanup, including internal core clocks. Excludes imports, fixture reads, process startup, compilation, broker and OS sandbox setup. Normal GC may defer reclamation. No runner reparses encoded output inside this timer just for checking.

| Workload | Apodexis | C | Rust | Python | JavaScript |
| --- | ---: | ---: | ---: | ---: | ---: |
| Scalar control | 2.27 [2.19–2.29] | 2.16 [2.13–2.19] | 2.05 [2.05–2.15] | 4.78 [4.73–4.85] | 2.31 [2.30–2.38] |
| Numeric loop · 256 | 16.00 [15.61–16.33] | 2.27 [2.27–4.82] | 2.20 [2.17–2.25] | 11.76 [11.73–12.17] | 2.63 [2.60–2.65] |
| Numeric loop · 2,048 | 122.91 [121.06–125.00] | 3.04 [2.98–3.11] | 2.67 [2.65–2.78] | 74.83 [73.61–103.35] | 3.48 [3.45–3.54] |
| Array sum · 128 | 7.38 [7.34–7.49] | 5.27 [5.22–5.32] | 3.94 [3.81–3.99] | 15.47 [15.38–15.56] | 4.16 [4.11–4.18] |
| Array sum · 4,096 | 180.65 [174.89–181.73] | 115.92 [115.08–117.80] | 62.65 [62.13–63.72] | 366.96 [365.02–372.81] | 54.96 [54.46–57.10] |
| Build array · 256 | 16.73 [16.60–19.47] | 9.97 [9.94–10.07] | 4.20 [4.16–4.77] | 13.69 [13.59–14.85] | 4.61 [4.59–4.65] |
| Build array · 4,096 | 258.92 [256.08–266.66] | 139.06 [137.95–140.32] | 56.30 [55.03–57.11] | 168.84 [164.85–170.84] | 45.74 [44.62–45.89] |
| Record diff · 32 | 133.58 [131.15–135.51] | 15.58 [15.47–16.51] | 23.37 [23.25–24.45] | 83.30 [82.15–104.01] | 22.38 [21.93–23.48] |
| Record diff · 64 | 498.24 [483.39–513.39] | 42.98 [42.93–43.70] | 59.94 [59.10–60.38] | 255.46 [252.68–263.98] | 39.02 [37.18–39.82] |

## Instrumented core — µs/invocation

Function/entrypoint and output allocation only. Apodexis generated validation and semantic fuel remain inside; typed baseline conversion is outside. Serialization and cleanup are outside. Clock overhead dominates the scalar control (approximately a microsecond here); do not interpret it as isolated arithmetic speed or subtract it from larger workloads.

| Workload | Apodexis | C | Rust | Python | JavaScript |
| --- | ---: | ---: | ---: | ---: | ---: |
| Scalar control | 1.13 [1.08–1.15] | 1.04 [1.03–1.05] | 1.00 [1.00–1.05] | 1.14 [1.11–1.16] | 1.08 [1.07–1.15] |
| Numeric loop · 256 | 13.40 [13.28–13.74] | 1.14 [1.14–1.54] | 1.11 [1.10–1.13] | 8.01 [7.98–8.29] | 1.35 [1.34–1.42] |
| Numeric loop · 2,048 | 111.23 [109.79–113.09] | 1.92 [1.87–1.97] | 1.58 [1.56–1.63] | 70.93 [69.81–97.59] | 2.20 [2.18–2.22] |
| Array sum · 128 | 3.13 [3.11–3.16] | 1.05 [1.05–1.08] | 1.07 [1.06–1.09] | 2.36 [2.34–2.40] | 1.46 [1.42–1.52] |
| Array sum · 4,096 | 66.65 [65.24–67.50] | 1.85 [1.79–1.91] | 2.59 [2.55–2.62] | 44.90 [44.23–45.54] | 2.87 [2.80–2.92] |
| Build array · 256 | 7.38 [7.31–7.41] | 1.10 [1.09–1.11] | 1.17 [1.16–1.31] | 4.87 [4.83–5.96] | 2.18 [2.16–2.20] |
| Build array · 4,096 | 115.13 [114.18–121.36] | 1.45 [1.39–1.58] | 2.69 [2.63–3.01] | 72.54 [70.40–74.69] | 10.99 [10.89–11.16] |
| Record diff · 32 | 121.05 [118.66–123.02] | 6.02 [5.97–6.42] | 5.81 [5.77–5.87] | 58.33 [57.03–78.68] | 7.84 [7.42–9.69] |
| Record diff · 64 | 462.23 [447.65–476.98] | 24.95 [24.88–25.28] | 23.22 [23.10–23.54] | 207.54 [204.06–215.32] | 16.93 [15.80–17.56] |

## One-shot invocation — ms/process

Parent-observed taskset/spawn, stdin transfer, stdout capture and wait on prebuilt programs. Includes imports/startup, JSON and exit, not compilation. Warm OS file caches. Apodexis standard ELF retains runtime restrictions and a source/fuel/status envelope; baseline programs emit bare result values and provide no equivalent sandbox. This is practical invocation latency, not matched security work.

| Workload | Apodexis | C | Rust | Python | JavaScript |
| --- | ---: | ---: | ---: | ---: | ---: |
| Scalar control | 1.92 [1.06–2.04] | 1.09 [0.89–1.85] | 1.22 [1.11–2.13] | 15.84 [15.72–16.15] | 19.23 [18.48–21.79] |
| Numeric loop · 256 | 1.84 [1.50–5.62] | 1.10 [1.05–4.22] | 1.22 [1.20–4.08] | 15.89 [15.56–21.91] | 19.78 [18.66–22.02] |
| Numeric loop · 2,048 | 2.33 [2.02–2.59] | 1.30 [1.06–1.55] | 1.23 [1.19–1.55] | 16.44 [16.04–17.06] | 19.38 [18.54–20.05] |
| Array sum · 128 | 1.71 [1.44–2.18] | 1.10 [1.04–1.23] | 1.20 [1.14–1.67] | 16.09 [15.74–18.32] | 19.91 [18.83–21.79] |
| Array sum · 4,096 | 2.33 [2.10–2.38] | 1.33 [1.29–1.48] | 1.34 [1.29–1.37] | 16.42 [16.10–18.11] | 19.73 [18.73–20.56] |
| Build array · 256 | 2.02 [1.73–3.04] | 1.11 [1.07–1.33] | 1.23 [1.19–1.29] | 15.81 [15.70–16.18] | 19.43 [18.48–20.50] |
| Build array · 4,096 | 2.64 [2.37–2.94] | 1.61 [1.37–4.24] | 1.39 [1.32–4.63] | 16.74 [15.94–35.09] | 19.75 [19.33–34.61] |
| Record diff · 32 | 2.18 [1.79–2.48] | 1.10 [1.05–1.58] | 1.49 [1.29–1.62] | 16.11 [16.05–16.74] | 20.46 [18.68–25.42] |
| Record diff · 64 | 3.00 [2.67–3.26] | 1.16 [1.12–1.39] | 1.30 [1.29–1.33] | 16.52 [16.36–17.08] | 21.68 [20.40–24.08] |

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
