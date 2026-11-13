#!/usr/bin/env python3
"""psdevinfo2c.py -- the device tables of the PostScript printer driver
(Driver/Printer/PScript) from its asm sources, as C.

    psdevinfo2c.py -o pscriptDevices.c pscriptDriverInfo.asm *Info.asm

pscriptDriverInfo.asm lists the devices ("DefPrinter PD_X, "name",
infoResource"); the branch for GPC_ONLY is left out, as the DOS16 build
does.  Each *Info.asm holds one or more device info resources: a
PrinterInfo, a GraphicsProperties, a PSInfoStruct and the prolog.

The output defines, for each resource, a structure of these four in a
row, so that the offsets the asm stored (PI_hiRes, PSIS_prolog) are
offsets from the start of the PrinterInfo, as in the resource; and the
table "printDevices" of {name, info} (PrintCom/printcom.h) in the order of the PD_ enum.

Records are filled by position, as esp does: the asm's paper input
initializers are written "< MF_x, TF_x, ASF_x >" but the record's
fields are PIO_MANUAL, PIO_ASF, PIO_TRACTOR, so the TF value lands in
PIO_ASF and the ASF value in PIO_TRACTOR.  The tables keep that.

Routine fields (PI_customEntry, PI_customExit) become the PSC_ numbers
of pscript.h; the UI fields (PI_mainUI, PI_optionsUI, PI_evalRoutine)
are 0: the print UI is not ported.
"""
import argparse
import re
import sys

ENUMS = {
    'NULL': 0, 'FALSE': 0, 'TRUE': 0xffff,
    'PT_RASTER': 0, 'PT_VECTOR': 1,
    'BMF_MONO': 0, 'BMF_4BIT': 1, 'BMF_8BIT': 2, 'BMF_24BIT': 3,
    'BMF_4CMYK': 4, 'BMF_3CMY': 5,
    'IC_NO_IEEE488': 0, 'IC_IEEE488': 1,
    'CC_NO_CUSTOM': 0, 'CC_CUSTOM': 1,
    'SC_NO_SCSI': 0, 'SC_SCSI': 1,
    'RC_NO_RS232C': 0, 'RC_RS232C': 1,
    'CC_NO_CENTRONICS': 0, 'CC_CENTRONICS': 1,
    'FC_NO_FILE': 0, 'FC_FILE': 1,
    'AC_NO_APPLETALK': 0, 'AC_APPLETALK': 1,
    'PS_DUMB_RASTER': 0, 'PS_DOES_FONTS': 1, 'PS_PDL': 2,
    'MF_NO_MANUAL': 0, 'MF_MANUAL1': 1, 'MF_MANUAL2': 2, 'MF_MANUAL3': 3,
    'TF_NO_TRACTOR': 0, 'TF_TRACTOR1': 1, 'TF_TRACTOR2': 2,
    'TF_TRACTOR3': 3,
    'ASF_NO_TRAY': 0, 'ASF_TRAY1': 1, 'ASF_TRAY2': 2, 'ASF_TRAY3': 3,
    'OC_NO_COPIES': 0, 'OC_COPIES': 1,
    'PS_NORMAL': 0, 'PS_REVERSE': 1,
    'OD_SIMPLEX': 0, 'OD_DUPLEXLO': 1, 'OD_DUPLEXSO': 2,
    'SO_NO_STAPLER': 0, 'SO_STAPLER': 1,
    'OS_NO_SORTER': 0, 'OS_SORTER': 1,
    'OB_NO_OUTPUTBIN': 0, 'OB_OUTPUTBIN1': 1, 'OB_OUTPUTBIN2': 2,
    'OB_OUTPUTBIN3': 3,
    # pscriptConstant.def
    'PR_MARGIN_LEFT': 18, 'PR_MARGIN_RIGHT': 18, 'PR_MARGIN_TRACTOR': 18,
    'PR_MARGIN_TOP': 18, 'PR_MARGIN_BOTTOM': 18,
}

PSFL = ['PSFL_STANDARD_13', 'PSFL_STANDARD_35N', 'PSFL_STANDARD_35C',
        'PSFL_AGFA_74', 'PSFL_DEC_29', 'PSFL_IBM_17', 'PSFL_TI_17',
        'PSFL_IBM_39', 'PSFL_IBM_43', 'PSFL_IBM_47', 'PSFL_MONOTYPE_8',
        'PSFL_MONOTYPE_70', 'PSFL_JAPANESE_2', 'PSFL_JAPANESE_5',
        'PSFL_WANG_14', 'PSFL_WANG_32', 'PSFL_ADOBE_TC1', 'PSFL_ADOBE_TC2',
        'PSFL_ADOBE_FULL_SET', 'PSFL_QMS_43']

# (field name, bit widths of the record fields, most significant first)
RECORDS = {
    'type': [1, 3],                     # :4, PT_TECH, PT_COLOR
    'connect': [1] * 7,                 # :1, IEEE488 .. APPLETALK
    'input': [2, 3, 2],                 # :1, PIO_MANUAL, PIO_ASF, PIO_TRACTOR
    'output': [1, 1, 2, 1, 1, 2],       # POO_COPIES .. POO_CATCHER
}


def strip_comments(text):
    out = []
    for line in text.splitlines():
        res, q = '', None
        for c in line:
            if q:
                res += c
                if c == q:
                    q = None
            elif c in '"\'':
                q = c
                res += c
            elif c == ';':
                break
            else:
                res += c
        out.append(res)
    return '\n'.join(out)


def value(tok):
    tok = tok.strip()
    if tok in ENUMS:
        return ENUMS[tok]
    if re.fullmatch(r'0x[0-9a-fA-F]+|\d+', tok):
        return int(tok, 0)
    if re.fullmatch(r'[0-9][0-9a-fA-F]*h', tok):
        return int(tok[:-1], 16)
    raise ValueError(f'unknown value "{tok}"')


def record(kind, toks):
    widths = RECORDS[kind]
    if len(toks) != len(widths):
        raise ValueError(f'{kind}: {len(toks)} values for {len(widths)} '
                         'fields')
    v = 0
    for w, t in zip(widths, toks):
        x = value(t)
        if x >= 1 << w:
            raise ValueError(f'{kind}: {t} does not fit {w} bits')
        v = (v << w) | x
    return v


def split_top(s):
    """split "a, <b, c>, d" at the commas outside <>"""
    parts, depth, cur = [], 0, ''
    for c in s:
        if c == '<':
            depth += 1
            if depth == 1:
                continue
        elif c == '>':
            depth -= 1
            if depth == 0:
                continue
        if c == ',' and depth == 0:
            parts.append(cur.strip())
            cur = ''
        else:
            cur += c
    if cur.strip():
        parts.append(cur.strip())
    return parts


def inner(s):
    """the text between the outermost < and its >"""
    i = s.index('<')
    depth = 0
    for j in range(i, len(s)):
        if s[j] == '<':
            depth += 1
        elif s[j] == '>':
            depth -= 1
            if depth == 0:
                return s[i + 1:j], s[j + 1:]
    raise ValueError('unbalanced <>')


def sub_lists(s):
    """the top-level items of a struct initializer; nested <..> stay
    lists of strings"""
    items, depth, cur, nested = [], 0, '', False
    for c in s:
        if c == '<':
            depth += 1
            if depth == 1:
                nested = True
                continue
        elif c == '>':
            depth -= 1
            if depth == 0:
                continue
        if c == ',' and depth == 0:
            items.append(([x.strip() for x in cur.split(',')]
                          if nested else cur.strip()))
            cur, nested = '', False
        else:
            cur += c
    if cur.strip():
        items.append(([x.strip() for x in cur.split(',')]
                      if nested else cur.strip()))
    return items


def c_string(data):
    out = '"'
    for b in data:
        if b == 0x0d:
            out += '\\r'
        elif b == 0x0a:
            out += '\\n"\n\t"'
        elif b in (0x22, 0x5c):
            out += '\\' + chr(b)
        elif 0x20 <= b < 0x7f:
            out += chr(b)
        else:
            out += '\\%03o' % b
    out += '"'
    return out.replace('\n\t""', '')


def prolog_bytes(lines):
    data = bytearray()
    for line in lines:
        line = line.strip()
        if not line:
            continue
        m = re.match(r'char\s+(.*)$', line)
        if not m:
            raise ValueError(f'prolog line "{line}"')
        rest = m.group(1)
        while rest:
            rest = rest.strip()
            if rest.startswith('"'):
                j = rest.index('"', 1)
                data += rest[1:j].encode('latin-1')
                rest = rest[j + 1:]
            elif rest.startswith('NL'):
                data += b'\r\n'
                rest = rest[2:]
            else:
                tok = re.match(r'[^,]+', rest).group(0)
                data.append(value(tok))
                rest = rest[len(tok):]
            rest = rest.strip()
            if rest.startswith(','):
                rest = rest[1:]
    return bytes(data)


class Info:
    pass


def parse_info(path):
    text = strip_comments(open(path, encoding='latin-1').read())
    infos = []
    for m in re.finditer(r'^(\w+)\s+segment\s+resource\s*$(.*?)^\1\s+ends',
                         text, re.M | re.S):
        inf = Info()
        inf.name, body = m.group(1), m.group(2)
        pi_text, rest = inner(body[body.index('PrinterInfo'):])
        items = sub_lists(pi_text)
        if len(items) != 20:
            raise ValueError(f'{inf.name}: {len(items)} PrinterInfo fields')
        (ptype, conn, smarts, entry, exit_, low, med, hi, draft, nlq,
         geom, syms, mtr, masf, pin, pout, width, mainui, optui,
         evalr) = items
        inf.type = record('type', ptype)
        inf.connect = record('connect', conn)
        inf.smarts = value(smarts)
        inf.entry = None if entry == 'NULL' else entry
        inf.exit = None if exit_ == 'NULL' else exit_
        for r in (low, med, draft, nlq, geom, syms):
            if r != 'NULL':
                raise ValueError(f'{inf.name}: unexpected mode/font table')
        hm = re.fullmatch(r'offset\s+(\w+)', hi)
        if not hm:
            raise ValueError(f'{inf.name}: PI_hiRes "{hi}"')
        inf.margin_tractor = [value(x) for x in mtr]
        inf.margin_asf = [value(x) for x in masf]
        inf.input = record('input', pin)
        inf.output = record('output', pout)
        inf.width = value(width)
        inf.ui = (mainui, optui, evalr)
        gm = re.search(hm.group(1) + r'\s+GraphicsProperties\s*<(.*?)>',
                       body, re.S)
        gp = [x.strip() for x in gm.group(1).split(',')]
        inf.gp = [value(x) for x in gp]
        pm = re.search(r'PSInfoStruct\s*<(.*?)>', body, re.S)
        ps = [x.strip() for x in pm.group(1).split(',')]
        if ps[0] not in PSFL:
            raise ValueError(f'{inf.name}: font list {ps[0]}')
        inf.fonts = ps[0]
        inf.level = value(ps[1])
        lm = re.fullmatch(r'offset\s+(\w+)', ps[3])
        label = lm.group(1)
        lines = re.search(r'^' + label + r'\s+label\s+byte\s*$(.*?)^\w+\s+'
                          r'label\s+byte', body, re.M | re.S).group(1)
        inf.prolog = prolog_bytes(lines.splitlines())
        infos.append(inf)
    return infos


def parse_devices(path):
    text = strip_comments(open(path, encoding='latin-1').read())
    out, skip, stack = [], False, []
    for line in text.splitlines():
        w = line.split()
        if not w:
            continue
        if w[0] == 'ifdef':
            stack.append(skip)
            skip = skip or w[1] == 'GPC_ONLY'
            continue
        if w[0] == 'else':
            skip = stack[-1] or not skip
            continue
        if w[0] == 'endif':
            skip = stack.pop()
            continue
        if skip or w[0] != 'DefPrinter':
            continue
        m = re.match(r'\s*DefPrinter\s+(\w+)\s*,\s*"([^"]*)"\s*,\s*(\w+)',
                     line)
        out.append((m.group(1), m.group(2), m.group(3)))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-o', required=True)
    ap.add_argument('driverinfo')
    ap.add_argument('infos', nargs='+')
    a = ap.parse_args()

    devices = parse_devices(a.driverinfo)
    infos = {}
    for f in a.infos:
        for inf in parse_info(f):
            infos[inf.name] = inf

    o = []
    o.append('/* generated by Port32/tools/psdevinfo2c.py from '
             'pscriptDriverInfo.asm\n * and the *Info.asm files; '
             'do not edit */')
    o.append('#include "pscript.h"\n#include <stddef.h>\n')
    for name in sorted(infos):
        inf = infos[name]
        n = len(inf.prolog)
        T = 'PSDev_' + name
        o.append(f'typedef struct {{\n    PrinterInfo pi;\n'
                 f'    GraphicsProperties hiRes;\n    PSInfoStruct ps;\n'
                 f'    char prolog[{n + 1}];\n}} {T};\n')
        # PS_INFO_STRUCT: the PSInfoStruct right after the properties
        o.append(f'typedef char {T}_check[offsetof({T}, ps) == '
                 f'offsetof({T}, hiRes) + sizeof(GraphicsProperties) '
                 f'? 1 : -1];\n')
        ent = f'PSC_{inf.entry}' if inf.entry else 'PSC_NONE'
        ext = f'PSC_{inf.exit}' if inf.exit else 'PSC_NONE'
        mt = ', '.join(str(x) for x in inf.margin_tractor)
        ma = ', '.join(str(x) for x in inf.margin_asf)
        gp = inf.gp
        o.append(f'static const {T} {name} = {{\n  {{\n'
                 f'    0x{inf.type:02x}, 0x{inf.connect:02x}, '
                 f'{inf.smarts},\t/* type, connect, smarts */\n'
                 f'    {ent}, {ext},\n'
                 f'    0, 0, offsetof({T}, hiRes), 0, 0,'
                 f'\t/* the modes */\n'
                 f'    0, 0,\t\t\t/* fonts, symbol sets */\n'
                 f'    {{ {mt} }},\t/* tractor margins */\n'
                 f'    {{ {ma} }},\t/* ASF margins */\n'
                 f'    0x{inf.input:02x}, 0x{inf.output:02x}, {inf.width},'
                 f'\t/* paper in, out, width */\n'
                 f'    0, 0, 0\t\t/* UI: {", ".join(inf.ui)} */\n'
                 f'  }},\n'
                 f'  {{ {gp[0]}, {gp[1]}, {gp[2]}, {gp[3]}, {gp[4]}, '
                 f'{gp[5]}, {gp[6]} }},\n'
                 f'  {{ {inf.fonts}, 0x{inf.level:04x}, {n}, '
                 f'offsetof({T}, prolog) }},\n'
                 f'  {c_string(inf.prolog)}\n}};\n')
    o.append('const PrintDeviceEntry printDevices[] = {')
    for i, (pd, nm, info) in enumerate(devices):
        if info not in infos:
            sys.exit(f'psdevinfo2c: {pd}: no info resource {info}')
        o.append(f'    {{ "{nm}", &{info}.pi }},\t/* {pd} = {2 * i} */')
    o.append('};\n')
    o.append(f'const word printNumDevices = {len(devices)};')
    with open(a.o, 'w') as f:
        f.write('\n'.join(o) + '\n')


if __name__ == '__main__':
    main()
