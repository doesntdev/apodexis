# Native MCP compile-once reuse measurement

Measured **2026-11-07T00:28:57Z**. This author-produced, single-host experiment measures native MCP request round trips, not core computation, whole-agent task speed or superiority over another language.

## Results

| Operation | Median ms | Min–max ms | Scored calls |
| --- | ---: | ---: | ---: |
| Compile | 19.371 | 18.894–20.352 | 31 |
| Cached invoke | 1.202 | 1.126–1.481 | 31 |
| Source-per-call run | 17.466 | 16.867–18.484 | 31 |

Cached invocation used **93.12% less median request time** than source-per-call execution on this one input. Compilation is reported separately; no unrelated timer is subtracted and no amortization threshold is inferred.

## Source and fixed work

- Public source checkpoint: [`2c589ec4d3ce`](https://github.com/doesntdev/apodexis/commit/2c589ec4d3ce2701609a090e3e6e2dda26624832); tree `5b69ccb9b09c2a90cbc791dcba330fff171ffba9`. Later documentation-only commits do not relabel these observations.
- Program: [`examples/config_validator.apx`](../../examples/config_validator.apx); exact source SHA-256 `0ebecdb6128235aa42e97c40b03c7f91bf5c03f58f4160c72a077b8e06d51711`.
- Input/limits: unchanged `development` case in [`examples/config-validator/cases.json`](../../examples/config-validator/cases.json). Fuel 100000, maximum input 65536 bytes, maximum output 16384 bytes.
- [Results and source/build fingerprints](results.json) include the exact input and expected execution record. [Original observations](samples.jsonl) retain every warmup and scored integer-nanosecond value unchanged, including their validated flag.
- All 72 invocation/run records, including warmups, matched the complete expected record with JSON type-sensitive canonical identity. All 37 compiled handles were released. The native configuration-validator suite separately covers 55 cases and 13 source-declared tests; this timing run uses one case, not all 55.

## Host and build

AMD Ryzen 9 7900X 12-Core Processor; Linux x86-64, glibc 2.43, logical CPU 23 and unchanged `performance` governor. The process/server/descendants inherited that one-CPU affinity and it was restored afterward. This was not an isolated or fixed-frequency machine. Load averages and OS details are retained in `results.json`.

The normal uninstrumented native build uses C99 `-O2`, QBE 1.3 and platform `as`/`ld`. Actual MCP server identity, compiler/QBE/runtime/start-object hashes and retained program-image hash are recorded. No strace, gprof, sanitizer, build or other task-owned CPU work ran during sampling. Separate qualification tracing/profile checks established compile-once reuse; they are not the latency timer.

## Protocol and recollection

1. Build locally with `make` and initialize one persistent `bin/apx-mcp` session. Discover its 13 tools. Never start a new server process per invocation.
2. Read the exact source and input above. Compile one retained handle before timing. `compile` checks but does not execute source tests; test/verify remains a separate operation.
3. Pin the observer/server/descendants to the highest-numbered allowed logical CPU and record the original affinity, machine, settings and binary hashes. Do not change system-wide policy or other services.
4. Prepare parameters before each clock. Run five warmup rounds, then 31 scored rounds. Rotate `(compile, invoke, run)` by the round index modulo three; restart the rotation for the scored phase. Keep every observation; no adaptive stopping or favorable resampling.
5. Time only the `tools/call` request with `perf_counter_ns`: JSON request encoding, IPC, server compilation/execution/limits, response encoding and client frame/JSON decoding. Buffer journals in memory. Disk flush, result assertions and release are outside the timer.
6. Release each newly compiled timed artifact outside its timer; retain the original handle for every invoke. Use the same exact input/limits in invoke and source-per-call run, with a fresh restricted worker and no result memoization. Match the entire execution record, not just output, preserving boolean/integer identity.
7. Release the retained handle, close the session, restore affinity and retain all raw values. Report median/min/max and actual counts separately for each arm.

The original private observer's digest is retained for custody, not advertised as shipped reproduction code. Recollecting with another MCP client implements this stated protocol but is not a bit-identical rerun of the original observer. Client/host differences and process/loader/JSON overhead remain part of results. No independent reproduction or cross-language ranking is claimed.

## Reaggregate without remeasuring

From the repository root, using optional Python 3 standard-library tooling:

```sh
python3 benchmarks/mcp_reuse/reaggregate.py
```

This checks original sample hashes, fixed counts/rotation, current program/fixture bytes and all published summaries. It does not execute Apodexis, contact a model/service, or start a new timing run. Python is not a compiler/MCP runtime or default native-test dependency. No GitHub workflow is introduced.
