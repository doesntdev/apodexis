# Read-only repository briefing

`examples/http_client/repo_brief.apx` is a native Apodexis application exposed as
one operator-configured MCP tool. It fetches and transforms public GitHub JSON
inside compiled Apodexis, through the existing typed `http.apx` module and broker.
There is no Python API adapter, response sanitizer, model, credential or dynamic
JSON value in the product path. The default authoring server remains 13 tools,
without caller-granted host authority.

## Run directly

From a source checkout, build `make -j2 all http-tls`. From a source-free TLS
bundle, the binary, support objects, examples, policy and manifest are already
included; keep the bundle's writable layout intact. Both use:

```sh
bin/apodexis serve --manifest examples/repo_brief/tool-server.json
```

A normal MCP stdio client initializes, discovers `repo_brief`, and calls it once:

```json
{"name":"repo_brief","arguments":{"input":{"repository":"doesntdev/apodexis","page_limit":1}}}
```

The protocol argument is the closed `{input: ...}` object. The application input
is the closed `{repository:str,page_limit:i64}` struct. Repository must byte-match
the reviewed operator configuration and page_limit must be 1, 2 or 3. Wrong
repository/limit returns `err repository_not_approved`/`err invalid_page_limit`
before any host operation. Unknown/missing/wrong-typed input fields are rejected
by native serving validation, also before any request. Callers cannot submit an
origin, URL, search query, credential, policy, source, path or budget override.

`repo_brief_config.apx` exports ordinary zero-argument `origin` and `repository`
functions. Defaults are `https://api.github.com` and `doesntdev/apodexis`. An
operator changing either must review the source and matching policy and restart
the server. There is no environment/debug override or global module search path.
Fixtures change only this tiny source configuration before server startup.

## Optional CLI

Python 3 standard library is optional convenience, not a native build/runtime
requirement:

```sh
python3 examples/repo_brief/run.py --binary ./bin/apodexis --page-limit 1 --output brief.json
```

`--manifest` selects an operator-owned manifest; the default is the adjacent
`tool-server.json`. The CLI launches the native stdio server, initializes,
discovers the one expected tool and performs one `tools/call`. It prints the
application Brief as JSON and optionally saves exactly those bytes, reading them
back before success. Observed MCP-call and HTTP-attempt counters are printed to
stderr. It does not fetch or normalize GitHub data. Complete reports
exit 0; partial/failed reports retain their actual JSON and exit 2. Protocol,
transport, native execution and domain-refusal failures print an error and exit
2, never fake data. The owned stdio process is closed/reaped on every exit.
Saving a report is an explicit local CLI action, not an APX file capability.

## Authority and fixed requests

The supplied v2 policy grants only ordinary verified HTTPS to `api.github.com`,
GET under `/repos/doesntdev/apodexis/` and `/search/`, and the three fixed headers:
`User-Agent: Apodexis-repo-brief`, `Accept: application/vnd.github+json`,
`X-GitHub-Api-Version: 2022-11-28`. It grants no files, environment, clock,
randomness, authentication or application crypto. Limits are 9 host requests,
1 MiB HTTP bodies, 2000 ms HTTP deadline, 30000 ms serving deadline, 1000000 fuel,
1024 input bytes and 131072 output bytes. No retries, polling or sleeps occur.
The optional TLS helper uses system trust; a plain distribution refuses HTTPS
rather than downgrading or invoking curl.

**The prefix policy cannot query-scope `/search/`.** The reviewed immutable
program, not the prefix grant alone, constructs this fixed query for every page:

- `/search/issues?q=repo%3Adoesntdev%2Fapodexis%20is%3Aissue%20is%3Aopen&sort=updated&order=desc&per_page=10&page=N`
- `/repos/doesntdev/apodexis/pulls?state=open&sort=updated&direction=desc&per_page=10&page=N`
- `/repos/doesntdev/apodexis/releases?per_page=5&page=N`

Issues exclude PRs server-side, and returned issue URLs must also match the
repository `/issues/<positive-number>` exactly. PR URLs must match `/pull/N`.
Releases must link under this repository's `/releases/tag/` with a nonempty,
bounded suffix without query/fragment/space, backslashes, literal ASCII C0
(U+0000..U+001F) or DEL (U+007F), their percent-encoded forms (hex-case
insensitive), or traversal/encoded-dot ambiguity. Ordinary non-control Unicode
and version tags remain valid; this is not a locale-dependent Unicode ban. Releases remain **API order**,
not semver order or a latest-stable recommendation. Pages are validated in full
before publication, then stable-first deduplicated by issue/PR number or release
tag. String bounds are 1024 title, 64 updated_at, 256 tag and 1024 release URL
UTF-8 bytes; empty/NUL selected strings and invalid numbers are refused.

## Exact application output

Native `structuredContent` remains the serving execution record, with status,
fuel and source/contract hashes. Its typed `output` is `result Brief str`:
`{tag:"ok",value:Brief}` or `{tag:"err",error:<domain-token>}`. The compatibility
text decodes to the same execution record; the CLI prints only Brief.

Brief fields are:

- `repository:str`, `repository_url:str`, `status:str`, `request_count:i64`,
  `flags:[str]`, `issues:Issues`, `pull_requests:Pulls`, `releases:Releases`.
- Each section has `items`, `observed_count:i64`, `pages_fetched:i64`,
  `requests:i64`, `complete:bool`, `status:str`, `reason:str`, `api_url:str`,
  `source_url:str`, `rate:Rate`, `source_changed:bool`.
- Issues additionally have `total_known:bool`, `total_count:i64`,
  `incomplete_results:bool`. Unknown total is explicitly distinguished from zero.
- Issue items: `number:i64,title:str,html_url:str,updated_at:str`.
- PR items add `draft:bool`; release items are
  `tag_name:str,html_url:str,prerelease:bool`.
- Rate fields are strings: `bucket,provider_resource,limit,remaining,reset,
  retry_after`. Bucket is `search` for issues and `core` for pulls/releases;
  absent provider metadata is empty. Metadata is the latest response's bounded
  header text (128 UTF-8 bytes; oversized/NUL becomes `unavailable`), not parsed
  quotas, an inferred retry time or permission to sleep.

A successful empty collection is complete. All sections complete => `complete`;
any incomplete/error section => `partial`; no validated page in any section =>
`failed`. Section `pages_fetched` counts **validated/published pages**, not failed
attempts. Requests includes failed/denied attempts. Prior validated pages remain
when a later page fails; a bad page's valid prefix is never published.

Section statuses include `ok`, `transport`, `rate_limited`, `forbidden` (403),
`inaccessible` (404), `http_error`, `protocol_error`, `invalid_data`,
`pagination_invalid`, `pagination_ambiguous`, `truncated`, `incomplete_results`
and `source_changed`. Reasons are bounded payload-free tokens or HTTP status
labels, never raw error bodies. HTTP 429 is always `rate_limited`; HTTP 403 is
`rate_limited` only with explicit well-formed exhaustion (`Remaining` is decimal
zero) or `Retry-After` decimal delta-seconds evidence, otherwise `forbidden` with
reason `http_403`. Evidence must be nonempty ASCII digits within the 128-byte
metadata bound; malformed/absent/duplicate headers are not throttle evidence.
Raw bounded rate metadata remains available on HTTP errors, with separate
search/core buckets. No retry or inferred timing is performed.

Link is metadata only. The program never follows its URL or redirects. It
constructs every request from fixed origin/repository/endpoint/page. The accepted
Link subset is comma-separated `<url>; rel="next|prev|first|last"`, one of each,
with the exact configured origin and consistent canonical positive page counters.
Query fields may be reordered and form-encoded once (`+`/`%20` spaces and
case-insensitive valid hex escapes), but must be unique known fields with exactly
the original semantic values, including search repo/is:issue/is:open predicates,
sort/order and page width. Named collection paths must match the configured repo;
pulls/releases also recognize provider metadata aliases
`/repositories/<positive-signed-i64-ID>/{pulls,releases}` with canonical decimal
IDs. These IDs are neither new authority nor verified repo identities: every
actual GET still uses the configured **named** path (or fixed search), never a
returned URL/ID. No `/repositories/` policy grant is added. Malformed, foreign,
ambiguous or contradictory metadata conservatively stops the section. A short
page with no continuation is terminal; a full page without terminal metadata is
not proof of completeness. Exhausted page budget is explicit. Search totals,
provider incomplete_results, duplicate identities and changed totals remain
visible even when pagination is truncated. Results are not an atomic snapshot.

Flags have stable order: `issues_incomplete`, `pull_requests_incomplete`,
`releases_incomplete`, `source_changed`, `provider_incomplete_results`,
`draft_pull_requests`, `prereleases` (included when applicable). Ordering is source
page/row order with first validated identity retained. No clock-derived age,
invented risk score or fabricated public repository rows are returned.

## Typed projection

`(json_decode_fields T text)` is appended pure intrinsic 57. It has exactly the
concrete JSON eligibility and once-only text evaluation of `json_decode`, but
normalizes structs by retaining declared required fields in declaration order and
ignoring other **struct** fields recursively, including structs in arrays/maps.
Maps still validate every value; option/result map wrappers remain exact.
Missing/wrong selected fields, duplicate names (even ignored names/escaped
aliases), malformed/trailing JSON, numeric/UTF-8/depth errors remain errors.
NUL-suffixed keys are distinct, not aliases for required fields. Strings preserve
NUL data and aliases remain immutable. Resource exhaustion and runtime traps
propagate to the existing live outer trap; no extra statement fuel is charged.
The parser, encoder, entrypoint ABI and strict `json_decode` remain unchanged.

## Controlled qualification

```sh
make -j2 all check test          # existing C/libc-only native suite
make test-sanitize
make test-repo-brief             # explicitly optional Python3 stdlib integration
```

The optional suite drives real native `serve`, owned loopback listeners and tiny
source/config copies using one shared binary/toolchain. Named test inventory and
actual GET/MCP counts are emitted. `APX_REPO_BRIEF_RECEIPTS=/absolute/directory`
retains raw responses, observed headers/paths, exact MCP frames and a separate
Python specification oracle's semantic report facts. It does not import APX
helpers or use Python to implement the workflow. The suite proves populated and
empty data, pagination/errors, invalid input/no-send and recovery, not model
speed, token savings or authoring reliability. `--tls-smoke` explicitly exercises
one populated workflow with an ephemeral local certificate and operator CA
snapshot; unavailable prerequisites fail, never silently skip.

A controller-owned bounded live call and independent schema/real-client,
SPEC-then-distinct-QUALITY review are separate acceptance gates. Fixture success
is not approval, publication or a claim that public data was populated.
