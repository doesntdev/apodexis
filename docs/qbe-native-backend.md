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

## Boundaries

- **Platform:** Linux x86-64/glibc with QBE `amd64_sysv`. Other backend targets are
  not qualified deployment targets.
- **Language/ABI:** ordinary source/type checking has a depth-64 budget. Protocol
  JSON and generated-schema wrappers have separate finite budgets. General
  custom-enum construction/execution and option/result/enum JSON inputs are not
  implemented; option/result values and matches inside native functions/tests
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
