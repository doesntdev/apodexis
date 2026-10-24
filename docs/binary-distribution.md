# Apodexis native binary distribution

A relocatable **Linux x86-64/glibc toolchain** for compiling checked Apodexis
programs. This file is also the bundle's installed `README.md`.

## Extract the bundle

Given `apodexis-linux-amd64.tar.gz` in the current directory:

```sh
tar -xzf apodexis-linux-amd64.tar.gz
cd apodexis-linux-amd64
```

The bundle contains:

- `bin/apodexis`: CLI compiler and tools; `bin/apx-mcp`: local stdio MCP server.
- `.build/tools/qbe`, `.build/apx_runtime.o` and `.build/apx_start.o`: backend and
  prebuilt compilation support.
- `resources/`: language, types, standard library and protocol schemas.
- `examples/` and `fixtures/`: runnable programs and reproducible inputs.
- `LICENSE`, `THIRD_PARTY_NOTICES.md`, `QBE-LICENSE`, `third_party/qbe/LICENSE`
  and `third_party/qbe-provenance.json`: first-party/upstream terms and provenance.

Keep the whole directory layout intact. The compiler finds its backend, support
objects and reference data relative to its executable. Moving only a binary onto
`PATH` is not an installation. Use the full binary path or add the intact bundle's
`bin/` directory to `PATH`.

Extract into a user-writable location. Keep `.build/` writable: `run`, `test`,
`verify` and `build` create temporary native build directories there. A read-only
installation may support checking and discovery but cannot compile/run programs.
The extracted binaries and support objects are ready to use; no source rebuild
is needed.

## Host requirements

To compile Apodexis source, the host needs:

- Linux x86-64 with a compatible glibc installation and dynamic loader at
  `/lib64/ld-linux-x86-64.so.2`.
- The platform assembler/linker at `/usr/bin/as` and `/usr/bin/ld`.
- Glibc linker-development files, typically `libc6-dev` on Debian/Ubuntu. Linking
  uses `-lc`; runtime `libc.so.6` alone does not supply the link-time library or
  linker script.

On Debian/Ubuntu, the assembler/linker and glibc link-time files are supplied by
`binutils` and `libc6-dev`. With the prerequisites present, the installed compiler
needs no C compiler, Python, Rust, Cargo, Node.js, model, API key or package
download. The optional HTTP GET capability uses `/usr/bin/curl`; pure programs
do not need it, and it is not bundled.

A **generated application** has a smaller boundary: a compatible Linux kernel and
runtime glibc. It does not need the bundle, original source, assembler/linker or
glibc development files merely to run. Neither the toolchain nor its generated
artifacts are universal cross-platform binaries.

## First program

Run these from the extracted bundle root:

```sh
bin/apodexis capabilities
bin/apodexis check examples/double_values.apx --json
bin/apodexis verify examples/double_values.apx --json
bin/apodexis run examples/double_values.apx --input fixtures/double_values/values.input.json --json
```

The fixture is `[1, -2, 0]`; a successful run returns a structured JSON envelope
with `status: "ok"` and `output: [2, -4, 0]`. Checking does not execute tests.
Verification combines formatting, checking and declared `X` tests, not every
possible invocation of `main`.

For a record transformation:

```sh
bin/apodexis contract examples/rowdiff.apx --json
bin/apodexis test examples/rowdiff.apx --json
bin/apodexis run examples/rowdiff.apx --input fixtures/rowdiff/changed.input.json --json
```

The changed fixture reports ID `a` as changed with no additions or removals.
Inspect both the execution verdict and the application value: a normal
`{"tag":"err","error":"reason"}` result can accompany `status: "ok"` and
process exit zero. ABI errors, traps and resource failures are separate failures.

## JSON entrypoints and limits

`main` takes zero parameters or one supported typed JSON parameter. A
zero-parameter function accepts absent/null input; omit CLI `--input`. Typed
inputs accept booleans, i64, finite f64, strings, arrays, typed maps and exact-field
structs. Numeric strings, missing/extra fields and option/result/enum inputs are
not accepted. Null is not a language value or a default typed input.

Option/result outputs are tagged objects. Builders remain local-only; general
custom-enum construction/execution is not implemented. Read `resources/types.md`
and inspect `contract` rather than guessing an input shape.

Use explicit caller-selected limits for representative runs:

```sh
bin/apodexis run examples/double_values.apx --input fixtures/double_values/values.input.json --fuel 100000 --max-input-bytes 1048576 --max-output-bytes 1048576 --json
```

Fuel, byte limits, runtime memory/recursion bounds and worker deadlines are
separate controls. A failed bound is a diagnostic, not an automatic retry with
more authority or resources.

## Build a standalone program

Choose unused output paths:

```sh
bin/apodexis emit-qbe examples/hello.apx -o .build/hello.ssa
bin/apodexis build examples/hello.apx -o .build/hello
printf 'null\n' | .build/hello
```

The final result's `output` is `"Hello, world!"`. `build` and `emit-qbe -o` refuse
to overwrite existing artifacts. Choose new writable paths when repeating the
example, or remove only your own generated output.

The generated executable reads one JSON input from stdin and writes a structured
execution result. It supports `--test`, `--fuel`, `--max-input-bytes` and
`--max-output-bytes`. It is a bounded one-shot program, not an HTTP listener or
persistent application server. `examples/http_hello.apx` and
`examples/web_static.apx` construct response data; they do not host websites.

## Language references and MCP

Read the bundled `resources/language.md`, `resources/types.md` and
`resources/stdlib.md`. They are also served through MCP as
`apodexis://spec/language`, `apodexis://spec/types` and `apodexis://spec/stdlib`.
The exact tool schemas are in `resources/schemas/mcp-agent-catalog.v1.json`.

Launch the **absolute path to `bin/apx-mcp`, with no arguments**, using the
client's local stdio configuration. For clients using `mcpServers`:

```json
{
  "mcpServers": {
    "apodexis": {
      "command": "/absolute/path/to/apodexis-linux-amd64/bin/apx-mcp",
      "args": []
    }
  }
}
```

Replace the placeholder with the real extracted path. Configuration formats
vary by client. stdout is reserved for newline-delimited JSON-RPC; there is no
network server or package-manager wrapper. Complete MCP initialization, then
inspect `tools/list`, `resources/list` and `apodexis_capabilities`.

Use a finite tool timeout around 130 seconds, separate from connection/startup
timeouts. Individual compilation/execution subprocesses have 30-second deadlines,
but one call can contain several subprocesses. Dispatch is serial and cancellation
does not interrupt an in-flight worker.

MCP accepts inline source/input, not filenames. Verify declared tests, inspect
the contract and run representative JSON inputs with explicit `limits`.
Compiler-issued canonical repairs require the current exact `source_text_hash`
and unchanged repair objects; they return revised inline source, not file writes.
Verify again after applying them.

## Host authority and security

Pure programs have no ambient file, network, environment, clock, entropy or
process access. Native workers retain seccomp and resource restrictions. Source
tests, `verify`, ordinary MCP calls and standalone artifacts without a trusted
broker channel remain ungranted.

An operator can grant supported effects only through CLI
`run --capabilities policy.json`. That policy is a separate operator-owned file,
not part of the bundle or an authority granted by generated source. The bundled
`resources/stdlib.md` defines `apodexis.host_policy.v1`, its exact grant fields
and operation bounds. Host observations/effects are not deterministic.

Apodexis is experimental, not a certified sandbox or the sole boundary for
hostile production code. The compiler, runtime, broker, QBE, distribution and
operating system are trusted. No WASM/WASI, GPU, database adapter or event loop
is provided. Follow the project's
[security policy](https://github.com/doesntdev/apodexis/security/policy) for
sensitive findings; do not post private data or exploit details publicly.

## Troubleshooting

| Symptom | Check |
| --- | --- |
| QBE or resources cannot be found | Restore the complete extracted layout. |
| Native build directory cannot be created | Ensure the bundle's `.build/` is writable. |
| Native linking fails | Check `/usr/bin/ld`, glibc development files and the expected dynamic loader. |
| Artifact creation is refused | Select an unused writable output path. |
| JSON input is rejected | Inspect `contract`; preserve exact types and fields. |
| A host operation returns `capability_denied` | This is the default; only an operator-owned CLI policy can grant effects. |
| MCP parsing/discovery fails | Launch the absolute binary directly, reserve stdout for JSON-RPC and refresh discovery. |

See the bundled `LICENSE`, `THIRD_PARTY_NOTICES.md` and `QBE-LICENSE` for terms
and attribution.
