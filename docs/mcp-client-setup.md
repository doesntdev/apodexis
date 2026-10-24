# Apodexis MCP client setup

## Build the local server

From the repository root, build the standalone native C server and its pinned QBE backend:

```bash
make
```

Configure clients with the resulting **absolute** path ending in
`bin/apx-mcp`, for example
`/absolute/path/to/apodexis/bin/apx-mcp`. The native build is the
supported client path. The executable takes no arguments, needs no `PATH`
wrapper, and reserves stdout exclusively for newline-delimited JSON-RPC.

## Hermes Agent

Add this stdio server to `~/.hermes/config.yaml`, replacing the command with
the absolute release-binary path:

```yaml
mcp_servers:
  apodexis:
    command: "/absolute/path/to/apodexis/bin/apx-mcp"
    args: []
    timeout: 130
    connect_timeout: 10
    supports_parallel_tool_calls: false
```

The standalone server dispatches requests serially; each native
compilation/execution subprocess has a finite 30-second bound. A compile-plus-run
call can include multiple subprocesses, so prefer a finite 130-second client tool
timeout rather than treating 30 seconds as the whole-call deadline. Verify
the entry with:

```bash
hermes mcp test apodexis
```

Then restart Hermes or run `/reload-mcp`. When capability discovery succeeds,
Hermes utility wrappers expose the server's six resources. Apodexis advertises
no prompts.

## Claude Desktop

In Claude Desktop's local MCP configuration, use this manual local-server
form, then restart Desktop:

```json
{
  "mcpServers": {
    "apodexis": {
      "command": "/absolute/path/to/apodexis/bin/apx-mcp",
      "args": []
    }
  }
}
```

This is the manual JSON configuration form for a local stdio server. It is
distinct from the newer Extensions UI and its packaged `.mcpb` installation
path; this guide does not claim that the Extensions UI accepts a raw binary.
Apodexis requires no credentials, environment variables, network URL, runtime
arguments, or package-manager wrapper.

## Claude Code

Register the official local stdio form at user scope:

```bash
claude mcp add --transport stdio --scope user apodexis -- /absolute/path/to/apodexis/bin/apx-mcp
claude mcp get apodexis
claude mcp list
```

Start a Claude Code session and use `/mcp` for the session view. Client command
syntax may vary by installed version; consult its official documentation and
verify actual server discovery. This guide does not imply that the client is
installed or configured on your machine.

## Codex-compatible clients

Codex CLI can register the local server directly:

```bash
codex mcp add apodexis -- /absolute/path/to/apodexis/bin/apx-mcp
```

The equivalent user configuration is:

```toml
[mcp_servers.apodexis]
command = "/absolute/path/to/apodexis/bin/apx-mcp"
args = []
startup_timeout_sec = 10
tool_timeout_sec = 130
```

Verify discovery, then inspect `/mcp` inside a Codex session:

```bash
codex mcp get apodexis --json
codex mcp list --json
```

This config applies to the local Codex host. ChatGPT web does not read a local
Codex configuration or launch binaries from the user's machine.

## Generic newline-delimited stdio

The following executable transcript speaks MCP `2025-11-25` directly. Each
input object occupies one line:

```bash
/absolute/path/to/apodexis/bin/apx-mcp <<'JSONRPC'
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25","capabilities":{},"clientInfo":{"name":"apodexis-manual-check","version":"1"}}}
{"jsonrpc":"2.0","method":"notifications/initialized","params":{}}
{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}
{"jsonrpc":"2.0","id":3,"method":"resources/list","params":{}}
{"jsonrpc":"2.0","id":4,"method":"resources/read","params":{"uri":"apodexis://spec/language"}}
{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"apodexis_capabilities","arguments":{}}}
JSONRPC
```

The initialized notification has no `id` and receives no response. stdout must
contain JSON-RPC only; logs or wrapper banners on stdout corrupt the protocol.
Correlate responses by JSON-RPC `id`. This native server dispatches calls serially.

## Authority and security boundary

Source text and JSON input are inline request values; no API accepts a path.
The six resources are fixed shipped data under `resources/`, and request/result shapes are checked against the bundled protocol catalog. The compiler/server has authority to read its own distribution data and launch only QBE, the platform assembler/linker and generated native workers. It exposes no client-selected paths, arbitrary process command, network request, environment access, roots, sampling, prompts, HTTP or host callback. Native computation workers have separate kernel syscall and resource restrictions.

The native server exposes ten exercised tools, including inline module bundling, checked metadata/pseudocode and source-hash-guarded canonical repairs. Nothing mutates a source file implicitly: repairs return revised inline source only. Query `tools/list` and capabilities to discover the exact supported native interface. Clients must impose a finite timeout; this serial server does not currently interrupt an in-flight worker on an MCP cancellation notification.

## Troubleshooting

- **Server will not start:** confirm the configured path is absolute, ends in
  `bin/apx-mcp`, exists, and is executable. Rebuild the release
  binary if necessary.
- **Protocol parse errors:** launch the binary directly, without a wrapper that
  prints to stdout. Only JSON-RPC may appear there.
- **Calls time out unexpectedly:** preserve timeout layering: the client tool
  timeout must exceed 30 seconds; its finite connect/startup timeout is
  separate.
- **Old or missing tools/resources:** test the server, then restart the client
  or refresh MCP discovery (`/reload-mcp`, `/mcp`, or the client's equivalent).
- **A request contains a filename:** read the file in the client and send its
  contents inline. Apodexis intentionally has no file/path authority.

## Official references

- [Hermes MCP documentation](https://hermes-agent.nousresearch.com/docs/user-guide/features/mcp)
- [Claude Code MCP documentation](https://docs.anthropic.com/en/docs/claude-code/mcp)
- [Claude Desktop local MCP help](https://support.anthropic.com/en/articles/10949351-getting-started-with-model-context-protocol-mcp-on-claude-for-desktop)
- [Codex MCP documentation](https://developers.openai.com/codex/mcp)
- [MCP 2025-11-25 specification](https://modelcontextprotocol.io/specification/2025-11-25)
