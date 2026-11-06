# Native C/QBE backend

Apodexis compiles to native Linux x86-64/glibc executables through a C99 frontend,
QBE 1.3 and the platform assembler/linker. The C value runtime implements checked
operations; it does not interpret source, ASTs or bytecode.

```text
UTF-8 source → parse/check → canonical source + checked AST
                                      ↓
                                   QBE IL
                                      ↓
                         QBE amd64_sysv → assembly
                                      ↓
                          as → object → ld + support
                                      ↓
                             native ELF executable
```

## Build and installation requirements

Source bootstrap needs a C99 compiler, GNU Make, assembler/linker and glibc
development files. The [Makefile](../Makefile) builds vendored QBE, the frontend,
MCP server and runtime/startup support without downloading packages. The default
native build and tests require no Python, Rust, Cargo, Node.js, model or API key.

An installed bundle compiles Apodexis source with its QBE executable and prebuilt
support objects, `/usr/bin/as`, `/usr/bin/ld`, and glibc linker-development files
(typically `libc6-dev` on Debian/Ubuntu). Linking uses `-lc` and
`/lib64/ld-linux-x86-64.so.2`; runtime `libc.so.6` alone is not a sufficient
link-time installation. No C compiler is invoked during installed compilation.

Keep the checkout/bundle layout intact and its `.build/` directory writable:
native compilation creates temporary directories there. Generated applications
need a compatible Linux kernel and runtime glibc, not the compiler, source,
QBE or glibc development files. See [binary installation](binary-distribution.md).

## Implementation layout

| Component | Responsibility |
| --- | --- |
| `compiler/apx_frontend.[ch]` | Bounded UTF-8 parsing, static checking, canonical formatting and caller-owned compiler C API. |
| `compiler/apx_qbe.[ch]` | Checked lowering to native control flow and runtime calls, with fuel and recursion guards. |
| `compiler/apx_metadata.c` | Checked facts, JSON entrypoint contracts and deterministic pseudocode projections. |
| `compiler/apx_hash.[ch]` | Self-contained SHA-256 for identities. |
| `compiler/apx_unicode.[ch]` | Pinned canonical string classification used in formatting and `apx1` identities. |
| `compiler/apx_driver.c` | CLI, stdio MCP, bundled-schema validation and bounded QBE/as/ld/worker orchestration without a shell. |
| `compiler/apx_modules.[ch]` | Deterministic sibling-file/inline module resolution, exports and namespace lowering. |
| `compiler/apx_host.[ch]` | Trusted explicit-policy broker for scoped file, environment, clock, entropy and exact-URL GET operations. |
| `runtime/apx_runtime.[ch]` | Immutable values, pure libraries, strict JSON ABI, checked operations, builders, fuel/traps and native harness. |
| `runtime/apx_start.s` | Native startup support for direct assembly/linking. |
| `third_party/qbe/` | Vendored backend source and upstream MIT license. |

The bundle contains `bin/apodexis`, `bin/apx-mcp`, `.build/tools/qbe`,
`.build/apx_runtime.o`, `.build/apx_start.o`, resources, examples, fixtures and
license notices. [QBE provenance](../third_party/qbe-provenance.json) records the
pinned upstream archive and hash.

## Compiler C API

The frontend exposes these entrypoints in
[`compiler/apx_frontend.h`](../compiler/apx_frontend.h):

```c
int apx_check_source(const char *source, char **canonical_out,
                     char **diagnostics_json_array_out);
int apx_compile_source(const char *source, char **il_out,
                       char **canonical_out, char **diagnostic_json_out);
int apx_compile_tests(const char *source, char **il_out,
                      char **canonical_out, char **diagnostic_json_out);
char *apx_format_source(const char *source, char **diagnostic_json_out);
int apx_describe_source(const char *source, char **explain_out,
                        char **contract_out, char **pseudocode_out,
                        char **diagnostic_json_out);
```

Outputs are initialized to null and are malloc-owned; callers release returned
strings with `free`. Successful parsing can supply canonical source even when
semantic checking fails.

- `apx_check_source` parses and collects independent recoverable semantic errors.
  It returns zero on success and one on errors, with a JSON diagnostic array
  (`[]` on success). The collection budget is 4,096 diagnostics; exceeding it
  reports `P006` and explicitly says checking is incomplete.
- `apx_compile_source` emits checked QBE IL. Passing `il_out == NULL` requests
  parse/check only, without requiring a host-compatible `main`.
- `apx_compile_tests` emits native declared-test code independently of the JSON
  entrypoint ABI. Helpers can be tested without a host-compatible `main`.
- `apx_format_source` is parse-only formatting, not a successful static check.
- `apx_describe_source` produces checked facts, contract and pseudocode; null
  output pointers skip unneeded projections. Compile/describe return the first
  error in their compact diagnostic output rather than the aggregate check array.

None of these APIs executes authored computation. The driver resolves modules
and orchestrates native builds/runs separately. This compiler interface is not a
supported application-embedding package or arbitrary host-callback interface;
internal AST declarations are not an application API.

## Diagnostics and authoring contracts

CLI `check --json` returns `apodexis.check_response.v1`. MCP `apodexis_check` and
the check context inside `verify` return `apodexis.repair_response.v1`. After a
valid parse, independent recoverable semantic errors are reported together.
Compilation and metadata generation fail closed on errors.

Diagnostics carry UTF-8 source locations; typed holes retain expected types and
lexical context. CLI/MCP diagnostic rendering has a separate 2 MiB transport
budget. An oversized collection or expanded hole scope produces explicit `P006`
incomplete/resource diagnostics, not a silently empty or complete-looking
truncated collection. This is separate from the C API's collection budget and
untrusted JSON input limits.

User-function arguments are positional; struct constructors use named fields.
Active locals and parameters cannot be shadowed. Checked `row.id`/`rows[index]`
reads normalize to field/index operations; `P out value` normalizes to
`S out (push out value)`. Prefix formatting gives shorthand and its exact
expansion the same canonical identity. Deleting a local annotation is still a
source change and may change that identity.

Arrays, records, maps and builders preserve immutable snapshots. Local binding
updates do not mutate aliases. Function signatures remain explicit; ambiguous
empty arrays and variant constructors need an expected type. See the
[language](../resources/language.md) and [types](../resources/types.md) references.

Repairs currently perform canonical-source replacement with exact source-text
hash and compiler-issued-payload preconditions. They return revised inline
source rather than writing files. Pseudocode is a readable projection, not an
alternative executable contract.

## Native scalar representation

Checked `i64` expressions, local bindings, function parameters and helper returns
use QBE `l` machine integers. `bool` uses QBE `w`, including consistent word-sized
local loads/stores and call signatures. Boolean `&` and `|` still evaluate both
operands in textual order. Integer comparisons and boolean operations consume
registers; a small nonallocating runtime helper preserves checked addition,
subtraction, multiplication, division and remainder. Both division and remainder
trap on `i64::MIN / -1` (or `% -1`), and both reject a zero divisor.

The public `apx_entry(ApxValue *)` ABI and `apx_test_run` remain boxed. The entry
wrapper validates input before unboxing a scalar parameter and boxes a scalar
answer. Scalars are explicitly boxed at generic builtin, record, variant and
host-operation boundaries, then checked and unboxed on scalar reads and match
payloads. Typed i64 literals and proven private i64 builders instead write raw
integers; typed `E` and index reads accept both packed and legacy boxed arrays
without allocating scalar boxes. Float/bool/string arrays and operations retain
their existing representations. Checked named-record reads use declaration-order
positions, as described below.

Statement/loop fuel charges and logical function enter/leave checks remain at
their existing points, with no batching. Small scalar leaf helpers may be
inlined under the bounded rules below; their logical calls are still metered.
Pure scalar numeric loops need no per-iteration scalar allocation; transformed
input values and public input/output objects still allocate.
`make test-native-scalars` checks emitted IR, links a real generated entrypoint
to an allocation observer, and exercises mixed boundaries, eager errors, checked arithmetic, fuel and recursion.
The same test's runtime helpers and linked observer run under ASan/UBSan in
`make test-sanitize`; ordinary driver-managed worker execution remains separate.

## Bounded helper inlining and compiled metering

The initial inline rule accepts only functions with one return statement, scalar
`i64`/`bool` parameters and result, and a leaf expression made from scalar
literals, parameters, checked integer arithmetic, comparisons and eager boolean
operations. No user calls occur in an eligible body, so recursive cycles cannot
qualify. Locals, branches, loops, containers, floats, generic builtins and host
effects retain out-of-line calls. An eligible expression has at most 32 AST nodes
and 16 parameters. Each emitted function expands at most 256 AST nodes and 32
call sites; remaining calls retain the same native ABI. Out-of-line definitions
are retained even for eligible helpers.

Arguments execute exactly once in textual order, in the caller, before the
logical callee enter. The inline expression substitutes those native registers
by checked callee parameter ID, not caller-local slots. This preserves bool/i64
widths, unused-argument effects and nested argument calls without binding-ID
collisions. An inlined call still enters, charges its return statement, computes
with the existing checked operations, and leaves. Physical call removal does not
remove logical recursion limits or any semantic fuel.

Fixed one-unit statement and two-unit taken-loop charges use direct QBE state
operations at the original points. Each charge checks unsigned addition wrap and
unsigned `next > limit`, then stores only on success. This includes nondefault
limits above `INT64_MAX` and near `UINT64_MAX`. There is no loop-wide batching or
speculative precharge. Logical enter stores the incremented depth before checking
the unchanged 256-frame limit; leave decrements only a nonzero depth. Per-function
cold blocks call the runtime's exact existing fuel/depth diagnostics and terminate
with QBE `hlt` if a failure helper were ever to return.

The trusted internal amd64_sysv `ApxExecutionState` ABI is align 8, size 24:
`uint64_t limit` at offset 0, `uint64_t used` at 8, `int32_t depth` at 16, and a
reserved zero word at 20. Header-level C99 layout assertions and linked tests pin
these offsets. Generated code and compatibility `apx_charge`, `apx_enter` and
`apx_leave` all use the one `apx_execution_state` object, initialized to a
1,000,000 fuel limit and zero usage/depth. Only addresses are hoisted, not shadow
counters across calls. Arena/JSON reset clears depth but preserves fuel limit
and usage; invocation/test drivers retain their existing initialization policy.
Private in-process fixture names remain aliases of this state. This is serial
execution state, not a new thread-safe or reentrant embedding API.

`make test-native-calls` checks eligible/fallback IL and expansion bounds, then
links generated objects to a runtime observer for exact complete outputs,
diagnostics, every charge boundary on finite statement/loop/call traces,
uint64 fuel edges, argument order and logical-depth failures before reset.
Direct/mutual recursion and calls into an inline leaf at the depth boundary are
included. Compatibility API interleaving, repeated entries, reset/recovery,
no-main source tests and real explicitly authorized argument file writes are
also exercised. `make test-sanitize` instruments both the observer and its linked
runtime with ASan/UBSan; generated QBE instructions are not sanitizer-instrumented.
These are correctness/representation checks, not a measured speedup claim.

## Runtime allocation and construction

Runtime values use bounded, zero-initialized execution-lifetime arenas. Small
objects bump through pooled blocks with at most 1 KiB of backing per block;
large requests receive exact-sized dedicated backing without displacing the
small-object pool. Returned addresses remain stable. Reset frees every linked
block and clears allocation/call-depth state without resetting semantic fuel.
There is no cross-invocation arena cache, garbage collector, reference counting,
large sparse reservation or new ownership annotation.

The requested-payload limit remains 128 MiB. Arena backing has a separate
256 MiB ceiling including block headers, padding and unused capacity. These are
not RSS measurements and do not increase the native worker's existing
address-space limit. The library-only CLI/MCP JSON implementation uses the same
allocator. Parsed values live until reset; encoded JSON strings remain
separately allocated, caller-owned strings valid after reset.

Builders use append-only blocks containing up to 64 element references. A
snapshot's length bounds its visible prefix. Appending at that block's current
frontier writes only a previously invisible slot; a divergent alias starts a new
block. `finish` materializes an indexed array and caches that immutable result
for repeated finishes of the same snapshot. This does not consume the builder,
mutate earlier arrays, preallocate the declared full capacity, or enlarge every
scalar value. This persistent path remains the fallback for aliases and forks.

Typed i64 literals use contiguous `int64_t` buffers behind the same opaque array
API. Parsed JSON arrays remain boxed: validation does not repack inputs. Equality
and JSON encoding use synchronous, nonescaping scalar
views; packed slice/set/concatenation preserve raw storage where possible. Other
compatibility consumers obtain stable, arena-owned scalar boxes through a bounded
memoized cache, never pointer-cast integer storage. `apx_json_item` contains boxing
allocation failures locally and returns null; successful items live until reset,
including across later encoding or construction. Raw runtime accessors still trap
through the caller's active execution boundary.

The compiler selects private i64 construction only after examining every use of
a checked local binding ID across the whole function, including branches. Its
permitted uses are `count`, `cap`, `finish` and a direct self-assigned `push` (also
`P` shorthand). Alias assignments, forks, embedding and any unsupported use disable
the optimization for the entire binding. Builder function-boundary types remain
rejected by the existing frontend. No ownership syntax or new ownership system
is introduced. Other builder element types retain the persistent implementation.

A proven private builder reuses one descriptor and grows its raw buffer
geometrically, bounded by declared capacity and existing allocation limits.
Capacity initialization is metadata-only, even at the maximum i64 capacity.
Finished arrays capture a buffer and length; append writes only the next invisible
slot, and growth retains the previous backing until reset. Repeated finishes at
an unchanged length reuse the cached array; later appends invalidate only the
builder's cache, not earlier arrays or boxed items. Allocation failures publish no
partial growth or finish state. All retained backing and compatibility boxes count
against the unchanged payload and backing limits.

`make test-native-collections` covers packed/generic consumers, extrema, alias and
fork fallback, captured prefixes at 0/1/63/64/65/4096, capacity/OOM recovery and
actual compiled-entry packing/private-append allocation. It is included in the
default suite and selected ASan/UBSan paths, alongside the scalar-loop observer.

Allocation representation can change OOM timing, but not the structured failure
contract or the quota itself. ASan builds add poisoned logical-object boundaries
inside arena blocks; selected developer sanitizer paths are not full generated
worker sanitizer coverage. The optional [memory benchmark tooling](../benchmarks/runtime_memory/README.md)
keeps timing, allocation counters, peak RSS and installed-process checks at
separate boundaries. It is not a compiler/runtime dependency or a universal
performance claim.

## Known record shapes and static input schemas

The compiler resolves each checked field read from its receiver's named type,
not a field-name-only table. The bounded `apx_field_index` helper reads a constant
declaration-order position and returns the ordinary boxed member; scalar reads
then use the existing checked unboxing boundary. Constructors, validated input
records and immutable `with` copies all retain that layout. Name-based `apx_field`
and C JSON getters remain available for dynamic/compatibility consumers.

Generated `apx_entry` calls `apx_validate_static` with artifact-lifetime readonly
QBE data, not a parsed schema or an arena-backed global cache. The checked
schema-eligibility/depth walk still runs before descriptor emission. Exported
`apx_schema_json` bytes and contract metadata are unchanged; the compatibility
`apx_validate(const char *, ApxValue *)` still parses and validates dynamic schema
JSON. Zero-parameter mains retain the null-schema pass-through behavior, and
source-test artifacts remain independent of host-input ABI eligibility.

The internal amd64_sysv C/QBE descriptor ABI is explicit: align 8, size 48,
six 8-byte slots (`kind` at 0, `name` at 8, `count` at 16, `item` at 24,
`error` at 32, `fields` at 40). Each field is an align-8, 16-byte pair of name
and descriptor pointers at offsets 0 and 8. Kind values are null=0, i64=1,
f64=2, bool=3, str=4, array=5, map=6, option=7, result=8, struct=9.
This trusted internal pointer ABI is not a wire format or an interface for
untrusted descriptor addresses. Null children, invalid kinds/counts and recursive
depth are defensively checked; descriptor storage must itself be addressable.

Validation retains declaration-order member errors before extra-field errors,
including the legacy extra-field selection rule. Typed map keys retain
length-aware unsigned UTF-8 sorting and embedded NUL bytes. Supported map
payloads include nested arrays/records/options/results, with `err` payloads in
`error`, not `value`. Integer-to-f64 promotion and text-backed parsed-integer
float normalization are unchanged. All transformed values and temporary map-key
arrays remain arena/quota-accounted; descriptors themselves allocate nothing
per invocation and survive reset without retaining execution values.

`make test-native-shapes` checks emitted constant positions, explicit C layout,
readonly ELF descriptor segments, exact exported schemas and linked-entry
outputs/errors/fuel. It interposes generated validator calls to reject dynamic
schema parsing and checks scalar entry allocations across repeated calls and
resets. Invalid descriptors, depth boundaries and allocation-failure recovery
are also exercised. The focused suite and its linked runtime observers run under
ASan/UBSan in `make test-sanitize`; QBE-generated instructions are not themselves
sanitizer-instrumented. These are representation/compatibility checks, not a
performance qualification claim.

## JSON fast paths and backend assessment

The bounded JSON number grammar is unchanged. Representable signed i64 tokens
are accumulated directly from the supplied bytes, with unsigned magnitude/range
checks and no temporary token or `strtoll`. Decimal/exponent tokens and integers
outside i64 retain the existing finite `strtod` fallback; raw large-integer text
remains available for exact unsigned64 access and encoding. Finite subnormals,
underflow, signed zero and float round-trip spelling retain their existing paths.
Signed integer output uses a bounded 20-byte stack buffer, not per-value
`snprintf`/`strlen`.

Unescaped strings are copied once after bounded control/UTF-8 validation; escaped
strings keep the escape/surrogate decoder. Encoding emits ordinary byte runs in
bulk and preserves its byte treatment of C-created strings. Key lengths, embedded
escaped NUL, sorted output, depth/quota checks and caller-owned encoded buffers
are unchanged. A separate correctness-only repair replaces the preexisting
`strchr` whitespace predicate, which also matched its terminating NUL, with
explicit space/tab/CR/LF recognition. Raw NUL at a whitespace-skip position is
now rejected as invalid JSON (for example the supplied bytes `null\0` or
`"a"\0`), an intentional invalid-input compatibility change. Raw NUL inside a
string remains invalid; escaped `\u0000` remains valid and length-aware. Earlier
benchmark evidence is historical and does not qualify this correctness repair.
Allocation representation and OOM locations may change, not the hard quotas or
guarded public-API recovery contract.

`make test-native-json` checks token allocation, forbidden integer formatting,
bulk copies, exact numeric/string/error vectors, bounded buffers, deterministic
fuzz, quotas and failure/reset/retry. It also compiles and links real generated
entries. The target is in the default suite; `make test-sanitize` instruments both
its observers and linked C runtime (not QBE-generated instructions).

Backend disposition: retain QBE `amd64_sysv` and the default C99 `-O2` build
flags. Inspection of current generated SSA and assembled machine code for
`benchmarks/runtime_memory/cases/{numeric-helpers,each-sum,builder-writeback,agent-rowdiff}.apx`
shows register-valued scalar loops, direct fuel/depth guards, checked-arithmetic
runtime calls, packed reads/private-builder calls, and indexed record reads.
Numeric main has four checked-arithmetic call sites and no call to its inlined
leaf; each-sum retains a typed item-read and checked-add call in its loop;
builder-writeback retains private append and checked increment calls. Rowdiff
retains generic string comparisons/builders and nested linear searches. Static
entry validation remains outside these authored functions.

For the inspected objects, numeric main (`f1`), each main (`f0`), builder main
(`f0`) and rowdiff's diff (`f3`) contain respectively 162/111/138/546 decoded
instructions and 6/5/6/49 call sites. These are static whole-function counts,
including cold failures and alignment instructions, not executed instruction
counts or timing attribution. Reproduce with `bin/apodexis emit-qbe SOURCE >
"$TMPDIR/program.ssa"`, `.build/tools/qbe -t amd64_sysv -o "$TMPDIR/program.s"
"$TMPDIR/program.ssa"`, `as "$TMPDIR/program.s" -o "$TMPDIR/program.o"`, then
`objdump -dr --no-show-raw-insn "$TMPDIR/program.o"`.

This structure identifies remaining runtime/lowering work, but does not establish
QBE as the dominant cost or a backend ceiling. No controlled alternative-backend
or release-flag comparison was performed; it supports neither a projected LLVM
speedup nor C/Rust parity. JSON-pipeline timing, core execution and cumulative
cross-language qualification remain separate measurements.

## Boundaries

- **Platform:** Linux x86-64/glibc with QBE `amd64_sysv`. Other backend targets are
  not qualified deployment targets.
- **Language/ABI:** ordinary source/type checking has a depth-64 budget. Protocol
  JSON and generated-schema wrappers have separate finite budgets. General
  custom-enum construction/execution and general option/result/enum JSON inputs
  are not implemented (options/results are supported inside typed map payloads);
  option/result values and matches inside native functions/tests
  are supported. Builders are local-only.
- **Execution:** runtime arenas, recursion, fuel, input/output and process output
  are bounded. Driver-managed children have CPU, address-space, stack, file-size
  and wall deadlines. Each compilation/execution subprocess has a 30-second
  deadline; a tool call can include several subprocesses.
- **Kernel:** native-worker seccomp denies filesystem opens, sockets and process
  creation. Allowed mappings are anonymous/non-executable. Failure to install
  required restrictions fails closed.
- **Host effects:** fixed typed calls use an inherited trusted broker channel,
  never direct worker host access. An operator-owned CLI `run --capabilities`
  policy grants specific operations. Tests, `verify`, ordinary MCP and artifacts
  without a trusted broker channel remain ungranted. The optional HTTP adapter
  uses `/usr/bin/curl`; it is not bundled or required for pure programs.
- **MCP:** local newline-delimited stdio JSON-RPC, serial dispatch and bounded
  frames. Cancellation does not interrupt an in-flight worker. Use a finite
  client tool timeout around 130 seconds, separate from connection/startup
  timeouts; see [client setup](mcp-client-setup.md).
- **Applications:** no WASM/WASI or GPU target, HTTP listener, database adapter,
  event loop, general function values, package registry or arbitrary host
  callbacks. Response examples transform data rather than serving requests.

Pure semantics do not make host observations deterministic. An application
`result` error is distinct from a trap or process failure. Program identities and
passing tests are not correctness proofs. The compiler, runtime, broker, QBE,
distribution and operating system remain trusted components; this experimental
implementation is not a certified sandbox. Read [SECURITY.md](../SECURITY.md) and
[capability policies](capabilities.md).

## Development verification

```sh
make all check test
make test-sanitize
make dist
```

The native suite covers primitives, pure libraries, modules, ABI/rejection
fixtures, declared tests, MCP discovery/execution, repair preconditions,
diagnostic transport, host policies, kernel restrictions, the compiler C API and
full-scalar Unicode formatting. Sanitizer tests instrument selected developer
paths with ASan/UBSan; sanitizer tooling is not a distributed dependency.

Packaging changes also need an extracted, relocated bundle smoke and a generated
application run. These commands describe the contributor gates, not an assertion
that they have run for any particular revision. See
[CONTRIBUTING.md](../CONTRIBUTING.md) for evidence and compatibility requirements.
