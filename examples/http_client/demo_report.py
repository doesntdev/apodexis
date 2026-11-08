# /// script
# requires-python = ">=3.11"
# dependencies = ["mcp==2.3.0", "jsonschema==4.26.0"]
# ///
"""Optional real-client smoke; synthetic owned API, never a third-party API.

Run with `uv run examples/http_client/demo_report.py` after `make`.
Add --https after `make http-tls`. This client is not a compiler/runtime
requirement. Private fixture inputs are always retired, even on failure.
"""
from __future__ import annotations

import argparse
import asyncio
import contextlib
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import ssl
import subprocess
import tempfile

from jsonschema import Draft202012Validator
from mcp import ClientSession, StdioServerParameters
from mcp.client.stdio import stdio_client
from mcp.shared.exceptions import MCPError
from mcp.types import CallToolResult, TextContent


def canonical(value: object) -> str:
    return json.dumps(value, sort_keys=True, ensure_ascii=False, separators=(",", ":"))


def private_file(path: Path, data: bytes) -> None:
    fd = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "wb") as stream:
        stream.write(data)


def toolchain_counts(trace: Path | None) -> dict[str, int]:
    counts = {"qbe": 0, "as": 0, "ld": 0}
    if trace is not None:
        for executable in re.findall(r'execve\("([^"\n]+)"', trace.read_text()):
            name = Path(executable).name
            if name in counts:
                counts[name] += 1
    return counts


async def demonstrate(args: argparse.Namespace) -> dict[str, object]:
    root = Path(__file__).resolve().parents[2]
    binary = (args.binary or root / "bin/apodexis").resolve(strict=True)
    trace = args.trace.resolve() if args.trace else None
    if trace is not None and trace.exists():
        raise FileExistsError("trace output must be new")
    token = secrets.token_urlsafe(32)
    observed: list[dict[str, object]] = []
    expected = {"records": 3, "total": 9, "counts": {"alpha": 6, "beta": 3}}
    dataset = {"items": [{"name": "alpha", "count": 2},
                         {"name": "beta", "count": 3},
                         {"name": "alpha", "count": 4}]}
    scratch = os.environ.get("TMPDIR")
    if not scratch:
        raise RuntimeError("set TMPDIR to an owned scratch directory")
    with tempfile.TemporaryDirectory(prefix="apx-api-report-", dir=scratch) as temporary:
        fixture = Path(temporary)
        reports = fixture / "reports"
        reports.mkdir(mode=0o700)
        private_file(fixture / "api-token", token.encode())
        context = None
        if args.https:
            openssl = shutil.which("openssl")
            if not openssl:
                raise RuntimeError("HTTPS fixture requires the optional openssl CLI")
            result = subprocess.run(
                [openssl, "req", "-x509", "-newkey", "rsa:2048", "-nodes",
                 "-keyout", str(fixture / "tls-key.pem"), "-out", str(fixture / "ca.pem"),
                 "-days", "1", "-subj", "/CN=owned-loopback-fixture",
                 "-addext", "subjectAltName=IP:127.0.0.1",
                 "-addext", "basicConstraints=critical,CA:TRUE"],
                capture_output=True, timeout=20, check=False,
            )
            if result.returncode:
                raise RuntimeError("owned TLS fixture certificate generation failed")
            # The broker deliberately refuses group-writable CA snapshots,
            # independent of the operator shell's umask/OpenSSL defaults.
            (fixture / "ca.pem").chmod(0o600)
            (fixture / "tls-key.pem").chmod(0o600)
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.load_cert_chain(fixture / "ca.pem", fixture / "tls-key.pem")

        async def handle(reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
            try:
                async with asyncio.timeout(5):
                    raw = await reader.readuntil(b"\r\n\r\n")
                    lines = raw.decode("ascii").split("\r\n")
                    method, target, _ = lines[0].split(" ")
                    headers = {}
                    for line in lines[1:]:
                        if line:
                            key, value = line.split(":", 1)
                            headers[key.lower()] = value.strip()
                    authorized = headers.get("authorization") == "Bearer " + token
                    # An unrelated workstation GET / monitor is not our API call.
                    if target == "/" and headers.get("user-agent") == "localops/0.1":
                        status, payload = 404, {"error": "not_an_api_request"}
                    else:
                        observed.append({"method": method, "path": target,
                                         "authenticated": authorized})
                        if not authorized:
                            status, payload = 403, {"error": "authentication_required"}
                        elif target == "/v1/items":
                            status, payload = 200, dataset
                        elif target == "/v1/bad":
                            status, payload = 200, {"items": [{"name": "alpha", "count": True}]}
                        else:
                            status, payload = 503, {"error": "fixture_unavailable"}
                    body = canonical(payload).encode()
                    writer.write((f"HTTP/1.1 {status} Fixture\r\nContent-Type: application/json\r\n"
                                  f"Content-Length: {len(body)}\r\nConnection: close\r\n\r\n").encode() + body)
                    await writer.drain()
            except (ConnectionError, asyncio.IncompleteReadError, asyncio.LimitOverrunError, TimeoutError):
                pass
            finally:
                writer.close()
                with contextlib.suppress(ConnectionError, ssl.SSLError):
                    await writer.wait_closed()

        server = await asyncio.start_server(handle, "127.0.0.1", 0, ssl=context)
        async with server:
            port = server.sockets[0].getsockname()[1]
            origin = f"{'https' if args.https else 'http'}://127.0.0.1:{port}"
            grant = {"origin": origin, "path_prefix": "/v1/", "methods": ["GET"], "headers": []}
            policy = {"schema": "apodexis.host_policy.v2", "write_root": str(reports),
                      "http": [grant], "http_timeout_ms": 2000, "max_requests": 2, "max_bytes": 16384}
            if args.https:
                policy["tls_ca_file"] = str(fixture / "ca.pem")
            private_file(fixture / "policy.json", canonical(policy).encode())
            limits = {"fuel": 100000, "max_input_bytes": 16384,
                      "max_output_bytes": 16384, "timeout_ms": 5000}
            source = str(root / "examples/http_client/report.apx")
            authorized_tool = {
                "name": "api_report", "description": "Fetch checked API records and write a scoped summary",
                "source": source, "limits": limits, "capabilities": "policy.json",
                "credentials": [{"origin": origin, "path_prefix": "/v1/", "methods": ["GET"],
                                 "header": "authorization", "secret_file": str(fixture / "api-token"),
                                 "prefix": "Bearer "}],
            }
            denied_tool = {"name": "api_report_denied", "description": "Same program with no host authority",
                           "source": source, "limits": limits}
            manifest = {"schema": "apodexis.tool_server.v1", "max_concurrent_calls": 2,
                        "tools": [authorized_tool, denied_tool]}
            private_file(fixture / "manifest.json", canonical(manifest).encode())
            command, command_args = str(binary), ["serve", "--manifest", str(fixture / "manifest.json")]
            if trace is not None:
                strace = shutil.which("strace")
                if not strace:
                    raise RuntimeError("optional compile-once trace requires strace")
                command, command_args = strace, ["-f", "-s", "256", "-e", "trace=execve,execveat,access,openat,fstat,newfstatat,poll,signalfd4",
                                                 "-o", str(trace), "--", command, *command_args]
            parameters = StdioServerParameters(command=command, args=command_args,
                                              env={"PATH": "/usr/bin:/bin", "LC_ALL": "C"})
            results = []
            with (fixture / "server.stderr").open("w+") as errors:
                try:
                    async with asyncio.timeout(35):
                        async with stdio_client(parameters, errlog=errors) as (read, write):
                            async with ClientSession(read, write, read_timeout_seconds=10) as session:
                                initialized = await session.initialize()
                                discovered = await session.list_tools()
                                assert [t.name for t in discovered.tools] == ["api_report", "api_report_denied"]
                                descriptors = {tool.name: tool for tool in discovered.tools}
                                output_schemas = {}
                                for descriptor in descriptors.values():
                                    output_schema = descriptor.output_schema
                                    assert isinstance(output_schema, dict)
                                    Draft202012Validator.check_schema(descriptor.input_schema)
                                    Draft202012Validator.check_schema(output_schema)
                                    output_schemas[descriptor.name] = output_schema
                                startup = toolchain_counts(trace)

                                async def call(name: str, url: str, destination: str):
                                    arguments = {"input": {"url": url, "destination": destination}}
                                    Draft202012Validator(descriptors[name].input_schema).validate(arguments)
                                    result = await session.call_tool(name, arguments)
                                    assert isinstance(result, CallToolResult)
                                    value = result.structured_content
                                    assert isinstance(value, dict)
                                    Draft202012Validator(output_schemas[name]).validate(value)
                                    assert isinstance(result.content[0], TextContent)
                                    assert canonical(json.loads(result.content[0].text)) == canonical(value)
                                    assert result.is_error == (value["status"] != "ok")
                                    assert token not in canonical(value)
                                    results.append({"tool": name, "status": value["status"], "output": value.get("output")})
                                    return value

                                first = await call("api_report", origin + "/v1/items", "report.json")
                                assert canonical(first["output"]) == canonical({"tag": "ok", "value": expected})
                                report = (reports / "report.json").read_bytes()
                                assert report == (canonical(expected) + "\n").encode()
                                # Startup owns the credential, not subsequent filesystem contents.
                                (fixture / "api-token").write_bytes(secrets.token_urlsafe(32).encode())
                                again = await call("api_report", origin + "/v1/items", "again.json")
                                assert canonical(again["output"]) == canonical(first["output"])
                                assert (reports / "again.json").read_bytes() == report
                                assert again["source_hash"] == first["source_hash"]
                                assert again["fuel_used"] == first["fuel_used"]
                                count = len(observed)
                                denied = await call("api_report_denied", origin + "/v1/items", "denied.json")
                                assert denied["output"] == {"tag": "err", "error": "capability_denied"}
                                refused = await call("api_report", origin + "/not-authorized", "refused.json")
                                assert refused["output"] == {"tag": "err", "error": "http_request_denied"}
                                assert len(observed) == count
                                malformed = await call("api_report", origin + "/v1/bad", "bad.json")
                                assert malformed["output"]["tag"] == "err"
                                assert malformed["output"]["error"].startswith("api_decode: ")
                                status = await call("api_report", origin + "/v1/unavailable", "status.json")
                                assert status["output"] == {"tag": "err", "error": "api_status_not_200"}
                                escaped = await call("api_report", origin + "/v1/items", "../escape.json")
                                assert escaped["output"] == {"tag": "err", "error": "filesystem_write_denied"}
                                assert not (fixture / "escape.json").exists()
                                for name in ["denied.json", "refused.json", "bad.json", "status.json"]:
                                    assert not (reports / name).exists()
                                try:
                                    await session.call_tool("api_report", {"input": {"url": origin + "/v1/items",
                                                            "destination": "override.json"}, "capabilities": {}})
                                except MCPError as error:
                                    assert error.error.code == -32602
                                else:
                                    raise AssertionError("caller authority override was not rejected")
                                await session.send_ping()
                                after = toolchain_counts(trace)
                                if trace is not None:
                                    assert startup == after == {"qbe": 2, "as": 2, "ld": 2}
                finally:
                    errors.flush()
                    errors.seek(0)
                    stderr = errors.read()
                    if token in stderr:
                        raise RuntimeError("credential sentinel appeared in native diagnostics")
                    if stderr.strip():
                        raise RuntimeError("native server: " + stderr[:512])
            assert observed and all(row["authenticated"] for row in observed)
            assert len(observed) == 5
            summary = {"status": "PASS", "fixture": "synthetic owned loopback API",
                       "transport": "verified HTTPS" if args.https else "loopback HTTP",
                       "protocol": initialized.protocol_version, "tools": list(descriptors),
                       "calls": len(results), "authenticated_http_requests": len(observed),
                       "report": expected, "schema_validation": "draft2020-12 inputs and every structured result",
                       "compile_once": {"startup": startup, "after_calls": after} if trace else "not traced",
                       "caller_override": "refused", "authority_isolation": "PASS",
                       "snapshot_and_fresh_budgets": "PASS", "guarded_report_readback": "byte-exact"}
            if args.save_report:
                with args.save_report.open("xb") as stream:
                    stream.write(report)
                summary["saved_report"] = str(args.save_report.resolve())
            assert token not in canonical(summary)
            return summary


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path)
    parser.add_argument("--https", action="store_true")
    parser.add_argument("--trace", type=Path)
    parser.add_argument("--save-report", type=Path)
    args = parser.parse_args()
    print(json.dumps(asyncio.run(demonstrate(args)), indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
