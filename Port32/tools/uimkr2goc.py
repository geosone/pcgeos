#!/usr/bin/env python3
"""uimkr2goc.py -- picture monikers and bitmaps of .ui files in goc.

Reads .ui files that define visMonikers with a gstring of their own
(GSSetAreaColorIndex, GSFillBitmapAtCP, GSDrawBitmapAtCP, Bitmap and the
bytes after it, GSEndString) and data chunks holding a Bitmap, for one
specific UI's conditions (#if NAME / #else / #endif), and writes them as
goc: each @visMoniker with its attributes and its gstring as bytes, each
chunk as an @chunk byte array, in data resources named as in the .ui.
Comments (/* */, //, and ; to the end of a line in a gstring) go.

usage: uimkr2goc.py out.goh file.ui ... [NAME=value ...]
The names default to the Motif build.  A .goh is meant to be included
by the goc file whose moniker lists use the monikers (goc needs a
moniker's attributes for a list); a .goc includes cGeode.goh itself.
"""
import re
import sys

DEFAULTS = {
    '_MOTIF': -1, '_ISUI': 0, '_STYLUS': 0, '_DUI': 0, '_OL_STYLE': 0,
    '_CUA_STYLE': -1, '_MENUS_PINNABLE': -1,
    'INCLUDE_CGA_MONIKERS': -1, 'INCLUDE_SM_MONIKERS': -1,
    'INCLUDE_SC_MONIKERS': -1, 'INCLUDE_LM_MONIKERS': -1,
    'INCLUDE_LC_MONIKERS': -1, 'INCLUDE_YM_MONIKERS': -1,
    'INCLUDE_YC_MONIKERS': -1,
    'WINDOW_CLOSE_BUTTON_IS_BIG_X': 0, 'THREE_DIMENSIONAL_BORDERS': 0,
}

BITMAP_HEADER = 6       # OB_width, OB_height, OB_compact, OB_type


def cond(expr, names):
    """the value of a #if expression: names, !, &&, ||, numbers"""
    e = re.sub(r'/\*.*?\*/', '', expr).strip()
    e = re.sub(r'\b([A-Za-z_]\w*)\b',
               lambda m: str(names.get(m.group(1), 0)), e)
    e = e.replace('&&', ' and ').replace('||', ' or ')
    e = re.sub(r'\bor\b', ' or ', e)
    e = re.sub(r'!(?!=)', ' not ', e)
    return bool(eval(e))


def preprocess(path, names):
    """the lines of the file whose conditions hold (#include ignored)"""
    out, stack = [], []
    text = re.sub(r'/\*.*?\*/', lambda m: '\n' * m.group(0).count('\n'),
                  open(path, encoding='latin-1').read(), flags=re.S)
    for line in text.split('\n'):
        s = line.split(';')[0].strip()
        # the assembler's conditionals inside a gstring
        m = re.match(r'(if|else|endif)\b(.*)', s)
        if m:
            s = '#' + m.group(1) + m.group(2)
        if s.startswith('#if'):
            on = all(stack)
            stack.append(on and cond(s[3:].replace('not ', '!'), names))
            stack[-1] = stack[-1] if on else False
            continue
        if s.startswith('#elif'):
            stack[-1] = False       # not used by the art files
            continue
        if s.startswith('#else'):
            outer = all(stack[:-1])
            stack[-1] = outer and not stack[-1]
            continue
        if s.startswith('#endif'):
            stack.pop()
            continue
        if s.startswith('#'):
            continue
        if all(stack):
            out.append(line)
    return out


def number(tok):
    """an asm number: 0101b, 0ffh, decimal"""
    t = tok.strip().lower()
    if t.endswith('b') and re.fullmatch(r'[01]+b', t):
        return int(t[:-1], 2)
    if t.endswith('h'):
        return int(t[:-1], 16)
    return int(t, 0)


BMF = {'BMF_MONO': 0, 'BMF_4BIT': 1, 'BMF_8BIT': 2, 'BMF_24BIT': 3,
       'BMF_4CMYK': 4, 'BMF_3CMY': 5}
BMT = {'BM_MASK': 0x10, 'BMT_MASK': 0x10, 'BMT_COMPLEX': 0x80,
       'BMT_HUGE': 0x40, 'BMT_PALETTE': 0x20, 'BMT_FORMAT': 0x07}


def bmtype(expr):
    """a BMType expression: <BMF_MONO>, mask BM_MASK or BMF_4BIT"""
    e = expr.replace('<', ' ').replace('>', ' ')
    v = 0
    for part in re.split(r'\bor\b', e):
        p = part.replace('mask', '').strip()
        if p in BMF:
            v |= BMF[p]
        elif p in BMT:
            v |= BMT[p]
        elif p:
            v |= number(p)
    return v


class Item:
    """a gstring element or bitmap: its goc text and size in bytes"""
    def __init__(self, text, size):
        self.text, self.size = text, size


def parse_data(lines):
    """Bitmap and bytes (and gstring elements) into items; a
    GSFillBitmapAtCP/GSDrawBitmapAtCP takes the size of the bitmap
    after it"""
    items = []
    pending = None          # index of a bitmap element waiting for its size
    body = ' '.join(l.split(';')[0] for l in lines)
    body = re.sub(r'\b\w+\s+label\s+byte\b', ' ', body)
    # statements: split before each keyword
    toks = re.split(r'(?=\b(?:GSBeginString|GSEndString|GSSetAreaColorIndex|'
                    r'GSFillBitmapAtCP|GSDrawBitmapAtCP|Bitmap|db|byte)\b)',
                    body)
    for t in toks:
        t = t.strip()
        if not t or t.startswith('GSBeginString'):
            continue
        if t.startswith('GSEndString'):
            items.append(Item('GSEndString()', 1))
        elif t.startswith('GSSetAreaColorIndex'):
            c = t.split(None, 1)[1].strip()
            items.append(Item('GSSetAreaColorIndex(%s)' % c, 2))
        elif t.startswith(('GSFillBitmapAtCP', 'GSDrawBitmapAtCP')):
            pending = len(items)
            items.append(Item(t.split()[0], 3))
        elif t.startswith('Bitmap'):
            m = re.match(r'Bitmap\s*<\s*([^,]+),\s*([^,]+),\s*([^,]+),\s*(.*)>\s*$', t)
            if not m:
                raise SystemExit('bad Bitmap: ' + t)
            w, h, c = (number(m.group(i)) for i in (1, 2, 3))
            items.append(Item('Bitmap(%d, %d, %d, 0x%02x)' %
                              (w, h, c, bmtype(m.group(4))), BITMAP_HEADER))
        elif t.startswith(('db', 'byte')):
            vals = [number(v) for v in t.split(None, 1)[1].split(',') if v.strip()]
            items.append(Item(', '.join('0x%02x' % v for v in vals), len(vals)))
        else:
            raise SystemExit('unknown: ' + t)
    # the sizes of the bitmap elements: what follows up to the next element
    for i, it in enumerate(items):
        if it.text in ('GSFillBitmapAtCP', 'GSDrawBitmapAtCP'):
            n = 0
            for j in items[i + 1:]:
                if j.text.startswith(('GS')):
                    break
                n += j.size
            it.text = '%s(%d)' % (it.text, n)
    return items


def convert(paths, names, header=False):
    """the goc text of the files"""
    res = {}                # resource -> [text]
    order = []
    for path in paths:
        lines = preprocess(path, names)
        i, resource = 0, None
        while i < len(lines):
            s = lines[i].strip()
            m = re.match(r'start\s+(\w+)', s)
            if m:
                resource = m.group(1)
                if resource not in res:
                    res[resource] = []
                    order.append(resource)
                i += 1
                continue
            if re.match(r'end\s+\w+', s):
                resource = None
                i += 1
                continue
            m = re.match(r'(visMoniker|chunk)\s+(\w+)\s*=\s*(data\s*)?\{', s)
            if not m:
                i += 1
                continue
            # the block up to its closing brace
            depth, block = 0, []
            while True:
                block.append(lines[i])
                depth += lines[i].count('{') - lines[i].count('}')
                i += 1
                if depth == 0:
                    break
            res[resource].append(emit(m.group(1), m.group(2), block))
    out = ['/*', ' * Generated by Port32/tools/uimkr2goc.py -- do not edit.',
           ' * From:'] + [' *   ' + p.split('/')[-1] for p in paths] + [' */', '']
    if not header:
        out.insert(len(out) - 1, '@include "cGeode.goh"')
    for r in order:
        if not res[r]:
            continue
        out.append('@start %s, data;' % r)
        out.append('')
        out.extend(res[r])
        out.append('@end %s;' % r)
        out.append('')
    return '\n'.join(out)


def emit(kind, name, block):
    text = '\n'.join(block)
    inner = text[text.index('{') + 1:text.rindex('}')]
    if kind == 'chunk':
        items = parse_data(inner.split('\n'))
        return wrap('@chunk byte %s[] = {' % name, items, '};')
    g = inner.index('gstring')
    attrs = inner[:g]
    gs = inner[inner.index('{', g) + 1:inner.rindex('}')]
    head = ['@visMoniker %s = {' % name]
    for a in attrs.split(';'):
        a = re.sub(r'\s+', ' ', a.split('//')[0]).strip()
        if a:
            head.append('    %s;' % a)
    items = parse_data(gs.split('\n'))
    return '\n'.join(head) + '\n' + wrap('    gstring {', items, '    }') + '\n}\n'


def wrap(first, items, last):
    out = [first]
    for k, it in enumerate(items):
        sep = ',' if k < len(items) - 1 else ''
        out.append('\t' + it.text + sep)
    out.append(last)
    return '\n'.join(out)


def main(argv):
    names = dict(DEFAULTS)
    files = []
    for a in argv[2:]:
        if '=' in a:
            k, v = a.split('=', 1)
            names[k] = int(v)
        else:
            files.append(a)
    open(argv[1], 'w').write(convert(files, names, argv[1].endswith('.goh')))


if __name__ == '__main__':
    main(sys.argv)
