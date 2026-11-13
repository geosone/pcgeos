#!/usr/bin/env python3
"""symtabgen.py -- the symbol set tables of the spooler (the upper 128
characters of the GEOS character set in a printer's symbol set, one
asm resource each: Library/Spool/Lib/lib*Tab.asm) as C.

    symtabgen.py -o out.c TABLE=file.asm ...

Each TABLE=file pair gives "const byte TABLE[128]" from the 128 "byte"
lines of the file's _TEXT_PRINTING branch (decimal, 0x hex, C_SPACE).
"""
import argparse
import re
import sys

SYMBOLS = {'C_SPACE': 0x20}


def value(tok):
    if tok in SYMBOLS:
        return SYMBOLS[tok]
    if re.fullmatch(r'0x[0-9a-fA-F]+', tok):
        return int(tok, 16)
    if re.fullmatch(r'[0-9a-fA-F]+[hH]', tok):
        return int(tok[:-1], 16)
    if re.fullmatch(r'\d+', tok):
        return int(tok)
    raise ValueError(f'unknown value {tok}')


def table(path):
    out = []
    for line in open(path, encoding='latin-1'):
        line = line.split(';', 1)[0].strip()
        m = re.match(r'byte\s+(\S+)$', line, re.I)
        if m:
            out.append(value(m.group(1)))
    if len(out) != 128:
        raise ValueError(f'{path}: {len(out)} entries, not 128')
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-o', required=True)
    ap.add_argument('tables', nargs='+')
    a = ap.parse_args()
    o = ['/* made by Port32/tools/symtabgen.py -- do not edit */',
         '#include <geos.h>', '']
    for t in a.tables:
        name, path = t.split('=', 1)
        v = table(path)
        o.append(f'const byte {name}[128] = {{')
        for i in range(0, 128, 12):
            o.append('    ' + ', '.join(f'0x{x:02x}' for x in v[i:i + 12])
                     + ',')
        o.append('};')
        o.append('')
    with open(a.o, 'w') as f:
        f.write('\n'.join(o))


if __name__ == '__main__':
    try:
        main()
    except ValueError as e:
        sys.exit(f'symtabgen: {e}')
