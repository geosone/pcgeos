#!/usr/bin/env python3
# checkdecl.py DIR... : every "@extern method Class, MSG... {" definition
# of the .goc files under DIR needs its declaration
#     @extern method Class, MSG...;
# in the file that has "@classdecl Class".  Without it goc leaves the
# method out of the class's method table without a word: the build
# passes and the message goes to the superclass.
# Exit status 1 if one is missing (ctest goc.method.declared).
import re
import sys
import glob


def main(roots):
    files = []
    for r in roots:
        files += sorted(glob.glob(r + '/**/*.goc', recursive=True))
    txt = {f: open(f, errors='replace').read() for f in files}
    decl = {}       # class -> messages declared beside its @classdecl
    where = {}      # class -> files with its @classdecl
    for f, t in txt.items():
        for m in re.finditer(r'^@classdecl\s+(\w+)', t, re.M):
            where.setdefault(m.group(1), []).append(f)
        for m in re.finditer(r'^@extern method\s+(?:\w+\s*,\s*)?(\w+Class)'
                             r'\s*,([^;{]*);', t, re.M):
            if re.search(r'^@classdecl\s+%s\b' % m.group(1), t, re.M):
                for msg in re.findall(r'MSG_\w+|_reloc', m.group(2)):
                    decl.setdefault(m.group(1), set()).add(msg)
    bad = 0
    for f, t in txt.items():
        for m in re.finditer(r'^@extern method\s+(?:(\w+)\s*,\s*)?(\w+Class)'
                             r'\s*,([^;{]*)\{', t, re.M):
            c = m.group(2)
            for msg in re.findall(r'MSG_\w+|_reloc', m.group(3)):
                if msg not in decl.get(c, set()):
                    print('%s: %s %s not declared (classdecl: %s)'
                          % (f, c, msg, ', '.join(where.get(c, ['none']))))
                    bad += 1
    print('%d goc files, %d methods not declared' % (len(files), bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
