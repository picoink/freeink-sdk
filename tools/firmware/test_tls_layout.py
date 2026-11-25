#!/usr/bin/env python3
"""Regression for an empty .tdata that becomes non-TLS alignment padding."""
import struct
import tempfile
import unittest
from pathlib import Path
from check_tls_layout import check


def elf(data_bytes=8, base_shift=0, memory_delta=0, alignment=8):
    data = bytearray(1024)
    struct.pack_into('<16sHHIIIIIHHHHHH', data, 0,
                     b'\x7fELF\x01\x01\x01'+bytes(9), 2, 243, 1, 0,
                     52, 256, 0, 52, 32, 1, 40, 3, 0)
    base, zeros = 0x40002000, 24
    struct.pack_into('<8I', data, 52, 7, 128, base, base,
                     data_bytes, data_bytes+zeros+memory_delta, 4, alignment)
    names = ('_thread_local_data_start', '_thread_local_data_end',
             '_thread_local_bss_start', '_thread_local_bss_end')
    strings = b'\0'; offsets = []
    for name in names:
        offsets.append(len(strings)); strings += name.encode()+b'\0'
    values = (base-base_shift, base+data_bytes, base+data_bytes, base+data_bytes+zeros)
    for i, (offset, value) in enumerate(zip(offsets, values)):
        struct.pack_into('<IIIBBH', data, 512+i*16, offset, value, 0, 0x10, 0, 0xfff1)
    data[640:640+len(strings)] = strings
    struct.pack_into('<10I', data, 296, 0, 2, 0, 0, 512, 64, 2, 0, 4, 16)
    struct.pack_into('<10I', data, 336, 0, 3, 0, 0, 640, len(strings), 0, 0, 1, 0)
    return data


class LayoutTests(unittest.TestCase):
    def inspect(self, data):
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory)/'firmware.elf'; p.write_bytes(data)
            return check(p)

    def test_initialized_anchor(self):
        self.assertEqual(self.inspect(elf())['data_bytes'], 8)

    def test_aligned_empty_data(self):
        self.assertEqual(self.inspect(elf(data_bytes=0))['zero_bytes'], 24)

    def test_observed_four_byte_padding(self):
        with self.assertRaisesRegex(ValueError, 'layout mismatch'):
            self.inspect(elf(data_bytes=0, base_shift=4))

    def test_wrong_zero_initialization_extent(self):
        with self.assertRaisesRegex(ValueError, 'layout mismatch'):
            self.inspect(elf(memory_delta=8))

    def test_incompatible_alignment(self):
        with self.assertRaisesRegex(ValueError, 'alignment'):
            self.inspect(elf(alignment=16384))

    def test_wrong_architecture(self):
        data = elf(); struct.pack_into('<H', data, 18, 3)
        with self.assertRaisesRegex(ValueError, 'RISC-V'):
            self.inspect(data)


if __name__ == '__main__':
    unittest.main()
