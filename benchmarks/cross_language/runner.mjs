#!/usr/bin/env node
// Local typed comparison kernels, not an Apodexis contract/fuel emulator.
// node runner.mjs --once KERNEL < input.json
// node runner.mjs --sample KERNEL INPUT EXPECTED REPS WARMUPS
// Number arithmetic is exact on the frozen corpus, NOT full checked-i64 parity.
// Garbage collection is normal V8 managed allocation; do not force per-call GC.
import { readFileSync } from 'node:fs';

const kernels = new Set(['scalar', 'numeric', 'each', 'builder', 'rowdiff']);
const now = process.hrtime.bigint;

function integer(value) {
  if (!Number.isSafeInteger(value)) throw new Error('expected safe integer');
  return value;
}
function objectWith(value, keys) {
  return value !== null && typeof value === 'object' && !Array.isArray(value)
    && Object.keys(value).length === keys.length
    && keys.every(key => Object.hasOwn(value, key));
}
function rows(value) {
  if (!Array.isArray(value)) throw new Error('expected row array');
  const result = [];
  for (const row of value) {
    if (!objectWith(row, ['id', 'hash']) || typeof row.id !== 'string'
        || typeof row.hash !== 'string') throw new Error('expected string id/hash row');
    result.push({ id: row.id, hash: row.hash });
  }
  return result;
}
function convert(kernel, value) {
  if (kernel === 'rowdiff') {
    if (!objectWith(value, ['before', 'after'])) throw new Error('expected before/after object');
    return { before: rows(value.before), after: rows(value.after) };
  }
  if (kernel === 'each') {
    if (!Array.isArray(value)) throw new Error('expected integer array');
    return value.map(integer);
  }
  integer(value);
  if (kernel === 'builder' && value < 0) throw new Error('negative builder capacity');
  return value;
}
function hasDuplicate(rows) {
  for (let i = 0; i < rows.length; i++) {
    for (let j = i + 1; j < rows.length; j++) {
      if (rows[i].id === rows[j].id) return true;
    }
  }
  return false;
}
function contains(rows, id) {
  for (const row of rows) if (row.id === id) return true;
  return false;
}
function hashOf(rows, id) {
  for (const row of rows) if (row.id === id) return row.hash;
  return '';
}
function compute(kernel, value) {
  if (kernel === 'scalar') return value + 1;
  if (kernel === 'numeric') {
    let total = 0;
    for (let index = 0; index < value; index++) total += index * 3 + 7;
    return total;
  }
  if (kernel === 'each') {
    let total = 0;
    for (const item of value) total += item;
    return total;
  }
  if (kernel === 'builder') {
    const out = [];
    for (let index = 0; index < value; index++) out.push(index);
    return out;
  }
  const { before, after } = value;
  if (hasDuplicate(before)) return { error: 'duplicate id in before', tag: 'err' };
  if (hasDuplicate(after)) return { error: 'duplicate id in after', tag: 'err' };
  const added = [], changed = [], removed = [];
  for (const row of after) {
    if (contains(before, row.id)) {
      if (hashOf(before, row.id) !== row.hash) changed.push(row.id);
    }
    // Preserve the independent second contains scan from the APX source.
    if (!contains(before, row.id)) added.push(row.id);
  }
  for (const row of before) if (!contains(after, row.id)) removed.push(row.id);
  return { tag: 'ok', value: { added, changed, removed } };
}
function sortedKeys(value) {
  if (Array.isArray(value)) return value.map(sortedKeys);
  if (value !== null && typeof value === 'object') {
    return Object.fromEntries(Object.keys(value).sort().map(key => [key, sortedKeys(value[key])]));
  }
  return value;
}
function invoke(kernel, source, expected, verify, keep = false) {
  let parsed = JSON.parse(source);
  let input = convert(kernel, parsed);
  const start = now();
  let result = compute(kernel, input);
  const core = now() - start;
  let wire = JSON.stringify(result);
  if (verify && wire !== expected) throw new Error('output mismatch');
  // Drop all per-invocation DOM, typed input, and typed output references.
  parsed = input = result = null;
  if (keep) return { core, wire };
  wire = null;
  return { core, wire: null };
}
function count(text, low, high) {
  if (!/^[0-9]+$/.test(text)) throw new Error('invalid repetition/warmup count');
  const n = Number(text);
  if (!Number.isInteger(n) || n < low || n > high) throw new Error('count out of range');
  return n;
}
function nsNumber(n) {
  if (n > BigInt(Number.MAX_SAFE_INTEGER)) throw new Error('elapsed ns exceeds exact JSON Number range');
  return Number(n);
}
function main(args) {
  if (args.length === 2 && args[0] === '--once' && kernels.has(args[1])) {
    console.log(invoke(args[1], readFileSync(0, 'utf8'), null, false, true).wire);
    return;
  }
  if (args.length !== 6 || args[0] !== '--sample' || !kernels.has(args[1])) {
    throw new Error('usage: --once KERNEL | --sample KERNEL INPUT EXPECTED REPS WARMUPS');
  }
  const [, kernel, inputPath, expectedPath, repsText, warmupsText] = args;
  const reps = count(repsText, 1, 1000000), warmups = count(warmupsText, 0, 100000);
  const source = readFileSync(inputPath, 'utf8');
  const expected = JSON.stringify(sortedKeys(JSON.parse(readFileSync(expectedPath, 'utf8'))));
  for (let i = 0; i < warmups; i++) invoke(kernel, source, expected, true);
  let core = 0n, last;
  const start = now();
  for (let i = 0; i < reps; i++) {
    const sample = invoke(kernel, source, expected, true, i === reps - 1);
    core += sample.core;
    if (sample.wire !== null) last = sample.wire;
  }
  const pipeline = now() - start;
  console.log(JSON.stringify({ schema: 'apodexis.cross_language_sample.v1', language: 'JavaScript',
    kernel, repetitions: reps, warmups, core_ns: nsNumber(core), pipeline_ns: nsNumber(pipeline),
    output: JSON.parse(last) }));
}
try { main(process.argv.slice(2)); }
catch (error) { console.error(`runner: ${error.message}`); process.exitCode = 1; }
