//! Local typed comparison kernels, not an APX execution-contract emulator.
//! Build offline from repo root, with target-dir in owned scratch:
//! cargo build --offline --release --locked
//!   --manifest-path benchmarks/cross_language/rust/Cargo.toml --target-dir "$TARGET"
//! Run: apodexis-cross-language-rust --once KERNEL < input.json
//!      apodexis-cross-language-rust --sample KERNEL INPUT EXPECTED REPS WARMUPS
use serde_json::{json, Value};
use std::hint::black_box;
use std::io::{self, Read};
use std::time::Instant;

#[derive(Clone, Copy)]
enum Kernel {
    Scalar,
    Numeric,
    Each,
    Builder,
    Rowdiff,
}
struct Row {
    id: String,
    hash: String,
}
enum Input {
    Integer(i64),
    Integers(Vec<i64>),
    Pair { before: Vec<Row>, after: Vec<Row> },
}
enum Output<'a> {
    Integer(i64),
    Integers(Vec<i64>),
    Diff {
        added: Vec<&'a str>,
        removed: Vec<&'a str>,
        changed: Vec<&'a str>,
    },
    Error(&'static str),
}

type Fallible<T> = Result<T, String>;

fn kernel_name(name: &str) -> Fallible<Kernel> {
    match name {
        "scalar" => Ok(Kernel::Scalar),
        "numeric" => Ok(Kernel::Numeric),
        "each" => Ok(Kernel::Each),
        "builder" => Ok(Kernel::Builder),
        "rowdiff" => Ok(Kernel::Rowdiff),
        _ => Err("unknown kernel".into()),
    }
}
fn integer(value: &Value) -> Fallible<i64> {
    value.as_i64().ok_or_else(|| "expected i64 integer".into())
}
fn rows(value: &Value) -> Fallible<Vec<Row>> {
    let items = value.as_array().ok_or("expected row array")?;
    let mut out = Vec::with_capacity(items.len());
    for item in items {
        let row = item.as_object().ok_or("expected row object")?;
        if row.len() != 2 {
            return Err("expected id/hash row".into());
        }
        let id = row
            .get("id")
            .and_then(Value::as_str)
            .ok_or("expected string id")?;
        let hash = row
            .get("hash")
            .and_then(Value::as_str)
            .ok_or("expected string hash")?;
        out.push(Row {
            id: id.to_owned(),
            hash: hash.to_owned(),
        });
    }
    Ok(out)
}
fn convert(kernel: Kernel, value: &Value) -> Fallible<Input> {
    match kernel {
        Kernel::Rowdiff => {
            let pair = value.as_object().ok_or("expected before/after object")?;
            if pair.len() != 2 {
                return Err("expected before/after object".into());
            }
            Ok(Input::Pair {
                before: rows(pair.get("before").ok_or("missing before")?)?,
                after: rows(pair.get("after").ok_or("missing after")?)?,
            })
        }
        Kernel::Each => {
            let items = value.as_array().ok_or("expected integer array")?;
            let mut out = Vec::with_capacity(items.len());
            for item in items {
                out.push(integer(item)?);
            }
            Ok(Input::Integers(out))
        }
        _ => {
            let n = integer(value)?;
            if matches!(kernel, Kernel::Builder) {
                let count = usize::try_from(n).map_err(|_| "invalid builder capacity")?;
                if count > isize::MAX as usize / std::mem::size_of::<i64>() {
                    return Err("builder allocation size overflow".into());
                }
            }
            Ok(Input::Integer(n))
        }
    }
}
fn duplicate(rows: &[Row]) -> bool {
    for i in 0..rows.len() {
        for j in (i + 1)..rows.len() {
            if rows[i].id == rows[j].id {
                return true;
            }
        }
    }
    false
}
fn contains(rows: &[Row], id: &str) -> bool {
    for row in rows {
        if row.id == id {
            return true;
        }
    }
    false
}
fn hash_of<'a>(rows: &'a [Row], id: &str) -> &'a str {
    for row in rows {
        if row.id == id {
            return &row.hash;
        }
    }
    ""
}
fn compute(kernel: Kernel, input: &Input) -> Output<'_> {
    match (kernel, input) {
        (Kernel::Scalar, Input::Integer(n)) => Output::Integer(n + 1),
        (Kernel::Numeric, Input::Integer(n)) => {
            let mut index = 0i64;
            let mut total = 0i64;
            while index < *n {
                total += index * 3 + 7;
                index += 1;
            }
            Output::Integer(total)
        }
        (Kernel::Each, Input::Integers(items)) => {
            let mut total = 0i64;
            for item in items {
                total += *item;
            }
            Output::Integer(total)
        }
        (Kernel::Builder, Input::Integer(n)) => {
            let mut out = Vec::with_capacity(*n as usize);
            let mut index = 0i64;
            while index < *n {
                out.push(index);
                index += 1;
            }
            Output::Integers(out)
        }
        (Kernel::Rowdiff, Input::Pair { before, after }) => {
            if duplicate(before) {
                return Output::Error("duplicate id in before");
            }
            if duplicate(after) {
                return Output::Error("duplicate id in after");
            }
            let mut added = Vec::with_capacity(after.len());
            let mut changed = Vec::with_capacity(after.len());
            let mut removed = Vec::with_capacity(before.len());
            for row in after {
                if contains(before, &row.id) && hash_of(before, &row.id) != row.hash {
                    changed.push(row.id.as_str());
                }
                // Not an else: the APX source performs another contains scan.
                if !contains(before, &row.id) {
                    added.push(row.id.as_str());
                }
            }
            for row in before {
                if !contains(after, &row.id) {
                    removed.push(row.id.as_str());
                }
            }
            Output::Diff {
                added,
                removed,
                changed,
            }
        }
        _ => unreachable!("validated kernel/input pairing"),
    }
}
fn transport(result: &Output<'_>) -> Value {
    match result {
        Output::Integer(n) => json!(n),
        Output::Integers(items) => json!(items),
        Output::Error(error) => json!({"tag": "err", "error": error}),
        Output::Diff {
            added,
            removed,
            changed,
        } => {
            json!({"tag": "ok", "value": {"added": added, "removed": removed, "changed": changed}})
        }
    }
}
fn invoke(
    kernel: Kernel,
    source: &[u8],
    expected: Option<&[u8]>,
    keep: bool,
) -> Fallible<(u64, Option<Vec<u8>>)> {
    let parsed: Value = serde_json::from_slice(source).map_err(|e| e.to_string())?;
    let input = black_box(convert(kernel, &parsed)?);
    let start = Instant::now();
    let output = black_box(compute(kernel, &input));
    let core = u64::try_from(start.elapsed().as_nanos()).map_err(|_| "core timer overflow")?;
    let value = transport(&output);
    let wire = serde_json::to_vec(&value).map_err(|e| e.to_string())?;
    if let Some(want) = expected {
        if wire != want {
            return Err("output mismatch".into());
        }
    }
    drop(value);
    drop(output);
    drop(input);
    drop(parsed);
    if keep {
        Ok((core, Some(wire)))
    } else {
        drop(wire);
        Ok((core, None))
    }
}
fn count(text: &str, low: u64, high: u64) -> Fallible<u64> {
    if text.is_empty() || !text.bytes().all(|b| b.is_ascii_digit()) {
        return Err("invalid repetition/warmup count".into());
    }
    let n: u64 = text.parse().map_err(|_| "count overflow")?;
    if n < low || n > high {
        return Err("count out of range".into());
    }
    Ok(n)
}
fn run(args: &[String]) -> Fallible<()> {
    if args.len() == 2 && args[0] == "--once" {
        let kernel = kernel_name(&args[1])?;
        let mut source = Vec::new();
        io::stdin()
            .read_to_end(&mut source)
            .map_err(|e| e.to_string())?;
        let (_, wire) = invoke(kernel, &source, None, true)?;
        println!(
            "{}",
            String::from_utf8(wire.unwrap()).map_err(|e| e.to_string())?
        );
        return Ok(());
    }
    if args.len() != 6 || args[0] != "--sample" {
        return Err("usage: --once KERNEL | --sample KERNEL INPUT EXPECTED REPS WARMUPS".into());
    }
    let kernel = kernel_name(&args[1])?;
    let reps = count(&args[4], 1, 1000000)?;
    let warmups = count(&args[5], 0, 100000)?;
    let source = std::fs::read(&args[2]).map_err(|e| e.to_string())?;
    let expected: Value =
        serde_json::from_slice(&std::fs::read(&args[3]).map_err(|e| e.to_string())?)
            .map_err(|e| e.to_string())?;
    let expected_wire = serde_json::to_vec(&expected).map_err(|e| e.to_string())?;
    for _ in 0..warmups {
        invoke(kernel, &source, Some(&expected_wire), false)?;
    }
    let mut core = 0u64;
    let mut last = None;
    let start = Instant::now();
    for i in 0..reps {
        let (elapsed, wire) = invoke(kernel, &source, Some(&expected_wire), i + 1 == reps)?;
        core = core.checked_add(elapsed).ok_or("core time sum overflow")?;
        if wire.is_some() {
            last = wire;
        }
    }
    let pipeline =
        u64::try_from(start.elapsed().as_nanos()).map_err(|_| "pipeline timer overflow")?;
    let output: Value = serde_json::from_slice(&last.unwrap()).map_err(|e| e.to_string())?;
    println!(
        "{}",
        json!({"schema": "apodexis.cross_language_sample.v1", "language": "Rust",
        "kernel": args[1], "repetitions": reps, "warmups": warmups, "core_ns": core,
        "pipeline_ns": pipeline, "output": output})
    );
    Ok(())
}
fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    if let Err(error) = run(&args) {
        eprintln!("runner: {error}");
        std::process::exit(1);
    }
}
