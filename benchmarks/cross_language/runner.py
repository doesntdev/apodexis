#!/usr/bin/env python3
"""Typed local comparison kernels, not an Apodexis execution-contract emulator.

Usage: python3 runner.py --once KERNEL < input.json
       python3 runner.py --sample KERNEL INPUT EXPECTED REPS WARMUPS
Only computation and result allocation are inside core_ns. Python's unbounded
integers are exact on the frozen corpus, not an implementation of checked i64.
"""
import json
import re
import sys
from time import perf_counter_ns

KERNELS = ("scalar", "numeric", "each", "builder", "rowdiff")
I64_MIN, I64_MAX = -(1 << 63), (1 << 63) - 1


def integer(value):
    if type(value) is not int or not I64_MIN <= value <= I64_MAX:
        raise ValueError("expected i64 integer")
    return value


def rows(value):
    if type(value) is not list:
        raise ValueError("expected row array")
    out = []
    for row in value:
        if (type(row) is not dict or set(row) != {"id", "hash"}
                or type(row["id"]) is not str or type(row["hash"]) is not str):
            raise ValueError("expected row with string id and hash")
        out.append((row["id"], row["hash"]))
    return out


def convert(kernel, value):
    if kernel == "rowdiff":
        if type(value) is not dict or set(value) != {"before", "after"}:
            raise ValueError("expected before/after object")
        return rows(value["before"]), rows(value["after"])
    if kernel == "each":
        if type(value) is not list:
            raise ValueError("expected integer array")
        return [integer(item) for item in value]
    value = integer(value)
    if kernel == "builder" and value < 0:
        raise ValueError("negative builder capacity")
    return value


def has_duplicate(items):
    for i in range(len(items)):
        for j in range(i + 1, len(items)):
            if items[i][0] == items[j][0]:
                return True
    return False


def contains(items, key):
    for row in items:
        if row[0] == key:
            return True
    return False


def hash_of(items, key):
    for row in items:
        if row[0] == key:
            return row[1]
    return ""


def compute(kernel, value):
    if kernel == "scalar":
        return value + 1
    if kernel == "numeric":
        index, total = 0, 0
        while index < value:
            total += index * 3 + 7
            index += 1
        return total
    if kernel == "each":
        total = 0
        for item in value:
            total += item
        return total
    if kernel == "builder":
        out = []
        index = 0
        while index < value:
            out.append(index)
            index += 1
        return out
    before, after = value
    if has_duplicate(before):
        return {"tag": "err", "error": "duplicate id in before"}
    if has_duplicate(after):
        return {"tag": "err", "error": "duplicate id in after"}
    added, changed, removed = [], [], []
    for key, digest in after:
        if contains(before, key):
            if hash_of(before, key) != digest:
                changed.append(key)
        # Deliberately repeat the source program's separate contains scan.
        if not contains(before, key):
            added.append(key)
    for key, _ in before:
        if not contains(after, key):
            removed.append(key)
    return {"tag": "ok", "value": {"added": added, "removed": removed,
                                      "changed": changed}}


def invalid_constant(value):
    raise ValueError(f"invalid JSON constant {value}")


def decode(source):
    return json.loads(source, parse_constant=invalid_constant)


def encode(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), allow_nan=False)


def invoke(kernel, source, expected, keep=False):
    parsed = decode(source)
    value = convert(kernel, parsed)
    start = perf_counter_ns()
    result = compute(kernel, value)
    core_ns = perf_counter_ns() - start
    wire = encode(result)
    if expected is not None and wire != expected:
        raise ValueError("output mismatch")
    del result, value, parsed
    if keep:
        return core_ns, wire
    del wire
    return core_ns, None


def count(text, low, high):
    if re.fullmatch(r"[0-9]+", text) is None:
        raise ValueError("invalid repetition/warmup count")
    value = int(text)
    if not low <= value <= high:
        raise ValueError("repetition/warmup count out of range")
    return value


def main(args):
    if len(args) == 2 and args[0] == "--once" and args[1] in KERNELS:
        _, wire = invoke(args[1], sys.stdin.buffer.read(), None, True)
        print(wire)
        return
    if len(args) != 6 or args[0] != "--sample" or args[1] not in KERNELS:
        raise ValueError("usage: --once KERNEL | --sample KERNEL INPUT EXPECTED REPS WARMUPS")
    _, kernel, input_path, expected_path, reps, warmups = args
    reps, warmups = count(reps, 1, 1000000), count(warmups, 0, 100000)
    with open(input_path, "rb") as stream:
        source = stream.read()
    with open(expected_path, "rb") as stream:
        expected = encode(decode(stream.read()))
    for _ in range(warmups):
        invoke(kernel, source, expected)
    total_core = 0
    wire = None
    start = perf_counter_ns()
    for index in range(reps):
        core_ns, wire = invoke(kernel, source, expected, index == reps - 1)
        total_core += core_ns
    pipeline_ns = perf_counter_ns() - start
    print(encode({"schema": "apodexis.cross_language_sample.v1", "language": "Python",
                  "kernel": kernel, "repetitions": reps, "warmups": warmups,
                  "core_ns": total_core, "pipeline_ns": pipeline_ns,
                  "output": decode(wire)}))


if __name__ == "__main__":
    try:
        main(sys.argv[1:])
    except (ValueError, OSError, OverflowError) as error:
        print(f"runner: {error}", file=sys.stderr)
        sys.exit(1)
