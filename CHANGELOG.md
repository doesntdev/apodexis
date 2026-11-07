# Changelog

Public source-release changes are recorded here.

## Unreleased

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
