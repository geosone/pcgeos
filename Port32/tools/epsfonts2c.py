#!/usr/bin/env python3
"""epsfonts2c.py -- the font tables of the PostScript translation library
in C, from its asm.

  epsfonts2c.py -o epsFonts.c --fontid Include/fontID.def \
      --order exportFontTables.asm exportFontTables.asm exportMoreFonts.asm

The Adobe font entries (AdobeFontEntry: a FontID, the style, the encoding,
the PostScript name), the sets of them, the font lists of each PSFontList
(fontMapOffsets of exportFontTables.asm, in its order), and the face
tables of CalcFontDiff (serifTable, sansTable, scriptTable: the style
bytes of the DefineFace lines of fontID.def, as FID_MAPPING_CODE builds
them).
"""
import argparse
import re
import sys


def logical_lines(path):
    """the lines of an asm file, comments gone, continuations joined"""
    out, cur = [], ''
    with open(path, encoding='latin-1') as f:
        for raw in f:
            line = raw.rstrip('\r\n')
            # a ';' outside a string starts a comment
            q, s = None, []
            for ch in line:
                if q:
                    s.append(ch)
                    if ch == q:
                        q = None
                elif ch in '"\'':
                    q = ch
                    s.append(ch)
                elif ch == ';':
                    break
                else:
                    s.append(ch)
            line = ''.join(s).rstrip()
            if line.endswith('\\'):
                cur += line[:-1] + ' '
                continue
            out.append(cur + line)
            cur = ''
    if cur:
        out.append(cur)
    return out


def style_c(expr):
    expr = expr.strip()
    if expr in ('0', ''):
        return '0'
    parts = [p.strip() for p in re.split(r'\bor\b', expr)]
    res = []
    for p in parts:
        m = re.match(r'mask\s+(TS_\w+)$', p)
        if not m:
            sys.exit('epsfonts2c: style %r' % expr)
        res.append(m.group(1))
    return ' | '.join(res)


def parse_tables(paths):
    entries = {}            # label -> (fid, style, encode, namelabel)
    names = {}              # namelabel -> string
    lists = {}              # label -> [labels]  (None ends)
    order = []              # labels in file order, for output
    head = {}               # label -> the first label stacked with it
    pending = []            # labels that start the next list
    cur = None
    for path in paths:
        for line in logical_lines(path):
            if not line.strip():
                continue
            m = re.match(r'^(\w+)\s+label\s+nptr\S*AdobeFontEntry', line)
            if m:
                pending.append(m.group(1))
                cur = None
                continue
            m = re.match(r'^(\w+)\s+word\s+(FID_\w+)\s*$', line)
            if m:
                entries[m.group(1)] = [m.group(2)]
                cur = ('entry', m.group(1))
                pending = []
                continue
            m = re.match(r'^\s+byte\s+length\s+(\w+)\s*,(.*),\s*(AFE_\w+)\s*$',
                         line)
            if m and cur and cur[0] == 'entry':
                entries[cur[1]] += [style_c(m.group(2)), m.group(3),
                                    m.group(1)]
                cur = None
                continue
            m = re.match(r'^(\w+)\s+char\s+"([^"]*)"', line)
            if m:
                names[m.group(1)] = m.group(2)
                continue
            m = re.match(r'^\s+(?:nptr|word)\s+(?:(?:offset\s+)?([A-Za-z_]\w*)|0)\s*$',
                         line)
            if m:
                if pending:
                    cur = ('list', list(pending))
                    for p in pending:
                        lists[p] = []
                        head[p] = pending[0]
                    order.append(pending[0])
                    pending = []
                if cur and cur[0] == 'list':
                    for p in cur[1]:
                        lists[p].append(m.group(1))
                    if m.group(1) is None:
                        cur = None
                continue
    return entries, names, lists, order, head


def parse_map_order(path):
    out, on = [], False
    for line in logical_lines(path):
        if re.match(r'^fontMapOffsets\s+label', line):
            on = True
            continue
        if on:
            m = re.match(r'^\s+nptr\s+(\w+)\s*$', line)
            if not m:
                break
            out.append(m.group(1))
    return out


def parse_faces(path):
    """the style bytes of all faces, as DefineFace puts them in one table,
    and where the sans serif and script faces start"""
    enums = {}
    allb = []
    starts = {}
    fam = None
    val = 0
    for line in logical_lines(path):
        m = re.match(r'^(\w+)\s+etype\s+byte(?:\s*,\s*(\w+))?', line)
        if m:
            val = int(m.group(2), 0) if m.group(2) else 0
            continue
        m = re.match(r'^(SF_\w+)\s+enum\s+\w+(?:\s*,\s*(\w+))?', line)
        if m:
            if m.group(2):
                val = int(m.group(2), 0)
            enums[m.group(1)] = val
            val += 1
            continue
        m = re.match(r'^DefineFace\s+(.*)$', line)
        if not m or re.match(r'macro\b', m.group(1)):
            continue
        args = [a.strip() for a in m.group(1).split(',')]
        if len(args) >= 3 and args[2]:
            fam = args[2]
            starts.setdefault(fam, len(allb))
        if len(args) < 2 or not args[1]:
            continue
        expr = args[1]
        v = 0
        for term in expr.split('+'):
            term = term.strip()
            v += enums[term] if term in enums else int(term, 0)
        allb.append(v & 0xff)
    return allb, starts


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-o', required=True)
    ap.add_argument('--fontid', required=True)
    ap.add_argument('--order', required=True)
    ap.add_argument('asm', nargs='+')
    a = ap.parse_args()

    entries, names, lists, order, head = parse_tables(a.asm)
    maps = parse_map_order(a.order)
    faces, starts = parse_faces(a.fontid)

    # the sets (lists of entries) and the font lists (lists of sets)
    def is_entry_list(lab):
        return all(x is None or x in entries for x in lists[lab])

    o = []
    o.append('/* epsFonts.c -- generated by Port32/tools/epsfonts2c.py from')
    o.append(' * %s and %s; do not edit */' %
             (' '.join(p.split('/')[-1] for p in a.asm),
              a.fontid.split('/')[-1]))
    o.append('#include "exportInt.h"')
    o.append('#include <fontID.h>')
    o.append('')
    used = set()
    for lab in lists:
        for x in lists[lab]:
            if x in entries:
                used.add(x)
    for lab in sorted(used, key=lambda k: list(entries).index(k)):
        e = entries[lab]
        if len(e) != 4:
            sys.exit('epsfonts2c: entry %s incomplete' % lab)
        o.append('static const EPSAdobeFont f_%s = { %s, %s, %s, %d, "%s" };' %
                 (lab, e[0], e[1], e[2], len(names[e[3]]), names[e[3]]))
    o.append('')
    done = set()
    for lab in order:
        if lab in done or not is_entry_list(lab):
            continue
        done.add(lab)
        items = ['&f_%s' % x for x in lists[lab] if x is not None]
        o.append('static const EPSAdobeFont *const s_%s[] = {' % lab)
        o.append('    ' + ',\n    '.join(items + ['NULL']))
        o.append('};')
    o.append('')
    for lab in order:
        if lab in done:
            continue
        done.add(lab)
        items = []
        for x in lists[lab]:
            if x is None:
                continue
            if x not in lists or not is_entry_list(x):
                sys.exit('epsfonts2c: %s in %s is no set' % (x, lab))
            items.append('s_%s' % x)
        o.append('static const EPSAdobeFont *const *const t_%s[] = {' % lab)
        o.append('    ' + ',\n    '.join(items + ['NULL']))
        o.append('};')
    o.append('')
    o.append('/* per PSFontList / 2 (fontMapOffsets) */')
    o.append('const EPSAdobeFont *const *const *const epsFontMaps[%d] = {' %
             len(maps))
    for lab in maps:
        if lab not in lists:
            sys.exit('epsfonts2c: font list %s unknown' % lab)
    # labels stacked on one list (Wang14 and Standard13) share it
    o.append('    ' + ',\n    '.join('t_%s' % head[lab] for lab in maps))
    o.append('};')
    o.append('')
    o.append('/* the face table of CalcFontDiff (fontID.def): the style of')
    o.append(' * each face that has one, in order; the serif faces from 0, the')
    o.append(' * sans serif and the script ones from epsFaceStart[1], [2], up to')
    o.append(' * [3] (the asm compared the index with ja: one past the end may')
    o.append(' * be read, the next table\'s first byte) */')
    rows = ['0x%02x' % v for v in faces] + ['0x00']
    o.append('const byte epsFaces[%d] = {' % len(rows))
    for i in range(0, len(rows), 12):
        o.append('    ' + ', '.join(rows[i:i + 12]) +
                 (',' if i + 12 < len(rows) else ''))
    o.append('};')
    s = [starts['FG_SERIF'], starts['FG_SANS_SERIF'], starts['FG_SCRIPT'],
         starts['FG_ORNAMENT']]
    o.append('const word epsFaceStart[4] = { %d, %d, %d, %d };' % tuple(s))
    with open(a.o, 'w') as f:
        f.write('\n'.join(o) + '\n')


if __name__ == '__main__':
    main()
