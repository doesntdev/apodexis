#!/usr/bin/env python3
"""Negative controls for the optional evidence qualifier, using a real run.

Usage: python3 test_qualification.py /absolute/path/to/evidence
Only JSON is copied; immutable binaries and corpus are read through symlinks.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

HERE = Path(__file__).resolve().parent
if len(sys.argv) < 2:
    raise SystemExit('usage: test_qualification.py evidence-directory')
EVIDENCE = Path(sys.argv.pop(1)).resolve()


class QualificationNegativeControls(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='apodexis-qualification-', dir=os.environ.get('TMPDIR'))
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for path in EVIDENCE.glob('*.json'):
            shutil.copyfile(path, self.root / path.name)
        for name in ('variants', 'corpus', 'repeat.c'):
            (self.root / name).symlink_to(EVIDENCE / name)
        self.raw_path = self.root / 'builders-vs-baseline.samples.json'
        self.rows = json.loads(self.raw_path.read_text(encoding='utf-8'))

    def rejected(self, expected_boundary):
        self.raw_path.write_text(json.dumps(self.rows), encoding='utf-8')
        result = subprocess.run([sys.executable, str(HERE / 'qualify.py'), str(self.root)],
                                capture_output=True, text=True, encoding='utf-8', timeout=20)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('AssertionError', result.stderr)
        self.assertIn(expected_boundary, result.stderr)

    def test_uniform_iteration_drift_across_both_variants(self):
        for row in self.rows:
            row['repetitions'] += 1
        self.rejected("r['repetitions'] == case['repetitions']")

    def test_duplicate_replaces_missing_round_without_changing_total(self):
        self.rows[-1] = dict(self.rows[0])
        self.rejected('len(observed) == len(set(observed))')

    def test_wrong_binary_binding(self):
        self.rows[0]['binary_sha256'] = '0' * 64
        self.rejected("r['binary_sha256'] == manifests")


if __name__ == '__main__':
    unittest.main()
