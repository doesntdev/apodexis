# Service configuration validator

[`examples/config_validator.apx`](../examples/config_validator.apx) is a compiled,
pure Apodexis tool using `(json_decode Config document)`. It returns only
`valid` and `errors`; it does not deploy anything or access files/network.

## Run it

From a built source checkout:

```sh
bin/apodexis test examples/config_validator.apx --json
bin/apodexis run examples/config_validator.apx --input examples/config-validator/valid.input.json --fuel 100000 --max-input-bytes 65536 --max-output-bytes 16384 --json
```

The example's `main` takes a **string containing JSON**, not a configuration
object directly. `valid.config.json` is the readable document;
`valid.input.json` is that document encoded as the entrypoint JSON string.
In MCP, pass the document string directly as `apodexis_run.input`, alongside
inline `source` and explicit `limits`. An object supplied as the outer input is
an ABI error, not a validation report.

The valid fixture returns:

```json
{"valid":true,"errors":[]}
```

## Reuse the validator through MCP

Keep one initialized native MCP session open:

1. Read `examples/config_validator.apx` in the client and supply its **exact inline
   text** as `apodexis_compile` arguments `{"source": <that source string>}`.
   The server does not read a filename. Compile checks but does not execute the
   13 declared tests; run them separately with `apodexis_test` or use verify.
2. On `status: "ok"`, retain the returned `artifact_id` and inspect `contract`.
   Call `apodexis_invoke` with exactly that ID, the configuration **document
   string** as `input`, and
   `{"fuel":100000,"max_input_bytes":65536,"max_output_bytes":16384}` as `limits`.
   Use the document string from `valid.input.json`, not that file's path and not
   a configuration object. For the valid fixture the execution record has
   `status: "ok"` and `output: {"valid":true,"errors":[]}`.
3. Invoke the same ID with each different document string. No source or backend
   build is repeated. Domain-invalid configurations still return successful
   execution with `output.valid: false`; ABI/resource errors are execution errors.
4. Release with `apodexis_release` arguments `{"artifact_id": <returned ID>}`.
   A later rule revision needs a newly compiled handle; an existing old-version
   handle stays independently immutable until released.

The 55 cases remain unchanged inputs/limits, not a claim that compilation ran
them. The [compiled tools guide](compiled-artifacts.md) defines response fields,
sealed session storage, quotas, errors and shutdown. The default store is bounded
and grants no file/network or other host access.

## Configuration contract

Every field is required; extra fields are rejected, including inside `server`.
There are no defaults or implicit conversions.

| Field | Type and rule |
| --- | --- |
| `name` | `str`; nonblank after ASCII whitespace trimming, no NUL. |
| `environment` | `str`; exactly `development`, `staging`, or `production`. |
| `server.host` | `str`; nonblank after ASCII trimming, no NUL. This is not DNS/hostname resolution or a complete hostname-syntax check. |
| `server.port` | `i64`; 1–65535 inclusive. |
| `timeout_ms` | `i64`; 100–300000 inclusive. |
| `retries` | `i64`; 0–10 inclusive. |
| `debug` | `bool`; must be false in production. |

Boolean, floating and textual values are not accepted for integer fields.
Valid Unicode strings remain supported. Domain failures accumulate in source
order as `name_invalid`, `environment_invalid`, `host_invalid`,
`port_out_of_range`, `timeout_out_of_range`, `retries_out_of_range`, and
`production_debug_enabled`. The fixed eight-item builder bounds this finite
rule set.

Malformed JSON or a structural/type error produces one error such as
`decode: invalid_json`, `decode: duplicate_field`, `decode: missing_field`,
`decode: extra_field`, `decode: type_mismatch`, or
`decode: numeric_out_of_range`. The parser/schema validator reports its first
failure; the tool does not echo document values or diagnostic messages.

A false validation result is successful execution with `valid: false`.
Fuel, memory and input/output exhaustion remain terminal runtime errors, not
validation reports. See [typed JSON semantics](../resources/stdlib.md#typed-json-documents).

## Native MCP authoring and maintenance

The program was authored through the actual `bin/apx-mcp` stdio server:
resource/tool discovery, typed-hole feedback, check, guarded application of
compiler-issued canonicalization repairs, verify, declared tests, explain and
bounded runs. The delivered file is the exact returned, verified canonical
source and is also discoverable in `apodexis://examples`.

A maintenance pass changed only the production-debug rule and added regression
tests. The same production/debug input first returned valid, then returned
`production_debug_enabled`; development debugging stayed valid. Stale source
hashes were refused without an applied repair.

For a future semantic change, author a new source candidate and submit it to
`apodexis_check`. Native `apodexis_apply_repairs` currently accepts only the
compiler-issued canonicalization repair returned for that exact candidate;
it is **not** an arbitrary AST/text-edit endpoint. Pass that repair unchanged
with the returned `source_text_hash`, then verify and run the returned source.
Typed-hole context helps author a correction but does not grant a fabricated
repair ID. Keep v1/v2 source and request/response receipts when maintaining a rule.

The program contains 13 source-declared tests. The accompanying
[`cases.json`](../examples/config-validator/cases.json) defines 55 reproducible
MCP input/limit cases, including strict/nested fields, duplicates, NUL, numeric
boundaries, aggregate domain errors and exact input/output limits. Default MCP
execution stays capability-denied; no operator policy is accepted by these calls.
