#!/usr/bin/env python3
"""check_ref.py -- compare the expected strings of the C port tests with the
output of the original GEOS math library.

    check_ref.py ffa_cases.h test_ffa.c ffaref.txt

ffaref.txt was written by GeosRef/ffaref.goc running under GEOS (see
README.md): one line per case, "name<TAB>result<TAB>count<TAB>depth".
Every FFA_CASE/FFA_RAW of ffa_cases.h and every checkFrame of test_ffa.c
must have a line there with the same result, a float stack depth of 0
afterwards, and nothing may be left over.  Exit code 0 if all agree.
"""
import re
import sys


def c_string(s):
    """value of a C string literal body (only the escapes the tests use)."""
    return re.sub(r'\\(.)', lambda m: {'n': '\n', 't': '\t'}.get(m.group(1), m.group(1)), s)


def main():
    if len(sys.argv) != 4:
        print(__doc__.strip().splitlines()[2].strip(), file=sys.stderr)
        return 2
    cases, test, ref_path = sys.argv[1:]
    expect = {}
    lit = r'"((?:[^"\\]|\\.)*)"'
    for m in re.finditer(r'^\s*FFA_(?:CASE|RAW)\(\s*' + lit + r'.*?' + lit + r'\s*\)',
                         open(cases).read(), re.S | re.M):
        expect[c_string(m.group(1))] = c_string(m.group(2))
    for m in re.finditer(r'checkFrame\(\s*' + lit + r'[^;]*?' + lit + r'\s*\)\s*;',
                         open(test).read(), re.S):
        expect[c_string(m.group(1))] = c_string(m.group(2))

    ref = {}
    for line in open(ref_path, encoding='latin-1'):
        line = line.rstrip('\r\n')
        if not line or line.startswith('#'):
            continue
        name, result, count, depth = line.split('\t')
        ref[name] = (result, int(count), int(depth))

    bad = 0
    for name, want in expect.items():
        if name not in ref:
            print('MISSING in reference: %s' % name)
            bad += 1
            continue
        result, count, depth = ref[name]
        if result != want:
            print('DIFF %s: port expects %r, GEOS gives %r' % (name, want, result))
            bad += 1
        if depth != 0:
            print('DEPTH %s: GEOS left %d numbers on the float stack' % (name, depth))
            bad += 1
    print('%d cases compared with the GEOS reference, %d problems' % (len(expect), bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
