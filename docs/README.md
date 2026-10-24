# Documentation

Start with the [project README](../README.md) for setup and a runnable program.
For authoring, read the [language specification entry point](specification.md).

## Current guides

| Guide | Purpose |
| --- | --- |
| [Specification](specification.md) | Reading order and implemented language/tooling boundaries. |
| [Native backend](qbe-native-backend.md) | C/QBE architecture, compiler C API, build requirements and execution limits. |
| [MCP client setup](mcp-client-setup.md) | Local stdio configuration, discovery, timeouts and troubleshooting. |
| [Host capabilities](capabilities.md) | Operator grants and a two-module file-report walkthrough. |
| [Binary distribution](binary-distribution.md) | Relocatable installed toolchain requirements and use. |

## Implemented references

- [Language](../resources/language.md): grammar, statements, expressions, modules
  and the agent authoring loop.
- [Types and JSON ABI](../resources/types.md): values, inference and exact
  input/output rules.
- [Standard library](../resources/stdlib.md): pure builtins, host-operation
  signatures, traps and limits.
- [Protocol catalog](../resources/schemas/mcp-agent-catalog.v1.json): MCP tool
  request/result schemas and discovery data.

The first three references are also available as the MCP resources
`apodexis://spec/language`, `apodexis://spec/types` and `apodexis://spec/stdlib`.
Discover the actual server's tools and capabilities before authoring.

## Project guides

- [Contributing](../CONTRIBUTING.md): source setup, tests and compatibility rules.
- [Security policy](../SECURITY.md): trust boundaries and sensitive issue reporting.
- [Changelog](../CHANGELOG.md): public release changes.
- [MIT License](../LICENSE) and [third-party notices](../THIRD_PARTY_NOTICES.md).
