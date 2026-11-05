#!/usr/bin/env python3
"""Reaggregate sanitized historical observations; not full artifact qualification."""
import json
from pathlib import Path
import statistics
import unittest


class PublishedRuntimeResults(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.evidence = json.loads(Path(__file__).with_name('results.json').read_text(encoding='utf-8'))

    def test_complete_timing_matrix_reaggregates(self):
        evidence = self.evidence
        cases = evidence['summary']['summary']
        rows = evidence['samples']
        keys = [(r['case'], r['variant'], r['mode'], r['round']) for r in rows]
        expected = {(c['case'], variant, mode, number) for c in cases
                    for variant in ('baseline', 'builders')
                    for mode in ('pipeline', 'core') for number in range(9)}
        self.assertEqual(len(keys), len(set(keys)))
        self.assertEqual(set(keys), expected)
        self.assertEqual(len(rows), evidence['summary']['sample_count'])
        for case in cases:
            for mode, elapsed in [('pipeline', 'elapsed_ns'), ('core', 'core_ns')]:
                for variant, field in [('baseline', 'base_ns'), ('builders', 'candidate_ns')]:
                    selected = [r for r in rows if (r['case'], r['variant'], r['mode']) == (case['case'], variant, mode)]
                    self.assertEqual(len({r['repetitions'] for r in selected}), 1)
                    self.assertEqual(statistics.median(r[elapsed] / r['repetitions'] for r in selected), case[mode][field])

    def test_isolated_memory_is_not_legacy_timer_rss(self):
        rows = self.evidence['isolated_memory']
        cases = {c['case'] for c in self.evidence['summary']['summary']}
        keys = [(r['case'], r['variant']) for r in rows]
        self.assertEqual(len(keys), len(set(keys)))
        self.assertEqual(set(keys), {(case, variant) for case in cases for variant in ('baseline', 'builders')})
        self.assertTrue(all(r['max_rss_kib'] > 0 for r in rows))
        self.assertTrue(all('peak_rss_kib' not in r for r in self.evidence['samples']))

    def test_incremental_stress_tradeoff_is_retained(self):
        trade = self.evidence['incremental_finish_tradeoff']
        for mode, elapsed in [('pipeline', 'elapsed_ns'), ('core', 'core_ns')]:
            for variant, field in [('arena-small', 'base_ns'), ('builders', 'candidate_ns')]:
                selected = [r for r in trade['samples'] if (r['variant'], r['mode']) == (variant, mode)]
                self.assertEqual({r['round'] for r in selected}, set(range(9)))
                self.assertEqual(len(selected), 9)
                self.assertEqual(statistics.median(r[elapsed] / r['repetitions'] for r in selected), trade['summary'][mode][field])
        self.assertGreater(trade['summary']['pipeline']['ratio'], 1)


if __name__ == '__main__':
    unittest.main(verbosity=2)
