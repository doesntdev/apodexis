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
  An operator-owned CLI `run --capabilities` policy or a fixed per-tool serving
  manifest grants effects. Source tests, verification and default authoring MCP
  calls remain ungranted. Host
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
WASM/WASI or GPU backend, HTTP listener, database adapter, language event loop, general
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

## Typed struct-field projection

`(json_decode_fields T text) -> result T str` is additive pure intrinsic 57.
Concrete JSON eligibility, once-only operand evaluation, parser errors,
numeric/UTF-8/duplicate/depth semantics and resource/trap behavior are identical
to `json_decode`. Only struct unknown-field selection differs: every declared
field remains required, fields normalize in declaration order, and extra struct
fields are ignored recursively, including within arrays/maps. All map values
and tagged map wrappers remain validated. Entrypoint ABI and `json_decode` stay
strict; parser/encoder and language version identity are not relaxed. See the
[repo brief application](repo-brief.md) for an operator-fixed native use case.

## Native HTTP client and JSON encoding

The current append-only builtin surface includes pure `json_encode` (56) and
brokered `http_request` (1007). The [HTTP client guide](http-client.md) defines
the ordinary typed module, strict v2 operator policy and bounded HTTP1.x wire.
CLI `run --capabilities` and operator-bound `serve --manifest` tools may enable
effects. Tests, verify and the default authoring MCP remain denied.
Default compiler/tests/HTTP use libc; verified HTTPS is an explicit optional
OpenSSL helper/profile. There is no curl/libcurl runtime dependency.

## Operator-configured tool serving

`apodexis serve --manifest` exposes only operator-selected named compiled tools,
with checked schemas, immutable startup policies, broker-held HTTP credentials,
fresh per-call budgets and bounded cancellation/deadline supervision. It does not
change the default authoring MCP or add a language event loop. See the
[serving contract and API-to-report example](tool-serving.md).

## Post-quantum transport and application encryption

V3 operator policies default to required TLS1.3/X25519MLKEM768/AES256 HTTPS,
without downgrade; legacy policy modes remain compatible. Appended host builtins
`pqc_seal` (1008) and `pqc_open` (1009) use named public/private-key grants and an
optional static OpenSSL 4.1+ helper for the narrow standard CMS ML-KEM-768 profile.
The ordinary typed module and operator-only `pqc-keygen` do not confer ambient
authority. Default authoring MCP remains 13 tools with host calls denied;
application-key operations are also denied in the separate serving mode. Pure semantics and worker
restrictions are unchanged. See [post-quantum encryption](post-quantum.md) for
exact profiles, provisioning, interoperability and security boundaries. Neither
surface claims PQ certificate signatures, sender authentication or FIPS validation.
