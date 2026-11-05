#!/usr/bin/env python3
"""Offline contract tests; also reaggregate committed public evidence if present."""
import copy
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock
import run


class AggregationTests(unittest.TestCase):
    def setUp(self):
        case = {'id': 'builder-3', 'kernel': 'builder', 'input': 3,
                'expected': [0, 1, 2], 'source_hash': 'apx1-fixture', 'fuel_used': 77,
                'input_sha256': run.value_hash(3), 'expected_sha256': run.value_hash([0, 1, 2]),
                'repetitions': {language: 10 for language in run.LANGUAGES}}
        commands: dict = {language: [language] for language in run.LANGUAGES}
        commands['Apodexis'] = {'builder': ['Apodexis']}
        self.protocol = {'cases': [case], 'languages': run.LANGUAGES, 'rounds': 3,
                         'warmups': 1000, 'commands': commands,
                         'binary_hashes': {language: 'binary-' + language for language in run.LANGUAGES}}
        self.protocol['binary_hashes']['Apodexis.elf'] = 'elf-fixture'
        self.rows = []
        for round_number in range(self.protocol['rounds']):
            for language in run.LANGUAGES:
                for kind in ('steady', 'oneshot'):
                    row = {'case': case['id'], 'kernel': 'builder', 'language': language,
                           'round': round_number, 'kind': kind, 'output': case['expected'],
                           'output_sha256': run.value_hash(case['expected']),
                           'input_sha256': case['input_sha256'], 'expected_sha256': case['expected_sha256'],
                           'binary_sha256': 'binary-' + language,
                           'repetitions': 10, 'warmups': 1000,
                           'core_ns': 1000 + round_number * 100,
                           'pipeline_ns': 2000 + round_number * 100,
                           'elapsed_ns': 5000 + round_number * 100}
                    if language == 'Apodexis':
                        row.update(source_hash=case['source_hash'], fuel_used=77)
                        if kind == 'oneshot':
                            row['binary_sha256'] = 'elf-fixture'
                    self.rows.append(row)

    def rejects(self, change):
        rows = copy.deepcopy(self.rows)
        change(rows)
        with self.assertRaises(ValueError):
            run.summarize(self.protocol, rows)

    def test_reaggregates_all_languages_and_layers(self):
        result = run.summarize(self.protocol, self.rows)
        self.assertEqual({r['language'] for r in result}, set(run.LANGUAGES))
        self.assertEqual(result[0]['core_ns'], {'median': 110.0, 'min': 100.0, 'max': 120.0, 'samples': 3})
        self.assertEqual(result[0]['elapsed_ns']['median'], 5100.0)

    def test_rejects_duplicate_round(self):
        self.rejects(lambda rows: rows.append(copy.deepcopy(rows[0])))

    def test_rejects_missing_round(self):
        self.rejects(lambda rows: rows.pop())

    def test_rejects_unknown_language(self):
        self.rejects(lambda rows: rows[0].update(language='invented'))

    def test_rejects_unknown_case(self):
        self.rejects(lambda rows: rows[0].update(case='different-input'))

    def test_rejects_wrong_binary(self):
        self.rejects(lambda rows: rows[0].update(binary_sha256='wrong'))

    def test_rejects_uniform_repetition_drift(self):
        def change(rows):
            for row in rows:
                if row['language'] == 'C' and row['kind'] == 'steady':
                    row['repetitions'] = 11
        self.rejects(change)

    def test_rejects_wrong_warmup(self):
        self.rejects(lambda rows: rows[0].update(warmups=999))

    def test_rejects_full_output_change(self):
        self.rejects(lambda rows: rows[0].update(output=[2, 1, 0]))

    def test_rejects_output_hash_change(self):
        self.rejects(lambda rows: rows[0].update(output_sha256='wrong'))

    def test_rejects_fuel_drift(self):
        self.rejects(lambda rows: rows[0].update(fuel_used=78))

    def test_rejects_impossible_timer(self):
        self.rejects(lambda rows: rows[0].update(core_ns=3000))

    def test_public_hash_only_rows_reaggregate_identically(self):
        public = [{k: v for k, v in row.items() if k != 'output'} for row in self.rows]
        self.assertEqual(run.summarize(self.protocol, public), run.summarize(self.protocol, self.rows))

    def test_interrupted_finalization_resumes_without_resampling(self):
        with tempfile.TemporaryDirectory(prefix='apx-bench-test-', dir=os.environ.get('TMPDIR')) as directory:
            out = Path(directory)
            run.store(out / 'protocol.json', self.protocol)
            (out / 'samples.jsonl').write_text(''.join(run.canonical(r) + '\n' for r in self.rows), encoding='utf-8')
            run.store(out / 'summary.json', run.summarize(self.protocol, self.rows))
            with (mock.patch.object(run, 'check_frozen'), mock.patch.object(run, 'add_binary_hashes'),
                  mock.patch.object(run, 'sample', side_effect=AssertionError('unexpected resampling')),
                  mock.patch.object(run, 'oneshot', side_effect=AssertionError('unexpected resampling'))):
                run.measure(out)
                run.verify(out)
                original = run.load(out / 'completion.json')
                run.measure(out)
                run.verify(out)
                self.assertEqual(run.load(out / 'completion.json'), original)

    def test_existing_conflicting_receipt_is_rejected(self):
        with tempfile.TemporaryDirectory(prefix='apx-bench-test-', dir=os.environ.get('TMPDIR')) as directory:
            path = Path(directory) / 'summary.json'
            run.store(path, {'value': 1})
            run.store(path, {'value': 1})
            with self.assertRaises(ValueError):
                run.store(path, {'value': 2})

    def test_corpus_has_independent_expected_values(self):
        cases = run.definitions()
        self.assertEqual(len(cases), 9)
        self.assertEqual({c['kernel'] for c in cases}, set(run.SOURCES))
        for case in cases:
            if case['kernel'] == 'each':
                self.assertEqual(case['expected'], sum(case['input']))
            elif case['kernel'] == 'numeric':
                self.assertEqual(case['expected'], sum(i * 3 + 7 for i in range(case['input'])))


class PublishedEvidenceTests(unittest.TestCase):
    def test_published_tables_reaggregate(self):
        path = Path(__file__).with_name('results.json')
        if not path.exists():
            self.skipTest('No published campaign yet')
        evidence = json.loads(path.read_text(encoding='utf-8'))
        self.assertEqual(run.summarize(evidence['protocol'], evidence['samples']), evidence['summary'])
        for case in evidence['protocol']['cases']:
            self.assertEqual(run.value_hash(case['input']), case['input_sha256'])
            self.assertEqual(run.value_hash(case['expected']), case['expected_sha256'])


if __name__ == '__main__':
    unittest.main(verbosity=2)
