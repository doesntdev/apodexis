# Contributing to Apodexis

Apodexis is experimental. Focused changes with a minimal reproduction, clear
contracts and tests are easier to review than a broad language expansion.
Start with the [README](README.md) and
[current language specification](docs/specification.md).

## Ways to contribute

- Report parser, type, runtime, diagnostic, module or MCP bugs.
- Add useful programs with source tests and representative JSON fixtures.
- Improve error locations, expected-type context and the repair workflow.
- Strengthen conformance, resource-limit, capability and packaging tests.
- Correct documentation against the implemented compiler.

Discuss new syntax, target/ABI changes, host capabilities, dependencies and major
architecture changes in an issue before implementing them. For sensitive findings,
follow [SECURITY.md](SECURITY.md) rather than posting exploit details publicly.

## Development setup

Use **Linux x86-64 with glibc**, a C99 compiler, GNU Make, the platform assembler
and linker, and glibc development files. See [initial setup](README.md#initial-setup)
for prerequisites. From a source checkout:

```sh
make
make check
make test
```

The build uses vendored QBE 1.3 and downloads no packages. The compiler, native
runtime, stdio MCP server and default tests require no Python, Rust, Cargo,
Node.js, model or provider credentials. The optional HTTP GET broker adapter
uses `/usr/bin/curl`; it is not a default test dependency.

The compiler creates temporary native artifacts under `.build/`; keep the
checkout writable and its directory layout intact. Installed compilation also
needs `/usr/bin/as`, `/usr/bin/ld` and glibc linker-development files. These are
distinct from the smaller kernel/runtime-libc requirements of generated programs.

GCC and Clang can be selected through `CC`. When changing compiler or flags,
start with a fresh checkout or run `make clean` first: Make does not track flag
changes as file dependencies. `make clean` removes generated compiler/build
outputs and QBE build products. Do not clean another developer's active build.

## Source layout

| Path | Responsibility |
| --- | --- |
| `compiler/apx_frontend.*` | Parsing, checking, canonical formatting and compiler C API. |
| `compiler/apx_qbe.*` | Checked lowering to QBE IL. |
| `compiler/apx_metadata.c` | Checked facts, contracts and pseudocode. |
| `compiler/apx_driver.c` | CLI, stdio MCP, protocol validation and bounded process orchestration. |
| `compiler/apx_modules.*` | Explicit module resolution and bundling. |
| `compiler/apx_host.*` | Trusted policy-controlled host broker. |
| `runtime/` | Immutable values, checked primitives, JSON, fuel/traps and startup. |
| `resources/` | Implemented language/type/stdlib references and protocol schemas. |
| `tests/` | Native C conformance, integration and safety tests. |
| `examples/`, `fixtures/` | Runnable programs and reproducible input/expected behavior. |
| `docs/` | Current specification, integration, capability and installation guides. |

Keep vendored changes separate and justified. Preserve
[QBE provenance](third_party/qbe-provenance.json) and
[license notices](THIRD_PARTY_NOTICES.md). Check consumers before changing
reference data or schemas.

## Verification

During development, run the affected focused tests. Before requesting review of
compiler/runtime, capability or protocol changes, run:

```sh
make all check test
make test-sanitize
make dist
```

- `make check` performs strict C syntax/warning checks.
- `make test` covers native behavior, fixtures, libraries, modules, MCP,
  diagnostics and kernel/capability restrictions. Source tests can exercise
  helpers without a JSON-compatible `main`.
- `make test-sanitize` exercises selected paths under AddressSanitizer and
  UndefinedBehaviorSanitizer using the developer C compiler. It is not a claim
  that every path is sanitizer-tested.
- `make dist` assembles the relocatable toolchain, references, examples and
  notices. Exercise the extracted bundle after installation or packaging changes.

Run the relevant verification locally and include the actual commands and results
with your change. This repository does not run CI on GitHub. Packaging changes
also require an extracted-bundle smoke test.

For documentation-only changes, check relative links/anchors and run every new
or changed runnable example. Command, installation and packaging edits also need
a fresh setup/distribution smoke. State which gates were not run and why.

For a bug fix, add a discriminating regression where practical: it should fail
for the original mechanism and pass after the fix. For new behavior, cover normal,
empty/boundary and rejection cases. Preserve technical compatibility tests and
acceptance thresholds rather than weakening them to make a change pass.

## Compatibility and safety rules

- Preserve the C99 frontend/runtime and QBE native architecture. Default build and
  test paths must retain their declared dependency boundary.
- Preserve canonical prefix formatting and `apx1` identities unless an explicit
  versioned compatibility change is agreed. Authoring shorthand does not justify
  silently changing canonical bytes or opcode behavior.
- Preserve checked values, evaluation order, immutable snapshots, diagnostic
  locations, fuel accounting and finite resource limits.
- Treat source, JSON input and MCP requests as untrusted. MCP receives inline
  values, not client-selected host paths or arbitrary process commands.
- Effects require an operator-owned CLI policy. Keep MCP, source tests and
  verification ungranted; preserve seccomp restrictions and narrow grants.
- Distinguish diagnostics, source-test verdicts, application `result` values and
  host/toolchain failures. A successful check is not a successful execution or
  proof of correctness.
- Keep compiler C API changes deliberate; the current API is not a supported
  application-embedding package or arbitrary host-callback interface.

## Issues and pull requests

Bug reports should include the revision, platform, `bin/apodexis capabilities`
output, minimal synthetic source/input, exact command, exit status, expected
behavior and actual sanitized diagnostics. Include toolchain versions when
relevant. Never post credentials, private datasets or environment dumps.

For pull requests:

- Explain the problem and the smallest complete solution.
- Keep unrelated formatting and refactors separate.
- Include tests and update current references when behavior changes.
- List commands actually executed, their outcomes and remaining limitations.
- Run `git diff --check` and inspect the files you stage.
- Exclude generated builds, local operator policies and credentials; use synthetic
  fixtures. An ignore rule is not a security boundary.

No automated approval, branch protection, support SLA or release schedule is
implied by this guide.

## License and attribution

Submit only material you have the right to contribute under the project's
[MIT License](LICENSE). Identify third-party code and preserve its original
notices; the project license does not replace upstream QBE attribution. Discuss
new dependencies and establish their provenance before adding them.
