#!/usr/bin/env python3
"""Check mask polarity, row padding, coverage and reproducible checked-in assets."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

from PIL import Image
from gen_icons import pack, rasterize

ROOT = Path(__file__).resolve().parents[1]
UI = ROOT.parents[1] / 'ui/FreeInkUI/include'


class IconsTest(unittest.TestCase):
    def test_all_coverage_levels_and_odd_row_padding(self):
        img = Image.new('L', (17, 17), 255)
        for x in range(16):
            img.putpixel((x, 3), 255 - x * 17)
        data, center = pack(img, 17)
        self.assertEqual(len(data), 9 * 17)
        self.assertEqual(data[27:36], [0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0])
        self.assertFalse(any(data[:27] + data[36:]))
        self.assertEqual(center, 3)

    def test_pinned_sources_and_antialiased_edges(self):
        metadata = json.loads((ROOT / 'reader/SOURCES.json').read_text())
        for entry in metadata['files']:
            source = ROOT / 'reader/svg' / entry['file']
            self.assertEqual(hashlib.sha256(source.read_bytes()).hexdigest(), entry['sha256'])
            data, _ = pack(rasterize(str(source), 40), 40)
            levels = {n for b in data for n in (b >> 4, b & 15)}
            self.assertIn(0, levels)
            self.assertIn(15, levels)
            self.assertGreater(len(levels), 2, source.name)

    def test_checked_in_headers(self):
        for manifest, sizes, header, extra in [
            ('reader', '24,40', 'FreeInkUIReaderIcons.h', []),
            ('keyboard', '24,28,32', 'FreeInkUIKeyboardIconData.h', ['--raw']),
        ]:
            with tempfile.TemporaryDirectory() as temp:
                output = Path(temp) / header
                subprocess.run([sys.executable, str(ROOT / 'tools/gen_icons.py'),
                                '--manifest', str(ROOT / f'manifests/{manifest}.txt'),
                                '--svgdir', str(ROOT / 'reader/svg'), '--sizes', sizes,
                                '--out', str(output), *extra], check=True)
                self.assertEqual(output.read_bytes(), (UI / header).read_bytes())


if __name__ == '__main__':
    unittest.main()
