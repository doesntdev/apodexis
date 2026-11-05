# Native runtime memory benchmarks

Optional, local developer tooling for the C99/QBE runtime. This is not part of
normal compilation, installed execution or the default test/build dependency
path. No GitHub workflow is required or installed.

The maintained corpus covers scalar arithmetic, scaled array iteration/indexing,
numeric helpers, nested records, builder accumulation, repeated finishes,
agent-style row differences and static response generation. It compares runtime
representations while holding the generated QBE program constant.

## Published observations

See [local results and limitations](../../docs/benchmarks.md) and the
[sanitized before/after observations](results.json). Run
`python3 benchmarks/runtime_memory/test_results.py` to reaggregate those timing
and memory facts offline. This does not replace full local artifact qualification.

## Requirements

- The normal native source toolchain: C99 compiler, bundled QBE, assembler/linker
  and glibc development files.
- Python 3 with its standard library, for optional evidence orchestration.
- GNU `/usr/bin/time` for separate process-memory observations.

These extra measurement tools are not dependencies of an installed Apodexis
compiler or generated application. The qualified deployment target remains
Linux x86-64/glibc; this harness does not establish a GPU or WebAssembly target.

## Reproduce a comparison

Run from the current checkout, use fresh evidence output, and do not run
unrelated builds or heavy workloads during sampling. The following revisions
identify the original baseline and arena-only implementation; the current
checkout supplies the combined arena/builder runtime.

```sh
git worktree add --detach ../apodexis-memory-baseline 364b2a5ee255e8049e74e1a43bfd1e31a4c937d1
git worktree add --detach ../apodexis-memory-arena 12064a5a67f205bf7de9515b6a16884a35a4139f
make -C ../apodexis-memory-baseline all
make -C ../apodexis-memory-arena all
make all check test
make test-sanitize

EVIDENCE="$PWD/.scratch/runtime-memory-run"
python3 benchmarks/runtime_memory/run.py --output "$EVIDENCE" prepare \
  --baseline ../apodexis-memory-baseline
python3 benchmarks/runtime_memory/run.py --output "$EVIDENCE" variant \
  arena-small ../apodexis-memory-arena
python3 benchmarks/runtime_memory/run.py --output "$EVIDENCE" variant builders .
python3 benchmarks/runtime_memory/run.py --output "$EVIDENCE" compare \
  baseline arena-small --label arena-small-v1
python3 benchmarks/runtime_memory/run.py --output "$EVIDENCE" compare \
  arena-small builders --label builders-vs-arena
python3 benchmarks/runtime_memory/run.py --output "$EVIDENCE" compare \
  baseline builders --label builders-vs-baseline
python3 benchmarks/runtime_memory/qualify.py "$EVIDENCE" --collect-memory
python3 benchmarks/runtime_memory/test_qualification.py "$EVIDENCE"
```

`prepare` verifies baseline native ELF outcomes, independent expected values and
route status, then captures a bounded baseline/noise pilot and freezes the
protocol before candidate sampling. It records source/input/binary/harness
identities, effective repetitions, compiler/platform, affinity and acceptance
bars. Builder sizes include 0, 1, 32, 256, 1,024 and 4,096 appends. Source/corpus,
repeat binaries and ELF artifacts are retained once per named producer path;
never edit a recorded runtime or reuse a variant name for different inputs.
Choose a new output directory for a new protocol. The qualifier requires the
exact local artifacts; archives/receipts are private operator evidence, not
additional shipped binaries or a public performance guarantee.

A completed `compare` command means collection and aggregation completed. It can
report failed control gates without an unsuccessful process exit. **Run the
qualifier before treating a candidate as accepted.** A failed control permits
one additional fresh rotated comparison under the same protocol, preserving
both results; it does not authorize adjusting the bars.

## What is measured

| Mode / observer | Boundary |
| --- | --- |
| `pipeline` | Repeated in-process JSON parse, generated entrypoint, output encoding/verification and memory reset. Files are read and code is built outside timing. Excludes process launch, CLI compilation, broker transport and worker sandbox setup. |
| `core` | Generated entrypoint only, including its typed-input validation; separate cleanup timing. Input parsing and output encoding/verification are outside this timer. Tiny entry timings include clock overhead and are not the main regression gate. |
| Counter binary | Separate compilation with heap/copy wrappers. Counts runtime `calloc` requests, their requested backing bytes, frees and prefix `memcpy` bytes. Never use its timings as speed evidence. |
| Isolated memory | GNU time uses a small C launcher to collect peak RSS independently of the Python launcher's inherited high-water floor. Original timed `getrusage` fields are retained but are not authoritative for small-process RSS. |
| ELF parity | Baseline and candidate one-shot sandboxed executables must preserve frozen output, source identity and semantic fuel. These checks are not server-throughput measurements. |

Requested payload bytes are cumulative allocation work within an invocation,
not logical live-value memory or physical RAM. Backing requests include arena
metadata and unused space but not all libc overhead. Peak RSS is a separate
process-level measurement. Do not subtract unmatched medians to manufacture a
compute-only result, report an OOM as an infinite speedup, or infer HTTP requests
per second from response construction.

The initial protocol uses nine rotated rounds and separately gates ordinary
controls, larger core measurements, finish-heavy stress and RSS. It requires a
minimum targeted improvement in the large builder workload. `qualify.py`
independently reconstructs medians and requires exact case/variant/round and
repetition agreement, binary bindings, output/fuel parity and the selected
control/target gates. `test_qualification.py` proves rejection of uniform
cross-variant iteration drift, duplicate/missing rounds and wrong binaries.

## Semantics and scope

Runtime allocation does not change the language's immutable snapshots, canonical
identity, eager evaluation, semantic fuel or default-denied capabilities. Memory
representation changes can change the allocation at which OOM occurs; neither
quota increases nor identical OOM locations are implied. A builder's declared
capacity is still metadata, not an eager capacity-sized allocation.

Append-only blocks preserve old snapshots through their length bounds. Branching
from an older frontier starts a new block; finishing materializes the indexed
items once for that exact snapshot. Later appends cannot change a finished
array. Programs that finish every intermediate prefix can still require growing
total materialization work.

See [runtime implementation](../../runtime/apx_runtime.c),
[native backend](../../docs/qbe-native-backend.md) and
[builder language contract](../../resources/stdlib.md).
