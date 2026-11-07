#!/usr/bin/env python3
"""Reaggregate the fixed MCP observations; never execute or remeasure a tool."""
import hashlib
import json
from pathlib import Path
import statistics

ARMS = ('compile', 'invoke', 'run')
FIELDS = {'arm', 'elapsed_ns', 'position', 'round', 'scored', 'validated'}
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def verify(data, rows, root):
    assert data['schema'] == 'apodexis.mcp_reuse_public_results.v1'
    assert data['status'] == 'PASS' and data['independently_reproduced'] is False
    protocol = data['protocol']
    assert protocol['arms'] == list(ARMS)
    assert type(protocol['warmup_rounds']) is int and protocol['warmup_rounds'] == 5
    assert type(protocol['scored_rounds']) is int and protocol['scored_rounds'] == 31
    assert len(rows) == protocol['total_observations'] == 108
    for row in rows:
        assert set(row) == FIELDS
        assert row['arm'] in ARMS
        assert type(row['elapsed_ns']) is int and row['elapsed_ns'] > 0
        assert type(row['scored']) is bool and row['validated'] is True
        assert type(row['round']) is int and type(row['position']) is int
    for scored, count in [(False, 5), (True, 31)]:
        phase = [row for row in rows if row['scored'] is scored]
        assert len(phase) == 3 * count
        assert {row['round'] for row in phase} == set(range(count))
        for round_index in range(count):
            values = sorted((row for row in phase if row['round'] == round_index),
                            key=lambda row: row['position'])
            assert [row['position'] for row in values] == [0, 1, 2]
            rotation = round_index % 3
            assert tuple(row['arm'] for row in values) == ARMS[rotation:] + ARMS[:rotation]
    summary = {}
    for arm in ARMS:
        values = [row['elapsed_ns'] for row in rows if row['scored'] and row['arm'] == arm]
        assert len(values) == 31
        summary[arm] = {'samples': 31, 'median_ms': statistics.median(values) / 1e6,
                        'min_ms': min(values) / 1e6, 'max_ms': max(values) / 1e6}
    assert summary == data['summary']
    run, invoke = summary['run']['median_ms'], summary['invoke']['median_ms']
    assert data['comparison'] == {
        'run_to_invoke_median_ratio': run / invoke,
        'median_request_latency_reduction_percent': (1 - invoke / run) * 100,
    }
    source = data['source']
    assert hashlib.sha256((root / source['program']).read_bytes()).hexdigest() == source['program_sha256']
    fixture = (root / source['fixture']).read_bytes()
    assert hashlib.sha256(fixture).hexdigest() == source['fixture_sha256']
    case = next(case for case in json.loads(fixture)['cases'] if case['id'] == source['case_id'])
    assert case['id'] == 'development'
    assert case['input'] == data['input'] and case['limits'] == data['limits']
    assert data['correctness']['complete_run_invoke_records_matched'] == 72
    assert data['correctness']['compiled_handles'] == data['correctness']['released_handles'] == 37
    return summary


def main():
    data = json.loads((HERE / 'results.json').read_text(encoding='utf-8'))
    raw = (HERE / 'samples.jsonl').read_bytes()
    assert hashlib.sha256(raw).hexdigest() == data['protocol']['samples_sha256']
    rows = [json.loads(line) for line in raw.splitlines()]
    summary = verify(data, rows, ROOT)
    print(json.dumps({'status': 'PASS', 'observations': len(rows), 'summary': summary}, sort_keys=True))


if __name__ == '__main__':
    main()
