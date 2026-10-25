# Apodexis

**An experimental compiled language for AI-agent-authored tools, with deterministic core semantics, bounded execution, and structured validation and repair over MCP.**

Apodexis turns agent-authored source into checked native programs. An agent can
inspect the language, submit source, receive structured diagnostics, apply
compiler-issued repairs, run source-declared tests, and execute against typed JSON
input under explicit limits. Humans can use the same compiler from the command line.

**Start here:** [Project website](https://doesntdev.github.io/apodexis/) ·
[Language specification](docs/specification.md) ·
[Initial setup](#initial-setup) · [Examples](#examples) ·
[Agent integration](#agent-integration-with-mcp) ·
[Contributing](CONTRIBUTING.md) · [MIT license](LICENSE)

## Status

The working implementation is a **standalone C99 frontend and C runtime using
QBE 1.3 to produce native Linux x86-64/glibc executables**. Its language kernel
reports version `0.1.11`. This is an experimental project, not a production-readiness
or independently audited security claim.

## Purpose

AI agents can already write Python, JavaScript and shell scripts. Apodexis explores
whether a smaller, typed, machine-checkable programming surface can make generated
tools easier to validate, repair, bound and audit.

The design puts these concerns in the normal authoring loop:

- **Check before running.** Explicit function signatures, typed values and structured
  diagnostics expose mistakes before an execution path reaches them.
- **Repair against compiler facts.** Diagnostics, expected types for typed holes,
  source locations and guarded repair objects are machine-readable interfaces.
- **Make behavior inspectable.** Canonical source, program identities, source tests,
  entrypoint contracts and deterministic pseudocode connect the authored program
  to the thing being reviewed and run.
- **Bound execution.** Checked arithmetic, indexing and builders operate within fuel,
  memory, recursion, input/output and worker limits.
- **Grant effects deliberately.** Pure computation has no ambient file, network,
  environment, clock, entropy or process access. Supported host operations require
  an operator-owned policy and a trusted broker.

Apodexis is not a prompt language, an agent planner or an ML framework. It does not
contain a model or require an API key. Agent clients may use their own models to
write its source. The project does not claim that agents perform better with it
than with established languages without a suitable comparison.

## What works today

| Area | Implemented surface |
| --- | --- |
| Native compilation | Source → C frontend → QBE IL → assembly → native ELF. No interpreter or JIT fallback. |
| Language | Functions, structs, arrays, typed string-keyed maps, conditionals, loops, option/result values and matching, local builders, assertions and traps. |
| Authoring | Checked `row.field` and `rows[index]` reads, inferred unambiguous locals, and `P` builder writeback. Arithmetic and ordinary calls remain prefix expressions. |
| Pure libraries | Unicode-scalar string operations, strict numeric conversions, immutable maps, array updates and record-copy updates. |
| Modules | Explicit imports/exports, sibling-file CLI resolution and inline MCP bundles. |
| Testing | `X` source tests; combined format/check/test verification; native conformance, regression and sandbox tests. |
| Agent tooling | Local stdio MCP discovery, language resources, structured checking, guarded canonical repairs, execution, contracts and pseudocode. |
| Host capabilities | Policy-controlled file reads/writes, named environment reads, wall clock, entropy and exact-URL HTTP GET through a broker. Denied by default. |

Important limits remain. There is no browser/WASM target, GPU backend, HTTP
listener, database adapter, event loop, package registry, LSP, general function
value or arbitrary host callback. General user-enum construction and
option/result/enum JSON inputs are not implemented. The compiler's C API is not a
promise of a packaged, stable application-embedding library.

## Initial setup

### Requirements

The supported native deployment target is **Linux x86-64 with glibc**. Other QBE
architectures and native Windows/macOS/mobile deployments are not qualified by
this implementation.

For a source build, install:

- A C99 compiler, such as GCC or Clang.
- GNU Make.
- The standard platform assembler and linker, `as` and `ld`.
- The platform C library and development files.
- `tar`/gzip if you want to create or extract a distribution.

For example, on Debian/Ubuntu these development prerequisites are supplied by:

```sh
sudo apt-get update
sudo apt-get install build-essential binutils
```

No Python, Rust, Cargo, Node.js, LLVM, model download or package-manager install is
needed for the native compiler, MCP server or default native test suite. QBE is
vendored with its [license](third_party/qbe/LICENSE) and
[pinned provenance](third_party/qbe-provenance.json); the source build does not
fetch compiler dependencies. The optional HTTP GET capability uses the fixed
`/usr/bin/curl` broker adapter; it is not a default build or test dependency.

### Build from a source checkout

Clone the public source repository, then build and test from its root:

```sh
git clone https://github.com/doesntdev/apodexis.git
cd apodexis
make
make check
make test
```

The main executables are:

```text
bin/apodexis   command-line compiler and tooling
bin/apx-mcp    local stdio MCP server
```

The compiler finds QBE, support objects and reference data relative to the
checkout/distribution. Keep that layout together rather than moving only
`bin/apodexis` onto your `PATH`. Use a user-writable checkout/distribution:
native compilation creates temporary directories inside its `.build/` directory.

## First program

[examples/double_values.apx](examples/double_values.apx) doubles an input array and
contains its own tests:

```apodexis
M double_values

F main values:[i64]->[i64]
  L out (buf i64 (# values))
  E value values
    P out (* value 2)
  R (finish out)

X empty
  A (= (main []) [])

X signed_values
  A (= (main [1 -2 0]) [2 -4 0])
```

Verify it and run the supplied JSON input:

```sh
bin/apodexis capabilities
bin/apodexis check examples/double_values.apx --json
bin/apodexis verify examples/double_values.apx --json
bin/apodexis run examples/double_values.apx --input fixtures/double_values/values.input.json --json
```

The fixture is exactly `[1, -2, 0]`. A successful run returns a structured JSON
envelope with `status: "ok"` and `output: [2, -4, 0]`, not just a bare array.
`check` does not execute the program; `verify` combines formatting, checking and
its declared `X` tests. Still run representative inputs through `main`.

A few rules make the syntax less surprising:

- `M` declares the module, `F` a function, `L` a local and `R` a return.
- `E value values` iterates values. `P out expression` appends to a builder and
  replaces that binding with its updated value; it does not mutate aliases.
- Use two-space indentation. Each statement/expression occupies one physical
  line; parentheses do not provide multiline continuation.
- Ordinary calls/operators are prefix, such as `(* value 2)` or `(helper input)`.
  Field and array reads can use `row.id` and `rows[index]`.
- Keep function signatures explicit. Infer straightforward locals, but retain
  annotations for ambiguous empty arrays or variant constructors.
- `X` declares a test and `A` asserts a boolean. A successful empty test list is
  not evidence that the program does the right thing.

Read the [implemented syntax](resources/language.md), [types and JSON ABI](resources/types.md)
and [standard library](resources/stdlib.md) for the complete supported surface.

## JSON entrypoints and results

- `main` has zero parameters or one explicitly typed parameter. Helpers may have
  multiple parameters, and source tests call helpers directly.
- A zero-parameter `main` uses absent/null JSON input: omit CLI `--input`, or send
  `"input": null` to MCP. Null is not a language value or a typed-parameter default.
- Inputs accept bool, i64, finite f64, strings, homogeneous arrays, typed maps as
  JSON objects and structs as exact-field JSON objects. Numeric strings, missing
  or extra struct fields, and option/result/enum inputs are rejected.
- Scalars, arrays, maps and structs encode as JSON values. Option/result outputs
  are tagged objects: for example, `{"tag":"some","value":7}`,
  `{"tag":"none"}`, `{"tag":"ok","value":7}` or
  `{"tag":"err","error":"reason"}`. Builders cannot cross the JSON boundary.
- Use `contract SOURCE --json` or MCP `apodexis_explain` to inspect the actual
  input/output contract. A static check or source test does not establish that
  a function is compatible with the JSON entrypoint ABI.

See the [types and JSON ABI reference](resources/types.md) for exact rules.

## Examples

| Example | What it demonstrates |
| --- | --- |
| [Hello](examples/hello.apx) | A zero-input tool and a source test. |
| [Double values](examples/double_values.apx) | Typed JSON array input, iteration, checked arithmetic and builder writeback. |
| [Row diff](examples/rowdiff.apx) | Structured input/output, deterministic added/removed/changed IDs, and duplicate handling. |
| [Compact authoring](examples/compact_authoring.apx) | Postfix reads, local inference and immutable builder snapshots. |
| [File report](examples/file_report/main.apx) and [report module](examples/file_report/report.apx) | Imports/exports, maps, strict parsing and explicitly granted file operations. |
| [HTTP response](examples/http_hello.apx) and [static response data](examples/web_static.apx) | Request/response values and asset selection as data transformations, **not running HTTP servers**. |

For a less trivial pure tool:

```sh
bin/apodexis run examples/rowdiff.apx --input fixtures/rowdiff/changed.input.json --json
bin/apodexis test examples/rowdiff.apx --json
bin/apodexis explain examples/rowdiff.apx --json
bin/apodexis pseudocode examples/rowdiff.apx
```

The changed fixture reports ID `a` as changed, with no additions or removals.
For real file I/O, follow the [capability-policy walkthrough](docs/capabilities.md).

## Command-line reference

Source commands below take `SOURCE`, a source path; `-` reads source from stdin.
Run from the source or extracted distribution root, or use an absolute compiler
path while keeping its distribution intact.

| Command | Purpose |
| --- | --- |
| `fmt SOURCE` | Print canonical source; never overwrite the source file implicitly. |
| `check SOURCE --json` | Return static diagnostics without executing the program. |
| `test SOURCE --json` | Compile and run source-declared tests. A host-compatible `main` is not required just to test helpers. |
| `verify SOURCE --json` | Return the combined format, check/repair and declared-test verdict. |
| `run SOURCE --input INPUT.json --json` | Compile and run the JSON entrypoint. Omit `--input` for a zero-parameter `main`. |
| `explain SOURCE --json` | Describe checked source and function/type facts. |
| `contract SOURCE --json` | Describe the entrypoint's JSON input/output contract. |
| `pseudocode SOURCE` | Print a deterministic, non-authoritative readable projection. |
| `bundle SOURCE` | Assemble explicit sibling modules into a single source; `--json` returns a structured bundle. |
| `emit-qbe SOURCE -o NEW.ssa` | Write checked QBE IL to a new artifact path. |
| `build SOURCE -o NEW_EXECUTABLE` | Build a native ELF executable at a new path. |
| `capabilities` | Report the current native backend, target and tooling surface. |
| `mcp` | Serve the local stdio MCP interface. |

`build` and `emit-qbe -o` refuse to overwrite an existing artifact. For example,
after building the compiler, choose unused output paths:

```sh
bin/apodexis emit-qbe examples/hello.apx -o .build/hello.ssa
bin/apodexis build examples/hello.apx -o .build/hello
printf 'null\n' | .build/hello
```

The generated program reads one JSON input from stdin and writes a structured
execution result. It needs a compatible Linux kernel and platform C library, not
the compiler, QBE, source file or repository. It is not a persistent application
server. Host effects remain denied without an appropriate trusted broker channel.

Execution controls include `--fuel`, `--max-input-bytes` and
`--max-output-bytes`. For example:

```sh
bin/apodexis run examples/rowdiff.apx --input fixtures/rowdiff/changed.input.json --fuel 100000 --max-input-bytes 1048576 --max-output-bytes 1048576 --json
```

### Success, application errors and exit codes

Inspect both the command verdict and the returned value. A well-executed program
can return an ordinary `result` error value, such as
`{"tag":"err","error":"duplicate id in before"}`, while the execution envelope
still has `status: "ok"` and exit code zero. That differs from an ABI error,
assertion failure, resource failure or runtime trap.

| Exit code | Meaning in the native CLI |
| --- | --- |
| `0` | The requested operation succeeded; inspect application-level result values separately. |
| `1` | `test` did not return a successful test verdict. |
| `2` | A check, verification or execution operation failed, or source failed backend validation. |
| `64` | Usage/argument error or invalid operator capability policy. |
| `66` | A requested source/input file was unreadable or exceeded its accepted read limit. |
| `70` | An explicit `build` failed in the host toolchain, or the driver encountered an internal fatal error. |
| `73` | A requested output artifact could not be created, including an existing output path. |

## Agent integration with MCP

Apodexis serves MCP `2025-11-25` over local newline-delimited stdio JSON-RPC.
Launch **the absolute path to `bin/apx-mcp` with no arguments**. It needs no API key,
network server, package-manager wrapper or separate interpreter.

For a client using the common `mcpServers` configuration shape:

```json
{
  "mcpServers": {
    "apodexis": {
      "command": "/absolute/path/to/apodexis/bin/apx-mcp",
      "args": []
    }
  }
}
```

Replace the placeholder with your real absolute path. Configuration formats differ
by client; see the [Hermes, Claude and Codex client guide](docs/mcp-client-setup.md).
Use a finite client tool timeout of about 130 seconds where supported, separate
from connection/startup timeouts. Each compilation/execution subprocess has a
30-second deadline, but one call can contain several subprocesses. Dispatch is
serial, and cancellation does not interrupt an in-flight worker. stdout is
reserved for JSON-RPC.

### Agent authoring loop

1. Discover `tools/list`, capabilities and resources. Read
   `apodexis://spec/language`, `apodexis://spec/types` and
   `apodexis://spec/stdlib` before inventing syntax or builtins.
2. Write complete source with useful `X` tests. For multiple modules, use
   `apodexis_bundle` with inline module sources first.
3. Call `apodexis_verify` and inspect its combined verdict and nested diagnostics.
4. Correct the source, or explicitly apply an unchanged compiler-issued repair
   with the current exact `source_text_hash`. Verify the resulting source again.
5. Inspect the entrypoint contract and run representative JSON inputs with
   explicit fuel/input/output limits. Retain the exact final source and result.

| Tool | Role |
| --- | --- |
| `apodexis_capabilities` | Discover the implemented backend and operations. |
| `apodexis_format` | Canonicalize source and return exact-text/canonical identities. |
| `apodexis_check` | Return diagnostics, typed-hole context and compiler-issued repairs. |
| `apodexis_explain` | Inspect checked module facts and the JSON entrypoint contract. |
| `apodexis_test` | Run declared source tests. |
| `apodexis_verify` | Combine formatting, checking and source tests. |
| `apodexis_run` | Execute checked source with JSON input and explicit limits. |
| `apodexis_apply_repairs` | Apply selected current repairs to exact source, returning revised inline source. |
| `apodexis_pseudocode` | Render checked source for human review. |
| `apodexis_bundle` | Resolve explicitly supplied inline modules. |

`apodexis_run` requires inline `source`, JSON `input` and a `limits` object.
Include all three limit fields: `fuel`, `max_input_bytes` and `max_output_bytes`.
For example, use `{"fuel":100000,"max_input_bytes":1048576,"max_output_bytes":1048576}`.
Byte limits may be null to omit that caller-selected cap; hard runtime and worker
bounds still apply. Send JSON null for a zero-parameter entrypoint's `input`.

The native repair mechanism currently supplies guarded canonical-source
replacement, not arbitrary semantic program synthesis. Repairs never silently
write host files. MCP source/input is inline and cannot self-grant host access.

## Determinism, identity and security boundaries

- **Pure computation:** checked values, explicit evaluation, canonical formatting,
  deterministic map ordering and fuel accounting make runs inspectable within the
  supported implementation. This is not a cross-platform bitwise floating-point
  parity guarantee.
- **Program identity:** `source_text_hash` identifies exact authored text where
  exposed; `source_hash` identifies canonical source. Short authoring forms can
  share an identity with their exact canonical expansion. A hash is not a proof of
  correctness or a record of all inputs, compiler versions, limits or host effects.
- **Values:** arrays, records, maps and builders preserve immutable snapshots.
  Updating a local binding does not mutate its old aliases.
- **Workers:** native execution has Linux seccomp restrictions and finite
  memory/recursion/CPU/input/output bounds. Workers cannot directly open files,
  use sockets or create processes.
- **Effects:** an operator-owned CLI `run --capabilities policy.json` can grant
  narrow brokered file/environment/time/entropy/HTTP GET operations. These host
  observations are not deterministic. No policy means no grants; source tests,
  verification, normal MCP calls and standalone programs without a trusted broker
  channel remain ungranted.
- **Trust:** the native compiler, runtime, broker, QBE and operating system are
  trusted components. A language designed for constrained code is not a guarantee
  against implementation defects or a replacement for operational isolation.

See [host capability policies](docs/capabilities.md), the
[backend boundaries](docs/qbe-native-backend.md#boundaries), and the
[security policy](SECURITY.md). Do not use this experimental implementation as the
sole boundary for hostile production workloads.

## Distribution

Build a relocatable native toolchain bundle:

```sh
make dist
```

The output is `.build/apodexis-linux-amd64.tar.gz`. It contains the native compiler
and MCP executables, QBE, prebuilt support objects, reference data, examples,
fixtures and license notices. Follow the [binary distribution guide](docs/binary-distribution.md)
for extraction and use.

To compile Apodexis source with that bundle, install the platform C library,
glibc linker-development files (typically `libc6-dev` on Debian/Ubuntu) and standard
`as`/`ld`. Keep its `.build/` directory writable. With those prerequisites present,
the installed compiler needs no C compiler or package download. Building Apodexis
itself from source still requires the C development prerequisites above. Merely
running a generated application needs the compatible kernel/runtime C library,
not the development files or compiler bundle.

## Development and verification

```sh
make check
make test
make test-sanitize
make dist
```

The native suite covers parser/checker behavior, libraries, module resolution,
JSON boundaries, canonical identities, diagnostic transport, source tests,
MCP discovery/execution, capability policies and kernel restriction probes.
The sanitizer target uses the developer C compiler's AddressSanitizer and
UndefinedBehaviorSanitizer support; that instrumentation is not a consumer
runtime dependency.

Run these checks locally before submitting compiler or runtime changes. Verify
the extracted distribution after packaging changes. This repository does not run
CI on GitHub; report the commands actually executed and their results.

## Documentation and project layout

- [Current language specification and reading order](docs/specification.md).
- [Syntax](resources/language.md), [types/JSON ABI](resources/types.md) and
  [builtins/host operations](resources/stdlib.md).
- [Native backend, compiler C API and boundaries](docs/qbe-native-backend.md).
- [MCP client setup](docs/mcp-client-setup.md).
- [Host capability walkthrough](docs/capabilities.md).
- [Documentation index](docs/README.md).
- [Public changelog](CHANGELOG.md), [contribution guide](CONTRIBUTING.md) and
  [security policy](SECURITY.md).

```text
compiler/       C frontend, checking, canonicalization, QBE lowering, CLI/MCP and broker
runtime/        C value/JSON runtime and native startup support
resources/      Implemented language references and protocol catalog/schemas
examples/       Runnable Apodexis programs, including a multi-file report tool
fixtures/       Example and conformance inputs/expected outcomes
tests/          Native C verification suite
third_party/    Vendored QBE source, license and provenance
docs/           Current specification, integration, capability and installation guides
```

## Troubleshooting

| Symptom | Check |
| --- | --- |
| `make` cannot find `cc`, `as` or `ld` | Install the source-build prerequisites; build on Linux x86-64/glibc. |
| A moved compiler cannot find QBE or reference data | Restore the whole checkout/distribution layout, not just the executable. |
| Native compilation cannot create a build directory or link `-lc` | Keep the distribution's `.build/` writable and install the glibc linker-development files. |
| `build` refuses an output | Choose a new writable artifact path; existing artifacts are intentionally not overwritten. |
| A host call returns `capability_denied` | That is the default. Review a minimal operator-owned CLI policy; MCP/tests do not grant effects. |
| Typed JSON input is rejected | Inspect `contract`/`explain`; missing/extra fields, numeric strings and unsupported variant input are not accepted. |
| MCP discovery fails or output is not JSON-RPC | Use the absolute server path directly, with no stdout banner, and refresh the client's discovery. |
| A run exhausts fuel or another bound | Inspect its diagnostic and input size; changing a limit is an explicit caller decision, not an automatic retry. |

## Contributing

Contributions are welcome, especially small reproducible bug reports, useful
programs, stronger conformance tests, precise diagnostics and clearer agent-facing
interfaces. Start with [CONTRIBUTING.md](CONTRIBUTING.md) for setup, testing,
architecture constraints and the pull-request checklist.

Discuss language, target, ABI or capability-policy changes before a large
implementation. Preserve canonical identity and technical compatibility tests;
new capabilities need implemented behavior, documented boundaries and tests.
Report sensitive issues using [SECURITY.md](SECURITY.md), not a public exploit dump.

## License

Apodexis first-party code and documentation are available under the
[MIT License](LICENSE). QBE retains its own MIT copyright/notice. See
[third-party notices](THIRD_PARTY_NOTICES.md) for attribution and dependency scope.
