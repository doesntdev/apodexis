# Local cross-language comparison

Optional developer measurement tools. Python, Node and Rust here are benchmark
requirements only: none is a compiler, build or runtime dependency of Apodexis.
No GitHub compiler CI, workflow, service or benchmark schedule is installed.

## Recorded local campaign

See [results and complete ranges](../../docs/benchmarks.md) and
[sanitized observations](results.json). The offline `test_results.py` check
reconstructs all cross-language tables from retained raw timing totals.
These are author-produced local measurements, not a language ranking.

## Scope frozen before scored runs

Compare Apodexis's current C99/QBE native implementation with explicit C, Rust,
Python and JavaScript implementations on the same valid JSON fixtures. Keep
all workloads, including unfavorable results; do not produce a composite
"language speed" ranking. This is a small, author-produced, single-host corpus,
not a comprehensive or independently reproduced language benchmark.

The kernels reproduce the existing native-memory corpus:

| Kernel | Scored inputs | Work |
| --- | --- | --- |
| Scalar control | 7 | Add one; primarily measures fixed costs. |
| Numeric helpers | 256, 2,048 | Loop and accumulate `i * 3 + 7`. |
| Array reduction | 128, 4,096 integers | Explicit loop sum; no built-in reduction. |
| Array construction | 256, 4,096 | Append `0..n-1`, then return the complete array. |
| Record differences | 32, 64 rows on each side | Duplicate detection, linear membership/hash scans, stable added/removed/changed lists. |

Record implementations retain the same nested-scan algorithm, not a hash-map
rewrite. The oracle independently uses sets/maps. Inputs and every complete
output are checked; scalar checksums alone are insufficient for collection
results. Correctness probes additionally include empty and signed collections,
changed row order, and duplicate IDs on either side.

## Measurement boundaries

- **Core:** per-invocation function/entrypoint work and output allocation. JSON
  parsing and encoding, fixture reads, process startup and teardown are outside
  the timer. Apodexis's generated typed-input validation and fuel charges remain
  inside its entrypoint; C/Rust typed-input conversion is before their kernels.
  This is not identical instruction work or identical safety guarantees.
- **In-process JSON pipeline:** fresh JSON parse and typed conversion, computation,
  complete output encoding/verification and cleanup. Includes the per-invocation
  core clocks and harness checks. Excludes imports, fixture reads, warmup,
  process startup, compilation, host broker and OS sandbox setup. Different JSON
  libraries and representations are part of this result. Every runner compares
  the complete encoded result with an untimed canonical expected encoding; no
  runner reparses its output inside the pipeline merely for verification. GC
  follows normal language behavior and may defer reclamation past a call.
- **One-shot tool invocation:** elapsed parent-side spawn/write/read/wait of a
  prebuilt program given JSON on stdin. Includes startup/imports, parse,
  computation, serialization and exit. Apodexis uses its standard generated ELF,
  including normal restrictions and its structured result envelope; the others
  emit the result value without that envelope or an equivalent sandbox. It is
  therefore practical invocation latency, not isolated computation speed.

Never subtract a startup control from another timing, compare medians across
these boundaries, call response construction server throughput, or claim
compilation, network, model/agent or end-to-end task speed from these results.

## Implementation differences

- C uses owned native arrays/row structures in its kernels and the bundled
  Apodexis JSON support for transport, not a best-in-class C JSON library.
- Rust uses owned `Vec`/row structures and pinned `serde_json`; release arithmetic
  overflow checks remain enabled. C arithmetic uses checked signed operations.
- Python uses ordinary integers, lists and dictionaries; JavaScript uses ordinary
  Number values, arrays and objects. All frozen numeric values are within exact
  JavaScript integer range. This does not establish arbitrary-i64 equivalence.
- Apodexis retains checked arithmetic, semantic fuel and immutable builder
  snapshots. Baselines do not emulate its fuel, memory quota, capability system
  or snapshot contract. Builders retain no earlier versions in these kernels,
  so ordinary mutable arrays are appropriate baseline implementations.
- Compilers/JITs may optimize equivalent loop implementations differently. No
  artificial volatile operation is inserted into the source algorithm.

## Protocol and reproduction

Build Apodexis with its documented native toolchain first (`make all check test`).
Additional optional tools are Python 3.11+ with its standard library, Node.js,
Cargo/Rust, GCC-compatible C, and `taskset` on Linux. Rust dependencies are pinned
in `rust/Cargo.lock`; populate the Cargo cache once if needed. The campaign itself
builds Rust offline and fetches nothing. Source releases remain source-only.

```sh
# Run from repository root; choose a new output path, not an existing campaign.
EVIDENCE="$PWD/.scratch/cross-language-run"
python3 benchmarks/cross_language/run.py --output "$EVIDENCE" prepare
python3 benchmarks/cross_language/run.py --output "$EVIDENCE" measure
python3 benchmarks/cross_language/run.py --output "$EVIDENCE" verify
python3 benchmarks/cross_language/test_results.py
```

`prepare` builds outside timers, verifies independent oracles, captures a pilot
and freezes the protocol before scoring. The default is nine serial rounds,
with case and language order rotated deterministically. Every process is pinned
to one recorded allowed logical CPU; no governor, boost or system-security
setting is changed. Normal warm OS file caches are retained. Each in-process
sample performs 1,000 untimed warmup invocations (including JIT work) and uses
pilot-calibrated repetitions targeting 30 ms, bounded to 5..100,000. Counts are
fixed per language/case across scored rounds, and verified from every receipt.
Warmup is a policy, not proof that all JIT tiers or GC behavior have stabilized.

The core figure is the median of nine **per-sample averages**, not a request
latency percentile. Publish per-case medians and min/max, with raw total ns and
repetition counts sufficient for independent reaggregation. Tiny control timings
include clock overhead. One-shot samples have no in-process warmup.

`verify` rejects incomplete/duplicate rounds, wrong case/language, mismatched
outputs or repetitions, changed source/fixture/binary identities and altered
aggregates. Resume retains already completed scored rows without resampling;
use a new campaign directory for changed code or protocol. Failed preparation
or measurements remain visible and are never relabeled successful. An offline
sanitized evidence export must keep every fact needed to reconstruct the
published tables while excluding private paths and planning archives.
