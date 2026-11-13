#!/usr/bin/env python3
"""asmchars2c.py -- the char data of asm files as C.

The PostScript translation library (Library/Trans/Graphics/Vector/EPS)
keeps its PostScript text as char data in asm resources:

    emitDL      char    " DL", NL       ; comment
    beginPSProlog   label   byte
        char    "%%BeginProlog", NL
    endPSProlog     label   byte

Every name with char data becomes a C array, e.g.

    const char emitDL[] = " DL\\r\\n";

and the bytes between "<name> label byte" and the next label become the
array <name> (the prolog: beginPSProlog up to endPSProlog), with
<name>Size, its length; a label named end* only ends a block.  A line
ending in a backslash goes on in the next.  Lengths are sizeof - 1, as "length <name>" was
in the asm.

Values: "strings" (with "" for a quote), NL (CR LF), C_CR, C_LF,
C_CTRL_D, numbers (decimal, 0x.., ..h).  COMMENT @ ... @ blocks and ;
comments are skipped.

With -H, a header declares the arrays with their sizes, so that
sizeof works on them in other files:

    extern const char emitDL[6];

usage: asmchars2c.py -o out.c [-H out.h] file.asm...
"""
import argparse
import re
import sys

CONSTS = {"NL": b"\r\n", "C_CR": b"\r", "C_LF": b"\n", "C_CTRL_D": b"\x04",
          "C_NULL": b"\0", "C_TAB": b"\t"}

DEF = re.compile(r"^([A-Za-z_]\w*)\s+char\s+(.*)$")
CONT = re.compile(r"^\s+char\s+(.*)$")
LABEL = re.compile(r"^([A-Za-z_]\w*)\s+label\s+byte\b", re.I)


def strip_comment(s):
    """the line without its ; comment (not inside a string)"""
    out, q = [], False
    for ch in s:
        if ch == '"':
            q = not q
        elif ch == ';' and not q:
            break
        out.append(ch)
    return "".join(out).strip()


def values(s, where):
    """the bytes of a comma separated value list"""
    data = b""
    i = 0
    while i < len(s):
        ch = s[i]
        if ch in " \t,":
            i += 1
        elif ch == '"':
            j = i + 1
            buf = []
            while True:
                if j >= len(s):
                    sys.exit("%s: open string" % where)
                if s[j] == '"':
                    if j + 1 < len(s) and s[j + 1] == '"':
                        buf.append('"')
                        j += 2
                        continue
                    break
                buf.append(s[j])
                j += 1
            data += "".join(buf).encode("latin-1")
            i = j + 1
        else:
            m = re.match(r"[\w]+", s[i:])
            if not m:
                sys.exit("%s: cannot read %r" % (where, s[i:]))
            tok = m.group(0)
            i += len(tok)
            if tok in CONSTS:
                data += CONSTS[tok]
            elif re.fullmatch(r"0x[0-9a-fA-F]+", tok):
                data += bytes([int(tok, 16)])
            elif re.fullmatch(r"[0-9][0-9a-fA-F]*h", tok):
                data += bytes([int(tok[:-1], 16)])
            elif tok.isdigit():
                data += bytes([int(tok)])
            else:
                sys.exit("%s: unknown value %s" % (where, tok))
    return data


def parse(path, arrays):
    with open(path, encoding="latin-1") as f:
        lines = f.read().split("\n")
    # a line ending in a backslash goes on in the next (kept as one line,
    # so that the line numbers stay right for the first)
    for n in range(len(lines) - 1):
        while strip_comment(lines[n].rstrip("\r")).endswith("\\"):
            s = strip_comment(lines[n].rstrip("\r"))[:-1]
            nxt = lines[n + 1].rstrip("\r").strip()
            lines[n] = s + " " + nxt
            lines[n + 1] = ""
            for k in range(n + 1, len(lines) - 1):
                if lines[k]:
                    break
    in_comment = False
    cur = None          # name collecting data
    label = None        # name of an open label block
    for n, raw in enumerate(lines, 1):
        where = "%s:%d" % (path, n)
        line = raw.rstrip("\r")
        if in_comment:
            if "@" in line:
                in_comment = False
            continue
        if re.match(r"^\s*COMMENT\s*@", line):
            in_comment = "@" not in line.split("@", 1)[1]
            continue
        s = strip_comment(line)
        if not s:
            continue
        m = LABEL.match(s)
        if m:
            if label:
                arrays.append([label[0], label[1], True])
            # an end label only ends the block before it
            label = (None if m.group(1).startswith("end")
                     else (m.group(1), b""))
            cur = None
            continue
        m = DEF.match(s)
        if m:
            data = values(m.group(2), where)
            if label:
                label = (label[0], label[1] + data)
            arrays.append([m.group(1), data])
            cur = arrays[-1]
            continue
        m = CONT.match(line.split(";")[0]) if line[:1] in " \t" else None
        if m or (line[:1] in " \t" and s.startswith("char")):
            data = values(strip_comment(line).split(None, 1)[1], where)
            if label:
                label = (label[0], label[1] + data)
            elif cur is not None:
                cur[1] += data
            continue
        cur = None      # anything else ends a definition
    if label and label[1]:
        sys.exit("%s: label %s has no end label" % (path, label[0]))


def c_string(data):
    out, col = ['"'], 1
    for b in data:
        if b == 0x22:
            t = '\\"'
        elif b == 0x5c:
            t = "\\\\"
        elif b == 0x0d:
            t = "\\r"
        elif b == 0x0a:
            t = "\\n"
        elif 0x20 <= b < 0x7f:
            t = chr(b)
        else:
            t = "\\%03o" % b
        out.append(t)
        col += len(t)
        if b == 0x0a and col > 1:
            out.append('"\n    "')
            col = 1
    out.append('"')
    s = "".join(out)
    return s[:-len('\n    ""')] if s.endswith('\n    ""') else s


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", required=True)
    ap.add_argument("-H")
    ap.add_argument("files", nargs="+")
    args = ap.parse_args()
    arrays = []
    for p in args.files:
        parse(p, arrays)
    seen = set()
    with open(args.o, "w") as f:
        f.write("/* generated by Port32/tools/asmchars2c.py from %s */\n\n"
                % ", ".join(p.split("/")[-1] for p in args.files))
        for a in arrays:
            name, data = a[0], a[1]
            if name in seen:
                sys.exit("%s defined twice" % name)
            seen.add(name)
            f.write("const char %s[] =\n    %s;\n" % (name, c_string(data)))
            if len(a) > 2:      # a label block: its size too
                f.write("const unsigned short %sSize = sizeof(%s) - 1;\n"
                        % (name, name))
    if args.H:
        guard = re.sub(r"\W", "_", args.H.split("/")[-1]).upper()
        with open(args.H, "w") as f:
            f.write("/* generated by Port32/tools/asmchars2c.py */\n"
                    "#ifndef %s\n#define %s\n\n" % (guard, guard))
            for x in arrays:
                f.write("extern const char %s[%d];\n" % (x[0], len(x[1]) + 1))
                if len(x) > 2:
                    f.write("extern const unsigned short %sSize;\n" % x[0])
            f.write("\n#endif\n")


if __name__ == "__main__":
    main()
