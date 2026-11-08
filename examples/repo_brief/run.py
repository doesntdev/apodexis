#!/usr/bin/env python3
"""Optional stdlib MCP client. All HTTP and report computation run in Apodexis."""
import argparse
import json
import pathlib
import queue
import subprocess
import sys
import threading


class Client:
    """Line-delimited MCP, correlated responses, bounded reads and shutdown."""

    def __init__(self, binary, manifest, timeout=40, env=None, cwd=None):
        self.process = subprocess.Popen(
            [str(binary), "serve", "--manifest", str(manifest)],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            env=env, cwd=cwd,
        )
        assert self.process.stdin is not None and self.process.stdout is not None and self.process.stderr is not None
        self.input = self.process.stdin
        self.output = self.process.stdout
        self.errors = self.process.stderr
        self.timeout = timeout
        self.sequence = 0
        self.calls = 0
        self.stderr = bytearray()
        self.messages = queue.Queue()
        self.transcript = []
        self.readers = [threading.Thread(target=self._read, daemon=True),
                        threading.Thread(target=self._errors, daemon=True)]
        for thread in self.readers:
            thread.start()

    def _read(self):
        try:
            while True:
                line = self.output.readline(4 * 1024 * 1024 + 1)
                if not line:
                    raise RuntimeError("MCP server closed stdout")
                if len(line) > 4 * 1024 * 1024 or not line.endswith(b"\n"):
                    raise RuntimeError("MCP reply exceeds client frame limit")
                self.messages.put(json.loads(line))
        except Exception as error:
            self.messages.put(error)

    def _errors(self):
        while True:
            block = self.errors.read(4096)
            if not block:
                return
            if len(self.stderr) < 65536:
                self.stderr.extend(block[:65536 - len(self.stderr)])

    def request(self, method, params=None):
        self.sequence += 1
        message = {"jsonrpc": "2.0", "id": self.sequence, "method": method}
        if params is not None:
            message["params"] = params
        self.transcript.append({"send": message})
        self.input.write(json.dumps(message).encode() + b"\n")
        self.input.flush()
        response = self.messages.get(timeout=self.timeout)
        if isinstance(response, Exception):
            raise response
        self.transcript.append({"receive": response})
        if response.get("jsonrpc") != "2.0" or response.get("id") != self.sequence:
            raise RuntimeError("MCP response correlation failed")
        if "error" in response:
            raise RuntimeError("MCP protocol error: " + json.dumps(response["error"]))
        if "result" not in response:
            raise RuntimeError("MCP reply has no result")
        return response["result"]

    def initialize(self):
        result = self.request("initialize", {
            "protocolVersion": "2025-11-25", "capabilities": {},
            "clientInfo": {"name": "repo-brief-stdlib", "version": "1"},
        })
        if result.get("protocolVersion") != "2025-11-25":
            raise RuntimeError("unsupported MCP protocol version")
        message = {"jsonrpc": "2.0", "method": "notifications/initialized"}
        self.transcript.append({"send": message})
        self.input.write(json.dumps(message).encode() + b"\n")
        self.input.flush()
        tools = self.request("tools/list").get("tools", [])
        if [tool.get("name") for tool in tools] != ["repo_brief"]:
            raise RuntimeError("manifest must expose exactly repo_brief")
        return tools[0]

    def call(self, value):
        self.calls += 1
        return self.request("tools/call", {"name": "repo_brief", "arguments": {"input": value}})

    def close(self):
        if not self.input.closed:
            self.input.close()
        try:
            self.process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.process.terminate()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=3)
        for thread in self.readers:
            thread.join(timeout=1)
        self.output.close()
        self.errors.close()

    def __enter__(self):
        return self

    def __exit__(self, *_):
        self.close()


def report_from(result):
    if result.get("isError"):
        raise RuntimeError("MCP execution failed: " + json.dumps(result.get("structuredContent")))
    record = result.get("structuredContent", {})
    if record.get("status") != "ok":
        raise RuntimeError("native execution failed: " + json.dumps(record))
    output = record.get("output", {})
    if output.get("tag") != "ok":
        raise RuntimeError("repo_brief refused: " + json.dumps(output))
    report = output.get("value")
    if not isinstance(report, dict) or report.get("status") not in ("complete", "partial", "failed"):
        raise RuntimeError("invalid repo_brief report")
    content = result.get("content", [])
    if len(content) != 1 or content[0].get("type") != "text" or json.loads(content[0]["text"]) != record:
        raise RuntimeError("MCP structured/text result mismatch")
    return report


def main():
    root = pathlib.Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=pathlib.Path, default=root / "bin/apodexis")
    parser.add_argument("--manifest", type=pathlib.Path, default=pathlib.Path(__file__).with_name("tool-server.json"))
    parser.add_argument("--repository", default="doesntdev/apodexis")
    parser.add_argument("--page-limit", type=int, choices=(1, 2, 3), default=1)
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()
    try:
        with Client(args.binary.resolve(), args.manifest.resolve()) as client:
            client.initialize()
            report = report_from(client.call({"repository": args.repository, "page_limit": args.page_limit}))
        if client.process.returncode != 0:
            raise RuntimeError("native MCP server exited unsuccessfully")
        text = json.dumps(report, ensure_ascii=False, separators=(",", ":")) + "\n"
        if args.output:
            args.output.write_text(text, encoding="utf-8")
            if args.output.read_text(encoding="utf-8") != text:
                raise RuntimeError("saved report readback mismatch")
        sys.stdout.write(text)
        print("MCP calls: " + str(client.calls) + "; HTTP requests: " + str(report["request_count"]), file=sys.stderr)
        # Partial/failed data is retained but never presented as full success.
        return 0 if report["status"] == "complete" else 2
    except (OSError, ValueError, RuntimeError, queue.Empty) as error:
        print("repo_brief: " + (str(error) or "MCP timeout"), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
