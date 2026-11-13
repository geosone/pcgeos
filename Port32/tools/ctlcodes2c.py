#!/usr/bin/env python3
"""ctlcodes2c.py -- a printer driver's control codes and constants in C.

Reads a driver's <name>ControlCodes.asm (the pr_codes_* tables: a count
byte, then the bytes; labels without data of their own share the next
label's) and <name>Constant.def (the "NAME = value" constants the
PrintCom C files use: band heights, interleaves, resolutions, PR_*) and
writes the driver's printcomDriver.h and <name>ControlCodes.c, the C
port beside the asm.  It is run once per driver; the output is read and
checked in like any other port.

    ctlcodes2c.py -n <name> -d <driver dir> [-t <title>]

The control codes keep the asm's comments.  A code whose bytes do not
match its count is refused, as are expressions the tool cannot read.
"""

import argparse
import os
import re
import sys
import textwrap

CHARS = {'C_NULL': 0, 'C_SOH': 1, 'C_STX': 2, 'C_ETX': 3, 'C_EOT': 4,
         'C_ENQ': 5, 'C_ACK': 6, 'C_BEL': 7, 'C_BS': 8, 'C_HT': 9,
         'C_TAB': 9, 'C_LF': 10, 'C_LINEFEED': 10, 'C_VT': 11, 'C_FF': 12,
         'C_CR': 13, 'C_SO': 14, 'C_SI': 15, 'C_DLE': 16, 'C_DC1': 17,
         'C_DC2': 18, 'C_DC3': 19, 'C_DC4': 20, 'C_NAK': 21, 'C_SYN': 22,
         'C_ETB': 23, 'C_CAN': 24, 'C_EM': 25, 'C_SUB': 26, 'C_ESC': 27,
         'C_FS': 28, 'C_GS': 29, 'C_RS': 30, 'C_US': 31, 'C_SPACE': 32,
         'C_DEL': 127}
# the constants a driver's C files use
CONST_RE = re.compile(r'^(?:LO|MED|HI)_RES_(?:BAND_HEIGHT|INTERLEAVE_FACTOR|'
                      r'BUFF_HEIGHT|X_RES|Y_RES)$|^PR_|^ENOUGH_BLANK$|'
                      r'^NUM_PINS$')


def fail(msg):
    sys.exit('ctlcodes2c: ' + msg)


def strip_comment(line):
    out, q = '', None
    for ch in line:
        if q:
            out += ch
            if ch == q:
                q = None
        elif ch in '"\'':
            q = ch
            out += ch
        elif ch == ';':
            break
        else:
            out += ch
    return out


def split_args(s):
    args, cur, q = [], '', None
    for ch in s:
        if q:
            cur += ch
            if ch == q:
                q = None
        elif ch in '"\'':
            q = ch
            cur += ch
        elif ch == ',':
            args.append(cur.strip())
            cur = ''
        else:
            cur += ch
    if cur.strip():
        args.append(cur.strip())
    return args


def read_constants(path):
    consts = {}
    if not os.path.exists(path):
        return consts
    for line in open(path, errors='replace'):
        m = re.match(r'^\s*([A-Za-z_]\w*)\s*(?:=|equ)\s*(.+)$',
                     strip_comment(line).rstrip())
        if not m:
            continue
        name, val = m.group(1), m.group(2).strip()
        v = evaluate(val, consts)
        if v is not None:
            consts[name] = v
    return consts


def evaluate(expr, consts):
    e = expr.strip()
    e = re.sub(r'\b([0-9][0-9a-fA-F]*)[hH]\b',
               lambda m: str(int(m.group(1), 16)), e)
    e = re.sub(r'\b0x[0-9a-fA-F]+\b', lambda m: str(int(m.group(0), 16)), e)

    def name(m):
        n = m.group(0)
        if n in consts:
            return str(consts[n])
        if n in CHARS:
            return str(CHARS[n])
        if n in ('shl', 'shr', 'or', 'and'):
            return {'shl': '<<', 'shr': '>>', 'or': '|', 'and': '&'}[n]
        raise KeyError(n)
    try:
        e = re.sub(r'\b[A-Za-z_]\w*\b', name, e)
        if not re.fullmatch(r'[0-9+\-*/()<>|& ]+', e):
            return None
        return int(eval(e.replace('/', '//')))
    except (KeyError, SyntaxError, ZeroDivisionError):
        return None


def c_item(tok, consts):
    """an asm byte operand as C items"""
    if tok[0] in '"\'' and tok[-1] == tok[0]:
        out = []
        for ch in tok[1:-1]:
            out.append("'\\''" if ch == "'" else
                       "'\\\\'" if ch == '\\' else "'%s'" % ch)
        return out, len(tok) - 2
    if tok in CHARS:
        return [tok], 1
    if re.fullmatch(r'[A-Za-z_]\w*', tok) and tok in consts and \
            CONST_RE.match(tok):
        return [tok], 1
    v = evaluate(tok, consts)
    if v is None:
        fail('cannot read byte "%s"' % tok)
    return [str(v & 0xff)], 1


def read_codes(path, consts):
    codes, pending, cur = [], [], None
    incomment = None
    for raw in open(path, errors='replace'):
        line = raw.rstrip('\n')
        if incomment:
            if incomment in line:
                incomment = None
            continue
        m = re.match(r'^\s*COMMENT\s*(\S)', line)
        if m:
            if m.group(1) not in line[m.end():]:
                incomment = m.group(1)
            continue
        if re.match(r'^\s*;', line):
            continue
        comment = ''
        body = strip_comment(line)
        if len(body) < len(line):
            comment = line[len(body) + 1:].strip()
        body = body.strip()
        if not body:
            continue
        m = re.match(r'^(\w+)\s+label\s+byte$', body)
        if m:
            if cur is not None and not cur['items']:
                pending = cur['aliases'] + [cur['name']]
            elif cur is not None:
                codes.append(cur)
            cur = {'name': m.group(1), 'items': [], 'aliases': pending}
            pending = []
            continue
        m = re.match(r'^byte\s+(.+)$', body)
        if m and cur is not None:
            items, n = [], 0
            for a in split_args(m.group(1)):
                it, k = c_item(a, consts)
                items += it
                n += k
            cur['items'].append((items, comment))
            continue
        if re.match(r'^\w+\s+(segment|ends)\b', body) or \
                body.startswith(('include', 'if', 'endif', 'else')):
            continue
        fail('cannot read "%s"' % body)
    if cur is not None:
        if not cur['items']:
            fail('%s has no bytes' % cur['name'])
        codes.append(cur)
    return codes


def check_count(code, consts):
    first = code['items'][0][0][0]
    try:
        count = int(first) if first not in CHARS else CHARS[first]
    except ValueError:
        fail('%s: the count is %s' % (code['name'], first))
    n = sum(len(i) for i, _ in code['items']) - 1
    if n != count:
        fail('%s: count %d, %d bytes' % (code['name'], count, n))


def emit_code(name, code):
    lines = []
    head = 'const byte %s[] = {' % name
    simple = len(code['items']) == 1 and not code['items'][0][1]
    allitems = [x for i, _ in code['items'] for x in i]
    if simple or all(not c for _, c in code['items']):
        text = head + ' ' + ', '.join(allitems) + ' };'
        if len(text) <= 78:
            return [text]
        out, cur = [], head
        for k, it in enumerate(allitems):
            piece = ' ' + it + (',' if k < len(allitems) - 1 else ' };')
            if len(cur) + len(piece) > 78:
                out.append(cur)
                cur = '   '
            cur += piece
        out.append(cur)
        return out
    lines.append(head)
    for k, (items, comment) in enumerate(code['items']):
        last = k == len(code['items']) - 1
        text = '    ' + ', '.join(items) + (' };' if last else ',')
        if comment:
            note = '/* %s */' % comment.replace('*/', '').rstrip('.')
            if len(text) + 2 + len(note) <= 78:
                text = text + '  ' + note
            else:
                lines.append('    ' + note)
        lines.append(text)
    return lines


def box(text):
    return [' * ' + l for l in textwrap.wrap(text, 73)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-n', '--name', required=True)
    ap.add_argument('-d', '--dir', required=True)
    ap.add_argument('-t', '--title', default=None)
    a = ap.parse_args()
    d, n = a.dir, a.name
    title = a.title or n
    consts = read_constants(os.path.join(d, n + 'Constant.def'))
    codes = read_codes(os.path.join(d, n + 'ControlCodes.asm'), consts)
    names = []
    for c in codes:
        check_count(c, consts)
        names += c['aliases'] + [c['name']]

    h = ['/*'] + box('printcomDriver.h -- the %s driver\'s constants and '
                     'control codes for the PrintCom files (the C form of '
                     '%sConstant.def; the codes are in %sControlCodes.c).'
                     % (title, n, n)) + [' */',
         '#ifndef PRINTCOMDRIVER_H',
         '#define PRINTCOMDRIVER_H', '']
    for k, v in consts.items():
        if CONST_RE.match(k):
            h.append('#define %s\t%d' % (k, v))
    h.append('')
    decl = 'extern const byte '
    line = decl
    for k, nm in enumerate(names):
        piece = nm + '[]' + (', ' if k < len(names) - 1 else ';')
        if len(line) + len(piece) > 76:
            h.append(line.rstrip())
            line = '    '
        line += piece
    h.append(line)
    h += ['', '#endif /* PRINTCOMDRIVER_H */', '']

    c = ['/*'] + box('%sControlCodes.c -- the control codes of the %s '
                     'driver (the C port of %sControlCodes.asm): a count '
                     'byte, then the bytes; and the driver table.'
                     % (n, title, n)) + [' */',
         '#include "printcom.h"',
         '#include "printcomDriver.h"', '']
    for code in codes:
        for al in code['aliases']:
            c += emit_code(al, code)
        c += emit_code(code['name'], code)
    c += ['',
          '/* the driver table: an extended driver, its devices in '
          'printDevices',
          ' * (Port32/ABI.md 7.6) */',
          'DriverExtendedInfoStruct DriverTable = {',
          '    { DRIVER_STRATEGY(PrintStrategy), DA_HAS_EXTENDED_INFO,',
          '      DRIVER_TYPE_PRINTER },',
          '    0',
          '};', '']
    open(os.path.join(d, 'printcomDriver.h'), 'w').write('\n'.join(h))
    open(os.path.join(d, n + 'ControlCodes.c'), 'w').write('\n'.join(c))


if __name__ == '__main__':
    main()
