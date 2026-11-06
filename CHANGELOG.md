# Changelog

Public source-release changes are recorded here.

## Unreleased

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
