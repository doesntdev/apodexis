#!/usr/bin/env python3
"""Independently reaggregate frozen samples and qualify exact native artifacts.

Memory samples use GNU time's small C parent, avoiding an inherited Python
high-water RSS floor in the timed harness's getrusage result. Original samples
are retained unmodified. This optional program is not a compiler dependency.
"""
import argparse
import itertools
import json
import os
from pathlib import Path
import re
import statistics
import subprocess

from run import digest, load, store, HERE

VARIANTS = ('baseline', 'arena-small', 'builders')
COMPARISONS = ('arena-small-v1', 'builders-vs-arena', 'builders-vs-baseline')


def checked_process(args, data=None):
    p = subprocess.run([str(x) for x in args], input=data, text=True,
                       encoding='utf-8', capture_output=True, timeout=30,
                       env={**os.environ, 'LC_ALL': 'C'})
    if p.returncode:
        raise RuntimeError(f'qualification child failed: {args}\n{p.stdout}\n{p.stderr}')
    matches = re.findall(r'^APX_RSS=(\d+)$', p.stderr, re.MULTILINE)
    if len(matches) != 1:
        raise RuntimeError(f'missing GNU time RSS receipt: {p.stderr}')
    return json.loads(p.stdout), int(matches[0])


def collect(out, protocol):
    memory = []
    elfs = []
    for variant in VARIANTS:
        target = out / 'variants' / variant
        for case in protocol['cases']:
            ident = case['id']
            exe = target / f'{ident}-timing'
            data, rss = checked_process(['/usr/bin/time', '-f', 'APX_RSS=%M', exe,
                out / 'corpus' / f'{ident}.input.json',
                out / 'corpus' / f'{ident}.expected.json', case['repetitions'], 'pipeline'])
            assert data['fuel_used'] == case['fuel_used'] and data['source_hash'] == case['source_hash']
            memory.append({'case': ident, 'variant': variant, 'max_rss_kib': rss,
                           'binary_sha256': digest(exe), 'sample': data})
            if variant in ('baseline', 'builders'):
                exe = target / f'{ident}.elf'
                source_input = (out / 'corpus' / f'{ident}.input.json').read_text(encoding='utf-8')
                expected = load(out / 'corpus' / f'{ident}.expected.json')
                for round_number in range(3):
                    data, rss = checked_process(['/usr/bin/time', '-f', 'APX_RSS=%M', exe,
                                                 '--fuel', '10000000'], source_input)
                    assert data['status'] == 'ok' and data['output'] == expected
                    assert data['fuel_used'] == case['fuel_used'] and data['source_hash'] == case['source_hash']
                    elfs.append({'case': ident, 'variant': variant, 'round': round_number,
                                 'max_rss_kib': rss, 'binary_sha256': digest(exe),
                                 'fuel_used': data['fuel_used'], 'source_hash': data['source_hash'],
                                 'output_verified': True})
    store(out / 'isolated-memory.json', memory)
    store(out / 'native-elf-parity.json', elfs)


def verify(out, collect_memory):
    protocol = load(out / 'protocol.json')
    cases = {c['id']: c for c in protocol['cases']}
    assert len(cases) == len(protocol['cases']) and cases
    assert digest(HERE / 'run.py') == protocol['runner_sha256'], 'timing producer changed'
    assert digest(out / 'repeat.c') == protocol['harness_sha256']
    manifests = {}
    for variant in VARIANTS:
        manifest = load(out / 'variants' / variant / 'manifest.json')
        assert manifest['name'] == variant and manifest['harness_sha256'] == protocol['harness_sha256']
        for rel, sha in manifest['files'].items():
            assert digest(out / 'variants' / variant / rel) == sha, (variant, rel)
        manifests[variant] = manifest
    for ident, case in cases.items():
        for suffix, key in [('apx', 'source_sha256'), ('input.json', 'input_sha256'),
                            ('expected.json', 'expected_sha256')]:
            assert digest(out / 'corpus' / f'{ident}.{suffix}') == case[key]
    recomputed = {}
    counts = {}
    for label in COMPARISONS:
        report = load(out / f'{label}.summary.json')
        rows = load(out / f'{label}.samples.json')
        base, candidate = report['base'], report['candidate']
        expected_keys = set(itertools.product(cases, (base, candidate), ('pipeline', 'core'),
                                             range(protocol['rounds'])))
        observed = [(r['case'], r['variant'], r['mode'], r['round']) for r in rows]
        assert len(observed) == len(set(observed)) and set(observed) == expected_keys
        assert len(rows) == report['sample_count']
        for r in rows:
            case = cases[r['case']]
            assert r['repetitions'] == case['repetitions'] and not r['instrumented']
            assert r['fuel_used'] == case['fuel_used'] and r['source_hash'] == case['source_hash']
            assert r['binary_sha256'] == manifests[r['variant']]['files'][f"{r['case']}-timing"]
        failed = []
        values = {}
        for ident, case in cases.items():
            values[ident] = {}
            for mode, field in [('pipeline', 'elapsed_ns'), ('core', 'core_ns')]:
                medians = {variant: statistics.median(r[field] / r['repetitions'] for r in rows
                           if r['case'] == ident and r['variant'] == variant and r['mode'] == mode)
                           for variant in (base, candidate)}
                ratio = medians[candidate] / medians[base]
                claimed = next(x for x in report['summary'] if x['case'] == ident)[mode]
                assert abs(claimed['ratio'] - ratio) < 1e-12
                gate = case['group'] != 'target' or ident in ('builder-0', 'builder-1')
                gate = gate and (mode != 'core' or medians[base] >= protocol['core_gate_minimum_baseline_ns'])
                threshold = (protocol['finish_stress_regression_limit'] if case['group'] == 'finish_stress'
                             else protocol[f'{mode}_regression_limit'])
                if gate and ratio > 1 + threshold:
                    failed.append(f'{ident}:{mode}')
                values[ident][mode] = {'baseline_ns': medians[base], 'candidate_ns': medians[candidate],
                                       'speedup': 1 / ratio, 'ratio': ratio}
        assert not failed, (label, failed)
        counters = load(out / f'{label}.counters.json')
        assert len(counters) == report['counter_sample_count'] == len(cases) * 2
        assert {(r['case'], r['variant']) for r in counters} == set(itertools.product(cases, (base, candidate)))
        for r in counters:
            case = cases[r['case']]
            assert r['instrumented'] and r['repetitions'] == 1
            assert r['fuel_used'] == case['fuel_used'] and r['source_hash'] == case['source_hash']
            assert r['binary_sha256'] == manifests[r['variant']]['files'][f"{r['case']}-counters"]
            if r['variant'] == 'builders':
                assert r['builder_prefix_memcpy_bytes'] == 0
        counts[label] = {'timing_samples': len(rows), 'counter_samples': len(counters)}
        recomputed[label] = values
    assert recomputed['builders-vs-baseline'][protocol['target_case']]['pipeline']['speedup'] >= protocol['target_required_speedup']
    if collect_memory:
        collect(out, protocol)
    memory = load(out / 'isolated-memory.json')
    assert len(memory) == len(cases) * len(VARIANTS)
    assert {(r['case'], r['variant']) for r in memory} == set(itertools.product(cases, VARIANTS))
    for r in memory:
        assert r['binary_sha256'] == manifests[r['variant']]['files'][f"{r['case']}-timing"]
        case = cases[r['case']]
        assert r['sample']['fuel_used'] == case['fuel_used'] and r['sample']['source_hash'] == case['source_hash']
        assert r['sample']['repetitions'] == case['repetitions'] and r['max_rss_kib'] > 0
    rss_failures = []
    for base, candidate in [('baseline', 'arena-small'), ('arena-small', 'builders'), ('baseline', 'builders')]:
        for ident in cases:
            rss = {v: next(r['max_rss_kib'] for r in memory if r['case'] == ident and r['variant'] == v)
                   for v in (base, candidate)}
            if rss[candidate] > rss[base] * (1 + protocol['rss_regression_limit']) + protocol['rss_absolute_allowance_kib']:
                rss_failures.append(f'{base}/{candidate}:{ident}')
    assert not rss_failures, rss_failures
    elfs = load(out / 'native-elf-parity.json')
    assert len(elfs) == len(cases) * 2 * 3
    assert {(r['case'], r['variant'], r['round']) for r in elfs} == set(itertools.product(cases, ('baseline', 'builders'), range(3)))
    for r in elfs:
        case = cases[r['case']]
        assert r['output_verified'] and r['fuel_used'] == case['fuel_used'] and r['source_hash'] == case['source_hash']
        assert r['binary_sha256'] == manifests[r['variant']]['files'][f"{r['case']}.elf"]
    result = {'schema': 'apodexis.runtime_memory_qualification.v1', 'status': 'PASS',
              'protocol_sha256': digest(out / 'protocol.json'), 'qualifier_sha256': digest(__file__),
              'counts': counts, 'isolated_memory_samples': len(memory), 'native_elf_parity_runs': len(elfs),
              'runtime_sha256': manifests['builders']['runtime_sha256'],
              'timing_rss_note': 'Original timed getrusage samples retain launcher high-water floors; use isolated GNU time memory receipts.',
              'comparisons': recomputed, 'isolated_memory': memory}
    store(out / 'qualification.json', result)
    print(json.dumps({'status': result['status'], 'counts': counts,
                      'isolated_memory_samples': len(memory), 'native_elf_parity_runs': len(elfs),
                      'builder4096': recomputed['builders-vs-baseline']['builder-4096'],
                      'builder4096_rss_kib': {r['variant']: r['max_rss_kib'] for r in memory if r['case'] == 'builder-4096'}}, indent=2))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('evidence', type=Path)
    p.add_argument('--collect-memory', action='store_true')
    args = p.parse_args()
    if hasattr(os, 'sched_getaffinity'):
        os.sched_setaffinity(0, {max(os.sched_getaffinity(0))})
    verify(args.evidence.resolve(), args.collect_memory)


if __name__ == '__main__':
    main()
