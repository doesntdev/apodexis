# Changelog

Public source-release changes are recorded here.

## Unreleased

### Read-only repository briefing

- Appended pure `json_decode_fields` (57), selecting required declared struct
  fields recursively without relaxing strict decoding, JSON syntax, maps, tagged
  wrappers, ABI validation or resource/trap containment.
- Added native Apodexis `repo_brief`, fixed public GitHub GET policy/manifest,
  bounded pagination and explicit partial/error/rate metadata. The optional
  stdlib MCP CLI and loopback/oracle integration do not fetch/normalize APIs in
  Python or change default authoring authority. See [repo brief](docs/repo-brief.md).

### Operator-configured compiled MCP tools

- Added separate `apodexis serve --manifest` with operator-selected named tools,
  compile-once sealed images, compiler-derived strict schemas and fixed per-tool
  host/fuel/input/output/deadline limits. Default authoring MCP stays 13 tools
  without host authority; callers cannot submit source or grants.
- Added event-driven stdio/child supervision, bounded concurrent calls with no
  queue, actual cancellation and fresh COW broker state. Owned process groups,
  independent image/broker descriptors and parent-death cleanup retain native
  syscall restrictions.
- Clarified ten existing object-only authoring output-schema roots for current
  MCP client discovery, without changing accepted DTOs, tool names or authority.
- Added private origin/path/method-bound HTTP header snapshots and exact injected
  wire-budget accounting, with no ambient credentials or application-key grants.
- Added a checked API-to-report program and optional real MCP-client HTTP/HTTPS
  fixture demo with byte-exact file readback. No third-party API integration,
  crypto extension, GitHub CI, publication or speed claim. See [tool serving](docs/tool-serving.md).

### Application post-quantum encryption (CLI)

- Appended `pqc_seal` (1008) and `pqc_open` (1009), plus typed Body/Envelope
  wrappers. A separate optional static OpenSSL 4.1+ helper uses standard CMS
  ML-KEM-768/HKDF-SHA256/AES256-WRAP/AES256-GCM and strict DER profile validation.
- Added strict v3 named public/private-key grants, sealed snapshots, private-file
  grant-overlap refusal, bounded IPC/deadlines and authenticated-only plaintext
  release. Ordinary MCP/tests/standalone execution remain default-denied.
- Added operator-only `pqc-keygen`, real roundtrip/interop/refusal and sanitizer
  qualification targets, explicit `dist-pqc` packaging and upstream notices.
  Default compiler/pure-worker builds stay libc-only and never auto-download
  OpenSSL. See [post-quantum encryption](docs/post-quantum.md).
- Content authentication is not sender identity, signatures, replay protection,
  forward secrecy or FIPS validation. No publication or benchmark claim.

### Required hybrid post-quantum HTTPS (transport slice)

- Added operator policy v3, defaulting to `pq_required`, with explicit `standard`
  compatibility mode; v1/v2 retain their behavior and reject the new field.
- Required mode rejects plaintext grants and requires verified TLS 1.3,
  X25519MLKEM768 and TLS_AES_256_GCM_SHA384. Negotiated parameters and HTTP/1.1
  ALPN are checked before HTTP bytes; no downgrade, retries or redirects.
- Added separate `test-http-pq` real-CLI/native-loopback qualification and PQ
  sanitizer mode. Default builds remain libc-only; optional `http-tls` uses
  system OpenSSL, and unsupported required algorithms fail explicitly.
- This implements hybrid transport key establishment, not post-quantum
  certificate signatures or application encryption. No MCP activation,
  publication or benchmark result is implied.

### Curl-free native HTTP client (CLI)

- Appended `json_encode` (56) and `http_request` (1007), plus the ordinary typed
  Body/Header/Request/Response module and example. Finite typed JSON, reversible
  UTF-8/base64, ordered duplicate headers and separate trailers are supported.
- Added a bounded native libc HTTP helper, separate optional verified OpenSSL TLS
  helper, strict v2 operator grants/CA snapshots and deadline/child supervision.
  Legacy exact-URL GET remains separate; no curl/libcurl, retries or redirects.
- Added native HTTP/TLS fixtures, implementation sanitizer and explicit default/TLS
  bundle profiles. MCP, tests and verify remain denied; 13 tools and existing
  worker seccomp/FD3/sealed-image controls remain unchanged. No publication or
  benchmark result is implied. See [HTTP client](docs/http-client.md).
- Bound complete outbound heads (including managed fields) before connection,
  and reject oversized malformed request structures before JSON DOM allocation.
  Exact wire-boundary and malformed-document regressions cover both paths.

### Session-scoped compiled MCP tools

- Added `apodexis_compile`, `apodexis_invoke` and `apodexis_release` to the native
  MCP catalog, bringing discovery to 13 tools. Source tests are checked during
  compile, not executed; test/verify and ordinary CLI/run behavior stay separate.
- Retain sealed executable memfds and owned identity metadata under bounded
  per-session count/byte admission. Repeated invokes use fresh restricted workers
  without source rechecking/building or result memoization. Explicit release,
  EOF and controlled shutdown reclaim entries.
- Added strict schemas, operator-only startup limits, lifecycle/error regressions
  and targeted ASan/UBSan/leak coverage of the real MCP driver/store. Language,
  protocol, SDK and server versions are unchanged. No new performance ranking is
  claimed. See [compiled tools](docs/compiled-artifacts.md).
- Published a separate fixed native-MCP latency study with 31 scored calls per
  arm, original observations and offline reaggregation. Updated the website and
  client guide for 13 tools; historical cross-language tables retain their source.
  See [MCP reuse methodology](benchmarks/mcp_reuse/README.md).

### Native data-tooling follow-up

- Checked indexing of non-`i64` arrays now avoids constructing boxed integer
  indices, retaining bounds checks, evaluation order, fuel and resource limits.
- `(json_decode T document)` decodes strict JSON into eligible declared types
  and returns `result T str`. Imported records and nested arrays/maps work;
  resource exhaustion remains terminal rather than becoming a data error.
- The pure configuration validator in `examples/config_validator.apx` includes
  13 source-declared tests and 55 reproducible MCP input/limit cases. Its native
  MCP authoring and maintenance workflow is documented in `docs/config-validator.md`.
- Existing cross-language benchmark tables retain their original measured
  source attribution. These additions are not a new cross-language campaign.

### Initial public source release

- Experimental C99 compiler/runtime with a vendored QBE 1.3 backend for
  Linux x86-64/glibc native programs.
- Typed functions, structs, arrays, immutable maps, option/result values,
  checked builders, modules and source-declared tests.
- Canonical source identities, structured diagnostics, typed-hole context,
  guarded canonical repairs, JSON contracts and pseudocode.
- CLI tooling and a local stdio MCP interface for discovery, checking,
  verification and bounded native execution.
- Default-denied host operations with explicit operator-owned CLI grants for
  scoped files, named environment reads, clock, entropy and exact-URL HTTP GET.
- Native conformance/safety tests, sanitizer targets, relocatable distribution
  packaging, implemented references and runnable examples.

The implementation is experimental. See the [README](README.md) for dependencies,
implemented boundaries and limitations, and [SECURITY.md](SECURITY.md) for trust
and reporting guidance.
