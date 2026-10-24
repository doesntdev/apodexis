# Changelog

Public source-release changes are recorded here.

## Unreleased

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
