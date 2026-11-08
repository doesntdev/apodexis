# Native HTTP client

Apodexis can make bounded HTTP/HTTPS request-response calls through an explicit
operator-owned broker. Activate it through CLI `run --capabilities` or the
separate [operator-configured serving mode](tool-serving.md). No curl executable
or libcurl is used. The native
worker's existing seccomp restrictions are unchanged. Raw sockets, DNS APIs,
listeners, WebSockets, streaming, redirects, automatic retries, proxies and
automatic cookie management are not provided. Default authoring MCP calls still
cannot activate networking or grant policies.

## Build and activate

`make` builds the libc-only `.build/tools/apx-http`. Default `all`, `check`, `test`
and `dist` need no OpenSSL. HTTPS additionally requires `make http-tls`, using
system OpenSSL headers/libraries through `pkg-config openssl`, and produces the
separate `.build/tools/apx-http-tls`. There is no download or insecure fallback.
Missing TLS returns `http_tls_unavailable`; HTTPS never becomes plaintext.
Generated pure executables retain their libc-only dependency boundary.

`bin/apodexis run entry.apx --input input.json --capabilities policy.json`
activates a direct CLI policy. A serving manifest may separately bind that policy
to an operator-selected compiled tool. No policy, source tests, `verify`, default
authoring MCP run and compiled invoke remain ungranted (`capability_denied`).
Callers cannot supply policy-shaped extra arguments or self-grant through
source/modules. The 13-tool authoring interface is unchanged. Serving uses an
independent CLOEXEC image descriptor alongside the broker channel; standalone
programs without a trusted channel remain denied. See [tool serving](tool-serving.md)
for private HTTP header bindings, startup snapshots and their additional bounds.

Example operator policy (replace the reserved nonconnecting example origin):

```json
{"schema":"apodexis.host_policy.v3","tls_security":"pq_required","max_bytes":1048576,"max_requests":64,"http_timeout_ms":5000,"http":[{"origin":"https://api.example.invalid:443","path_prefix":"/v1/","methods":["GET","POST"],"headers":["authorization","content-type","accept"]}]}
```

V3 defaults `tls_security` to `pq_required` when omitted. Only the exact strings
`pq_required` and `standard` are accepted. Required mode permits HTTPS grants
only, including legacy exact `http_get` URLs; a plaintext grant rejects the whole
policy before execution or connection. A v3 policy without networking grants is
valid and grants no networking. Explicit `"tls_security":"standard"` opts into
ordinary HTTP and verified TLS 1.2..1.3, with no guarantee of post-quantum security.
V1/v2 retain their prior transport behavior and reject `tls_security`; migrating
to v3 is an operator decision, not a guest Request setting. Unknown, duplicate or
mistyped policy fields fail closed. V3 also accepts named `crypto_keys` and
`crypto_timeout_ms` for the separate application-encryption helper; neither
field grants network access. Those application-key fields remain CLI-only and
are refused in serving policies. See [post-quantum key policies](post-quantum.md).

A grant compares exact scheme, case-insensitive ASCII hostname/bracketed IP and
effective numeric port. Origin has no path, query, fragment, wildcard or userinfo.
`path_prefix` is absolute and slash-terminated: `/v1/` never grants `/v1evil/`.
Query values may vary. Path bytes are forwarded without normalization; v2 rejects
literal dot/dot-dot segments, backslashes, malformed escapes and percent-encoded
dot/slash/backslash/percent/control ambiguity instead of repairing them.
Every caller header must be listed in the same matching grant. The exact uppercase
method must be one of GET, HEAD, POST, PUT, PATCH, DELETE or OPTIONS and explicitly
granted. This authorizes an origin and its DNS trust, **not** a public-IP firewall:
explicit private/loopback origins are supported. Unknown fields/types, duplicate
grants/methods/header names, invalid origins/prefixes and malformed policies fail
closed. Up to 64 grants are allowed. Empty or absent grants deny.

The retained read_root/write_root/env/clock/random/http_get/max_bytes/max_requests
fields keep their prior semantics. V1 exact raw `http_get` grants do not authorize
`http_request`; v2 prefix grants do not authorize `http_get`. The legacy wrapper
still succeeds only for a UTF-8 2xx body, preserves literal brace paths and omits
fragments on the wire. Its existing denial/non-UTF8/failed categories remain.

## Typed API

Copy `examples/http_client/http.apx` beside your entrypoint and use `I h "http"`.
There is no implicit module search path; an MCP bundle may explicitly supply this
ordinary pure module but still cannot activate networking in the default
authoring server. Serving authority comes only from its startup manifest.

- `h::Body`: `encoding:str`, `data:str`.
- `h::Header`: `name:str`, `value:h::Body`.
- `h::Request`: `method:str`, `url:str`, `headers:[h::Header]`, `body:h::Body`.
- `h::Response`: `status:i64`, `headers:[h::Header]`, `trailers:[h::Header]`,
  `body:h::Body`.
- `h::text str -> Body`; `h::header str str -> Header`.
- `h::body_text Body -> result str str`: UTF-8 succeeds, base64 returns
  `http_non_utf8`, unknown encoding returns `invalid_http_body`.
- `h::request Request -> result Response str` encodes, calls the broker and
  decodes the ordinary Response. An impossible malformed success becomes
  `invalid_http_response`.

`http_request` (opcode 1007) takes a strict Request JSON **string** and returns
`result str str`: success contains one Response JSON document. Duplicate, extra,
missing or mistyped fields are invalid. A completed 3xx/4xx/5xx is success data,
not a transport failure. `json_encode` (pure opcode 56) accepts the same checked
concrete JSON-eligible types as `json_decode`, including its map-payload exception.
It evaluates once, preserves aliases/arena/fuel behavior, and propagates normal
resource failures. Nonfinite f64 anywhere in a nested value traps R999, never
silently becomes JSON null. No dynamic JSON type or new reserved record exists.

Typed document bodies use `h::text (json_encode payload)` and an explicit
`h::header "content-type" "application/json"`. Match returned `h::body_text`,
then use `(json_decode ApplicationType document)` for typed inbound data.
The supplied `examples/http_client/main.apx` accepts this exact input shape:

```json
{"method":"POST","url":"https://api.example.invalid/v1/items","headers":[{"name":"content-type","value":{"encoding":"utf8","data":"application/json"}}],"body":{"encoding":"utf8","data":"{\"id\":7}"}}
```

Body and header values use exactly `utf8` or standard RFC4648 canonical padded
`base64` (no whitespace; zero unused pad bits; empty is valid). UTF-8 bodies can
contain NUL. Response encoding is UTF-8 only after full strict validation,
otherwise canonical base64. No decompression or charset conversion occurs.
Header names are ASCII tokens, returned lowercase; duplicate order is retained.
Values trim only surrounding SP/HTAB, preserve obs-text through base64 and reject
CR/LF/NUL, other C0 controls except HTAB, and DEL. Callers cannot set Host,
Content-Length, Transfer-Encoding, Connection, Expect, Upgrade, TE, Trailer,
Accept-Encoding, Proxy-Authorization or Proxy-Connection. Transport emits Host,
Content-Length, Connection: close and Accept-Encoding: identity.

## Protocol and limits

One fresh HTTP/1.x connection per request, no pooling/pipelining/replay. Accept
HTTP/1.0 or 1.1 final statuses 200..599 and at most ten informational blocks;
101/upgrade is rejected. HEAD/204/304 are bodyless even with representation lengths.
Return initial headers separately from trailers. Content-Length, exactly chunked
transfer coding, or connection-close framing is supported. Duplicate CL (even
equal), TE+CL, other transfer codings, malformed chunks/extensions/trailers,
forbidden framing trailers, premature EOF and overruns are errors, not truncation.

`max_bytes` is 1..1048576 and bounds decoded request and response bodies
independently. Encoded lengths are checked before decoding/allocation. Lines
(including the request/status line) are at most 8192 bytes excluding CRLF.
The complete head is limited to 65536 bytes, including start line, field CRLFs
and terminating blank line. The 128-field limit includes all four managed
request fields, leaving at most 124 caller fields; response limits aggregate
informational/final headers and trailers. Outbound accounting uses the actual
serialized start line, authority and Content-Length before any connection.
Request JSON also receives a nonallocating shape-work check before the general
DOM parser: impossible oversized structures fail as `invalid_http_request`,
while structural punctuation and escaped quotes inside body strings remain data.
Response wire and broker/helper frames are each at most 8388608 bytes. Existing
arena/output budgets remain separate. `max_requests` defaults to 64, maximum
1024; denied/invalid calls consume the request budget. `http_timeout_ms` defaults
to 5000 and may only be reduced (1..5000).

The broker starts a monotonic deadline before fork/exec and bounds resolver,
connect, TLS, send/read and IPC together with the enclosing 30-second execution
deadline. DNS runs only in the one-shot helper. Helpers join the worker process
group, check parent-death identity, close unrelated descriptors and are exactly
waited/reaped; CLI cancellation uses the same supervision path.

Errors are payload-free tokens: `capability_denied`, `invalid_http_request`,
`http_request_denied`, `http_header_limit`, `http_byte_limit`, `http_timeout`,
`http_resolve_failed`, `http_connect_failed`, `http_tls_unavailable`,
`http_tls_failed`, `http_pq_unavailable`, `http_protocol_error`, `http_transport_failed`,
`invalid_http_response`. Existing host budget/channel errors remain separate.
No URL/body/credential-bearing error text is returned.

**An error after sending can follow a completed remote effect.** A timeout,
connection loss or TLS close is not proof that POST/PUT/PATCH/DELETE did nothing.
No automatic retry is performed. Use application idempotency/readback protocols.

## Verified TLS and distribution

The optional helper uses OpenSSL with configuration loading disabled and a
sanitized environment (no inherited proxy, credentials, SSL_CERT_FILE/DIR or
SSL_CONF configuration). It verifies the full chain and DNS hostname or IP,
sends SNI for DNS and offers HTTP/1.1 ALPN only. In required mode it configures
**TLS 1.3 only**, **X25519MLKEM768 only** and
**TLS_AES_256_GCM_SHA384 only**. It independently checks the negotiated
version/group/cipher and selected HTTP/1.1 ALPN before sending any HTTP bytes.
A classical-only peer, TLS 1.2 peer, incompatible cipher, missing ALPN or failed
certificate verification is refused without downgrade, retry or replay.
Unsupported fixed hybrid TLS configuration returns `http_pq_unavailable` before
DNS/connect where possible; an absent helper remains `http_tls_unavailable`,
and other handshake/trust failures remain `http_tls_failed`. The legacy GET
wrapper keeps its generic `http_get_failed` mapping but uses the same mode.
Hybrid key establishment combines X25519 and ML-KEM-768; it does **not** make
certificate signatures post-quantum, nor implement application-level encryption.

System OpenSSL 3.5+ with this group available is needed for required mode.
The helper also compiles against older supported OpenSSL 3.x using public string
group APIs; unavailable required algorithms fail closed, never choose standard.
Standard mode and v1/v2 retain verified TLS 1.2..1.3 behavior.
System roots are used unless the operator sets `tls_ca_file` in v2/v3: an absolute,
non-symlink, bounded regular CA PEM file owned by the current operator/root and
not group/world-writable. It is snapshotted and validated at policy load, replaces
system roots for that policy, and requires the TLS helper. It is never a program
argument or an insecure-verification switch. No client certificates or CA
directory controls are exposed.

`make dist` explicitly packages only libc HTTP, even if a stale TLS helper exists.
`make dist-http` builds and packages both helpers in
`apodexis-linux-amd64-http.tar.gz`. `HTTP-PROFILE` records the selected profile.
The TLS bundle additionally needs compatible system libssl.so.3/libcrypto.so.3
and trusted roots (or an explicit operator CA snapshot). Neither bundle needs a
C compiler for installed execution. See [binary distribution](binary-distribution.md)
and [third-party notices](../THIRD_PARTY_NOTICES.md).

## Local qualification

`make test` includes native C HTTP fixtures and legacy HTTP vectors without curl.
`make test-http-tls` explicitly exercises OpenSSL-generated synthetic loopback
certificates and does not silently skip unavailable TLS. `make test-http-sanitize
HTTP_SANITIZE_TLS=1` instruments the real broker/driver/helper and JSON runtime
with ASan/UBSan, and the generated worker runtime with UBSan trap instrumentation
compatible with unchanged seccomp. It shares QBE/startup/source rather than
copying build trees. Resolver-stall coverage uses only a test-linked wrapper;
there is no production environment or guest debug switch.

`make test-http-pq` is separate explicit hybrid qualification, with no skips.
It drives the real CLI policy/broker/helper against the existing synthetic native
loopback fixture and checks the server-observed protocol, group and cipher.
It covers v3 defaults, standard/classical positive controls, classical/TLS1.2/
cipher/trust/ALPN refusal with zero HTTP payload, rejected plaintext policies,
guest overrides, old-policy compatibility, missing helpers, legacy HTTPS and
no redirect/retry. A test-linked unavailable-group helper checks fail-closed
capability failure without changing any system provider or production setting.
Use `make test-http-sanitize HTTP_SANITIZE_TLS=1 HTTP_SANITIZE_PQ=1` to run the
same PQ cases through the instrumented driver, broker and TLS helper.
Synthetic certificate private keys remain in memory; only public CA certificates
are written to owned fixtures and all fixture outputs are removed on success.
Internally, the broker sends an exact `tls_security` string in its trusted helper
context; the helper strictly validates that context and sets `pq_required` on
`ApxHttpRequest` only after parsing the four-field guest Request. Neither field
is part of the guest API or serialized Response.
