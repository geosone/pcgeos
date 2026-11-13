#!/usr/bin/env python3
"""dmdevinfo2c.py -- the device tables of a raster printer driver (the
dot matrix drivers of Driver/Printer/DotMatrix) from its asm sources, as
C.

    dmdevinfo2c.py -o devices.c [-D constants.def]... [--fonts font.asm]
                   driverinfo.asm info.asm...

driverinfo.asm lists the devices ("DefPrinter PD_X, "name", infoRes")
and holds the PrintDriverInfo with the ISO substitution table and the
ASCII translation list (chunks in the asm).  Each info.asm holds one or
more device info resources: a PrinterInfo, its GraphicsProperties, a
FontGeometry table, the pitch tables and a symbol set list (the control
codes by PrinterSymbolSet).  The font file (--fonts) holds the
printerFontInfo resource: lists of FontEntry offsets and the
FontEntries; the PCL drivers have none (their NLQ list is an empty list
in the info resource, the fonts are the printer's).  The -D files give the constants (NAME = value).

The output defines, for each info resource, a structure of its parts in
the order of the asm, so that the offsets the asm stored (PI_lowRes,
PI_fontGeometries, FG_pitchTab) are offsets from the start of the
PrinterInfo, as in the resource; printerFontInfo likewise (PI_draft and
PI_nlq are offsets into it); the table "printDevices" (PrintCom/printcom.h)
in the order of the PD_ enum; printDriverInfo and printTransTables (the
tables of SpoolUpdateTranslationTable, Internal/spoolDr.h).

Port32 differences: FE_command, the offset of a control code in the
asm, is an index into printFontCommands[] (1 on; 0: none), as are the
entries of a symbol set list.  The UI
fields (PI_mainUI, PI_optionsUI, PI_evalRoutine) are 0, as is
GP_colorCorrection (a handle of a correction table): the print UI and
color correction are not ported.  Records are filled by position, as
esp does (psdevinfo2c.py).
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
    'PR_DONT_RESEND': 0, 'PR_RESEND': 1,
    'PDT_PRINTER': 0, 'PDT_PLOTTER': 1, 'PDT_FACSIMILE': 2,
    'PDT_CAMERA': 3, 'PDT_OTHER': 4,
    'FID_INVALID': 0, 'FID_DTC_URW_ROMAN': 0x1000, 'FID_DTC_URW_SANS': 0x1200,
    'FID_DTC_URW_MONO': 0x1400,
    'TP_PROPORTIONAL': 0, 'TP_5_PITCH': 1, 'TP_6_PITCH': 2,
    'TP_10_PITCH': 3, 'TP_10_6_PITCH': 0x6a, 'TP_12_PITCH': 0x6b,
    'TP_15_PITCH': 0x6c, 'TP_16_PITCH': 0x6d, 'TP_16_6_PITCH': 0xa6,
    'TP_17_PITCH': 0xa7, 'TP_19_2_PITCH': 0xc0, 'TP_20_PITCH': 0xc1,
    'TP_24_PITCH': 0xc2,
    'PSS_ASCII7': 0, 'PSS_IBM437': 2, 'PSS_IBM850': 4, 'PSS_IBM860': 6,
    'PSS_IBM863': 8, 'PSS_IBM865': 10, 'PSS_ROMAN8': 12, 'PSS_WINDOWS': 14,
    'PSS_VENTURA': 16, 'PSS_LATIN1': 18, 'PSS_PCGEOS': 20,
}

PTS = ['FUTURE', 'OVERLINE', 'QUADHEIGHT', 'DBLHEIGHT', 'DBLWIDTH',
       'REVERSE', 'OUTLINE', 'SHADOW', 'STRIKETHRU', 'UNDERLINE', 'ITALIC',
       'BOLD', 'NLQ', 'SUPERSCRIPT', 'SUBSCRIPT', 'CONDENSED']
for _i, _n in enumerate(PTS):
    ENUMS['mask PTS_' + _n] = 1 << _i

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
    out = conditionals(out)
    # line continuations
    return '\n'.join(out).replace('\\\n', ' ')


def conditionals(lines):
    """the lines of ifdef/ifndef/else/endif with no symbol defined (as
    the standard build: no GPC_ONLY); other conditionals are refused"""
    out, stack = [], []
    for line in lines:
        w = line.split()
        if w and w[0] in ('ifdef', 'ifndef'):
            stack.append(w[0] == 'ifndef')
            continue
        if w and w[0] in ('if', 'ife', 'elseif'):
            raise ValueError(f'conditional "{line.strip()}"')
        if w and w[0] == 'else':
            stack[-1] = not stack[-1]
            continue
        if w and w[0] == 'endif':
            stack.pop()
            continue
        out.append(line if all(stack) else '')
    return out


def value(tok):
    tok = ' '.join(tok.split())
    if tok in ENUMS:
        return ENUMS[tok]
    if ' or ' in tok:
        v = 0
        for t in tok.split(' or '):
            v |= value(t)
        return v
    if re.fullmatch(r'0x[0-9a-fA-F]+|\d+', tok):
        return int(tok, 0)
    if re.fullmatch(r'[0-9][0-9a-fA-F]*h', tok, re.I):
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


def inner(s):
    """the text between the outermost < and its >, and the rest"""
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
    """the top-level items of a struct initializer; nested <..> become
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


def offset_of(tok):
    """"offset x" / "offset res:x" -> x; NULL -> None"""
    if tok == 'NULL':
        return None
    m = re.fullmatch(r'offset\s+(?:\w+:)?(\w+)', tok)
    if not m:
        raise ValueError(f'not an offset: "{tok}"')
    return m.group(1)


def read_constants(path):
    text = strip_comments(open(path, encoding='latin-1').read())
    for line in text.splitlines():
        m = re.match(r'\s*(\w+)\s*=\s*(\S+)\s*$', line)
        if m:
            try:
                ENUMS[m.group(1)] = value(m.group(2))
            except ValueError:
                pass


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
        if entry != 'NULL' or exit_ != 'NULL':
            raise ValueError(f'{inf.name}: custom entry/exit routines')
        inf.syms = offset_of(syms)
        inf.modes = [offset_of(x) for x in (low, med, hi)]
        inf.draft = offset_of(draft)
        inf.nlq = offset_of(nlq)
        inf.geom = offset_of(geom)
        inf.margin_tractor = [value(x) for x in mtr]
        inf.margin_asf = [value(x) for x in masf]
        inf.input = record('input', pin)
        inf.output = record('output', pout)
        inf.width = value(width)
        inf.ui = (mainui, optui, evalr)
        # the parts after the PrinterInfo, in their order
        inf.parts = []
        for pm in re.finditer(r'^(\w+)\s+GraphicsProperties\s*<(.*?)>',
                              body, re.M | re.S):
            gp = [x.strip() for x in pm.group(2).split(',')]
            vals = [value(x) for x in gp[:6]]
            inf.parts.append((pm.start(), 'gp', pm.group(1), vals))
        if inf.geom:
            gm = re.search(r'^' + inf.geom + r'\s+FontGeometry(.*?)'
                           r'word\s+FID_INVALID', body, re.M | re.S)
            ents = []
            rest = gm.group(1)
            while '<' in rest:
                t, rest = inner(rest)
                f = [x.strip() for x in t.split(',')]
                ents.append((value(f[0]), value(f[1]), offset_of(f[2])))
            inf.parts.append((gm.start(), 'geom', inf.geom, ents))
        for lm in re.finditer(r'^(\w+)\s+label\s+byte\s*$(.*?)(?=^\w+\s+'
                              r'(?:label|ends)|\Z)', body, re.M | re.S):
            vals = [value(b.group(1)) for b in
                    re.finditer(r'^\s*byte\s+(\S+)\s*$', lm.group(2), re.M)]
            inf.parts.append((lm.start(), 'pitch', lm.group(1), vals))
        # lists of words: the symbol set list (control codes by
        # PrinterSymbolSet) and empty font lists
        for lm in re.finditer(r'^(\w+)\s+label\s+word\s*$(.*?)(?=^\w+\s+'
                              r'(?:label|ends)|\Z)', body, re.M | re.S):
            vals = [offset_of(b.group(1)) for b in
                    re.finditer(r'^\s*word\s+(.+?)\s*$', lm.group(2), re.M)]
            if lm.group(1) != inf.syms and vals:
                raise ValueError(f'{inf.name}: word list {lm.group(1)}')
            inf.parts.append((lm.start(), 'words', lm.group(1), vals))
        inf.parts.sort(key=lambda p: p[0])
        names = {p[2] for p in inf.parts}
        if inf.syms and inf.syms not in names:
            raise ValueError(f'{inf.name}: no symbol set list {inf.syms}')
        for x in inf.modes:
            if x and x not in names:
                raise ValueError(f'{inf.name}: no mode {x}')
        infos.append(inf)
    return infos


def parse_devices(path):
    text = strip_comments(open(path, encoding='latin-1').read())
    out = []
    for m in re.finditer(r'^\s*DefPrinter\s+(\w+)\s*,\s*"([^"]*)"\s*,'
                         r'\s*(\w+)', text, re.M):
        out.append((m.group(1), m.group(2), m.group(3)))
    pdi = sub_lists(inner(text[text.index('PrintDriverInfo'):])[0])
    words = []
    im = re.search(r'^(\w+)\s+chunk\.word(.*?)(?=^\s*$|^\S)', text,
                   re.M | re.S)
    if im:
        words = [value(x) for x in im.group(2).replace('\n', ' ').split(',')
                 if x.strip()]
    trans = b''
    tm = re.search(r'^\w+\s+chunk\.char\s+(.*)$', text, re.M)
    if tm:
        for part in tm.group(1).split(','):
            part = part.strip()
            if part.startswith('"'):
                trans += part.strip('"').encode('latin-1')
            else:
                trans += bytes([value(part)])
    return out, pdi, words, trans


def parse_fonts(path):
    text = strip_comments(open(path, encoding='latin-1').read())
    body = re.search(r'^printerFontInfo\s+segment\s+resource\s*$(.*?)'
                     r'^printerFontInfo\s+ends', text, re.M | re.S).group(1)
    lists, labels, pending, cur = [], {}, [], None
    entries = []
    pos = 0
    for line in body.splitlines():
        w = line.split()
        if not w:
            continue
        if len(w) >= 3 and w[1] == 'label':
            pending.append(w[0])
            cur = None
            continue
        if w[0] == 'nptr':
            if cur is None:
                cur = [pending[0], []]
                lists.append(cur)
                for p in pending:
                    labels[p] = pending[0]
                pending = []
            cur[1].append(w[1])
            continue
        if w[0] == 'word' and cur is not None:
            cur = None
            continue
    for m in re.finditer(r'^(\w+)\s+FontEntry\s*<', body, re.M):
        t, _ = inner(body[m.end() - 1:])
        f = [x.strip() for x in t.split(',')]
        if len(f) != 7:
            raise ValueError(f'{m.group(1)}: {len(f)} FontEntry fields')
        entries.append((m.group(1), value(f[0]), value(f[1]), value(f[2]),
                        value(f[3]), offset_of(f[4]), value(f[5]),
                        value(f[6])))
    return lists, labels, entries


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-o', required=True)
    ap.add_argument('-D', action='append', default=[])
    ap.add_argument('--fonts')
    ap.add_argument('--header', default='printcom.h')
    ap.add_argument('driverinfo')
    ap.add_argument('infos', nargs='+')
    a = ap.parse_args()

    for d in a.D:
        read_constants(d)
    devices, pdi, iso, trans = parse_devices(a.driverinfo)
    infos = {}
    for f in a.infos:
        for inf in parse_info(f):
            infos[inf.name] = inf
    lists, labels, entries = (parse_fonts(a.fonts) if a.fonts
                              else ([], {}, []))

    o = ['/* generated by Port32/tools/dmdevinfo2c.py from the driver\'s '
         'info files;\n * do not edit */',
         f'#include "{a.header}"', '#include <stddef.h>', '']

    # the font commands
    cmds = []
    for e in entries:
        if e[5] and e[5] not in cmds:
            cmds.append(e[5])
    for inf in infos.values():
        for _, kind, pname, vals in inf.parts:
            if kind == 'words':
                cmds += [c for c in vals if c and c not in cmds]
    for c in cmds:
        o.append(f'extern const byte {c}[];')
    o.append('const byte *const printFontCommands[] = {\n    NULL,')
    for c in cmds:
        o.append(f'    {c},')
    o.append('};\n')

    # printerFontInfo
    used = {n for l in lists for n in l[1]}
    o.append('typedef struct {\n    word zero;')
    for name, ents in lists:
        o.append(f'    word {name}[{len(ents) + 1}];')
    for e in entries:
        o.append(f'    FontEntry {e[0]};')
    o.append('} PrintFontInfo;\n')
    o.append('static const PrintFontInfo printerFontInfo = {\n    0,')
    for name, ents in lists:
        for n in ents:
            if n not in [e[0] for e in entries]:
                sys.exit(f'dmdevinfo2c: no FontEntry {n}')
        o.append('    { ' + ', '.join(f'offsetof(PrintFontInfo, {n})'
                                      for n in ents) + ', 0 },')
    for (n, fid, size, pitch, sset, cmd, st, sts) in entries:
        ci = cmds.index(cmd) + 1 if cmd else 0
        o.append(f'    {{ 0x{fid:04x}, {size}, 0x{pitch:02x}, {sset}, {ci},'
                 f' 0x{st:04x}, 0x{sts:04x} }},\t/* {n} */')
    o.append('};\n')
    o.append('const byte *const printFontInfoBase = '
             '(const byte *) &printerFontInfo;\n')

    for name in sorted(infos):
        inf = infos[name]
        T = 'DMDev_' + name
        o.append('typedef struct {\n    PrinterInfo pi;')
        for _, kind, pname, vals in inf.parts:
            if kind == 'gp':
                o.append(f'    GraphicsProperties {pname};')
            elif kind == 'geom':
                o.append(f'    FontGeometry {pname}[{len(vals) + 1}];')
            elif kind == 'words':
                o.append(f'    word {pname}[{max(len(vals), 1)}];')
            else:
                o.append(f'    byte {pname}[{len(vals)}];')
        o.append(f'}} {T};\n')

        def off(x):
            return f'offsetof({T}, {x})' if x else '0'

        names = {p[2] for p in inf.parts}

        def foff(x):
            if not x:
                return '0'
            if x in names:
                # a font list in the info resource (the PCL drivers' empty
                # NLQ list: the mode is there, its fonts the printer's)
                return off(x)
            return f'offsetof(PrintFontInfo, {labels[x]})'
        mt = ', '.join(str(x) for x in inf.margin_tractor)
        ma = ', '.join(str(x) for x in inf.margin_asf)
        o.append(f'static const {T} {name} = {{\n  {{\n'
                 f'    0x{inf.type:02x}, 0x{inf.connect:02x}, '
                 f'{inf.smarts},\t/* type, connect, smarts */\n'
                 f'    0, 0,\t\t\t/* custom entry, exit */\n'
                 f'    {off(inf.modes[0])}, {off(inf.modes[1])},\n'
                 f'    {off(inf.modes[2])},\n'
                 f'    {foff(inf.draft)}, {foff(inf.nlq)},\n'
                 f'    {off(inf.geom)}, {off(inf.syms)},'
                 f'\t/* fonts, symbol sets */\n'
                 f'    {{ {mt} }},\t/* tractor margins */\n'
                 f'    {{ {ma} }},\t/* ASF margins */\n'
                 f'    0x{inf.input:02x}, 0x{inf.output:02x}, {inf.width},'
                 f'\t/* paper in, out, width */\n'
                 f'    0, 0, 0\t\t/* UI: {", ".join(inf.ui)} */\n  }},')
        for _, kind, pname, vals in inf.parts:
            if kind == 'gp':
                o.append('  { ' + ', '.join(str(v) for v in vals) +
                         ', 0 },\t/* ' + pname + ' */')
            elif kind == 'geom':
                o.append('  {')
                for fid, size, pt in vals:
                    o.append(f'    {{ 0x{fid:04x}, {size}, {off(pt)} }},')
                o.append('    { 0, 0, 0 }\n  },')
            elif kind == 'words':
                o.append('  { ' + (', '.join(str(cmds.index(c) + 1) if c
                                             else '0' for c in vals) or '0')
                         + ' },\t/* ' + pname + ' */')
            else:
                o.append('  { ' + ', '.join(f'0x{v:02x}' for v in vals) +
                         ' },\t/* ' + pname + ' */')
        o.append('};\n')

    o.append('const PrintDeviceEntry printDevices[] = {')
    for i, (pd, nm, info) in enumerate(devices):
        if info not in infos:
            sys.exit(f'dmdevinfo2c: {pd}: no info resource {info}')
        o.append(f'    {{ "{nm}", &{info}.pi }},\t/* {pd} = {2 * i} */')
    o.append('};\n')
    o.append(f'const word printNumDevices = {len(devices)};\n')

    # the PrintDriverInfo and the tables of its chunks
    o.append('const PrintDriverInfo printDriverInfo = {\n'
             f'    {value(pdi[0])}, {value(pdi[1])},\n'
             '    0, 0,\t\t/* the tables: printTransTables */\n'
             f'    {value(pdi[4])}, {value(pdi[5])}\n}};\n')
    if iso:
        o.append('static const word isoSubstitutions[] = {')
        for i in range(0, len(iso), 8):
            o.append('    ' + ', '.join(f'0x{x:04x}' for x in iso[i:i + 8])
                     + ',')
        o.append('};')
    tb = ', '.join(str(b) for b in trans) or '0'
    o.append(f'static const byte asciiTrans[] = {{ {tb} }};')
    o.append('const SpoolTransTables printTransTables = {\n'
             f'    {"isoSubstitutions" if iso else "NULL"}, asciiTrans, '
             f'{len(trans)}\n}};')
    with open(a.o, 'w') as f:
        f.write('\n'.join(o) + '\n')


if __name__ == '__main__':
    try:
        main()
    except ValueError as e:
        sys.exit(f'dmdevinfo2c: {e}')
