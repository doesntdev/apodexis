# Operator-configured compiled MCP tools

Use `apodexis serve` to expose reviewed Apodexis programs as named tools. The
operator selects the programs, policies and limits before startup; tool callers
supply only typed JSON input. Programs compile once at startup and each call runs
a fresh restricted native worker. Results are not memoized.

This is separate from the **default authoring server**, `bin/apx-mcp` or
`apodexis mcp`. Its 13 authoring/compiled-handle tools remain default-denied and
cannot submit grants. Serving mode exposes only its configured tools: no source
submission, compile/release handles, resource browsing, live reload or remote
administration. `apodexis capabilities` describes the legacy CLI/authoring
surface, not a loaded serving manifest; use that server's `tools/list`.

## Start with a pure tool

From a built checkout or extracted writable distribution, save this as
`.build/tools.json`:

```json
{
  "schema": "apodexis.tool_server.v1",
  "max_concurrent_calls": 2,
  "tools": [
    {
      "name": "double_values",
      "description": "Double a checked integer array",
      "source": "../examples/double_values.apx",
      "limits": {
        "fuel": 100000,
        "max_input_bytes": 16384,
        "max_output_bytes": 16384,
        "timeout_ms": 1000
      }
    }
  ]
}
```

Launch it directly, reserving stdout for JSON-RPC:

```sh
bin/apodexis serve --manifest .build/tools.json
```

Use **`apodexis serve`**, not the `apx-mcp` authoring alias. A stdio MCP client
launches the absolute binary with arguments `serve`, `--manifest`, and the
absolute manifest path. No live client configuration is written by these tools.
After normal MCP initialization, `tools/list` advertises `double_values` with a
closed input object generated from its checked compiler contract. Call it with:

```json
{"name":"double_values","arguments":{"input":[1,-2,0]}}
```

Its successful application output is `[2,-4,0]`. The serving result's
`structuredContent` contains `status`, typed `output`, `fuel_used`,
`source_hash` and `contract_hash`. The compatibility text parses to that same
object. Success output is validated against the advertised schema before
publication. Direct option/result JSON inputs retain the compiler's existing
restrictions; tagged map payloads and tagged output values are distinct cases.
Unsupported types or over-budget schema projections fail startup, not open into
an unconstrained input model.

## Manifest contract

All objects are strict: unknown, missing required, duplicate or wrong-typed
fields are refused. Root fields are exactly `schema`, `max_concurrent_calls`
and `tools`. Each tool requires `name`, `description`, `source` and `limits`;
optional fields are `capabilities` and `credentials`. Credentials require a
capability policy. A missing policy grants nothing.

| Setting | Bound |
| --- | --- |
| Configured tools | 1–32, unique names |
| Name | 1–64 ASCII letters, digits, underscore or hyphen |
| Description | Nonempty, at most 4096 UTF-8 bytes |
| Manifest | Regular file, at most 1 MiB |
| Retained native images | At most 64 MiB across the server |
| Concurrent calls | 1–4; excess calls return `busy`, with no queue |
| `fuel` | Integer 0–1000000000 |
| `max_input_bytes`, `max_output_bytes` | Integer 0–1048576 each |
| `timeout_ms` | Integer 1–30000 |

`source` and `capabilities` paths are absolute or relative to the resolved
manifest's directory. Source imports use the existing explicit sibling-module
resolver. Startup snapshots the compiled image, policy, roots and credential
values. Later file edits do not change admitted tools or rotate credentials;
stop and restart deliberately to reload. Each tool compiles independently,
even when two entries select the same source with different authority.

Caller arguments are exactly `{"input": ...}`; a zero-argument `main` uses null.
There are no caller-supplied source, roots, paths to configuration, credentials
or limits. Paths/URLs *inside a program's typed input* remain subject to its
operator policy. Byte limits apply to compact JSON values. Fuel, program memory
and host request counters start fresh for each call; intentionally granted
external files and services do not reset.

## Authenticated API-to-report example

[`examples/http_client/report.apx`](../examples/http_client/report.apx) uses the
existing typed HTTP module to fetch JSON, decode exact `Dataset`/`Item` records,
validate nonnegative counts, accumulate duplicate names, and write a canonical
JSON report under a granted root. The endpoint must supply this application
shape; it is not a universal adapter for arbitrary APIs:

```json
{"items":[{"name":"alpha","count":2},{"name":"beta","count":3},{"name":"alpha","count":4}]}
```

The verified report bytes are:

```json
{"counts":{"alpha":6,"beta":3},"records":3,"total":9}
```

The file includes one final newline. Wrong-typed/unknown API fields, unsuccessful
status codes and invalid business data produce application errors without
writing a report. The pure source tests include aggregation, validation,
strict decoding and default host denial:

```sh
bin/apodexis test examples/http_client/report.apx --json
```

### Operator policy and credential reference

The following reserved origin is deliberately nonconnecting. Replace it with an
API you are authorized to use, adapt the program's checked DTOs, create a
separate narrow report root, and provision the credential privately. Build
`make http-tls` before verified HTTPS use. A v2 policy uses ordinary verified
HTTPS; existing v3 transport policies remain explicit operator choices.

For a manifest in `.build/`, save `.build/report-policy.json`:

```json
{
  "schema": "apodexis.host_policy.v2",
  "write_root": "/absolute/work/reports",
  "http": [
    {"origin":"https://api.example.invalid","path_prefix":"/v1/","methods":["GET"],"headers":[]}
  ],
  "max_bytes": 16384,
  "max_requests": 2,
  "http_timeout_ms": 2000
}
```

Then save `.build/report-tools.json`:

```json
{
  "schema": "apodexis.tool_server.v1",
  "max_concurrent_calls": 2,
  "tools": [
    {
      "name": "api_report",
      "description": "Fetch checked API records and write a scoped report",
      "source": "../examples/http_client/report.apx",
      "capabilities": "report-policy.json",
      "credentials": [
        {
          "origin": "https://api.example.invalid",
          "path_prefix": "/v1/",
          "methods": ["GET"],
          "header": "authorization",
          "secret_file": "/absolute/private/api-token",
          "prefix": "Bearer "
        }
      ],
      "limits": {"fuel":100000,"max_input_bytes":16384,"max_output_bytes":16384,"timeout_ms":5000}
    }
  ]
}
```

`secret_file` contains the exact secret bytes, **without a newline**. It must be
an absolute nonsymlink path through nonsymlink directories, a single-link regular
file owned by the operator, mode 0400 or 0600, and 1–4096 bytes. The value must be
printable ASCII without leading/trailing spaces. The explicit prefix is at most
128 printable ASCII bytes, cannot begin with a space, and may be empty. Neither
value is silently trimmed. Keep this file outside **both** granted file roots;
explicit environment grants containing its value are also refused.

Each binding must match exactly one HTTP grant by scheme/host/effective port,
exact slash-prefix and method set. Ambiguous grants, overlapping duplicate bound
headers, reserved header names and malformed bindings fail startup. There are at
most 16 bindings per tool. Credential-bearing plaintext is permitted only for
literal `127.0.0.1`/`::1` loopback services; other origins require verified HTTPS.
The broker adds the bound header, rejects a caller/header conflict as
`http_credential_conflict`, and rechecks the complete emitted header budget
before connecting. Injected fields and bytes count against ordinary HTTP limits.
There are no redirects, automatic retries or ambient proxy credentials.

Call `api_report` with `{"input":{"url":"https://api.example.invalid/v1/items",
"destination":"summary.json"}}` after selecting a real authorized endpoint.
The two allowed host requests cover one fetch and one file write. The program
never receives the credential value; the HTTP helper receives it through private
IPC, not argv or an inherited worker environment. A trusted API could itself
return a received credential in its response: this is **not universal response
redaction**. Review the endpoint and program, not just the origin string.

Application crypto grants (`crypto_keys`/`crypto_timeout_ms`) are rejected in
serving policies, and `pqc_seal`/`pqc_open` remain denied in this mode. The
existing operator CLI crypto implementation is unchanged.

### Run the complete local example

The optional Python MCP client creates a real authenticated **synthetic owned
loopback API**, invokes the native server, validates its schemas and every result
with Draft 2020-12 JSON Schema, and reads back exact report bytes. It also checks
an ungranted second tool, caller overrides, malformed upstream data, HTTP failure,
path traversal, startup credential snapshots and fresh per-call budgets. It is
not a third-party API integration or a compiler/runtime dependency.

```sh
uv run examples/http_client/demo_report.py
make http-tls
uv run examples/http_client/demo_report.py --https
```

The HTTPS fixture uses an ephemeral owned CA/key and the existing verified TLS
helper. It does not change system trust. Optional `--trace NEW_FILE` requires
`strace` and proves QBE/assembler/linker execution counts do not grow after
startup. `--save-report NEW_FILE` retains the actual nonsecret fixture report;
private fixture inputs are removed even after failure. The script pins its
optional MCP/JSON-Schema dependencies in PEP 723 metadata.

## Lifecycle, failures and boundaries

Serving mode supports initialize/initialized, ping, tools/list, tools/call and
actual `notifications/cancelled`. Discovery is immutable (`listChanged: false`).
The parent waits on stdio, result channels, child-exit notifications and the
nearest active deadline rather than a periodic polling loop. Calls run in owned
process groups with parent-death cleanup; a cancelled slot is not reusable until
owned work is reaped. Cancellation IDs are type-sensitive: integer `40` is not
string `"40"`. Cancelled active calls emit no late response. EOF, write failure
and SIGTERM retire owned work and transient job channels.

Incoming frames have at most 4 MiB before LF, with fixed receive storage; outgoing
complete frames are at most 4 MiB. Raw worker replies have at most 2 MiB. Request
IDs are strings/integers whose encoded representation is at most 256 bytes.
Descriptors are bounded to 256 KiB per tool, and queued response storage to
8 MiB. Operator limits remain below worker hard limits. There is no indefinite
work queue or authority-bearing environment knob.

### Serving JSON work admission

Byte limits alone do not bound DOM duplicate-key searches or recursive typed
validation. Before the parent materializes any caller frame **or raw worker
reply**, a serving-only allocation-free lexical scan applies these additional
operational caps to the complete JSON document, including protocol/result
wrappers and arbitrary nested records/maps:

| Published `capabilities.experimental.apodexisJsonLimits` field | Bound / unit |
| --- | --- |
| `maxRequestBytes` | 4194304 bytes before LF (4 MiB) |
| `maxWorkerReplyBytes` | 2097152 bytes (2 MiB) |
| `maxObjectMembers` | 256 members in each object |
| `maxValues` | 16384 values, counting containers and scalars, not object keys |
| `maxDepth` | 64 simultaneously open object/array containers |
| `maxObjectWork` | 1048576, the sum of squared member counts over all objects |
| `maxValidationWork` | 1048576 per input or output descriptor validation |

The scan recognizes quotes and escaped backslashes; punctuation and escaped NUL
inside string data do not add shape work. It does not replace the JSON parser:
malformed under-budget JSON retains the existing parse-error behavior. Each
new member `m` charges `2*m-1`, so object work is exactly the sum of `n*n` and
bounds the existing parser's preceding-key duplicate comparisons. After a cap
fails, request scanning performs only linear, bounded scalar-ID recovery; it
never builds the rejected DOM. All lexical loops observe shutdown signals.
Worker-reply scanning stops on the first limit. The parent also preflights its
normalized result before parsing it for output validation.

Descriptor validation has a separate finite counter: each recursive node visit
costs one unit and each object lookup costs one plus that object's member count,
including schema lookups and `oneOf` branches. Exhaustion stops validation.
Thus values inside the shape caps can still exceed the validation-work cap.
These are operational service limits, **not changes to the compiler's type
schemas, general JSON parser or language semantics**. Checked records with more
than 256 fields fail serving startup rather than being advertised as usable.

Over-budget requests receive the small JSON-RPC error `-32000` (`Serving JSON
resource limit exceeded`) before whole-document DOM parsing when a shape cap
fails, or when the validation counter is exhausted. The response preserves a
bounded valid scalar request ID when recoverable, even if it follows the large
input; absent, invalid or duplicate IDs use null. This does not release an
active ID or admit any work. Over-budget worker/result data becomes the bounded
structured tool error `resource_limit`. The transport byte caps and operator
input/output limits still apply independently. These finite bounds are not a
universal latency or throughput guarantee.

Unknown tools/invalid arguments are JSON-RPC errors. Bounded execution/service
failures are structured tool errors (`busy`, `input_limit`, `output_limit`,
`deadline`, `runtime_failed`, `worker_failed`, `service_unavailable`,
`invalid_result`, `resource_limit`) without internal source/path/secret text. A normal application
`result` error still appears under a successful execution's `output`; inspect
that value rather than only `isError`.

**Cancellation or an error does not roll back completed external effects.** A
remote request or file replacement may already have committed. Design retries
around idempotency/readback and dedicated roots. This example uses a GET and a
replacement report rather than a non-idempotent remote write.

Run the native gates without Python:

```sh
make test-serving test-http-credentials
make test-serving-sanitize test-http-credentials-sanitize
```

No GitHub CI, service, watcher, live client setup or publication is created.
