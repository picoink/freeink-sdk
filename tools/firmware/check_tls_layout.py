#!/usr/bin/env python3
"""Reject RISC-V firmware whose FreeRTOS TLS copy disagrees with ELF TLS."""
import argparse
import struct
from pathlib import Path


def check(path):
    data = Path(path).read_bytes()
    if data[:6] != b'\x7fELF\x01\x01':
        raise ValueError('Expected a little-endian ELF32 firmware')
    h = struct.unpack_from('<16sHHIIIIIHHHHHH', data)
    if h[2] != 243:
        raise ValueError('Expected RISC-V firmware')
    phoff, shoff, phsize, phnum, shsize, shnum = h[5], h[6], h[9], h[10], h[11], h[12]
    programs = [struct.unpack_from('<8I', data, phoff+i*phsize) for i in range(phnum)]
    tls = [p for p in programs if p[0] == 7]
    if len(tls) != 1:
        raise ValueError('Expected exactly one PT_TLS segment')
    p = tls[0]
    sections = [struct.unpack_from('<10I', data, shoff+i*shsize) for i in range(shnum)]
    symbols = {}
    for section in sections:
        if section[1] != 2:
            continue
        names = sections[section[6]]
        strings = data[names[4]:names[4]+names[5]]
        for offset in range(section[4], section[4]+section[5], section[9]):
            name, value, size, info, other, index = struct.unpack_from('<IIIBBH', data, offset)
            end = strings.find(b'\0', name)
            symbols[strings[name:end].decode('utf-8', errors='strict')] = value
    labels = ('_thread_local_data_start', '_thread_local_data_end',
              '_thread_local_bss_start', '_thread_local_bss_end')
    try:
        start, data_end, bss_start, end = (symbols[n] for n in labels)
    except KeyError as error:
        raise ValueError(f'Missing FreeRTOS TLS linker symbol: {error}') from error
    expected = (start, data_end-start, end-start)
    actual = (p[2], p[4], p[5])
    if actual != expected or data_end != bss_start:
        raise ValueError(f'FreeRTOS TLS layout mismatch: PT_TLS={actual}, copy={expected}; '
                         'task locals would initialize at the wrong offsets')
    if not p[7] or p[2] % p[7] or end < data_end or data_end < start:
        raise ValueError('Invalid TLS alignment or section bounds')
    return dict(base=start, data_bytes=p[4], zero_bytes=p[5]-p[4], alignment=p[7])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', type=Path)
    args = parser.parse_args()
    print('FreeRTOS TLS layout verified:', check(args.elf))
