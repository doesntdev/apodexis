#!/usr/bin/env python3
"""Optional real native serve/HTTP integration. No external API or Python product path.

APX_REPO_BRIEF_BINARY selects an already built binary (also a relocated bundle).
APX_REPO_BRIEF_RECEIPTS retains exact fixture/MCP/oracle observations per test.
--tls-smoke selects one populated workflow over locally verified HTTPS.
"""
import copy
import importlib.util
import json
import os
import re
from pathlib import Path
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, parse_qsl, urlsplit, unquote

ROOT = Path(__file__).resolve().parents[1]
BINARY = Path(os.environ.get("APX_REPO_BRIEF_BINARY", ROOT / "bin/apodexis")).resolve()
PRODUCT = BINARY.parent.parent
TLS = "--tls-smoke" in sys.argv
if TLS:
    sys.argv.remove("--tls-smoke")
spec = importlib.util.spec_from_file_location("repo_brief_client", PRODUCT / "examples/repo_brief/run.py")
assert spec is not None and spec.loader is not None
client_module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(client_module)
Client, report_from = client_module.Client, client_module.report_from
REPO = "doesntdev/apodexis"
HUMAN = "https://github.com/" + REPO
KINDS = ("issues", "pulls", "releases")
SECTIONS = ("issues", "pull_requests", "releases")


def path_for(kind, page):
    if kind == "issues":
        return "/search/issues?q=repo%3Adoesntdev%2Fapodexis%20is%3Aissue%20is%3Aopen&sort=updated&order=desc&per_page=10&page=" + str(page)
    if kind == "pulls":
        return "/repos/" + REPO + "/pulls?state=open&sort=updated&direction=desc&per_page=10&page=" + str(page)
    return "/repos/" + REPO + "/releases?per_page=5&page=" + str(page)


def item(kind, number):
    # Real GitHub-shaped unselected extras, including nested arrays/objects.
    if kind == "releases":
        return {"tag_name": "v" + str(number), "html_url": HUMAN + "/releases/tag/v" + str(number),
                "prerelease": number == 1, "id": number, "assets": [{"id": 77}], "body": None}
    value = {"number": number, "title": "Fixture é " + str(number),
             "html_url": HUMAN + ("/issues/" if kind == "issues" else "/pull/") + str(number),
             "updated_at": "2026-11-01T12:00:00Z", "user": {"login": "fixture", "id": 99},
             "labels": [], "body": "not selected", "state": "open"}
    if kind == "pulls":
        value["draft"] = number == 1
    return value


def response(kind, rows=None, total=None, incomplete=False, status=200, headers=None, raw=None):
    rows = [] if rows is None else rows
    body = {"total_count": len(rows) if total is None else total, "incomplete_results": incomplete,
            "items": rows, "search_type": "lexical"} if kind == "issues" else rows
    rates = {"x-ratelimit-resource": "search" if kind == "issues" else "core",
             "x-ratelimit-limit": "10", "x-ratelimit-remaining": "9", "x-ratelimit-reset": "123456"}
    rates.update(headers or {})
    return {"status": status, "headers": rates,
            "body": json.dumps(body, ensure_ascii=False, separators=(",", ":")).encode() if raw is None else raw}


def pages(populated=True):
    return {(kind, 1): response(kind, [item(kind, 1)] if populated else []) for kind in KINDS}


def strict_json(wire):
    def unique(pairs):
        value = {}
        for key, element in pairs:
            if key in value:
                raise ValueError("duplicate_field")
            value[key] = element
        return value
    return json.loads(wire.decode("utf-8"), object_pairs_hook=unique,
                      parse_constant=lambda _: (_ for _ in ()).throw(ValueError("invalid_json")))


def metadata_page(url, origin, kind):
    """Independent urllib parser: semantic query equality, never a request target."""
    if (not url.startswith(origin + "/") or url.count("?") != 1 or "#" in url
            or any(ord(c) < 33 or ord(c) == 127 for c in url)
            or re.search(r"%(?![0-9a-fA-F]{2})", url)):
        raise ValueError("malformed metadata URL")
    parsed, approved = urlsplit(url), urlsplit(origin)
    if (parsed.scheme, parsed.netloc) != (approved.scheme, approved.netloc):
        raise ValueError("foreign origin")
    fixed = urlsplit(path_for(kind, 1))
    if parsed.path != fixed.path:
        alias = re.fullmatch(r"/repositories/([1-9][0-9]{0,18})/(pulls|releases)", parsed.path)
        if kind == "issues" or alias is None or alias[2] != kind or int(alias[1]) > 2**63 - 1:
            raise ValueError("foreign route")
    pairs = parse_qsl(parsed.query, keep_blank_values=True, strict_parsing=True, errors="strict")
    fields = dict(pairs)
    if len(fields) != len(pairs):
        raise ValueError("duplicate query field")
    page = fields.pop("page", "")
    if re.fullmatch(r"[1-9][0-9]{0,18}", page) is None or int(page) > 2**63 - 1:
        raise ValueError("noncanonical page")
    expected = dict(parse_qsl(fixed.query))
    del expected["page"]
    if fields != expected:
        raise ValueError("changed query semantics")
    return int(page)


def reference(plan, origin, budget, denied=False):
    """Independent specification oracle; consumes raw fixture bytes, not APX helpers.

    Produces semantic report facts including publication boundaries, dedup/order,
    completeness, source links, buckets and counts. Error-kind tokens are checked
    separately by explicit test assertions where parser implementations differ.
    """
    out: dict = {"repository": REPO, "repository_url": HUMAN}
    flags = []
    for kind, name in zip(KINDS, SECTIONS):
        section = {"items": [], "observed_count": 0, "pages_fetched": 0, "requests": 0,
                   "complete": False, "status": "transport", "api_url": origin + path_for(kind, 1),
                   "source_url": HUMAN + "/" + ("issues" if kind == "issues" else kind),
                   "rate": dict(bucket="search" if kind == "issues" else "core", provider_resource="",
                                limit="", remaining="", reset="", retry_after=""), "source_changed": False}
        if kind == "issues":
            section.update(total_known=False, total_count=0, incomplete_results=False)
        seen = set()
        for page in range(1, budget + 1):
            section["requests"] += 1
            received = None if denied else plan.get((kind, page))
            if received is None:
                break
            headers = received["headers"]
            for key, field in (("x-ratelimit-resource", "provider_resource"), ("x-ratelimit-limit", "limit"),
                               ("x-ratelimit-remaining", "remaining"), ("x-ratelimit-reset", "reset"),
                               ("retry-after", "retry_after")):
                text = headers.get(key, "")
                section["rate"][field] = text if len(text) <= 128 and "\0" not in text else "unavailable"
            status = received["status"]
            if status != 200:
                remaining, retry = headers.get("x-ratelimit-remaining", ""), headers.get("retry-after", "")
                exhausted = re.fullmatch(r"0{1,128}", remaining) is not None
                retry_evidence = re.fullmatch(r"[0-9]{1,128}", retry) is not None
                section["status"] = ("rate_limited" if status == 429 or status == 403 and (exhausted or retry_evidence)
                                     else "forbidden" if status == 403 else "inaccessible" if status == 404
                                     else "protocol_error" if 300 <= status < 400 else "http_error")
                break
            total, incomplete = 0, False
            try:
                document = strict_json(received["body"])
                rows = document["items"] if kind == "issues" else document
                if type(rows) is not list or len(rows) > (5 if kind == "releases" else 10):
                    raise ValueError("bad page")
                if kind == "issues":
                    total, incomplete = document["total_count"], document["incomplete_results"]
                    if type(total) is not int or not 0 <= total <= 2**63 - 1 or total < len(rows) or type(incomplete) is not bool:
                        raise ValueError("bad search")
                selected = []
                for row in rows:
                    fields = ("tag_name", "html_url", "prerelease") if kind == "releases" else (
                        ("number", "title", "html_url", "updated_at", "draft") if kind == "pulls" else
                        ("number", "title", "html_url", "updated_at"))
                    picked = {key: row[key] for key in fields}
                    for key, value in picked.items():
                        expected = bool if key in ("draft", "prerelease") else int if key == "number" else str
                        if type(value) is not expected:
                            raise ValueError("bad type")
                    if kind == "releases":
                        if not 0 < len(picked["tag_name"].encode()) <= 256 or "\0" in picked["tag_name"]:
                            raise ValueError("bad tag")
                        url = urlsplit(picked["html_url"])
                        if (url.scheme, url.netloc) != ("https", "github.com") or not url.path.startswith("/" + REPO + "/releases/tag/") or not url.path.removeprefix("/" + REPO + "/releases/tag/") or url.query or url.fragment or any(ord(c) <= 32 or ord(c) == 127 for c in picked["html_url"]) or len(picked["html_url"].encode()) > 1024 or any(c in picked["html_url"] for c in ("\\", "\r", "\n", "\t")) or any(token in picked["html_url"].lower().removeprefix(HUMAN.lower() + "/releases/tag/") for token in ("..", "%2e", "%25", "%5c")) or any(ord(c) < 32 or ord(c) == 127 for c in unquote(picked["html_url"])):
                            raise ValueError("bad release URL")
                    else:
                        if not 0 < picked["number"] <= 2**63 - 1 or not 0 < len(picked["title"].encode()) <= 1024 or "\0" in picked["title"] or not 0 < len(picked["updated_at"].encode()) <= 64 or "\0" in picked["updated_at"]:
                            raise ValueError("bad row")
                        if picked["html_url"] != HUMAN + ("/issues/" if kind == "issues" else "/pull/") + str(picked["number"]):
                            raise ValueError("wrong repository URL")
                    selected.append(picked)
            except (ValueError, KeyError, TypeError, UnicodeError):
                section["status"] = "protocol_error" if not is_utf8(received["body"]) else "invalid_data"
                break
            if kind == "issues":
                if section["total_known"] and section["total_count"] != total:
                    section["source_changed"] = True
                section.update(total_known=True, total_count=total,
                               incomplete_results=section["incomplete_results"] or incomplete)
            for row in selected:
                identity = row["tag_name"] if kind == "releases" else row["number"]
                if identity in seen:
                    section["source_changed"] = True
                else:
                    seen.add(identity)
                    section["items"].append(row)
            section["pages_fetched"] += 1
            section["observed_count"] = len(section["items"])
            if kind == "issues" and section["total_count"] < section["observed_count"]:
                section["source_changed"] = True
            link = headers.get("link", "")
            relations = {}
            valid_link = True
            for entry in link.split(",") if link else []:
                match = re.fullmatch(r'\s*<([^>]+)>\s*;\s*rel="(next|prev|first|last)"\s*', entry)
                if match is None or match[2] in relations:
                    valid_link = False
                    break
                url, rel = match.groups()
                try:
                    target = metadata_page(url, origin, kind)
                except (ValueError, UnicodeError):
                    valid_link = False
                    break
                relations[rel] = target
            if any(rel in relations and relations[rel] != target for rel, target in
                   (("next", page + 1), ("prev", page - 1), ("first", 1))):
                valid_link = False
            if "last" in relations and (relations["last"] < page or
                    ("next" in relations and relations["last"] < page + 1) or
                    ("next" not in relations and relations["last"] > page)):
                valid_link = False
            if not valid_link:
                section["status"] = "pagination_invalid"
                break
            if "next" in relations:
                if page == budget:
                    section["status"] = "truncated"
                    break
                continue
            if len(rows) == (5 if kind == "releases" else 10) and relations.get("last") != page:
                section["status"] = "pagination_ambiguous"
            elif section["source_changed"]:
                section["status"] = "source_changed"
            elif kind == "issues" and section["incomplete_results"]:
                section["status"] = "incomplete_results"
            elif kind == "issues" and total != section["observed_count"]:
                section["status"] = "pagination_ambiguous"
            else:
                section.update(complete=True, status="ok")
            break
        out[name] = section
        if not section["complete"]:
            flags.append(name + "_incomplete")
    if any(out[name]["source_changed"] for name in SECTIONS):
        flags.append("source_changed")
    if out["issues"]["incomplete_results"]:
        flags.append("provider_incomplete_results")
    if any(row["draft"] for row in out["pull_requests"]["items"]):
        flags.append("draft_pull_requests")
    if any(row["prerelease"] for row in out["releases"]["items"]):
        flags.append("prereleases")
    out["flags"] = flags
    out["status"] = "complete" if all(out[name]["complete"] for name in SECTIONS) else (
        "failed" if all(out[name]["pages_fetched"] == 0 for name in SECTIONS) else "partial")
    out["request_count"] = sum(out[name]["requests"] for name in SECTIONS)
    return out


def is_utf8(wire):
    try:
        wire.decode("utf-8")
        return True
    except UnicodeError:
        return False


class Fixture:
    def __init__(self, directory, tls=False):
        self.plan = {}
        self.observed = []
        self.errors = []
        fixture = self

        class Handler(BaseHTTPRequestHandler):
            def do_GET(self):
                try:
                    parsed = urlsplit(self.path)
                    kind = "issues" if parsed.path == "/search/issues" else parsed.path.rsplit("/", 1)[-1]
                    page = int(parse_qs(parsed.query)["page"][0])
                    if kind not in KINDS or self.path != path_for(kind, page):
                        raise AssertionError("unexpected request: " + self.path)
                    fixture.observed.append({"method": self.command, "path": self.path, "headers": dict(self.headers)})
                    result = fixture.plan.get((kind, page))
                    if result is None:
                        self.connection.shutdown(socket.SHUT_RDWR)
                        self.connection.close()
                        return
                    self.send_response(result["status"])
                    for key, value in result["headers"].items():
                        # http.server serializes latin-1; preserve fixture UTF-8 wire
                        # bytes explicitly rather than silently changing/rejecting them.
                        self.send_header(key, value.encode("utf-8").decode("latin-1"))
                    self.send_header("Content-Length", str(len(result["body"])))
                    self.send_header("Connection", "close")
                    self.end_headers()
                    self.wfile.write(result["body"])
                except Exception as error:
                    fixture.errors.append(repr({"error": repr(error), "requestline": self.requestline,
                                                "peer": self.client_address}))

            def do_POST(self):
                fixture.errors.append("unexpected POST")
                self.send_error(405)

            def log_message(self, *_):
                pass

        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.ca = None
        if tls:
            key, cert = directory / "fixture-key.pem", directory / "fixture-ca.pem"
            command = ["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
                       "-subj", "/CN=repo-brief-loopback", "-addext", "subjectAltName=IP:127.0.0.1",
                       "-keyout", str(key), "-out", str(cert)]
            generated = subprocess.run(command, capture_output=True, check=True)
            cert.chmod(0o600)
            key.chmod(0o600)
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.load_cert_chain(cert, key)
            context.set_alpn_protocols(["http/1.1"])
            self.server.socket = context.wrap_socket(self.server.socket, server_side=True)
            self.ca = cert
            self.tls_setup = {"argv": command, "stdout": generated.stdout.decode(), "stderr": generated.stderr.decode()}
        self.origin = ("https" if tls else "http") + "://127.0.0.1:" + str(self.server.server_port)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def close(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=3)
        if self.thread.is_alive():
            raise RuntimeError("fixture listener did not stop")


class Workflow(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.storage = tempfile.TemporaryDirectory(prefix="apx-repo-brief-", dir=os.environ["TMPDIR"])
        cls.addClassCleanup(cls.storage.cleanup)
        cls.directory = Path(cls.storage.name)
        cls.fixture = Fixture(cls.directory, TLS)
        cls.addClassCleanup(cls.fixture.close)
        cls.outside = Fixture(cls.directory)
        cls.addClassCleanup(cls.outside.close)
        cls.source = cls.directory / "source"
        cls.source.mkdir()
        for name in ("repo_brief.apx", "http.apx"):
            (cls.source / name).write_bytes((PRODUCT / "examples/http_client" / name).read_bytes())
        (cls.source / "repo_brief_config.apx").write_text(
            'M repo_brief_config\nV origin\nV repository\nF origin->str\n  R ' + json.dumps(cls.fixture.origin)
            + '\nF repository->str\n  R "doesntdev/apodexis"\n')
        policy = json.loads((PRODUCT / "examples/repo_brief/github-policy.json").read_text())
        for grant in policy["http"]:
            grant["origin"] = cls.fixture.origin
        if cls.fixture.ca:
            policy["tls_ca_file"] = str(cls.fixture.ca)
        (cls.directory / "policy.json").write_text(json.dumps(policy))
        cls.manifest = json.loads((PRODUCT / "examples/repo_brief/tool-server.json").read_text())
        cls.manifest["tools"][0].update(source=str(cls.source / "repo_brief.apx"), capabilities="policy.json")
        (cls.directory / "manifest.json").write_text(json.dumps(cls.manifest))
        denied = copy.deepcopy(cls.manifest)
        del denied["tools"][0]["capabilities"]
        (cls.directory / "denied.json").write_text(json.dumps(denied))

    @classmethod
    def tearDownClass(cls):
        if cls.fixture.errors or cls.outside.errors or cls.outside.observed:
            raise RuntimeError("unexpected listener traffic: " + repr(cls.fixture.errors + cls.outside.errors + cls.outside.observed))

    def setUp(self):
        self.fixture.plan = pages()
        self.fixture.observed.clear()
        self.fixture.errors.clear()
        self.outside.observed.clear()
        self.outside.errors.clear()
        self.artifact: dict = {"test": self.id(), "binary": str(BINARY), "origin": self.fixture.origin}
        self.addCleanup(self.receipt)

    def receipt(self):
        self.artifact["http"] = self.fixture.observed
        self.artifact["fixture_errors"] = self.fixture.errors
        self.artifact["outside_http"] = self.outside.observed
        self.artifact["outside_errors"] = self.outside.errors
        self.artifact["responses"] = [{"kind": kind, "page": page, "status": value["status"],
                                      "headers": value["headers"], "body_hex": value["body"].hex()}
                                     for (kind, page), value in self.fixture.plan.items() if value is not None]
        if self.fixture.ca:
            self.artifact["tls_setup"] = self.fixture.tls_setup
        target = os.environ.get("APX_REPO_BRIEF_RECEIPTS")
        if target:
            directory = Path(target)
            directory.mkdir(parents=True, exist_ok=True)
            (directory / (self._testMethodName + ".json")).write_text(json.dumps(self.artifact, indent=2) + "\n")

    def invoke(self, budget=1, denied=False, value=None):
        client = Client(BINARY, self.directory / ("denied.json" if denied else "manifest.json"))
        try:
            tool = client.initialize()
            self.artifact["discovery"] = tool
            result = client.call({"repository": REPO, "page_limit": budget} if value is None else value)
            self.artifact["result"] = result
            self.assertEqual(client.calls, 1)
            self.assertFalse(self.fixture.errors)
            self.assertFalse(self.outside.errors)
            self.assertFalse(self.outside.observed)
            return result
        finally:
            client.close()
            self.artifact.update(mcp=client.transcript, mcp_calls=client.calls,
                                 server_exit=client.process.returncode,
                                 server_stderr=client.stderr.decode(errors="replace"))

    def workflow(self, budget=1, denied=False):
        actual = report_from(self.invoke(budget, denied))
        expected = reference(self.fixture.plan, self.fixture.origin, budget, denied)
        facts = copy.deepcopy(actual)
        for name in SECTIONS:
            self.assertIsInstance(facts[name].pop("reason"), str)
        self.artifact.update(actual=actual, oracle=expected)
        self.assertEqual(facts, expected)
        requests = [path_for(kind, page) for kind, name in zip(KINDS, SECTIONS)
                    for page in range(1, expected[name]["requests"] + 1)]
        self.assertEqual([x["path"] for x in self.fixture.observed], [] if denied else requests)
        for request in self.fixture.observed:
            self.assertEqual(request["method"], "GET")
            lower = {k.lower(): v for k, v in request["headers"].items()}
            self.assertEqual(lower["user-agent"], "Apodexis-repo-brief")
            self.assertEqual(lower["accept"], "application/vnd.github+json")
            self.assertEqual(lower["x-github-api-version"], "2022-11-28")
            self.assertNotIn("authorization", lower)
        return actual

    def test_01_populated_extras_flags(self):
        report = self.workflow()
        self.assertEqual(report["flags"], ["draft_pull_requests", "prereleases"])
        self.assertEqual(report["request_count"], 3)
        # Native input is nested under the protocol's exact closed {input: ...}.
        schema = self.artifact["discovery"]["inputSchema"]
        self.assertFalse(schema["additionalProperties"])

    def test_02_empty_success(self):
        self.fixture.plan = pages(False)
        self.assertEqual(self.workflow()["status"], "complete")

    def more(self, count=2):
        for kind in KINDS:
            width = 5 if kind == "releases" else 10
            for page in range(1, count + 1):
                rows = [item(kind, n) for n in range(1 + (page - 1) * width, 1 + page * width)] if page < count else [item(kind, 100)]
                link = {"link": '<' + self.fixture.origin + path_for(kind, page + 1) + '>; rel="next"'} if page < count else {}
                self.fixture.plan[kind, page] = response(kind, rows, total=(count - 1) * width + 1, headers=link)

    def test_03_multiple_pages(self):
        self.more()
        self.assertEqual(self.workflow(2)["request_count"], 6)

    def test_04_nine_request_bound(self):
        self.more(3)
        self.assertEqual(self.workflow(3)["request_count"], 9)

    def test_05_truncated_budget(self):
        self.more()
        self.assertEqual(self.workflow()["issues"]["status"], "truncated")

    def test_06_incomplete_results(self):
        self.fixture.plan["issues", 1] = response("issues", [item("issues", 1)], incomplete=True)
        self.assertEqual(self.workflow()["issues"]["reason"], "provider_incomplete_results")

    def failure(self, code):
        self.fixture.plan["issues", 1] = response("issues", status=code, raw=b'{"message":"UNTRUSTED_ERROR_BODY"}', headers={"retry-after": "17", "x-ratelimit-remaining": "0"})
        report = self.workflow()
        self.assertEqual(report["issues"]["reason"], "http_" + str(code))
        self.assertEqual(report["issues"]["rate"]["retry_after"], "17")
        self.assertNotIn("UNTRUSTED_ERROR_BODY", json.dumps(report))

    def test_07_forbidden_403(self):
        # Historical case identity retained: this fixture has real throttle evidence.
        self.failure(403)
        self.assertEqual(self.artifact["actual"]["issues"]["status"], "rate_limited")

    def test_08_throttled_429(self):
        self.failure(429)

    def test_09_inaccessible_404(self):
        self.failure(404)

    def test_10_server_500(self):
        self.failure(500)

    def test_11_transport_close(self):
        self.fixture.plan["issues", 1] = None
        self.assertEqual(self.workflow()["issues"]["status"], "transport")

    def invalid(self, wire, reason):
        self.fixture.plan["issues", 1] = response("issues", raw=wire)
        report = self.workflow()
        self.assertEqual(report["issues"]["reason"], reason)
        self.assertEqual(report["issues"]["items"], [])

    def test_12_malformed_json(self):
        self.invalid(b'{"items":[1,]}', "invalid_json")

    def test_13_duplicate_ignored_json(self):
        self.invalid(rb'{"total_count":0,"incomplete_results":false,"items":[],"x":1,"\u0078":2}', "duplicate_field")

    def test_14_wrong_selected_type(self):
        self.invalid(b'{"total_count":"0","incomplete_results":false,"items":[]}', "numeric_out_of_range")

    def test_15_missing_selected_field(self):
        self.invalid(b'{"total_count":0,"items":[]}', "missing_field")

    def test_16_duplicate_rows(self):
        self.fixture.plan["issues", 1] = response("issues", [item("issues", 1), item("issues", 1)], total=2)
        self.assertEqual(self.workflow()["issues"]["status"], "source_changed")

    def test_17_source_url_mismatch(self):
        row = item("issues", 1)
        row["html_url"] = HUMAN + "/pull/1"
        self.fixture.plan["issues", 1] = response("issues", [row])
        self.assertEqual(self.workflow()["issues"]["reason"], "invalid_item")

    def test_18_malicious_link_no_send(self):
        self.fixture.plan["issues", 1]["headers"]["link"] = '<' + self.outside.origin + '/never>; rel="next"'
        self.assertEqual(self.workflow(3)["issues"]["status"], "pagination_invalid")
        self.assertEqual(len(self.fixture.observed), 3)

    def test_19_malformed_link(self):
        self.fixture.plan["pulls", 1]["headers"]["link"] = "nonsense"
        self.assertEqual(self.workflow()["pull_requests"]["status"], "pagination_invalid")

    def refused(self, value, reason):
        result = self.invoke(value=value)
        record = result["structuredContent"]
        if reason:
            self.assertEqual(record["output"], {"tag": "err", "error": reason})
        else:
            self.assertTrue(result["isError"])
        self.assertEqual(self.fixture.observed, [])

    def test_20_wrong_repository(self):
        self.refused({"repository": "other/repo", "page_limit": 1}, "repository_not_approved")

    def test_21_zero_limit(self):
        self.refused({"repository": REPO, "page_limit": 0}, "invalid_page_limit")

    def test_22_negative_limit(self):
        self.refused({"repository": REPO, "page_limit": -1}, "invalid_page_limit")

    def test_23_over_three_limit(self):
        self.refused({"repository": REPO, "page_limit": 4}, "invalid_page_limit")

    def test_24_unknown_grant_field(self):
        with Client(BINARY, self.directory / "manifest.json") as client:
            client.initialize()
            with self.assertRaisesRegex(RuntimeError, "-32602"):
                client.call({"repository": REPO, "page_limit": 1, "capabilities": {"http": []}})
            self.artifact.update(mcp=client.transcript, mcp_calls=client.calls)
        self.assertEqual(self.fixture.observed, [])

    def test_25_default_denied(self):
        report = self.workflow(denied=True)
        self.assertEqual(report["status"], "failed")
        self.assertEqual(report["issues"]["reason"], "capability_denied")

    def test_26_recovery_same_server(self):
        with Client(BINARY, self.directory / "manifest.json") as client:
            client.initialize()
            refused = client.call({"repository": REPO, "page_limit": 0})
            self.assertEqual(refused["structuredContent"]["output"]["error"], "invalid_page_limit")
            self.assertEqual(self.fixture.observed, [])
            self.assertEqual(report_from(client.call({"repository": REPO, "page_limit": 1}))["status"], "complete")
            self.artifact.update(mcp=client.transcript, mcp_calls=client.calls)
            self.assertEqual(client.calls, 2)
        self.assertEqual(len(self.fixture.observed), 3)

    def test_27_cli_save_readback(self):
        target = self.directory / "report.json"
        argv = [sys.executable, str(PRODUCT / "examples/repo_brief/run.py"), "--binary", str(BINARY),
                "--manifest", str(self.directory / "manifest.json"), "--output", str(target)]
        process = subprocess.run(argv, capture_output=True, timeout=45)
        self.artifact.update(argv=argv, stdout=process.stdout.decode(), stderr=process.stderr.decode(), exit_code=process.returncode)
        self.assertEqual(process.returncode, 0, process.stderr)
        self.assertEqual(process.stderr, b"MCP calls: 1; HTTP requests: 3\n")
        self.artifact["mcp_calls"] = 1
        self.assertEqual(target.read_bytes(), process.stdout)
        self.assertEqual(json.loads(process.stdout)["request_count"], 3)
        self.assertEqual(len(self.fixture.observed), 3)
        target.unlink()

    def test_28_late_page_failure_retains_earlier(self):
        self.more()
        self.fixture.plan["issues", 2] = response("issues", status=500, raw=b'not reflected')
        self.assertEqual(self.workflow(2)["issues"]["observed_count"], 10)

    def test_29_no_page_prefix_publication(self):
        self.fixture.plan["pulls", 1] = response("pulls", [item("pulls", 1), dict(item("pulls", 2), draft="yes")])
        self.assertEqual(self.workflow()["pull_requests"]["items"], [])

    def test_30_full_page_without_link(self):
        self.fixture.plan["releases", 1] = response("releases", [item("releases", n) for n in range(1, 6)])
        self.assertEqual(self.workflow()["releases"]["status"], "pagination_ambiguous")

    def test_31_changed_total(self):
        self.more()
        self.fixture.plan["issues", 2] = response("issues", [item("issues", 100)], total=12)
        self.assertTrue(self.workflow(2)["issues"]["source_changed"])

    def test_32_redirect_not_followed(self):
        self.fixture.plan["issues", 1] = response("issues", status=302, headers={"location": self.outside.origin + "/never"})
        self.assertEqual(self.workflow()["issues"]["status"], "protocol_error")

    def test_33_non_utf8(self):
        self.invalid(b'\xff', "non_utf8_body")

    def test_34_oversized_page(self):
        self.fixture.plan["issues", 1] = response("issues", [item("issues", n) for n in range(1, 12)])
        self.assertEqual(self.workflow()["issues"]["reason"], "oversized_page")

    def test_35_release_source_url(self):
        self.fixture.plan["releases", 1] = response("releases", [dict(item("releases", 1), html_url="https://github.com/other/repo/releases/tag/v1")])
        self.assertEqual(self.workflow()["releases"]["reason"], "invalid_item")

    def test_36_duplicate_release_identity(self):
        self.fixture.plan["releases", 1] = response("releases", [item("releases", 1), item("releases", 1)])
        self.assertTrue(self.workflow()["releases"]["source_changed"])


    def test_37_cli_partial_is_not_success(self):
        self.fixture.plan["issues", 1] = response("issues", status=403, raw=b"error-body")
        argv = [sys.executable, str(PRODUCT / "examples/repo_brief/run.py"), "--binary", str(BINARY),
                "--manifest", str(self.directory / "manifest.json")]
        process = subprocess.run(argv, capture_output=True, timeout=45)
        self.artifact.update(argv=argv, stdout=process.stdout.decode(), stderr=process.stderr.decode(), exit_code=process.returncode)
        self.assertEqual(process.returncode, 2)
        self.assertEqual(process.stderr, b"MCP calls: 1; HTTP requests: 3\n")
        self.artifact["mcp_calls"] = 1
        self.assertEqual(json.loads(process.stdout)["status"], "partial")
        self.assertEqual(len(self.fixture.observed), 3)

    def test_38_cross_page_duplicates(self):
        self.more()
        self.fixture.plan["pulls", 2] = response("pulls", [item("pulls", 1)])
        report = self.workflow(2)
        self.assertEqual(report["pull_requests"]["observed_count"], 10)
        self.assertTrue(report["pull_requests"]["source_changed"])

    def test_39_invalid_last_link_no_send(self):
        self.fixture.plan["issues", 1]["headers"]["link"] = '<' + self.outside.origin + '/never>; rel="last"'
        self.assertEqual(self.workflow()["issues"]["status"], "pagination_invalid")

    def test_40_negative_item_number(self):
        self.fixture.plan["issues", 1] = response("issues", [dict(item("issues", 1), number=-1)])
        self.assertEqual(self.workflow()["issues"]["reason"], "invalid_item")

    def test_41_invalid_search_total(self):
        self.fixture.plan["issues", 1] = response("issues", [item("issues", 1)], total=0)
        self.assertEqual(self.workflow()["issues"]["reason"], "invalid_search_total")

    def test_42_bounded_retry_metadata(self):
        self.fixture.plan["issues", 1]["headers"]["retry-after"] = "9" * 129
        self.assertEqual(self.workflow()["issues"]["rate"]["retry_after"], "unavailable")


    def test_43_ordinary_github_link_relations(self):
        self.more()
        for kind in KINDS:
            self.fixture.plan[kind, 1]["headers"]["link"] += ', <' + self.fixture.origin + path_for(kind, 2) + '>; rel="last"'
            self.fixture.plan[kind, 2]["headers"]["link"] = '<' + self.fixture.origin + path_for(kind, 1) + '>; rel="prev", <' + self.fixture.origin + path_for(kind, 1) + '>; rel="first"'
        self.assertEqual(self.workflow(2)["status"], "complete")

    def test_44_full_page_explicit_terminal(self):
        rows = [item("releases", n) for n in range(1, 6)]
        self.fixture.plan["releases", 1] = response("releases", rows, headers={"link": '<' + self.fixture.origin + path_for("releases", 1) + '>; rel="last"'})
        self.assertTrue(self.workflow()["releases"]["complete"])

    def test_45_duplicate_relation(self):
        link = '<' + self.fixture.origin + path_for("issues", 2) + '>; rel="next"'
        self.fixture.plan["issues", 1]["headers"]["link"] = link + ", " + link
        self.assertEqual(self.workflow(3)["issues"]["status"], "pagination_invalid")

    def test_46_recovery_after_protocol_refusal(self):
        with Client(BINARY, self.directory / "manifest.json") as client:
            client.initialize()
            with self.assertRaisesRegex(RuntimeError, "-32602"):
                client.call({"repository": REPO, "page_limit": 1, "origin": self.outside.origin})
            self.assertEqual(self.fixture.observed, [])
            report = report_from(client.call({"repository": REPO, "page_limit": 1}))
            self.assertEqual(report["status"], "complete")
            self.artifact.update(mcp=client.transcript, mcp_calls=client.calls)
        self.assertEqual(len(self.fixture.observed), 3)


    def test_47_unicode_malicious_link_is_data_error(self):
        self.fixture.plan["issues", 1]["headers"]["link"] = '<' + self.outside.origin + '/é>; rel="next"'
        self.assertEqual(self.workflow()["issues"]["status"], "pagination_invalid")

    def test_48_unicode_release_url_no_trap(self):
        self.fixture.plan["releases", 1] = response("releases", [dict(item("releases", 1), tag_name="é", html_url=HUMAN + "/releases/tag/é")])
        self.assertEqual(self.workflow()["status"], "complete")

    def test_49_release_traversal_refused(self):
        self.fixture.plan["releases", 1] = response("releases", [dict(item("releases", 1), html_url=HUMAN + "/releases/tag/../../../other")])
        self.assertEqual(self.workflow()["releases"]["reason"], "invalid_item")

    def test_50_release_space_refused(self):
        self.fixture.plan["releases", 1] = response("releases", [dict(item("releases", 1), html_url=HUMAN + "/releases/tag/a b")])
        self.assertEqual(self.workflow()["releases"]["reason"], "invalid_item")


    def test_51_short_unicode_url_refused_without_trap(self):
        self.fixture.plan["releases", 1] = response("releases", [dict(item("releases", 1), html_url="😀" * 20)])
        self.assertEqual(self.workflow()["releases"]["reason"], "invalid_item")

    def test_52_short_unicode_last_link_without_trap(self):
        self.fixture.plan["issues", 1]["headers"]["link"] = '<' + "😀" * 30 + '>; rel="last"'
        self.assertEqual(self.workflow()["issues"]["status"], "pagination_invalid")


    def test_53_forbidden_core_and_search(self):
        for kind in KINDS:
            self.fixture.plan[kind, 1] = response(kind, status=403, raw=b'NO_RAW_ERROR_BODY')
        report = self.workflow()
        for section in SECTIONS:
            self.assertEqual(report[section]["status"], "forbidden")
            self.assertEqual(report[section]["reason"], "http_403")
        self.assertNotIn("NO_RAW_ERROR_BODY", json.dumps(report))

    def test_54_provider_alias_form_query_order_named_requests(self):
        # Synthetic rows; replay provider metadata spellings, not real repo data.
        self.more(3)
        for kind in KINDS:
            for page in range(1, 4):
                relations = [("last", 3)]
                if page < 3:
                    relations.append(("next", page + 1))
                if page > 1:
                    relations.extend([("prev", page - 1), ("first", 1)])
                links = []
                for relation, target in relations:
                    path, query = path_for(kind, target).split("?")
                    if kind != "issues":
                        # IDs are metadata, not verified repo identities or authority.
                        path = "/repositories/" + ("1" if page == 1 else "9223372036854775807") + "/" + kind
                    query = "&".join(reversed(query.split("&")))
                    query = query.replace("%20", "+").replace("%3A", "%3a").replace("%2F", "%2f")
                    query = query.replace("sort=updated", "sort=%75pdated").replace("page=", "page=%3")
                    links.append('<' + self.fixture.origin + path + '?' + query + '>; rel="' + relation + '"')
                self.fixture.plan[kind, page]["headers"]["link"] = ", ".join(links)
        report = self.workflow(3)
        self.assertEqual(report["status"], "complete")
        self.assertEqual(report["request_count"], 9)
        self.assertFalse(any("/repositories/" in request["path"] for request in self.fixture.observed))

    def test_55_release_ascii_boundary_unicode_version_positives(self):
        tags = ["v1.2.3-rc.1+build.7", "é-😀", "v", "v", "v "]
        # Only ASCII C0/DEL is refused, not a broader Unicode/locale class.
        self.fixture.plan["releases", 1] = response("releases", [dict(item("releases", n), tag_name=tag,
            html_url=HUMAN + "/releases/tag/" + tag) for n, tag in enumerate(tags, 1)],
            headers={"link": '<' + self.fixture.origin + path_for("releases", 1) + '>; rel="last"'})
        self.assertTrue(self.workflow()["releases"]["complete"])


# Individually named native-served cases: every wire variant has its own receipt.
def release_refusal(suffix):
    def case(self):
        valid = item("releases", 1)
        bad = dict(item("releases", 2), html_url=HUMAN + "/releases/tag/v2" + suffix)
        self.fixture.plan["releases", 1] = response("releases", [valid, bad])
        report = self.workflow()
        self.assertEqual(report["releases"]["reason"], "invalid_item")
        self.assertEqual(report["releases"]["items"], [])  # whole page, not prefix
    return case


for code in (*range(32), 127):
    variants = {"literal": chr(code), "percent_lower": "%" + format(code, "02x")}
    if format(code, "02x") != format(code, "02X"):
        variants["percent_upper"] = "%" + format(code, "02X")
    for spelling, suffix in variants.items():
        setattr(Workflow, f"test_56_release_ascii_{code:02x}_{spelling}", release_refusal(suffix))
for name, suffix in {"query": "?q=x", "fragment": "#frag", "backslash": "\\",
                     "encoded_backslash": "%5C", "encoded_dot": "%2E", "double_encoding": "%2501"}.items():
    setattr(Workflow, "test_57_release_" + name, release_refusal(suffix))


def status_case(headers, expected, code=403):
    def case(self):
        self.fixture.plan["issues", 1] = response("issues", status=code, headers=headers, raw=b'NO_RAW_ERROR_BODY')
        report = self.workflow()
        self.assertEqual(report["issues"]["status"], expected)
        self.assertEqual(report["issues"]["reason"], "http_" + str(code))
        self.assertNotIn("NO_RAW_ERROR_BODY", json.dumps(report))
    return case


for name, headers, expected, code in [
    ("missing", {"x-ratelimit-remaining": ""}, "forbidden", 403),
    ("malformed", {"x-ratelimit-remaining": "0x", "retry-after": "soon"}, "forbidden", 403),
    ("signed", {"x-ratelimit-remaining": "-0", "retry-after": "+17"}, "forbidden", 403),
    ("oversized", {"x-ratelimit-remaining": "0" * 129, "retry-after": "9" * 129}, "forbidden", 403),
    ("unicode_digits", {"x-ratelimit-remaining": "０", "retry-after": "１７"}, "forbidden", 403),
    ("zero_remaining", {"x-ratelimit-remaining": "0"}, "rate_limited", 403),
    ("retry_seconds", {"retry-after": "17"}, "rate_limited", 403),
    ("retry_zero", {"retry-after": "0"}, "rate_limited", 403),
    ("429_no_evidence", {"x-ratelimit-remaining": "", "retry-after": "bad"}, "rate_limited", 429),
]:
    setattr(Workflow, "test_58_http_" + name, status_case(headers, expected, code))


def metadata_refusal(kind, mutate):
    def case(self):
        url = self.fixture.origin + path_for(kind, 2)
        self.fixture.plan[kind, 1]["headers"]["link"] = '<' + mutate(self, url) + '>; rel="next"'
        section = SECTIONS[KINDS.index(kind)]
        self.assertEqual(self.workflow(3)[section]["status"], "pagination_invalid")
        self.assertEqual(len(self.fixture.observed), 3)
    return case


for name, kind, mutate in [
    ("wrong_predicate", "issues", lambda s, u: u.replace("is%3Aissue", "is%3Apr")),
    ("wrong_state", "issues", lambda s, u: u.replace("is%3Aopen", "is%3Aclosed")),
    ("wrong_repo_predicate", "issues", lambda s, u: u.replace("doesntdev%2Fapodexis", "other%2Frepo")),
    ("double_decoded_predicate", "issues", lambda s, u: u.replace("%3A", "%253A")),
    ("duplicate_page", "issues", lambda s, u: u + "&page=2"),
    ("encoded_duplicate_key", "issues", lambda s, u: u + "&%71=x"),
    ("duplicate_q", "issues", lambda s, u: u + "&q=x"),
    ("unknown_field", "issues", lambda s, u: u + "&extra=x"),
    ("missing_width", "issues", lambda s, u: u.replace("&per_page=10", "")),
    ("wrong_sort", "issues", lambda s, u: u.replace("updated", "created")),
    ("wrong_order", "issues", lambda s, u: u.replace("desc", "asc")),
    ("wrong_width", "issues", lambda s, u: u.replace("per_page=10", "per_page=100")),
    ("short_escape", "issues", lambda s, u: u.replace("%3A", "%3")),
    ("invalid_escape", "issues", lambda s, u: u.replace("%3A", "%zz")),
    ("control_escape", "issues", lambda s, u: u.replace("%20", "%0a")),
    ("nonascii_escape", "issues", lambda s, u: u.replace("%20", "%ff")),
    ("extra_question", "issues", lambda s, u: u + "?x"),
    ("fragment", "issues", lambda s, u: u + "#fragment"),
    ("leading_zero_page", "issues", lambda s, u: u.replace("page=2", "page=02")),
    ("signed_page", "issues", lambda s, u: u.replace("page=2", "page=%2B2")),
    ("zero_page", "issues", lambda s, u: u.replace("page=2", "page=0")),
    ("overflow_page", "issues", lambda s, u: u.replace("page=2", "page=9223372036854775808")),
    ("skipped_page", "issues", lambda s, u: u.replace("page=2", "page=3")),
    ("foreign_origin", "pulls", lambda s, u: u.replace(s.fixture.origin, s.outside.origin)),
    ("foreign_named_repo", "pulls", lambda s, u: u.replace(REPO, "other/repo")),
    ("wrong_alias_collection", "pulls", lambda s, u: u.replace("/repos/" + REPO + "/pulls", "/repositories/1024554267/releases")),
    ("zero_alias_id", "pulls", lambda s, u: u.replace("/repos/" + REPO, "/repositories/0")),
    ("leading_zero_alias_id", "pulls", lambda s, u: u.replace("/repos/" + REPO, "/repositories/01")),
    ("negative_alias_id", "pulls", lambda s, u: u.replace("/repos/" + REPO, "/repositories/-1")),
    ("overflow_alias_id", "pulls", lambda s, u: u.replace("/repos/" + REPO, "/repositories/9223372036854775808")),
    ("nonnumeric_alias_id", "pulls", lambda s, u: u.replace("/repos/" + REPO, "/repositories/abc")),
    ("alias_path_tail", "pulls", lambda s, u: u.replace("/pulls?", "/pulls/other?")),
    ("search_alias", "issues", lambda s, u: u.replace("/search/issues", "/repositories/1/issues")),
    ("wrong_pull_state", "pulls", lambda s, u: u.replace("state=open", "state=all")),
    ("wrong_release_width", "releases", lambda s, u: u.replace("per_page=5", "per_page=10")),
]:
    setattr(Workflow, "test_59_metadata_" + name, metadata_refusal(kind, mutate))


class RecordedResult(unittest.TextTestResult):
    records = []

    def addSuccess(self, test):
        super().addSuccess(test)
        self.records.append({"id": test.id(), "status": "passed"})

    def addFailure(self, test, err):
        super().addFailure(test, err)
        self.records.append({"id": test.id(), "status": "failure", "detail": self._exc_info_to_string(err, test)})

    def addError(self, test, err):
        super().addError(test, err)
        self.records.append({"id": test.id(), "status": "error", "detail": self._exc_info_to_string(err, test)})


if __name__ == "__main__":
    suite = unittest.TestSuite([Workflow("test_01_populated_extras_flags")]) if TLS else unittest.defaultTestLoader.loadTestsFromTestCase(Workflow)
    result = unittest.TextTestRunner(verbosity=2, resultclass=RecordedResult).run(suite)
    summary = {"tests_run": result.testsRun, "failures": len(result.failures), "errors": len(result.errors),
               "skipped": len(result.skipped), "tests": result.records, "tls": TLS}
    print(json.dumps(summary, indent=2))
    target = os.environ.get("APX_REPO_BRIEF_RECEIPTS")
    if target:
        Path(target).mkdir(parents=True, exist_ok=True)
        (Path(target) / "inventory.json").write_text(json.dumps(summary, indent=2) + "\n")
    sys.exit(0 if result.wasSuccessful() else 1)
