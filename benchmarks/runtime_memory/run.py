#!/usr/bin/env python3
"""Optional stdlib-only evidence orchestration; never a compiler dependency.

prepare freezes baseline oracles and a baseline/noise pilot before candidates.
variant freezes a runtime source and builds untimed repeat/ELF artifacts.
compare performs bounded rotated samples under the frozen protocol.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import statistics
import subprocess
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
CFLAGS = ['-std=c99', '-O2', '-Wall', '-Wextra', '-Werror', '-fno-pie']


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run(args, cwd=None, data=None, timeout=30):
    p = subprocess.run([str(x) for x in args], cwd=cwd, input=data,
                       text=True, encoding='utf-8', capture_output=True,
                       timeout=timeout, env={**os.environ, 'LC_ALL': 'C'})
    if p.returncode:
        raise RuntimeError(f'{args!r}: exit {p.returncode}\n{p.stdout}\n{p.stderr}')
    return p.stdout


def write_once(path, content):
    path = Path(path)
    raw = content.encode('utf-8') if isinstance(content, str) else content
    if path.exists():
        if path.read_bytes() != raw:
            raise RuntimeError(f'refusing to rewrite frozen evidence: {path}')
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open('xb') as f:
        f.write(raw)


def store(path, obj):
    write_once(path, json.dumps(obj, ensure_ascii=False, sort_keys=True, indent=2) + '\n')


def load(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def definitions():
    cases: list[tuple[str, str, object, object, str]] = [('scalar', 'scalar.apx', 7, 8, 'control')]
    for size in (128, 4096):
        values = list(range(size))
        cases.append((f'each-{size}', 'each-sum.apx', values, sum(values), 'control'))
    for size in (128, 2048):
        values = list(range(size))
        cases.append((f'index-{size}', 'length-index.apx', values, sum(values), 'control'))
    for size in (256, 2048):
        cases.append((f'numeric-{size}', 'numeric-helpers.apx', size,
                      sum(i * 3 + 7 for i in range(size)), 'control'))
    for size in (128, 1024):
        values = list(range(size))
        obj = {'detail': {'values': values, 'label': 'nested'}, 'unrelated': ['kept'] * 32}
        cases.append((f'nested-{size}', 'nested-record.apx', obj,
                      sum(values) + size * len('nested'), 'control'))
    for size in (0, 1, 32, 256, 1024, 4096):
        cases.append((f'builder-{size}', 'builder-writeback.apx', size, list(range(size)), 'target'))
    cases.append(('finish-prefixes-256', 'finish-prefixes.apx', 256,
                  2 * sum(range(1, 257)), 'finish_stress'))
    for size in (32, 64):
        before = [{'id': f'id-{i}', 'hash': 'old'} for i in range(size)]
        after = [{'id': f'id-{i}', 'hash': 'new' if i % 3 == 0 else 'old'}
                 for i in range(size // 4, size + size // 4)]
        obj = {'before': before, 'after': after}
        want = {'tag': 'ok', 'value': {
            'added': [x['id'] for x in after if x['id'] not in {b['id'] for b in before}],
            'removed': [x['id'] for x in before if x['id'] not in {a['id'] for a in after}],
            'changed': [x['id'] for x in after
                        if x['id'] in {b['id'] for b in before} and x['hash'] == 'new']}}
        cases.append((f'rowdiff-{size}', 'agent-rowdiff.apx', obj, want, 'control'))
    for name, source, path in [('http-root', 'http_hello.apx', '/'),
                               ('web-root', 'web_static.apx', '/'),
                               ('web-css', 'web_static.apx', '/style.css'),
                               ('web-missing', 'web_static.apx', '/missing')]:
        obj = {'method': 'GET', 'path': path, 'headers': [], 'query': []}
        cases.append((name, source, obj, None, 'control'))
    return cases


def build_variant(out, name, repo, cases):
    repo = Path(repo).resolve()
    target = out / 'variants' / name
    if (target / 'manifest.json').exists():
        manifest = load(target / 'manifest.json')
        for rel, expected in manifest['files'].items():
            if digest(target / rel) != expected:
                raise RuntimeError(f'changed variant artifact: {name}/{rel}')
        return manifest
    target.mkdir(parents=True, exist_ok=True)
    for f in ('apx_runtime.c', 'apx_runtime.h'):
        write_once(target / 'runtime' / f, (repo / 'runtime' / f).read_bytes())
    runtime = target / 'runtime' / 'apx_runtime.c'
    harness = out / 'repeat.c'
    for suffix, extra in [('timing', []), ('counters', ['-DAPX_BENCH_COUNTERS'])]:
        obj = target / f'repeat-{suffix}.o'
        run(['cc', *CFLAGS, *extra, f'-DAPX_RUNTIME_SOURCE="{runtime}"', '-c', harness, '-o', obj])
        for case in cases:
            stem = case['id']
            run(['cc', '-no-pie', obj, out / 'corpus' / f'{stem}.o', '-lm',
                 '-o', target / f'{stem}-{suffix}'])
    for case in cases:
        run([repo / 'bin' / 'apodexis', 'build', out / 'corpus' / f"{case['id']}.apx",
             '-o', target / f"{case['id']}.elf"])
    files = {str(p.relative_to(target)): digest(p)
             for p in sorted(target.rglob('*')) if p.is_file()}
    manifest = {'schema': 'apodexis.runtime_memory_variant.v1', 'name': name,
                'head': run(['git', 'rev-parse', 'HEAD'], cwd=repo).strip(),
                'runtime_git_blob': run(['git', 'hash-object', 'runtime/apx_runtime.c'], cwd=repo).strip(),
                'runtime_sha256': digest(runtime), 'harness_sha256': digest(harness),
                'flags': CFLAGS, 'files': files}
    store(target / 'manifest.json', manifest)
    return manifest


def sample(out, variant, case, mode, repetitions, instrumented=False):
    suffix = 'counters' if instrumented else 'timing'
    target = out / 'variants' / variant
    executable = target / f"{case['id']}-{suffix}"
    data = load_json_output = json.loads(run([executable,
        out / 'corpus' / f"{case['id']}.input.json",
        out / 'corpus' / f"{case['id']}.expected.json", repetitions, mode], timeout=20))
    if (data['source_hash'] != case['source_hash'] or data['fuel_used'] != case['fuel_used']
            or data['repetitions'] != repetitions or data['mode'] != mode
            or data['instrumented'] != instrumented):
        raise RuntimeError(f'sample contract mismatch: {variant}/{case["id"]}')
    data.update({'case': case['id'], 'variant': variant,
                 'binary_sha256': digest(executable)})
    return load_json_output


def prepare(out, baseline):
    if (out / 'protocol.json').exists():
        raise RuntimeError('protocol already frozen; use a new evidence directory')
    out.mkdir(parents=True, exist_ok=True)
    write_once(out / 'repeat.c', (HERE / 'repeat.c').read_bytes())
    cases = []
    baseline = Path(baseline).resolve()
    for ident, source, input_value, expected_value, group in definitions():
        source_path = HERE / 'cases' / source
        if not source_path.is_file():
            source_path = ROOT / 'examples' / source
        raw = source_path.read_bytes()
        stem = out / 'corpus' / ident
        write_once(stem.with_suffix('.apx'), raw)
        text = json.dumps(input_value, ensure_ascii=False, separators=(',', ':'))
        write_once(stem.with_suffix('.input.json'), text)
        il = run([baseline / 'bin' / 'apodexis', 'emit-qbe', stem.with_suffix('.apx')])
        write_once(stem.with_suffix('.ssa'), il)
        run([baseline / '.build' / 'tools' / 'qbe', '-o', stem.with_suffix('.s'), stem.with_suffix('.ssa')])
        run(['as', stem.with_suffix('.s'), '-o', stem.with_suffix('.o')])
        run([baseline / 'bin' / 'apodexis', 'build', stem.with_suffix('.apx'), '-o', stem.with_suffix('.elf')])
        oracle = json.loads(run([stem.with_suffix('.elf'), '--fuel', '10000000'], data=text))
        if oracle['status'] != 'ok':
            raise RuntimeError(f'baseline cannot execute {ident}: {oracle}')
        if expected_value is not None and oracle['output'] != expected_value:
            raise RuntimeError(f'independent expected output disagrees: {ident}')
        if group == 'control' and source in ('http_hello.apx', 'web_static.apx'):
            want_status = 404 if ident == 'web-missing' else 200
            if oracle['output']['value']['status'] != want_status:
                raise RuntimeError(f'baseline route fixture mismatch: {ident}')
        expected = json.dumps(oracle['output'], ensure_ascii=False, sort_keys=True, separators=(',', ':'))
        write_once(stem.with_suffix('.expected.json'), expected)
        store(stem.with_suffix('.oracle.json'), oracle)
        cases.append({'id': ident, 'group': group, 'source_sha256': hashlib.sha256(raw).hexdigest(),
                      'input_sha256': digest(stem.with_suffix('.input.json')),
                      'expected_sha256': digest(stem.with_suffix('.expected.json')),
                      'source_hash': oracle['source_hash'], 'fuel_used': oracle['fuel_used']})
    store(out / 'cases.json', cases)
    build_variant(out, 'baseline', baseline, cases)
    pilot = []
    for case in cases:
        probe = sample(out, 'baseline', case, 'pipeline', 1)
        reps = max(1, min(20000, math.ceil(20000000 / max(1, probe['elapsed_ns']))))
        case['repetitions'] = reps
        for mode in ('pipeline', 'core'):
            for _ in range(3):
                pilot.append(sample(out, 'baseline', case, mode, reps))
    store(out / 'pilot.json', pilot)
    protocol = {'schema': 'apodexis.runtime_memory_protocol.v1', 'cases': cases,
        'rounds': 9, 'pipeline_regression_limit': 0.10,
        'core_regression_limit': 0.10, 'core_gate_minimum_baseline_ns': 10000,
        'finish_stress_regression_limit': 0.20,
        'rss_regression_limit': 0.10, 'rss_absolute_allowance_kib': 1024,
        'target_required_speedup': 1.20, 'target_case': 'builder-4096',
        'control_repeat_rule': 'one fresh 9-round rotated comparison if a control fails; preserve both',
        'candidate_policy': 'never change thresholds after candidate samples; reject a confirmed failing candidate',
        'layers': {'pipeline': 'in-process parse, generated entry, encode, validation, reset; excludes build/launch/sandbox',
                   'core': 'generated entry only, with separate reset timing; JSON setup/encoding excluded'},
        'cpu_affinity': sorted(os.sched_getaffinity(0)) if hasattr(os, 'sched_getaffinity') else None,
        'platform': platform.platform(), 'compiler': run(['cc', '--version']).splitlines()[0],
        'harness_sha256': digest(out / 'repeat.c'), 'runner_sha256': digest(__file__),
        'frozen_before_candidate': True}
    store(out / 'protocol.json', protocol)
    print(json.dumps({'prepared_cases': len(cases), 'pilot_samples': len(pilot),
                      'protocol': str(out / 'protocol.json')}))


def compare(out, base, candidate, label):
    protocol = load(out / 'protocol.json')
    cases = protocol['cases']
    for name in (base, candidate):
        build_variant(out, name, out, cases)  # validates existing immutable artifacts
    raw = []
    for round_number in range(protocol['rounds']):
        order = (base, candidate) if round_number % 2 == 0 else (candidate, base)
        rotated = cases[round_number % len(cases):] + cases[:round_number % len(cases)]
        for case in rotated:
            for mode in ('pipeline', 'core'):
                for variant in order:
                    row = sample(out, variant, case, mode, case['repetitions'])
                    row['round'] = round_number
                    raw.append(row)
    store(out / f'{label}.samples.json', raw)
    counters = [sample(out, variant, case, 'pipeline', 1, True)
                for variant in (base, candidate) for case in cases]
    store(out / f'{label}.counters.json', counters)
    summary = []
    failed = []
    for case in cases:
        result = {'case': case['id'], 'group': case['group']}
        for mode, key in [('pipeline', 'elapsed_ns'), ('core', 'core_ns')]:
            values = {}
            for variant in (base, candidate):
                rows = [r for r in raw if r['case'] == case['id'] and r['variant'] == variant and r['mode'] == mode]
                assert len(rows) == protocol['rounds']
                values[variant] = statistics.median(r[key] / r['repetitions'] for r in rows)
            ratio = values[candidate] / values[base]
            result[mode] = {'base_ns': values[base], 'candidate_ns': values[candidate], 'ratio': ratio}
            threshold = (protocol['finish_stress_regression_limit'] if case['group'] == 'finish_stress'
                         else protocol[f'{mode}_regression_limit'])
            gate = case['group'] != 'target' and (mode != 'core' or values[base] >= protocol['core_gate_minimum_baseline_ns'])
            if gate and ratio > 1 + threshold:
                failed.append(f'{case["id"]}:{mode}')
        rss = {variant: max(r['peak_rss_kib'] for r in raw
                           if r['case'] == case['id'] and r['variant'] == variant)
               for variant in (base, candidate)}
        result['peak_rss_kib'] = rss
        if rss[candidate] > rss[base] * (1 + protocol['rss_regression_limit']) + protocol['rss_absolute_allowance_kib']:
            failed.append(f'{case["id"]}:rss')
        summary.append(result)
    result = {'schema': 'apodexis.runtime_memory_comparison.v1', 'base': base, 'candidate': candidate,
              'protocol_sha256': digest(out / 'protocol.json'), 'sample_count': len(raw),
              'counter_sample_count': len(counters), 'summary': summary, 'failed_controls': failed,
              'control_gates_passed': not failed}
    store(out / f'{label}.summary.json', result)
    print(json.dumps(result, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    sub = parser.add_subparsers(dest='operation', required=True)
    p = sub.add_parser('prepare'); p.add_argument('--baseline', required=True)
    p = sub.add_parser('variant'); p.add_argument('name'); p.add_argument('repository')
    p = sub.add_parser('compare'); p.add_argument('base'); p.add_argument('candidate'); p.add_argument('--label', required=True)
    args = parser.parse_args()
    if hasattr(os, 'sched_getaffinity'):
        os.sched_setaffinity(0, {max(os.sched_getaffinity(0))})
    out = args.output.resolve()
    if args.operation == 'prepare':
        prepare(out, args.baseline)
    elif args.operation == 'variant':
        protocol = load(out / 'protocol.json')
        print(json.dumps(build_variant(out, args.name, args.repository, protocol['cases']), indent=2))
    else:
        compare(out, args.base, args.candidate, args.label)


if __name__ == '__main__':
    main()
