# Explicit host capabilities

Pure Apodexis programs do not have ambient host access. Supported host operations
return typed results through a trusted broker, enabled only by an operator-owned
policy passed to **CLI `run --capabilities`**.

Read the [stdlib host-operation contract](../resources/stdlib.md#explicit-host-operations)
for exact signatures, bounds and failure behavior. Do not accept a policy supplied
by untrusted generated source as if it were an operator grant.

## Supported operations

| Operation | Grant boundary |
| --- | --- |
| `fs_read` | Relative regular files under one declared read root. |
| `fs_write` | Relative regular files under one declared write root. |
| `env_get` | Explicitly named environment variables. |
| `clock_ms` | Explicit wall-clock grant. |
| `random_hex` | Explicit entropy grant. |
| `http_get` | Exact HTTP(S) URLs; the optional fixed `/usr/bin/curl` adapter. |

No policy means no grants. Source tests, `verify`, ordinary MCP calls and generated
standalone programs without a trusted broker channel remain ungranted. They cannot
self-authorize I/O. The native worker's seccomp restrictions remain active even
when a trusted broker services a permitted request.

Clock/entropy/network/file observations are external inputs or effects, not a
promise of deterministic results. Keep pure computation and effectful execution
separate in tests and evidence.

## Walkthrough: a two-file report tool

The [entry module](../examples/file_report/main.apx) imports the
[report library](../examples/file_report/report.apx), reads `name,count` rows,
accumulates counts in a map and writes a report.

From the source checkout or extracted distribution root:

```sh
mkdir -p .build/report-demo/input .build/report-demo/output
printf 'alpha,2\nbeta,3\nalpha,4\n' > .build/report-demo/input/items.txt
printf '%s\n' '{"source":"items.txt","destination":"summary.csv"}' > .build/report-demo/input.json
bin/apodexis verify examples/file_report/main.apx --json
```

Its pure parser/rendering tests run without file grants. Running the application
without a policy demonstrates default denial:

```sh
bin/apodexis run examples/file_report/main.apx --input .build/report-demo/input.json --json
```

The command can execute successfully while returning
`{"tag":"err","error":"capability_denied"}` as its application `output`.
Inspect that value, not only the process exit code.

### Create a minimal operator policy

Save the following JSON as `.build/report-demo/policy.json`, replacing the two
absolute directory placeholders with your **actual, separate demo directories**.
`realpath .build/report-demo/input` and `realpath .build/report-demo/output` show
the paths. Leave the other grants disabled.

```json
{
  "schema": "apodexis.host_policy.v1",
  "read_root": "/absolute/path/to/apodexis/.build/report-demo/input",
  "write_root": "/absolute/path/to/apodexis/.build/report-demo/output",
  "env": [],
  "clock": false,
  "random": false,
  "http_get": [],
  "max_bytes": 1048576,
  "max_requests": 16
}
```

Then run with that explicitly reviewed policy:

```sh
bin/apodexis run examples/file_report/main.apx --input .build/report-demo/input.json --capabilities .build/report-demo/policy.json --json
```

The successful result contains a summary with `total` equal to `9` and label
`written`. The file `.build/report-demo/output/summary.csv` contains:

```text
alpha,6
beta,3
```

The JSON byte/request bounds above are choices for this demonstration, not a
claim that every application should receive those limits. The `.build/` directory
is ignored; do not commit real operator policies or private input data.

## Safety and failure details

- Read/write roots are absolute existing directories. Program file arguments are
  relative paths under those roots, not arbitrary host paths.
- Absolute program paths, dot/dot-dot components, symlink traversal, nonregular
  files and hardlinked files are rejected by the implemented broker contract.
- Filesystem writes use an owned temporary file and rename. A durability error
  after rename can be reported after replacement; an error is not proof that no
  external effect occurred. Read back exact targets before assuming an outcome.
- HTTP access requires an exact allowlisted URL and an installed `/usr/bin/curl`.
  Redirects, URL globs, inherited proxy/credentials and arbitrary shell commands
  are not enabled. No HTTP call is needed for this walkthrough or default tests.
- Host-operation errors are ordinary `result` values. Fuel, resource and safety
  traps remain separate terminal execution failures.
- Use dedicated narrow roots rather than granting a home directory or repository
  wholesale. Operator grants are authority, not annotations in source.

See [the security policy](../SECURITY.md) and
[native backend boundaries](qbe-native-backend.md#boundaries) for the trust model.
