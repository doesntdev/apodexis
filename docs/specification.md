# Language specification

This is the reading-order entry point for the **implemented Apodexis language
and toolchain**. The executable language kernel identifies itself as `0.1.11`;
that identity does not imply API stability or production readiness.

## Reading order

| Contract | Reference |
| --- | --- |
| Grammar, layout, statements, expressions, modules and authoring loop | [Language reference](../resources/language.md) |
| Types, inference, immutable values and exact JSON input/output rules | [Types and JSON ABI](../resources/types.md) |
| Pure builtins, host-operation signatures, traps and fuel | [Standard library](../resources/stdlib.md) |
| MCP operations and request/result schemas | [Protocol catalog](../resources/schemas/mcp-agent-catalog.v1.json) |
| Native compilation, compiler C API and deployment limits | [Native backend](qbe-native-backend.md) |
| Operator-owned grants and a runnable file-report example | [Capability guide](capabilities.md) |

The first three references are also served as `apodexis://spec/language`,
`apodexis://spec/types` and `apodexis://spec/stdlib`. Humans and agents use the
same contract. Discover `tools/list`, `resources/list` and
`apodexis_capabilities` against the actual server before assuming syntax,
library names or operation availability.

## Core model

- UTF-8 source has one `M` module and explicit typed function boundaries.
  Blocks use two-space indentation, with one physical line per statement or
  expression. Ordinary calls and arithmetic are prefix.
- Functions, structs, arrays, typed string-keyed maps, option/result values and
  matching, local builders, assertions, traps and `X` source tests are supported.
  Checked postfix reads and `P` builder writeback normalize to prefix operations.
- Values preserve immutable snapshots. Local updates replace bindings without
  mutating aliases. Integer arithmetic, indexing and builder capacities are
  checked; statements and loop iterations consume deterministic fuel.
- `main` has zero parameters or one supported JSON input parameter. Source tests
  can exercise helpers independently of that host entrypoint ABI. Struct inputs
  require exact fields; malformed input is not silently defaulted.
- `source_text_hash` identifies exact authored text where exposed; `source_hash`
  identifies canonical source. Neither is a proof of correctness or a complete
  record of compiler, input, limits and host observations.
- Supported host operations return typed results through a trusted broker.
  Only an operator-owned CLI `run --capabilities` policy grants effects.
  Source tests, verification and ordinary MCP calls remain ungranted. Host
  observations and effects are not deterministic.

## Validation and execution

`check` parses and statically checks source without executing it. `test` compiles
and runs declared `X` tests. `verify` combines formatting, checking/repair context
and those tests; it does not run every possible input through `main`. Empty tests
and a successful check are not evidence of representative application behavior.

Use `contract` or MCP `apodexis_explain` to inspect the JSON entrypoint, then run
representative inputs with explicit limits. A successfully executed application
may return a normal `result` error value while the execution envelope is still
`status: "ok"`. That differs from a runtime trap, ABI error or host-toolchain
failure.

Compiler-issued repairs currently provide guarded canonical-source replacement,
not general program synthesis. They return revised inline source, never silent
host-file edits. Apply current repair objects unchanged with the exact
`source_text_hash` precondition, then verify the revised source. See
[MCP client setup](mcp-client-setup.md) for protocol and timeout boundaries.

## Implemented boundaries

The native target is Linux x86-64/glibc through C99 and QBE 1.3. There is no
WASM/WASI or GPU backend, HTTP listener, database adapter, event loop, general
function value, arbitrary host callback or package registry. Request/response
examples construct data; they do not host websites.

User-enum declarations and runtime encoding do not establish general custom-enum
construction/execution support. Option/result values work within native functions
and tests, but option/result/enum JSON inputs are not implemented. Builders are
local-only and cannot cross the host boundary. The compiler C API is not a
supported application-embedding package.

Consult the detailed references for accepted forms and limits. If source and
reference disagree, report a minimal reproduction and the exact diagnostic via
the [contribution guide](../CONTRIBUTING.md); a discovered defect does not silently
redefine the contract. For trust and disclosure boundaries, read
[SECURITY.md](../SECURITY.md).
