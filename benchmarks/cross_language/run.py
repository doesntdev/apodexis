#!/usr/bin/env python3
"""Optional, stdlib-only local benchmark coordinator; no compiler dependency."""
import argparse
from collections import Counter
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import shutil
import statistics
import subprocess
import sys
import sysconfig
import time
import tomllib

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
CORPUS = ROOT / 'benchmarks/runtime_memory/cases'
LANGUAGES = ['Apodexis', 'C', 'Rust', 'Python', 'JavaScript']
ROUNDS, WARMUPS = 9, 1000
CFLAGS = ['-std=c99', '-O3', '-Wall', '-Wextra', '-Werror']
APXFLAGS = ['-std=c99', '-O2', '-Wall', '-Wextra', '-Werror', '-fno-pie']
SOURCES = {'scalar': 'scalar.apx', 'numeric': 'numeric-helpers.apx',
           'each': 'each-sum.apx', 'builder': 'builder-writeback.apx',
           'rowdiff': 'agent-rowdiff.apx'}


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(',', ':'), ensure_ascii=False)


def value_hash(value):
    return hashlib.sha256(canonical(value).encode('utf-8')).hexdigest()


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def load(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def store(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        if load(path) != value:
            raise ValueError(f'existing receipt differs: {path.name}')
        return
    with path.open('x', encoding='utf-8') as f:
        json.dump(value, f, sort_keys=True, indent=2, ensure_ascii=False)
        f.write('\n')


def invoke(args, data=None, timeout=90, cpu=None):
    command = [str(x) for x in args]
    if cpu is not None:
        command = ['taskset', '-c', str(cpu), *command]
    p = subprocess.run(command, input=data, capture_output=True, text=True,
                       encoding='utf-8', timeout=timeout,
                       env={**os.environ, 'LC_ALL': 'C', 'PYTHONHASHSEED': '0'})
    if p.returncode:
        raise RuntimeError(f'{command!r}: exit {p.returncode}\n{p.stdout}\n{p.stderr}')
    return p.stdout


def oracle_rows(before, after):
    for label, rows in [('before', before), ('after', after)]:
        if len({r['id'] for r in rows}) != len(rows):
            return {'tag': 'err', 'error': f'duplicate id in {label}'}
    b = {r['id']: r['hash'] for r in before}
    a = {r['id']: r['hash'] for r in after}
    return {'tag': 'ok', 'value': {
        'added': [r['id'] for r in after if r['id'] not in b],
        'removed': [r['id'] for r in before if r['id'] not in a],
        'changed': [r['id'] for r in after if r['id'] in b and b[r['id']] != r['hash']]}}


def definitions():
    cases = [{'id': 'scalar', 'kernel': 'scalar', 'input': 7, 'expected': 8}]
    for n in (256, 2048):
        # Closed-form independent oracle; implementations use the explicit loop.
        cases.append({'id': f'numeric-{n}', 'kernel': 'numeric', 'input': n,
                      'expected': 3 * n * (n - 1) // 2 + 7 * n})
    for n in (128, 4096):
        cases.append({'id': f'each-{n}', 'kernel': 'each', 'input': list(range(n)),
                      'expected': n * (n - 1) // 2})
    for n in (256, 4096):
        cases.append({'id': f'builder-{n}', 'kernel': 'builder', 'input': n,
                      'expected': list(range(n))})
    for n in (32, 64):
        before = [{'id': f'id-{i}', 'hash': 'old'} for i in range(n)]
        after = [{'id': f'id-{i}', 'hash': 'new' if i % 3 == 0 else 'old'}
                 for i in range(n // 4, n + n // 4)]
        cases.append({'id': f'rowdiff-{n}', 'kernel': 'rowdiff',
                      'input': {'before': before, 'after': after},
                      'expected': oracle_rows(before, after)})
    return cases


def extra_probes():
    r = lambda ident, h: {'id': ident, 'hash': h}
    probes = [('scalar', -3, -2), ('numeric', 0, 0), ('each', [], 0),
              ('each', [1, -2, 0, 17], 16), ('builder', 0, []), ('builder', 1, [0])]
    pairs = [([], []), ([r('a', 'x')], [r('a', 'y')]),
             ([r('a', 'x'), r('b', 'z')], [r('b', 'z'), r('a', 'x')]),
             ([r('a', 'x'), r('a', 'y')], [r('b', 'z')]),
             ([r('a', 'x')], [r('b', 'y'), r('b', 'z')])]
    probes += [('rowdiff', {'before': b, 'after': a}, oracle_rows(b, a)) for b, a in pairs]
    return probes


def source_paths():
    tracked = invoke(['git', 'ls-files', '-z'], timeout=20).split('\0')
    paths = [ROOT / p for p in tracked if p and
             (p == 'Makefile' or p.startswith(('src/', 'compiler/', 'include/', 'runtime/', 'vendor/', 'third_party/')))]
    paths += [p for p in HERE.rglob('*') if p.is_file() and
              (p.suffix in ('.c', '.rs', '.mjs', '.py') or p.name in ('Cargo.toml', 'Cargo.lock'))
              and '__pycache__' not in p.parts]
    paths += [CORPUS / name for name in SOURCES.values()]
    return sorted(set(paths))


def build(out):
    build_dir = out / 'build'
    build_dir.mkdir(parents=True)
    commands = {}
    invoke(['cc', *CFLAGS, '-DAPX_LIBRARY_ONLY', HERE / 'runner.c',
            ROOT / 'runtime/apx_runtime.c', '-lm', '-o', build_dir / 'c-runner'])
    commands['C'] = [str(build_dir / 'c-runner')]
    invoke(['cargo', 'build', '--offline', '--locked', '--release', '--manifest-path',
            HERE / 'rust/Cargo.toml', '--target-dir', build_dir / 'rust'], timeout=240)
    package = tomllib.loads((HERE / 'rust/Cargo.toml').read_text(encoding='utf-8'))['package']['name']
    commands['Rust'] = [str(build_dir / 'rust/release' / package)]
    commands['Python'] = [sys.executable, str(HERE / 'runner.py')]
    commands['JavaScript'] = [shutil.which('node'), str(HERE / 'runner.mjs')]
    apx = {}
    for kernel, name in SOURCES.items():
        stem = build_dir / kernel
        stem.with_suffix('.ssa').write_text(invoke([ROOT / 'bin/apodexis', 'emit-qbe', CORPUS / name]),
                                           encoding='utf-8')
        invoke([ROOT / '.build/tools/qbe', '-o', stem.with_suffix('.s'), stem.with_suffix('.ssa')])
        invoke(['as', stem.with_suffix('.s'), '-o', stem.with_suffix('.o')])
        invoke(['cc', *APXFLAGS, f'-DAPX_RUNTIME_SOURCE="{ROOT / "runtime/apx_runtime.c"}"',
                '-c', HERE / 'apx_repeat.c', '-o', build_dir / 'apx-repeat.o']) if not apx else None
        invoke(['cc', '-no-pie', build_dir / 'apx-repeat.o', stem.with_suffix('.o'),
                '-lm', '-o', build_dir / f'apx-{kernel}'])
        invoke([ROOT / 'bin/apodexis', 'build', CORPUS / name,
                '-o', build_dir / f'apx-{kernel}.elf'])
        apx[kernel] = [str(build_dir / f'apx-{kernel}')]
    commands['Apodexis'] = apx
    return commands


def sample(out, protocol, case, language, repetitions, warmups):
    command = protocol['commands'][language]
    if language == 'Apodexis':
        command = command[case['kernel']]
    row = json.loads(invoke([*command, '--sample', case['kernel'],
        out / 'fixtures' / (case['id'] + '.input.json'),
        out / 'fixtures' / (case['id'] + '.expected.json'), repetitions, warmups],
        cpu=protocol['cpu']))
    if row.get('schema') != 'apodexis.cross_language_sample.v1':
        raise ValueError('wrong sample schema')
    row.update(case=case['id'], kind='steady', output_sha256=value_hash(row.get('output')),
               binary_sha256=digest(command[0]),
               input_sha256=case['input_sha256'], expected_sha256=case['expected_sha256'])
    validate_sample(protocol, case, language, row, repetitions, warmups)
    return row


def oneshot(protocol, case, language):
    if language == 'Apodexis':
        command = [protocol['commands'][language][case['kernel']][0] + '.elf', '--fuel', '10000000']
    else:
        command = [*protocol['commands'][language], '--once', case['kernel']]
    # Input preparation and output correctness checking are outside this timer.
    text = canonical(case['input'])
    start = time.perf_counter_ns()
    raw = invoke(command, text, cpu=protocol['cpu'])
    elapsed = time.perf_counter_ns() - start
    decoded = json.loads(raw)
    if language == 'Apodexis':
        if decoded.get('status') != 'ok' or decoded['source_hash'] != case['source_hash'] or decoded['fuel_used'] != case['fuel_used']:
            raise ValueError('standard ELF execution identity or fuel mismatch')
        decoded = decoded['output']
    if canonical(decoded) != canonical(case['expected']):
        raise ValueError(f'one-shot mismatch: {language}/{case["id"]}')
    return {'case': case['id'], 'language': language, 'kernel': case['kernel'],
            'kind': 'oneshot', 'elapsed_ns': elapsed, 'output': decoded,
            'output_sha256': value_hash(decoded), 'binary_sha256': digest(command[0]),
            'input_sha256': case['input_sha256'], 'expected_sha256': case['expected_sha256']}


def validate_sample(protocol, case, language, row, reps, warmups):
    if (row.get('language') != language or row.get('kernel') != case['kernel'] or
        row.get('repetitions') != reps or row.get('warmups') != warmups or
        canonical(row.get('output')) != canonical(case['expected'])):
        raise ValueError('sample identity/count/output mismatch')
    for key in ('core_ns', 'pipeline_ns'):
        if type(row.get(key)) is not int or row[key] <= 0:
            raise ValueError('invalid measurement total')
    if row['core_ns'] > row['pipeline_ns']:
        raise ValueError('core exceeds enclosing pipeline')
    if language == 'Apodexis' and (row.get('fuel_used') != case['fuel_used'] or row.get('source_hash') != case['source_hash']):
        raise ValueError('Apodexis execution contract drift')


def check_frozen(out, protocol):
    if digest(out / 'protocol.json') != (out / 'protocol.sha256').read_text(encoding='utf-8').strip():
        raise ValueError('frozen protocol changed')
    for rel, expected in protocol['sources'].items():
        if digest(ROOT / rel) != expected:
            raise ValueError(f'measured source changed: {rel}')
    for rel, expected in protocol['artifacts'].items():
        if digest(out / rel) != expected:
            raise ValueError(f'measured artifact changed: {rel}')
    for executable, expected in protocol['interpreters'].items():
        if digest(executable) != expected:
            raise ValueError('interpreter changed')


def prepare(out, cpu):
    out.mkdir(parents=True, exist_ok=False)
    protocol = {'schema': 'apodexis.cross_language_protocol.v1', 'rounds': ROUNDS,
                'warmups': WARMUPS, 'languages': LANGUAGES, 'cpu': cpu}
    commands = build(out)
    protocol['commands'] = commands
    cases = definitions()
    for case in cases:
        for suffix, value in [('input', case['input']), ('expected', case['expected'])]:
            path = out / 'fixtures' / f'{case["id"]}.{suffix}.json'
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(canonical(value), encoding='utf-8')
            case[suffix + '_sha256'] = digest(path)
        result = json.loads(invoke([commands['Apodexis'][case['kernel']][0] + '.elf', '--fuel', '10000000'],
                                   canonical(case['input']), cpu=cpu))
        if result.get('status') != 'ok' or canonical(result.get('output')) != canonical(case['expected']):
            raise ValueError(f'Apodexis independent oracle mismatch: {case["id"]}')
        case['source_hash'], case['fuel_used'] = result['source_hash'], result['fuel_used']
        case['source_sha256'] = digest(CORPUS / SOURCES[case['kernel']])
        for language in LANGUAGES:
            oneshot(protocol, case, language)
    probes = []
    for kernel, value, expected in extra_probes():
        for language in LANGUAGES:
            case = {'id': 'probe', 'kernel': kernel, 'input': value, 'expected': expected,
                    'input_sha256': value_hash(value), 'expected_sha256': value_hash(expected)}
            if language == 'Apodexis':
                result = json.loads(invoke([commands[language][kernel][0] + '.elf', '--fuel', '10000000'], canonical(value), cpu=cpu))
                if result.get('status') != 'ok' or canonical(result['output']) != canonical(expected):
                    raise ValueError('Apodexis correctness probe failed')
            else:
                oneshot(protocol, case, language)
            probes.append({'kernel': kernel, 'language': language, 'input': value, 'expected': expected, 'passed': True})
    store(out / 'probes.json', probes)
    negatives = []
    wrong_path = out / 'wrong-expected.json'
    wrong_path.write_text('null', encoding='utf-8')
    for language in LANGUAGES:
        for case in {c['kernel']: c for c in cases}.values():
            argv = commands[language][case['kernel']] if language == 'Apodexis' else commands[language]
            for count, expected_path in [(1, wrong_path), (0, out / 'fixtures' / (case['id'] + '.expected.json'))]:
                try:
                    invoke([*argv, '--sample', case['kernel'], out / 'fixtures' / (case['id'] + '.input.json'), expected_path, count, 0], cpu=cpu)
                except RuntimeError:
                    negatives.append({'language': language, 'kernel': case['kernel'], 'probe': 'wrong-output' if count else 'zero-repetitions', 'rejected': True})
                else:
                    raise ValueError(f'negative check accepted: {language}/{case["kernel"]}/{count}')
    store(out / 'negative-probes.json', negatives)
    pilot = []
    for case in cases:
        case['repetitions'] = {}
        for language in LANGUAGES:
            row = sample(out, protocol, case, language, 10, WARMUPS)
            pilot.append(row)
            case['repetitions'][language] = max(5, min(100000, math.ceil(30000000 / (row['pipeline_ns'] / 10))))
    store(out / 'pilot.json', pilot)
    protocol.update(cases=cases, c_flags=CFLAGS, apodexis_harness_flags=APXFLAGS,
                    frozen_before_scoring=True,
                    platform={'os': platform.system(), 'release': platform.release(), 'machine': platform.machine(),
                              'cpu': next(item['data'] for item in json.loads(invoke(['lscpu', '-J']))['lscpu'] if item['field'] == 'Model name:'),
                              'cc': invoke(['cc', '--version']).splitlines()[0],
                              'rustc': invoke(['rustc', '--version']).strip(),
                              'cargo': invoke(['cargo', '--version']).strip(),
                              'python': sys.version, 'python_jit': os.environ.get('PYTHON_JIT', 'default-off'),
                              'python_gil_disabled': sysconfig.get_config_var('Py_GIL_DISABLED'),
                              'node': invoke(['node', '--version']).strip(),
                              'glibc': platform.libc_ver(), 'load_before': os.getloadavg(),
                              'governor': Path(f'/sys/devices/system/cpu/cpu{cpu}/cpufreq/scaling_governor').read_text().strip()},
                    sources={str(p.relative_to(ROOT)): digest(p) for p in source_paths()},
                    artifacts={str(p.relative_to(out)): digest(p) for p in sorted(out.rglob('*')) if p.is_file()},
                    interpreters={path: digest(path) for path in [sys.executable, shutil.which('node')]})
    store(out / 'protocol.json', protocol)
    (out / 'protocol.sha256').write_text(digest(out / 'protocol.json') + '\n', encoding='utf-8')
    print(canonical({'prepared_cases': len(cases), 'pilot_samples': len(pilot),
                     'correctness_probes': len(probes), 'negative_probes': len(negatives), 'protocol': str(out / 'protocol.json')}))


def read_rows(path):
    if not Path(path).exists():
        return []
    with Path(path).open(encoding='utf-8') as f:
        return [json.loads(line) for line in f if line.strip()]


def summarize(protocol, rows, require_complete=True):
    cases = {case['id']: case for case in protocol['cases']}
    expected_keys = {(i, kind, case, lang) for i in range(protocol['rounds'])
                     for kind in ('steady', 'oneshot') for case in cases for lang in protocol['languages']}
    seen = Counter()
    for row in rows:
        key = (row.get('round'), row.get('kind'), row.get('case'), row.get('language'))
        if key not in expected_keys:
            raise ValueError('unexpected case/language/round/kind')
        seen[key] += 1
        if seen[key] != 1:
            raise ValueError('duplicate measurement')
        case, language = cases[row['case']], row['language']
        if (row.get('output_sha256') != value_hash(case['expected']) or
            row.get('input_sha256') != case['input_sha256'] or row.get('expected_sha256') != case['expected_sha256']):
            raise ValueError('fixture/output identity mismatch')
        if 'output' in row and canonical(row['output']) != canonical(case['expected']):
            raise ValueError('full output mismatch')
        command = protocol['commands'][language]
        if language == 'Apodexis':
            command = command[case['kernel']]
        binary = command[0] + ('.elf' if language == 'Apodexis' and row['kind'] == 'oneshot' else '')
        expected_binary = protocol['binary_hashes'][binary]
        if row.get('binary_sha256') != expected_binary:
            raise ValueError('binary identity mismatch')
        if row['kind'] == 'steady':
            # Public exports retain the verified hash, not repeated large arrays.
            checked = {**row, 'output': case['expected']} if 'output' not in row else row
            validate_sample(protocol, case, language, checked,
                            case['repetitions'][language], protocol['warmups'])
        elif type(row.get('elapsed_ns')) is not int or row['elapsed_ns'] <= 0:
            raise ValueError('invalid one-shot elapsed time')
    if require_complete and set(seen) != expected_keys:
        raise ValueError('incomplete measurement matrix')
    result = []
    for case in protocol['cases']:
        for language in protocol['languages']:
            item = {'case': case['id'], 'language': language}
            for metric in ('core_ns', 'pipeline_ns', 'elapsed_ns'):
                kind = 'oneshot' if metric == 'elapsed_ns' else 'steady'
                matched = [r for r in rows if r['case'] == case['id'] and r['language'] == language and r['kind'] == kind]
                if not matched:
                    continue
                values = [r[metric] / (r['repetitions'] if kind == 'steady' else 1) for r in matched]
                item[metric] = {'median': statistics.median(values), 'min': min(values), 'max': max(values), 'samples': len(values)}
            result.append(item)
    return result


def add_binary_hashes(protocol):
    hashes = {}
    for language, command in protocol['commands'].items():
        if language == 'Apodexis':
            for argv in command.values():
                for path in (argv[0], argv[0] + '.elf'):
                    hashes[path] = digest(path)
        else:
            hashes[command[0]] = digest(command[0])
    protocol['binary_hashes'] = hashes


def measure(out):
    protocol = load(out / 'protocol.json')
    check_frozen(out, protocol)
    add_binary_hashes(protocol)
    rows_path = out / 'samples.jsonl'
    rows = read_rows(rows_path)
    summarize(protocol, rows, require_complete=False)
    done = {(r['round'], r['kind'], r['case'], r['language']) for r in rows}
    with rows_path.open('a', encoding='utf-8') as f:
        cases = protocol['cases']
        for round_number in range(protocol['rounds']):
            check_frozen(out, protocol)
            ordered = cases[round_number % len(cases):] + cases[:round_number % len(cases)]
            for index, case in enumerate(ordered):
                offset = (round_number + index) % len(LANGUAGES)
                order = LANGUAGES[offset:] + LANGUAGES[:offset]
                for kind in ('steady', 'oneshot'):
                    for language in order:
                        key = (round_number, kind, case['id'], language)
                        if key in done:
                            continue
                        row = (sample(out, protocol, case, language, case['repetitions'][language], protocol['warmups'])
                               if kind == 'steady' else oneshot(protocol, case, language))
                        row['round'] = round_number
                        f.write(canonical(row) + '\n')
                        f.flush()
                        rows.append(row)
                        done.add(key)
            print(f'completed round {round_number + 1}/{protocol["rounds"]}', flush=True)
    check_frozen(out, protocol)
    summary = summarize(protocol, rows)
    store(out / 'summary.json', summary)
    completion_path = out / 'completion.json'
    load_after = load(completion_path)['load_after'] if completion_path.exists() else os.getloadavg()
    store(completion_path, {'scored_rows': len(rows), 'load_after': list(load_after),
                            'protocol_sha256': digest(out / 'protocol.json'),
                            'samples_sha256': digest(rows_path)})
    print(canonical({'scored_rows': len(rows), 'summary_rows': len(summary)}))


def verify(out):
    protocol = load(out / 'protocol.json')
    check_frozen(out, protocol)
    add_binary_hashes(protocol)
    rows = read_rows(out / 'samples.jsonl')
    result = summarize(protocol, rows)
    if result != load(out / 'summary.json'):
        raise ValueError('stored aggregate differs from raw samples')
    completion = load(out / 'completion.json')
    if completion['protocol_sha256'] != digest(out / 'protocol.json') or completion['samples_sha256'] != digest(out / 'samples.jsonl'):
        raise ValueError('completion digest mismatch')
    if completion['scored_rows'] != len(rows):
        raise ValueError('completion count mismatch')
    print(canonical({'verified': True, 'cases': len(protocol['cases']), 'languages': protocol['languages'],
                     'scored_rows': len(rows), 'summary_rows': len(result)}))


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cpu', type=int, default=min(os.sched_getaffinity(0)))
    parser.add_argument('command', choices=['prepare', 'measure', 'verify'])
    args = parser.parse_args()
    if args.cpu not in os.sched_getaffinity(0):
        parser.error('CPU is outside current affinity')
    os.chdir(ROOT)
    out = args.output.resolve()
    if args.command == 'prepare':
        prepare(out, args.cpu)
    else:
        {'measure': measure, 'verify': verify}[args.command](out)


if __name__ == '__main__':
    main()
