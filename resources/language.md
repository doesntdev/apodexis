# Apodexis language: complete implemented authoring reference

This resource, `apodexis://spec/types`, and `apodexis://spec/stdlib` describe the
implemented compiler exposed by this MCP server. Read all three before writing
code. They define the implemented language and its supported operations. The
compiler's structured diagnostics decide what is accepted.

## Source grammar and layout

- Begin with exactly one `M module_name` at indentation zero.
- Top-level declarations are `T Name struct`, `T Name enum`,
  `F name parameter:Type ...->ReturnType`, and `X test_name`.
- Use two spaces per block level, never tabs. Blank lines are allowed.
- Keep each declaration, statement, and expression on **one physical line**;
  parentheses do not enable multiline continuation. Long lines are valid.
- There are no braces, colons after block headers, commas between arguments,
  semicolons, infix operators, expression statements, or implicit returns.
  There is no `B` statement for loop-breaking; top-level B metadata is separate.
- Use simple ASCII names, distinct functions/types/test names, and descriptive
  locals. Functions and types may refer to declarations elsewhere in the module.
- Every function has an explicit return type. A zero-argument header is
  `F main->i64`; a zero-argument call is `(main)`.
- `main` is the JSON entrypoint: zero or one parameter, with supported JSON input
  types. Helpers can take multiple parameters. Tests invoke helpers directly.
- `B` benchmark metadata is parsed but is not executable program behavior.

## Every executable statement

| Form | Meaning |
| --- | --- |
| `L name:Type expression` | Declare an explicitly typed local. |
| `L name expression` | Declare a local with inferred type. |
| `S name expression` | Replace an existing binding, preserving its type. |
| `P builder expression` | Append one item and write the updated builder back to that binding. |
| `R expression` | Return a function value immediately. |
| `? condition` | Execute a nested block if a bool condition is true. |
| `W condition` | Repeat a nested block while a bool condition is true. |
| `E item array_expression` | Iterate array values, binding each element. |
| `C variant_expression` | Match an option/result/enum using exhaustive arms. |
| `A bool_expression` | Assert true; false produces an assertion failure. |
| `Z "message"` | Produce a runtime trap, not a result/error variant. |

Standalone calls are not statements. In particular, `(push out value)` on its
own line is invalid; use `P out value`. It is exactly one
`S out (push out value)` statement, not in-place mutation of builder aliases.
The target is one existing bare builder name, not a field or indexed value.
The item is evaluated eagerly before the same capacity check as `push`.
Likewise `L temporary (helper input)` binds a helper's result. There is no discard
statement, assignment operator, `else`, `elif`, `break`, `continue`, or ternary.
For conditional return use `? condition` with a nested `R`, then a following `R`
for the other path. A `?` can contain any number of statements. An empty block
is legal but never substitutes for a required result.

Bindings are block-scoped; update an outer binding with `S` rather than declaring
it inside a block when it must survive the block. A new local, loop variable or
case payload cannot shadow an active outer local or parameter. Fresh sibling
scopes may reuse a name after the previous scope has ended. Conditions must be
bool. `E` iterates values, not numeric indices;
use an i64 local and a `W` loop when an index is needed. Always update the index
inside its loop. The checker rejects statically missing returns; ensure every
actual function path returns or deliberately traps.

## Expression grammar

Literals are `true`, `false`, signed i64 integers, decimal f64 literals such as
`1.5`, quoted strings, and array literals `[expression expression ...]`.
A variable name is an expression. `_` is a typed hole for diagnostics and repair
suggestions, never a runnable finished value. There is no null/unit literal.

A call is `(head argument argument ...)`. Arithmetic and ordinary calls remain
prefix: `(+ a b)`, `(& (= a b) (! done))`.

Use postfix reads for data access: `row.id`, `rows[index]`,
`input.detail.values[0]`, or `(fetch rows)[(index)].id`. Chains read left to right;
base/index expressions execute once. Attach the suffix directly to its base,
and the field directly to its dot. Index brackets contain exactly one expression,
which may contain inner whitespace: `rows[(+ index 1)]`. Separate array elements
and call arguments with whitespace; adjacent brackets attach as an index suffix.
Reads retain declared-field checks and array bounds checks. This does not add
methods, dynamic properties, string indexing or field/element assignment.
Prefix `(. row id)` and `(@ rows index)` remain accepted. `fmt` emits the existing
prefix canonical form, so both spellings have one canonical source/hash and old
canonical `apx1` identities are unchanged. Format/repair output may be longer
than the compact authoring source. Removing an explicit local annotation is a
source edit and can change the hash even when execution remains the same.
Equality is `(= left right)`, not `==`. Boolean conjunction/disjunction are `&`
and `|`, not `and`/`or`. They evaluate both operands: do not use them to guard an
unsafe index. First use a `?` or `W` to prove an index is in bounds, then call `@`.

User functions require positional arguments in declared order: `(add 2 3)`.
Named arguments are reserved for struct constructors; `(add left=2 right=3)` is
rejected with `N104`. Builtins use positional arguments, except `buf`'s first
argument is a type. Struct constructors require every field exactly once as
named arguments: `(Point x=2 y=3)`. Field reads use `point.x`, or the canonical
`(. point x)` where `x` is the declared field name. There is no array/struct
element assignment: construct a new value.

Strings support escaped quote, backslash, `\n`, `\r`, `\t`, `\0`, and
`\u{HEX}` Unicode scalar escapes. Do not assume JSON's `\uXXXX` escape is source
syntax. When putting source into a tool argument, let the JSON encoder escape
source newlines/quotes/backslashes; the decoded source must contain real lines.

## Local annotations

Prefer `L total 0`, `L seen false`, `L label "ok"`, and
`L out (buf i64 (# values))` when the initializer determines the type.
Keep parameter/return and declared struct-field types explicit. Keep local
annotations for ambiguous empty arrays and option/result constructors, such as
`L empty:[str] []` and `L found:option Row (none)`. Keep `buf`'s element type;
`(buf (# values))` is not supported. No implicit numeric conversion is added.

## Complete programs: locals, control flow, functions, and tests

```apodexis
M accumulate

F sum values:[i64]->i64
  L total 0
  E value values
    S total (+ total value)
  R total

F main input:[i64]->i64
  R (sum input)

X empty
  A (= (sum []) 0)

X mixed
  A (= (sum [3 -1 5]) 7)
```

```apodexis
M counting

F nonnegative value:i64->i64
  ? (< value 0)
    R 0
  R value

F main input:i64->i64
  L index 0
  L total 0
  W (< index (nonnegative input))
    S total (+ total index)
    S index (+ index 1)
  R total

X cases
  A (= (main -2) 0)
  A (= (main 0) 0)
  A (= (main 4) 6)
```

## MCP authoring workflow and exact success conditions

1. Discover `apodexis_capabilities`; read the three spec resources and, if useful,
   `apodexis://examples`. Resource reads are MCP `resources/read` operations, not
   filesystem reads. Clients exposing only function tools can bridge this
   operation as a resource-reading tool; its URI must be a discovered resource.
2. Write a complete module, including useful `X` assertions for normal, empty,
   and boundary cases. Send the entire source string to `apodexis_verify`.
3. Verification returns format, check/repairs, and declared-test results. A
   top-level `status: "ok"` is the verification verdict; checking alone does not
   prove behavior. Inspect nested diagnostics and failing tests on errors.
4. Correct the source yourself or explicitly select compiler-issued repairs
   using `apodexis_apply_repairs`. Use the returned exact `source_text_hash` as
   the precondition, pass repair objects unchanged, and opt into suggestive
   repairs deliberately. After any edit, verify the exact final source again.
5. `apodexis_explain` shows the entrypoint contract. `apodexis_pseudocode` renders
   checked source for review; it does not replace compilation or execution.
6. Call `apodexis_run` with `source`, JSON `input`, and explicit `limits`, e.g.
   `{"fuel":100000,"max_input_bytes":1048576,"max_output_bytes":1048576}`.
   For no-parameter main pass JSON null as the absent input. Inspect both
   `status` and `output`. Malformed typed input is an ABI error, not empty data.
7. A program is finished only when final verification and representative runs
   succeed. Supply the same exact final source you tested, without fences or
   pseudocode. Do not assert that tests ran when no tool executed them.

## Modules and imports

Use `I alias "module_key"` at top level, `V symbol` to export a function or type,
and `alias::symbol` in call heads and type positions. For example:

```text
M application
I math "arithmetic"
F main value:i64->i64
  R (math::double value)
```

The imported module starts with `M arithmetic`, exports `double` with `V double`,
and declares the function normally. Private declarations remain usable within
that module, not through its import alias. Import cycles, missing modules,
invalid keys, conflicting names and private lookups are rejected. Keys are simple
ASCII identifiers, not paths. The `__apx_` namespace is reserved in module bundles.

CLI commands resolve `<module_key>.apx` beside the entry file. `bundle` returns a
single-module source containing every selected dependency and imported test. MCP
clients call `apodexis_bundle` with `source` and a `modules` object of inline sources,
then pass the returned `source` to the existing check/verify/run tools. No source
path, package download or host grant is accepted through that tool. Canonical
bundle identity includes the selected dependency code; subsequent compiler spans
and safe whole-source repairs refer to the assembled source, not original files.

## Pure library and explicit host effects

Strings have Unicode-scalar operations and strict numeric conversions. `map T`
is a typed string-keyed immutable map. Array updates return new values; use
`(with record field=value ...)` for typed immutable record updates. Neither form
changes old aliases. See the complete builtin signatures in the stdlib resource.

Host operations are `fs_read`, `fs_write`, `env_get`, `clock_ms`, `random_hex`, and
`http_get`. They return typed results, not ambient access. By default all return
`(err "capability_denied")`. Only CLI `run --capabilities policy.json` activates
an operator-owned policy. Tests, verify and ordinary MCP calls stay ungranted.
A trusted native broker enforces each request; the compiled worker cannot open
files, use sockets or spawn processes directly. Host observations/effects are
not deterministic; pure computation and default-denied execution remain so.

There are still no closures, general function values, sorting builtin, arbitrary
host commands, database adapters, HTTP listeners or automatic package registry.
Use native modules, typed values and the published catalog rather than inventing
unsupported names.
