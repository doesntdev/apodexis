# Session-scoped compiled tools

The native stdio MCP server provides `apodexis_compile`, `apodexis_invoke` and
`apodexis_release`. Compile checked source once, invoke its immutable native
program with different JSON inputs, then release its handle. These are MCP tools,
not new CLI commands. `apodexis_run` and ordinary CLI execution remain available.

## Author, verify, compile, invoke

Initialize MCP and send `notifications/initialized` before calling tools. Read the
language/types/stdlib resources and discover the actual tool schemas first.

1. Author complete source and verify it with `apodexis_verify` or execute its
   declared tests with `apodexis_test`. Compilation checks source tests but **does
   not execute them**, and therefore is not a test-verification verdict.
2. Call `apodexis_compile` with exactly one argument, `source`. For imports, call
   `apodexis_bundle` first and compile the returned assembled source. Neither tool
   accepts a source filename, uploaded executable or host policy.
3. Require `status: "ok"`, inspect the returned contract and retain `artifact_id`.
4. Call `apodexis_invoke` with that ID, JSON `input` and explicit `limits`. Inspect
   both the execution status and application output. Each call starts a fresh
   restricted native worker; there is no result memoization.
5. Release the handle when finished. Keep exact source separately if it must be
   compiled in a later session; handles are not portable or persistent.

For example, the compile tool arguments for a complete small program are:

```json
{"source":"M reuse\nF main value:i64->i64\n  R (+ value 1)\n"}
```

Use the **actual returned ID** in the following invoke arguments. The all-zero ID
below illustrates the format only; sending it as-is normally returns not found:

```json
{"artifact_id":"apxa1:00000000000000000000000000000000","input":4,"limits":{"fuel":100000,"max_input_bytes":65536,"max_output_bytes":16384}}
```

With the real successful compile handle, input `4` returns output `5`. Reuse that
handle with another input without supplying source again. Release arguments are
exactly `{"artifact_id":"<actual returned artifact_id>"}`; replace the placeholder
with the returned string. These are tool arguments inside ordinary MCP
`tools/call` requests, not standalone JSON-RPC messages.

See the [configuration validator](config-validator.md#reuse-the-validator-through-mcp)
for the same workflow on a larger tool. At installed-tool use, compilation needs
the intact native distribution, its QBE/support objects, platform `as`/`ld` and
glibc development linker files, not a C compiler, Python or Rust. Building the
compiler itself from source still needs the documented C development toolchain.

## Measured reuse

For one fixed configuration-validator input, native MCP request medians were
19.371 ms for compile, 1.202 ms for cached invoke and 17.466 ms for source-per-call
run. This one-host, 31-scored-call-per-arm result includes JSON/IPC/native worker
overhead, not just program computation. It is not a whole-agent speedup or a
cross-language ranking. [Read the protocol, ranges and original observations](../benchmarks/mcp_reuse/README.md).

## Exact responses

All responses are under MCP `result.structuredContent`, with a matching text
content item and `isError` reflecting the tool status. The canonical
[protocol catalog](../resources/schemas/mcp-agent-catalog.v1.json) contains the
closed input/output schemas. Unknown argument fields are rejected.

### Compile

`apodexis.compile_response.v1` always contains `schema`, `status`, `language`,
`version` and `diagnostics`. Success additionally requires every field below:

| Field | Meaning |
| --- | --- |
| `artifact_id` | Opaque `apxa1:` plus 32 lowercase hexadecimal characters, from OS entropy. |
| `source_hash` | Existing canonical `apx1:` source identity. |
| `source_text_hash` | Existing exact authored-text `apxs1:` identity. |
| `contract_hash` | Existing entrypoint contract identity. |
| `program_sha256` | SHA-256 of the complete native ELF, including binary NUL bytes. |
| `stored_bytes` | Actual ELF length plus the entry's complete owned retained metadata charge; not RSS. |
| `toolchain` | Closed object described below. |
| `contract` | Complete existing `apodexis.entrypoint_contract_response.v1`, including its own `schema`, `status`, `language`, `version`, `source_hash`, `contract_hash` and `contract`. |

`toolchain` contains exactly `backend: "qbe"`, `target: "amd64_sysv"`,
`compiler_sha256`, `qbe_sha256`, `runtime_sha256` and `start_sha256`. Each digest
is 64 lowercase hexadecimal characters over the full binary length. Compiler
identity is the running `/proc/self/exe` image, even if its original pathname was
replaced. No build path or file descriptor is exposed.

Compile errors have `status: "error"`, nonempty truthful `diagnostics`, and no
usable handle or success metadata. Frontend diagnostics retain original-source
spans. Failed checking/building, storage, output validation or transport admission
cannot leave a hidden active handle. A success is admitted only after its complete
response fits the frame budget and is written/flushed. A near-limit request ID
that cannot fit even a tool error envelope receives a smaller JSON-RPC error.

### Invoke

Arguments are exactly `artifact_id`, `input` and `limits`. `limits` is the unchanged
RunLimits object: all of `fuel`, `max_input_bytes`, `max_output_bytes` are required.
Fuel is a nonnegative full-u64 integer. Each byte limit is a nonnegative full-u64
integer or null. Booleans, negative values and overflowing integers are not limits.
Byte caps do not replace hard runtime/worker bounds.

The result is the existing `apodexis.execution_record.v1`, with no artifact-only
fields: `schema`, `status`, `language`, `version`, `protocol_version`, `sdk_version`,
`input_hash`, `limits`, `fuel_used`, and the ordinary optional `source_hash`,
`contract_hash`, `output`, `output_hash`, `diagnostic`. It has the same record,
output/hash, diagnostic, omission and fuel semantics as `apodexis_run` on the exact
source/input/limits. Reinvocation uses captured identity and the retained ELF;
it does not check/describe source or invoke QBE, the assembler or the linker.
After an ABI, fuel, output or other execution failure, the next call still starts
with fresh worker, memory, fuel and I/O state.

### Release and errors

`apodexis.release_response.v1` contains exactly `schema`, `status`, `language`,
`version`, `artifact_id`, `released`, `diagnostics`. Known active handles return
`status: "ok"`, `released: true`, and empty diagnostics, closing their executable
and freeing the associated count/byte charge. A second release is **not** a silent
success: it returns `status: "error"`, `released: false` and a diagnostic.

| Diagnostic | Phase / kind | Meaning |
| --- | --- | --- |
| `A203` | `artifact` / `artifact_not_found` | Well-formed unknown, released or foreign-session ID. Invoke uses zero fuel and omits source/contract identity. |
| `A204` | `artifact` / `artifact_limit_exceeded` | Admission would exceed count or retained-byte quota; existing handles stay valid. |
| `A205` | `artifact` / `artifact_store_failed` | Sealing, storage or metadata/response publication failed. |

Malformed IDs (including embedded NUL, uppercase hex, wrong lengths or paths) and
invalid argument shapes produce the existing invalid-request JSON-RPC error.
IDs are never normalized or interpreted as paths. New lifecycle calls without a
request ID do not compile, execute or release anything.

## Storage, limits and shutdown

The default store holds at most **8 active artifacts** and **64 MiB** of charged
ELF/owned metadata. There is no eviction, TTL, persistent database or background
janitor. Operator-only process startup environment can override:

| Variable | Allowed decimal value |
| --- | --- |
| `APX_MCP_MAX_ARTIFACTS` | 1..64 |
| `APX_MCP_MAX_ARTIFACT_BYTES` | 1..268435456 bytes (256 MiB) |

Only decimal digits are accepted: no sign, whitespace, suffix or empty value.
Invalid settings fail MCP startup with a clear generic error, without echoing
arbitrary environment contents. Ordinary non-MCP CLI behavior ignores them.
Models cannot choose storage limits in tool arguments. Actual configured values
are returned by `apodexis_capabilities.compiled_artifacts`, a closed object with
`scope: "session"`, `storage: "sealed_memfd"`, `max_artifacts` and `max_bytes`.

Executable images live in CLOEXEC memfds sealed against write/grow/shrink and
further seal changes. Workers execute retained descriptors rather than mutable
paths; unrelated cached descriptors are closed, and the executed source does not
inherit the image descriptor. The store retains owned fixed identity buffers,
not request JSON graphs, source or ASTs, across per-frame arena resets.

EOF, protocol-error exit and controlled SIGTERM/SIGINT shutdown clear the store.
Controlled shutdown kills/reaps an owned active worker before exit, without
allocation/free inside signal handlers. SIGKILL closes kernel descriptors but is
not a graceful child-reaping guarantee. There is no MCP cancellation protocol.

Compilation and invocation do not grant file, network, environment, clock,
entropy or process access. Default-denied host behavior, sanitized worker
environment, seccomp and resource restrictions remain in force. Independent
handles, including old/new source versions, remain immutable until released.
This is still experimental software, not a certified hostile-code sandbox.
