#!/usr/bin/env python3
"""gpgeode.py -- the GEOS data of a geode module from its .gp file.

Usage:  gpgeode.py [-o OUT.c] [--rev FILE.rev] [-D NAME]... [--res RES.h]
                   [--objs OBJS.h] GEODE.gp [FILE.goc...]

Writes the C definition of __geos_module (Port32/include/geosmod.h): what
glue put into the geode header from the geode parameter file.  Resource
numbers are those of gpres.py (the .gp "resource" lines, then the @start
resources of the .goc files); resources made by goc -cg point to their
GocResource descriptors.  --res and --objs name the headers of gpres.py
and of cmake/GeosGocObjs.cmake to include (for GOCR_RES_* and the
chunk constants of "appobj").  -D defines a name for ifdef/ifndef.

Handled like glue (Tools/glue/parse.y): name, longname, tokenchars,
tokenid, type (attributes; without "single" the geode is multi-launchable),
class, appobj, entry, heapspace, stack, library [noload [fixed]],
resource <name> <flags> (RESF_* as glue computes them), export [x as y],
publish, skip <n>, skip until <n>, ifdef/ifndef/else/endif.  Ignored:
usernotes, platform, ship, exempt, nosort, incminor, load, rev.

The protocol comes from the first "P major.minor" line of the .rev file
(0.0 without one).  The protocol a library needs is 0.0 ("any") for now:
glue took it from the library's .ldf file.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gpres  # noqa: E402

GA = {'process': 0x8000, 'library': 0x4000, 'driver': 0x2000,
      'system': 0x0800, 'appl': 0x0200, 'uses-coproc': 0x0020,
      'needs-coproc': 0x0010, 'has-gcm': 0x0008, 'c-api': 0x0004,
      'single': 0, 'discardable-dgroup': 0}
GA_MULTI_LAUNCHABLE = 0x0400

RESF_DISCARDED, RESF_LMEM, RESF_SWAPABLE = 0x0002, 0x0008, 0x0010
RESF_DISCARDABLE, RESF_SHARED, RESF_FIXED = 0x0020, 0x0040, 0x0080
RESF_CONFORMING, RESF_CODE, RESF_OBJECT = 0x0100, 0x0200, 0x0400
RESF_READ_ONLY, RESF_UI = 0x0800, 0x1000
RESF_STANDARD = 0x0032


def res_flags(args):
    """RESF_* of a resource line, as glue's resArg rules compute them."""
    f = RESF_STANDARD & ~RESF_DISCARDABLE
    for a in args:
        if a == 'read-only':
            f |= RESF_READ_ONLY
            if not f & RESF_FIXED:
                f |= RESF_DISCARDABLE
        elif a == 'discardable':
            if not f & RESF_FIXED:
                f |= RESF_DISCARDABLE
        elif a == 'discard-only':
            f = (f & ~RESF_SWAPABLE) | RESF_DISCARDABLE
        elif a == 'swap-only':
            f = (f & ~RESF_DISCARDABLE) | RESF_SWAPABLE
        elif a == 'swapable':
            f |= RESF_SWAPABLE
        elif a == 'no-swap':
            f &= ~RESF_SWAPABLE
        elif a == 'no-discard':
            f &= ~RESF_DISCARDABLE
        elif a == 'preload':
            f &= ~RESF_DISCARDED
        elif a == 'fixed':
            f = (f | RESF_FIXED) & ~(RESF_SWAPABLE | RESF_DISCARDABLE |
                                     RESF_DISCARDED | RESF_LMEM)
        elif a == 'conforming':
            f |= RESF_CONFORMING
        elif a == 'shared':
            f |= RESF_SHARED
        elif a == 'code':
            f |= RESF_CODE
        elif a in ('data', 'ui-object', 'object', 'lmem'):
            if a == 'lmem':
                f = (f | RESF_LMEM) & ~RESF_CODE
                continue
            if a == 'data':
                f &= ~(RESF_CODE | RESF_DISCARDABLE)
            elif a == 'ui-object':
                f = (f | RESF_OBJECT | RESF_UI | RESF_LMEM | RESF_SHARED) \
                    & ~(RESF_CODE | RESF_DISCARDABLE)
            else:
                f = (f | RESF_OBJECT | RESF_LMEM) \
                    & ~(RESF_CODE | RESF_UI | RESF_DISCARDABLE)
            if f & (RESF_READ_ONLY | RESF_FIXED) == RESF_READ_ONLY:
                f |= RESF_DISCARDABLE
        else:
            raise ValueError(f'unknown resource flag "{a}"')
    return f


def tokens(line):
    """Words of a .gp line; a quoted string is one word; # starts a comment."""
    out, i = [], 0
    while i < len(line):
        c = line[i]
        if c == '#':
            break
        if c.isspace() or c == ',':
            i += 1
        elif c == '"':
            j = line.index('"', i + 1)
            out.append(line[i:j + 1])
            i = j + 1
        else:
            j = i
            while j < len(line) and not line[j].isspace() and \
                    line[j] not in ',#':
                j += 1
            out.append(line[i:j])
            i = j
    return out


class Gp:
    def __init__(self, text, defines):
        self.name = self.ext = ''
        self.longname = None
        self.tokenchars = '\0\0\0\0'
        self.tokenid = 0
        self.attrs = 0
        self.typed = False
        self.process_class = self.appobj = self.entry = None
        self.heapspace = self.stack = 0
        self.libraries = []     # (name, flags)
        self.exports = []       # (symbol or None, name, published)
        self.resources = {}     # upper name -> flags
        stack = []              # ifdef: is the enclosing part active?
        active = True
        for lineno, raw in enumerate(text.splitlines(), 1):
            w = tokens(raw)
            if not w:
                continue
            kw = w[0].lower()
            if kw in ('ifdef', 'ifndef'):
                stack.append(active)
                defined = len(w) > 1 and w[1] in defines
                active = active and (defined if kw == 'ifdef' else
                                     not defined)
                continue
            if kw == 'else':
                active = stack[-1] and not active if stack else not active
                continue
            if kw == 'endif':
                active = stack.pop() if stack else True
                continue
            if not active:
                continue
            try:
                self.line(kw, w[1:])
            except (ValueError, IndexError) as e:
                sys.exit(f'line {lineno}: {e or "syntax error"}: {raw.strip()}')
        if self.typed and not self.attrs_single:
            self.attrs |= GA_MULTI_LAUNCHABLE

    attrs_single = False

    def line(self, kw, a):
        if kw == 'name':
            base, _, ext = a[0].partition('.')
            self.name, self.ext = base[:8], ext[:4]
        elif kw == 'longname':
            self.longname = a[0].strip('"')
        elif kw == 'tokenchars':
            self.tokenchars = (a[0].strip('"') + '\0' * 4)[:4]
        elif kw == 'tokenid':
            self.tokenid = int(a[0], 0)
        elif kw == 'type':
            self.typed = True
            for t in a:
                t = t.lower()
                if t not in GA:
                    raise ValueError(f'unknown type "{t}"')
                self.attrs |= GA[t]
                if t == 'single':
                    self.attrs_single = True
        elif kw == 'class':
            self.process_class = a[0]
        elif kw == 'appobj':
            self.appobj = a[0]
        elif kw == 'entry':
            self.entry = a[0]
        elif kw == 'heapspace':
            self.heapspace = int(a[0].rstrip('kK'), 0) * \
                (1024 // 16 if a[0][-1:] in 'kK' else 1)
        elif kw == 'stack':
            self.stack = int(a[0], 0)
        elif kw == 'library':
            flags = 0
            rest = [x.lower() for x in a[1:]]
            if rest[:1] == ['noload']:
                flags = 2 if rest[1:2] == ['fixed'] else 1
            self.libraries.append((a[0], flags))
        elif kw == 'resource':
            if a[0].lower() == 'ifdef':
                return
            self.resources[a[0].upper()] = res_flags([x.lower()
                                                      for x in a[1:]])
        elif kw == 'export':
            i = 0
            while i < len(a):
                sym = a[i]
                if i + 2 < len(a) and a[i + 1].lower() == 'as':
                    self.exports.append((sym, a[i + 2], False))
                    i += 3
                else:
                    self.exports.append((sym, sym, False))
                    i += 1
        elif kw == 'publish':
            self.exports.append((a[0], a[0], True))
        elif kw == 'skip':
            if a[0].lower() == 'until':
                if not re.fullmatch(r'\d+', a[1]):
                    raise ValueError('"skip until <constant>" not supported')
                n = int(a[1]) - len(self.exports)
            else:
                n = int(a[0], 0)
            self.exports.extend([(None, None, False)] * max(n, 0))
        elif kw in ('usernotes', 'platform', 'ship', 'exempt', 'nosort',
                    'incminor', 'load', 'rev'):
            pass
        else:
            raise ValueError(f'unknown keyword "{kw}"')


def goc_starts(path):
    """(name, is_data) of the @start lines of a .goc file"""
    with open(path, encoding='latin-1') as f:
        text = re.sub(r'/\*.*?\*/', '', f.read(), flags=re.DOTALL)
    return [(m.group(1), 'data' in m.group(2).lower())
            for m in re.finditer(r'^\s*@start\s+(\w+)([^;\n]*)', text,
                                 re.MULTILINE)]


def protocol(rev):
    if rev and os.path.exists(rev):
        with open(rev, encoding='latin-1') as f:
            for line in f:
                m = re.match(r'P\s+(\d+)\.(\d+)', line)
                if m:
                    return int(m.group(1)), int(m.group(2))
    return 0, 0


def c_string(s):
    return '"' + ''.join(c if c.isprintable() and c not in '"\\'
                         else f'\\{ord(c):03o}' for c in s) + '"'


def main():
    args = sys.argv[1:]
    out = rev = res_h = objs_h = None
    defines = set()
    while args and args[0].startswith('-'):
        opt = args.pop(0)
        if opt == '-o':
            out = args.pop(0)
        elif opt == '--rev':
            rev = args.pop(0)
        elif opt == '--res':
            res_h = args.pop(0)
        elif opt == '--objs':
            objs_h = args.pop(0)
        elif opt.startswith('-D'):
            defines.add(opt[2:] or args.pop(0))
        else:
            sys.exit(__doc__)
    if not args or not args[0].lower().endswith('.gp'):
        sys.exit(__doc__)
    with open(args[0], encoding='latin-1') as f:
        gp_text = f.read()
    gp = Gp(gp_text, defines)

    # resource numbers exactly as gpres.py makes them
    names, case, goc_res = [], {}, {}
    for name in gpres.gp_resources(gp_text):
        if name.upper() not in names:
            names.append(name.upper())
            case[name.upper()] = name
    for path in args[1:]:
        for name, is_data in goc_starts(path):
            goc_res[name.upper()] = (name, is_data)
            if name.upper() not in names:
                names.append(name.upper())
                case[name.upper()] = name

    L = ['/* generated by Port32/tools/gpgeode.py from '
         + os.path.basename(args[0]) + ' */',
         '#include <geos.h>', '#include <object.h>', '#include <geosmod.h>']
    for h in (res_h, objs_h):
        if h:
            L.append(f'#include "{h}"')
    L.append('')
    if gp.process_class:
        L.append(f'extern ClassStruct {gp.process_class};')
    for up in names:
        if up in goc_res:
            L.append(f'extern const GocResource __goc_resource_{goc_res[up][0]};')
    syms = sorted({s for s, _, _ in gp.exports if s} |
                  ({gp.entry} if gp.entry else set()))
    for s in syms:
        if s != gp.process_class:
            L.append(f'extern char {s}[];')
    L.append('')

    count = gpres.FIRST + len(names)
    L.append(f'Handle __goc_resource_handles[{count}];')
    L.append('static const GeosModuleLibrary libraries[] = {')
    for name, flags in gp.libraries:
        L.append(f'    {{{c_string(name)}, 0, 0, {flags}}},')
    L.append('    {0, 0, 0, 0}\n};')
    L.append('static const GeosModuleExport exports[] = {')
    for sym, name, pub in gp.exports:
        if sym is None:
            L.append('    {0, 0, 0},')
        else:
            L.append(f'    {{(const void *) &{sym}, {c_string(name)}, '
                     f'{1 if pub else 0}}},')
    L.append('    {0, 0, 0}\n};')
    L.append('static const GeosModuleResource resources[] = {')
    L.append('    {0, "(core block)", 0},')
    L.append('    {0, "(fixed data)", 0},')
    for up in names:
        if up in gp.resources:
            flags = gp.resources[up]
        elif up in goc_res:
            # only in a .goc file: an LMem block (glue: object block
            # unless "@start x, data")
            flags = res_flags(['lmem'] if goc_res[up][1] else ['object'])
        else:
            flags = res_flags([])
        desc = f'&__goc_resource_{goc_res[up][0]}' if up in goc_res else '0'
        L.append(f'    {{{desc}, {c_string(case[up])}, 0x{flags:04x}}},')
    L.append('};')
    L.append('')
    major, minor = protocol(rev)
    appobj = f'GOCR_XSOPTR({gp.appobj})' if gp.appobj else '0'
    L += ['const GeosModuleInfo __geos_module = {',
          '    GEOS_MODULE_MAGIC, GEOS_MODULE_VERSION,',
          f'    {c_string(gp.name.ljust(8))}, {c_string(gp.ext.ljust(4))},',
          f'    {c_string(gp.longname) if gp.longname is not None else "0"},',
          f'    {{{{{", ".join(str(ord(c)) for c in gp.tokenchars)}}}, {gp.tokenid}}},',
          f'    0x{gp.attrs:04x}, {major}, {minor}, {gp.heapspace}, {gp.stack},',
          f'    {"&" + gp.process_class if gp.process_class else "0"},',
          f'    {appobj},',
          f'    {"(const void *) &" + gp.entry if gp.entry else "0"},',
          f'    {len(gp.libraries)}, libraries,',
          f'    {len(gp.exports)}, exports,',
          f'    {count}, resources,',
          '    __goc_resource_handles',
          '};']
    text = '\n'.join(L) + '\n'
    if out:
        with open(out, 'w') as f:
            f.write(text)
    else:
        sys.stdout.write(text)


if __name__ == '__main__':
    main()
