# Apodexis standard library: every implemented builtin

The standard library provides 56 pure operations (including special `with` and `json_decode` lowering)
and nine explicitly controlled host operations. Core primitives and essential pure
libraries are listed below; use only these implemented names and signatures. All calls are prefix
expressions on one source line. In signatures below, T is notation for the
actual concrete type, not a user-defined generic parameter. Numeric operations
have exactly two operands unless stated otherwise.

| Names | Signature / behavior |
| --- | --- |
| `+`, `-`, `*`, `/` | `(operator a b)`: matching i64 or matching f64 -> same numeric type. Integer division truncates toward zero. Division by zero traps. |
| `%` | `(% a b)`: i64, i64 -> i64 remainder; zero divisor traps. |
| `=`, `!=` | `(operator a b)`: matching non-builder types -> bool; structural comparison for arrays/structs/variants. |
| `<`, `<=`, `>`, `>=` | `(operator a b)`: matching numeric types -> bool. Not string ordering. |
| `!` | `(! value)`: bool -> bool negation. |
| `&`, `\|` | `(operator a b)`: bool, bool -> bool. Both operands evaluate; no short-circuiting. The actual disjunction builtin is `|`. |
| `cat` | `(cat a b)`: str, str -> str concatenation. Exactly two strings. |
| `#` | `(# value)`: array or str -> i64. Arrays count elements; strings count UTF-8 bytes. |
| `@` | `values[index]`, canonical `(@ values index)`: [T], i64 -> T, zero-based. Negative or out-of-range index traps. No string indexing. |
| `.` | `value.field`, canonical `(. value field)`: declared struct -> field type; no dynamic properties or methods. |
| `buf` | `(buf T capacity)`: type T and i64 capacity -> builder T. Capacity must be nonnegative. |
| `push` | `(push buffer item)`: builder T, T -> updated builder T. Exceeding capacity traps. Save its returned value. |
| `finish` | `(finish buffer)`: builder T -> [T]. |
| `count` | `(count buffer)`: builder T -> i64 current element count. |
| `cap` | `(cap buffer)`: builder T -> i64 fixed capacity. |
| `i64` | `(i64 value)`: i64/f64 -> i64; f64 conversion truncates toward zero and saturates out-of-range values. |
| `f64` | `(f64 value)`: i64/f64 -> f64; large i64 values can lose precision. |
| `ok` | `(ok value)`: payload T -> result T E, using an expected result type. |
| `err` | `(err error)`: payload E -> result T E, using an expected result type. |
| `some` | `(some value)`: payload T -> option T, using an expected option type. |
| `none` | `(none)`: no argument -> option T, using an expected option type. |

`P buffer item` is a statement, not a thirtieth builtin. It lowers exactly to
`S buffer (push buffer item)`: one fuel-charged binding update, with immutable
alias snapshots, normal item typing, eager item evaluation and capacity traps.

`buf`'s type argument can be nested or named, e.g. `(buf [i64] 3)` or
`(buf Point 3)`. It is not a quoted string or generic bracket expression.
Use `array_cat`, `array_get`, `array_set`, `array_slice`, and `with` for immutable
value operations; the string/conversion/map additions are documented below.
There is no sort builtin or reflection. Use `json_encode`/`json_decode` for typed JSON
documents, and loops
and builders when incremental array construction is needed. For an upper-bound number of appended items,
choose a capacity that covers that bound; allocation does not extend capacity.
Builders are values: assigning one copies its logical state; later push/finish
results do not mutate existing bindings. `finish` returns an array and does not
change the builder's type or make it serializable.

```apodexis
M doubled

F double_values values:[i64]->[i64]
  L out (buf i64 (# values))
  E value values
    P out (* value 2)
  A (= (count out) (# values))
  A (= (cap out) (# values))
  R (finish out)

F main input:[i64]->[i64]
  R (double_values input)

X empty
  A (= (double_values []) [])

X values
  A (= (double_values [1 -2 0]) [2 -4 0])
```

## Runtime traps and limits

Index/builder-capacity errors, division by zero, assertion failure, explicit
`Z "message"`, invalid entrypoint ABI input, fuel exhaustion, and configured input/output
limits are errors, not implicit error variants. Only `json_decode` contains its
document failures, including duplicate fields, as explicit result errors. No `try`, exception catch, or
`traps` expression is implemented. Test valid behavior with `X`/`A`; examine
expected-error behavior with explicit `apodexis_run` calls and their diagnostics.
Do not use unchecked indexing merely because a bool expression has a guard.

Each executed statement consumes one fuel unit; each while/each iteration also
consumes two additional fuel units.
Builtin/user-function evaluation and string/builder allocation have no separate
fuel surcharge; statements executed inside a helper still cost fuel. Helpers,
loops, and self-tests must remain bounded. `apodexis_run` requires explicit
`limits`; input/output byte limits are independent of fuel. The `apodexis_run`
input schema defines the caller-selected limit fields and accepted ranges.
Caller-selected limits do not remove the separate runtime and worker bounds.

## Essential pure libraries

All functions retain prefix call syntax. No locale-dependent Unicode case rules
are implied: the two case-conversion operations change ASCII letters only.

| Name | Signature and exact behavior |
| --- | --- |
| str_chars | str -> i64 Unicode scalar count; `#` still counts UTF-8 bytes. |
| str_char | str, i64 -> option str; one scalar, none for invalid index. |
| str_slice | str, i64 start, i64 count -> str; scalar units, nonnegative checked bounds. |
| str_find | str, str -> option i64; first scalar index; empty needle matches zero. |
| str_split | str, str delimiter -> [str]; delimiter must be nonempty; empty segments preserved. |
| str_trim | str -> str; trim ASCII space/tab/newline/carriage-return/form-feed/vertical-tab. |
| str_replace | str, str needle, str replacement -> str; nonempty needle, all nonoverlapping matches. |
| str_lower_ascii / str_upper_ascii | str -> str; other scalars and NUL preserved. |
| parse_i64 | str -> result i64 str; full optional-minus decimal token, checked range. |
| parse_f64 | str -> result f64 str; full finite decimal/exponent token, finite underflow accepted. |
| str_i64 | i64 -> str decimal formatting. |
| str_f64 | finite f64 -> str round-trip spelling; nonfinite runtime value traps. |
| map | `(map T)` -> map T; empty typed string-keyed map. |
| map_get | map T, str -> option T. |
| map_put | map T, str, T -> new map T with insert/replacement. |
| map_remove | map T, str -> new map T without key; missing key is harmless. |
| map_has | map T, str -> bool. |
| map_keys / map_values | map T -> [str] / [T], sorted unsigned UTF-8 key order. |
| array_get | [T], i64 -> option T; negative/out-of-range returns none. |
| array_set | [T], i64, T -> [T]; new array, checked index. |
| array_slice | [T], i64 start, i64 count -> [T]; new array, checked bounds. |
| array_cat | [T], [T] -> new [T]. |
| with | `(with record field=value ...)` -> same record type; named immutable updates. |
| json_decode | `(json_decode T text)` -> result T str; concrete JSON-compatible type marker, str evaluated once. See below. |

`parse_i64`/`parse_f64` reject whitespace, leading plus, trailing data, embedded NUL, overflow,
NaN and infinity. Parse failure is `(err "invalid i64")` or `(err "invalid f64")`.
Slices/array replacement trap on invalid bounds; empty split/replace patterns
trap. These are not implicit option/result failures.

```apodexis
M library_example
T Row struct
  name:str
  score:i64
F main->map i64
  L row (Row name="alpha" score=2)
  L revised (with row score=4)
  L scores (map i64)
  R (map_put scores revised.name revised.score)
X snapshots
  L original [1 2]
  L revised (array_set original 0 9)
  A (= original [1 2])
  A (= revised [9 2])
  A (= (str_slice "aé😀" 1 2) "é😀")
```

## JSON encoding and HTTP module

`(json_encode value)` (56) returns `str` using existing checked JSON eligibility,
including tagged option/result map payloads. Direct option/result, enums,
builders and ineligible nested shapes are rejected. Evaluation is once; aliases,
arena/fuel and normal resource traps are preserved. Nested nonfinite f64 traps
R999 instead of encoding null. For outbound documents use `h::text (json_encode
payload)` with an explicit content-type header.

`http_request` (1007) takes strict Request JSON text and returns Response JSON text
or a payload-free error token. The ordinary sibling module at
`examples/http_client/http.apx` provides Body/Header/Request/Response and
text/header/body_text/request helpers. Its record names are not reserved types.
See `docs/http-client.md` for exact fields, canonical padded base64, seven methods,
strict v2 grants, framing/bounds, TLS and post-send effect uncertainty.

## Typed JSON documents

`(json_decode T text)` takes a concrete type marker and one `str` expression,
evaluated exactly once. It returns `result T str` without an expected-type hint.
T must satisfy the existing JSON input schema rules: primitives, arrays, typed
maps and exact-field structs, with the existing tagged option/result map-payload
exception. Direct option/result, enum, builder and unresolved targets are rejected
even by check-only tools. No dynamic schema or `any` value is introduced.

Success normalizes values exactly like entrypoint input: i64 accepts only an
in-range integer token (not bool, float or numeric text); f64 accepts integer or
finite floating numbers, including subnormal/underflow values. Structs retain
declaration order; maps sort by unsigned UTF-8 key bytes, including embedded NUL.
JSON whitespace, UTF-8, escapes/surrogates and trailing-data checks remain strict.
Escaped JSON NUL is allowed in strings/keys; raw NUL bytes are not ignored.

A101/A102/A103 and A104/`duplicate_field` `abi_input` failures return `err` with the stable diagnostic kind,
such as `invalid_json`, `numeric_out_of_range`, `type_mismatch`, `missing_field`,
`extra_field`, `duplicate_field` or `type_nesting_limit_exceeded`. No document text or diagnostic
message is returned. All other failures propagate, including operand
traps, R204 memory exhaustion and R202 when decoded text exceeds 8 MiB.
Decode retains existing values/snapshots and does not reset the arena. It adds no
per-builtin fuel surcharge; helper statements and existing hard budgets still apply.

Complete example (save as `decode_example.apx`, then run
`apodexis test decode_example.apx --json`; `run --input` takes a JSON **string**
containing the document, e.g. `"{\"label\":\"demo\",\"ports\":[80,443]}"`):

```apodexis
M decode_example
T Settings struct
  label:str
  ports:[i64]
F main text:str->result Settings str
  R (json_decode Settings text)
X valid
  C (main "{\"ports\":[80,443],\"label\":\"demo\"}")
    ok settings
      A (= settings.label "demo")
      A (= settings.ports [80 443])
    err problem
      A false
X missing
  C (main "{\"label\":\"demo\"}")
    ok settings
      A false
    err problem
      A (= problem "missing_field")
```

## Typed field projection

`(json_decode_fields T text) -> result T str` (pure opcode 57) has the same
concrete JSON type-marker eligibility and once-only text evaluation as
`json_decode`. It requires all selected struct fields and returns a fresh
normalized struct in declaration order, ignoring extra struct fields recursively
through arrays and map values. Map entries and tagged option/result wrappers are
not projected away. Unknown fields must still contain valid JSON with unique
names; escaped duplicates and NUL-suffixed keys retain exact parser semantics.
Wrong/missing selected values, malformed/trailing JSON, numeric/UTF-8/depth
errors remain errors; resource failures and traps propagate without resetting
aliases or replacing the live outer trap. `json_decode`, entrypoint ABI checks
and `json_encode` remain strict/unchanged. No host permission is required.
See [repo brief](../docs/repo-brief.md) for a native fixed-policy API workflow.

## Explicit host operations

| Name | Signature |
| --- | --- |
| fs_read | str relative_path -> result str str; UTF-8 regular file. |
| fs_write | str relative_path, str text -> result i64 str; atomically replace/create file, return bytes written. |
| env_get | str name -> result str str; one operator-allowlisted environment value. |
| clock_ms | zero args -> result i64 str; Unix wall-clock milliseconds. |
| random_hex | i64 bytes -> result str str; entropy encoded as hex, 0..256 bytes. |
| http_get | str exact_url -> result str str; allowlisted HTTP(S) GET, UTF-8 2xx body only. |
| http_request | str strict_request_json -> result str str; native HTTP status/headers/trailers/body document, operator v2/v3 CLI or serving grants only. |
| pqc_seal | str strict_seal_json -> result str str; standard CMS Envelope JSON, CLI v3 public-key grant only (1008). |
| pqc_open | str strict_open_json -> result str str; authenticated Body JSON, CLI v3 private-key grant only (1009). |

All are denied without an operator policy. CLI `run --capabilities policy.json`
activates an explicit v1, v2 or v3 host policy; a separate `serve --manifest` mode
can bind immutable per-tool policies and private HTTP header snapshots. Serving
callers cannot grant authority, and application-key fields/operations remain
disabled there. See [tool serving](../docs/tool-serving.md). In direct policies, the retained fields are read_root/write_root (absolute
directories), env (names), clock/random (bool), http_get (exact URLs), max_bytes
(1..1048576) and max_requests (1..1024; default 64). Unspecified grants are denied.
Requests use relative files under the supplied roots; absolute paths, `.`/`..`,
symlink traversal and nonregular/hardlinked files are rejected. Writes use an
owned temporary file and rename; a post-rename durability failure may report an
error after replacement, so applications must not assume every error means no
write occurred. HTTP redirects, URL glob expansion, proxy/credential inheritance
and arbitrary shell commands are not enabled. Native HTTP uses a libc helper;
HTTPS needs the optional verified OpenSSL helper. Neither uses curl/libcurl.
V2 adds strict http origin/path/method/header grants, a reducible 1..5000 ms
deadline and an operator-only tls_ca_file snapshot. V3 defaults to required
TLS1.3/X25519MLKEM768/TLS_AES_256_GCM_SHA384; only explicit `tls_security: "standard"`
permits ordinary HTTP/TLS. V1/v2 preserve legacy modes and reject v3 fields.
See [HTTP client](../docs/http-client.md).

V3 additionally accepts up to 16 named `crypto_keys` slots: `public_key_file`
grants seal, `private_key_file` grants open; neither implies the other. Key files
are operator-owned, strictly checked and snapshotted into sealed memfds, outside
granted file roots. Guests pass slot names, never key bytes/paths or algorithms.
Seal input is `{key,body}`; open input is `{key,envelope}`. Body is exactly
`{encoding,data}` (`utf8` or canonical padded base64); Envelope is exactly
`{format,data}`, using `cms-authenveloped-mlkem768-aes256gcm-v1` and base64 DER.
Success is one JSON document inside the ordinary `result str str`.
The ordinary module `examples/pqc/pqc.apx` supplies typed seal/open wrappers.

The optional static OpenSSL 4.1+ helper performs standard CMS ML-KEM-768,
HKDF-SHA256, AES256-WRAP and AES256-GCM. Plaintext is released only after profile
validation and authentication; authorized malformed/tampered open requests return
payload-free `pqc_decrypt_failed`. `max_bytes` bounds content, DER has a separate
4096-byte allowance, and byte/frame/request/deadline limits remain independent.
`crypto_timeout_ms` only reduces the 5000 ms default. Missing slots/directions
return `pqc_key_denied`; missing helper returns `pqc_unavailable`. Key generation
is the separate operator CLI `pqc-keygen`, never a builtin or MCP tool.
Recipient SPKI pinning is not sender identity, signatures, replay protection or
forward secrecy. See [post-quantum encryption](../docs/post-quantum.md) for the
complete policy, exact profile, key security and error contract.

Native worker seccomp remains active; only a broker channel gains read/write
permission. Tests, verify, default MCP and standalone artifacts without a trusted
broker channel have no grants. Capability errors are ordinary result values;
resource/fuel/safety traps are still terminal. The broker enforces byte/request
bounds, and the native runtime/driver retain memory, CPU and wall deadlines.
