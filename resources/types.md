# Apodexis types and JSON ABI: implemented authoring reference

## Type grammar and inference

Scalar types are `i64`, `f64`, `bool`, and `str`. There is no `unit` type.
Composite forms are `[T]`, `map T` (string keys), named structs/enums, `option T`, `result T E`, and
local-only `builder T`. Type constructors are prefix and can nest:
`[[i64]]`, `option [str]`, `result [Point] str`, `builder [i64]`.
Infer locals whose initializer already determines the type: `L total 0`,
`L seen false`, `L point (Point x=1 y=2)`, `L out (buf i64 8)`.
Keep explicit function parameter/return and struct-field types.
Use explicit local annotations for empty arrays and option/result constructors
when no function argument/return or other expected type supplies their type.
There are no implicit numeric conversions or truthy values; use `(i64 value)`
or `(f64 value)` explicitly. Integer literals are i64; decimal literals are f64.
Arrays are homogeneous. `L empty:[str] []` is valid; an untyped empty array
without an expected item type cannot be inferred.

Struct declaration fields are each on their own indented line (`name:Type`).
Construct with all named fields exactly once; read with `value.field` (canonical
`(. value field)`). Arrays support `values[index]` (canonical `(@ values index)`).
Nested structs and arrays of structs are supported. Struct values are not maps:
unknown fields, missing fields, and field type mismatches are errors.

```apodexis
M points

T Point struct
  x:i64
  y:i64

F magnitude point:Point->i64
  R (+ (* point.x point.x) (* point.y point.y))

F main input:Point->i64
  R (magnitude input)

X origin
  A (= (magnitude (Point x=0 y=0)) 0)

X offset
  A (= (magnitude (Point y=4 x=3)) 25)
```

Option constructors are `(some value)` and `(none)`. Result constructors are
`(ok value)` and `(err error)`. They require an expected option/result type,
for example a typed local or the function's return type. To inspect variants,
use `C expression`, then each arm at two more spaces and its body at another
two. `some payload`/`ok payload`/`err payload` bind a payload; `none` binds none.
All variants must appear once. Payload bindings exist only inside their arm.
A result is a normal value; `(err ...)` does not trap or automatically return.

```apodexis
M safe_division

F divide value:i64 divisor:i64->result i64 str
  ? (= divisor 0)
    R (err "zero divisor")
  R (ok (/ value divisor))

F main input:i64->result i64 str
  R (divide 20 input)

X success
  C (divide 20 4)
    ok answer
      A (= answer 5)
    err problem
      A false

X failure
  C (divide 20 0)
    ok answer
      A false
    err problem
      A (= problem "zero divisor")

X optional
  L present:option i64 (some 7)
  L absent:option i64 (none)
  C present
    some value
      A (= value 7)
    none
      A false
  C absent
    some value
      A false
    none
      A true
```

Declared enums use `T Name enum` with indented variant declarations; a payload
field is written `Variant field:Type`. Case arms bind the declared payloads.
Do not invent `Name.Variant` source constructors: the current call checker does
not expose general user-enum construction. User-enum input also does not cross
the current JSON ABI. Use option/result when you need constructed variants.

Builders are local-only construction values. `(buf T capacity)` creates one,
`(push builder item)` returns an updated one, and `(finish builder)` returns
`[T]`. A builder is not permitted in function parameter/return types or struct
fields, including nested positions. Builders cannot cross the JSON ABI or be
returned as host values. No equality comparison is supported on builders.
Use `P name item` to append and retain the updated builder; its canonical
expansion is `S name (push name item)`. Neither form changes existing aliases.
`L out (buf i64 8)` infers `builder i64` without repeating that local annotation.

## Exact JSON input boundary

`main` may have zero parameters or one parameter. JSON input accepts bool, i64,
finite f64 (also integer JSON numbers), str, arrays, typed maps as JSON objects, and declared structs as
exact-field objects. A no-parameter main accepts absent/null input; this does
not mean null is a typed language value. A typed parameter does not accept null.
Option, result, user-enum, builder, and null inputs are not supported.
Numeric strings are not numbers; booleans are not integers. Missing and extra
struct fields are errors. Malformed input is rejected by the boundary before
main runs; do not turn it into a successful default or empty response.

For the Point example, JSON `{"x":3,"y":4}` produces JSON number `25`.
Arrays use JSON arrays, including `[]` where the contract determines the type.
For a struct with an array field, pass `{"items":[...]}`, not a tagged variant.
Read `apodexis_explain` for the actual contract instead of guessing field types.

## Exact JSON output boundary

Scalars and arrays encode directly; structs are field objects. Variant output:

- some: `{"tag":"some","value":...}`; none: `{"tag":"none"}`.
- ok: `{"tag":"ok","value":...}`; err: `{"tag":"err","error":...}`.
- A user-enum runtime variant encodes `{"tag":"Variant"}` without a payload
  or `{"tag":"Variant","value":...}` with a runtime payload. This encoding
  does not grant enum input or source-construction support.

Null/unit values, fixed arrays, aliases, tuples, generics, function values,
and numeric widths other than i64/f64 are not implemented. Recursive by-value
struct/type cycles are rejected. Syntax and inferred-type nesting have shared
bounded depth checks; keep deeply nested constructions shallow and consult
structured diagnostics rather than relying on unbounded recursion.

## Immutable maps and value updates

`map T` maps UTF-8 string keys to one concrete value type. `(map T)` constructs an
empty map without redundant local annotation; maps may nest in arrays/structs/maps.
Builders cannot be stored in maps. Keys are compared by unsigned UTF-8 bytes,
including embedded NUL, and enumeration/JSON encoding is in sorted key order.
Maps compare structurally regardless of insertion order. JSON input objects are
validated against their value type; map output is a JSON object. Option/result
map values can be constructed and returned, but do not grant option/result input.

`map_put`, `map_remove`, `array_set`, `array_slice`, `array_cat`, and
`(with record field=value ...)` return new immutable values. Update payloads are
evaluated once in authored order. `with` requires at least one declared field,
rejects duplicates/unknown fields and preserves the record's type. Other aliases
retain the old value. Safe `map_get` and `array_get` return options.

Host calls have fixed signatures: fs_read/env_get/random_hex/http_get return
`result str str`; fs_write/clock_ms return `result i64 str`. Their result error is
an ordinary recoverable capability/operation error, separate from runtime traps.
