#!/usr/bin/env python3
"""
localgen.py -- the tables of the asm localization code
(Library/Kernel/Local, SBCS) as C for the C kernel.

    localgen.py -o out.c --chars char.def --lexical lexical.def file.asm...

Each data table of the files becomes a C array named "local" + the asm
label (UpcaseTable -> localUpcaseTable), each "NAME = value" constant a
const word LOCAL_NAME.  The tables are:

    Label Type \\            values separated by commas, over lines
        v, v, ...
    Label label byte         then lines of "db v[, v]..."

Type is a known one: CharClass (a record of 12 one-bit fields and 4
unused bits, a word), LexicalOrder and Lex1stOrder (byte enums of
Include/Internal/lexical.def), Chars and Char (bytes; Chars from
Include/char.def).  A value is a number (0x10, 10h, 16), a character
('a'), a symbol of those enums or the constants before it, or a record
initializer Type<...>.  Conditionals take the SBCS side (DBCS_PCGEOS is
0).  The values are resolved here: the C file has numbers, with the
asm's comment of each.
"""
import argparse
import os
import re
import sys

BYTE_TYPES = {"LexicalOrder", "Lex1stOrder", "Chars", "Char", "byte"}
WORD_TYPES = {"CharClass"}
CHARCLASS_FIELDS = 12
# symbols of conditionals
CONDS = {"DBCS_PCGEOS": 0}


def strip_comment(line):
    out, q = [], None
    for ch in line:
        if q:
            out.append(ch)
            if ch == q:
                q = None
            continue
        if ch in "'\"":
            q = ch
        elif ch == ";":
            break
        out.append(ch)
    return "".join(out)


def cond_value(expr):
    expr = expr.strip()
    neg = False
    if expr.startswith("not "):
        neg, expr = True, expr[4:].strip()
    v = CONDS.get(expr)
    if v is None:
        raise ValueError("unknown condition: " + expr)
    return (not v) if neg else bool(v)


def lines_sbcs(path):
    """the lines of a file with the conditionals taken, comments kept"""
    stack = []          # (active, taken)
    active = True
    out = []
    in_comment = False
    with open(path, encoding="latin1") as f:
        for raw in f:
            line = raw.rstrip("\r\n")
            if in_comment:
                if "@" in line or "}" in line:
                    in_comment = False
                continue
            s = line.strip()
            if s.startswith("COMMENT"):
                rest = s[len("COMMENT"):].strip()
                if rest and rest[0] in "@}" and \
                        rest[0] not in rest[1:]:
                    in_comment = True
                continue
            m = re.match(r"(if|elseif|else|endif)\b\s*(.*)", s)
            if m:
                kw, arg = m.group(1), strip_comment(m.group(2)).strip()
                if kw == "if":
                    stack.append((active, False))
                    v = active and cond_value(arg)
                    stack[-1] = (active, v)
                    active = v
                elif kw == "elseif":
                    outer, taken = stack[-1]
                    v = outer and not taken and cond_value(arg)
                    stack[-1] = (outer, taken or v)
                    active = v
                elif kw == "else":
                    outer, taken = stack[-1]
                    active = outer and not taken
                    stack[-1] = (outer, True)
                else:
                    active = stack.pop()[0]
                continue
            if active:
                out.append(line)
    return out


def read_enums(path, syms):
    """NAME enum Type[, value] after "Type etype size[, first[, step]]" """
    types = {}
    for line in lines_sbcs(path):
        s = strip_comment(line).strip()
        m = re.match(r"(\w+)\s+etype\s+\w+\s*(?:,\s*([^,]+))?"
                     r"(?:,\s*(.+))?$", s)
        if m:
            first = evaluate(m.group(2), syms) if m.group(2) else 0
            step = evaluate(m.group(3), syms) if m.group(3) else 1
            types[m.group(1)] = [first, step]
            continue
        m = re.match(r"(\w+)\s*(?:=|\bequ\b)\s*(.+)$", s)
        if m:
            try:
                syms[m.group(1)] = evaluate(m.group(2), syms)
            except (ValueError, SyntaxError, NameError):
                pass
            continue
        m = re.match(r"(\w+)\s+enum\s+(\w+)\s*(?:,\s*(.+))?$", s)
        if m and m.group(2) in types:
            t = types[m.group(2)]
            if m.group(3):
                t[0] = evaluate(m.group(3), syms)
            syms[m.group(1)] = t[0]
            t[0] += t[1]


def evaluate(expr, syms):
    e = expr.strip()
    m = re.fullmatch(r"'\\(.)'", e)		# '\\' is the backslash
    if m:
        return ord(m.group(1))
    m = re.fullmatch(r"'(.)'", e)
    if m:
        return ord(m.group(1))
    if e == "''''":
        return ord("'")

    def num(mm):
        t = mm.group(0)
        if re.fullmatch(r"0x[0-9a-fA-F]+", t):
            return str(int(t, 16))
        if re.fullmatch(r"[0-9][0-9a-fA-F]*h", t, re.I):
            return str(int(t[:-1], 16))
        if re.fullmatch(r"[0-9]+", t):
            return t
        if t in syms:
            return str(syms[t])
        if t in ("shl", "shr", "or", "and", "not"):
            return {"shl": "<<", "shr": ">>", "or": "|", "and": "&",
                    "not": "~"}[t]
        raise ValueError("unknown symbol: " + t)
    py = re.sub(r"'(.)'", lambda mm: str(ord(mm.group(1))), e)
    py = re.sub(r"\b\w+\b", num, py)
    return int(eval(py, {"__builtins__": {}}))


def split_values(text):
    out, depth, cur = [], 0, []
    for ch in text:
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth -= 1
        if ch == "," and depth == 0:
            out.append("".join(cur))
            cur = []
        else:
            cur.append(ch)
    if "".join(cur).strip():
        out.append("".join(cur))
    return [v.strip() for v in out if v.strip()]


def value(v, typ, syms):
    m = re.fullmatch(r"(\w+)\s*<(.*)>", v)
    if m:
        if m.group(1) != "CharClass":
            raise ValueError("unknown record: " + v)
        bits = [evaluate(x, syms) for x in m.group(2).split(",")]
        w = 0
        for i, b in enumerate(bits[:CHARCLASS_FIELDS]):
            if b:
                w |= 1 << (15 - i)
        return w
    return evaluate(v, syms)


def parse_tables(path, syms):
    """[(label, ctype, [(value, comment)])], {name: value}"""
    tables, consts = [], {}
    lines = lines_sbcs(path)
    i = 0
    while i < len(lines):
        line = lines[i]
        s = strip_comment(line).strip()
        m = re.match(r"(\w+)\s*=\s*(.+)$", s)
        if m and not line[:1].isspace():
            try:
                syms[m.group(1)] = consts[m.group(1)] = \
                    evaluate(m.group(2), syms)
            except (ValueError, SyntaxError, NameError):
                pass
            i += 1
            continue
        m = re.match(r"(\w+)\s+label\s+byte$", s)
        if m:
            label, vals = m.group(1), []
            i += 1
            while i < len(lines):
                body = strip_comment(lines[i]).strip()
                com = lines[i][len(strip_comment(lines[i])):].lstrip(";")
                mm = re.match(r"db\s+(.+)$", body)
                if not body:
                    i += 1
                    continue
                if not mm:
                    break
                for v in split_values(mm.group(1)):
                    vals.append((value(v, "byte", syms), com.strip()))
                i += 1
            tables.append((label, "byte", vals))
            continue
        m = re.match(r"(\w+)\s*(\\)$", s)
        if m and not line[:1].isspace():
            # "Label \": the type is that of the records that follow
            nxt = strip_comment(lines[i + 1]).strip() if i + 1 < len(lines) \
                else ""
            rm = re.match(r"(\w+)\s*<", nxt)
            if rm and rm.group(1) in WORD_TYPES:
                s = "%s %s \\" % (m.group(1), rm.group(1))
        m = re.match(r"(\w+)\s+(\w+)\s*(\\)?\s*(.*)$", s)
        if m and (m.group(2) in BYTE_TYPES or m.group(2) in WORD_TYPES) \
                and not line[:1].isspace():
            label, typ = m.group(1), m.group(2)
            ctype = "word" if typ in WORD_TYPES else "byte"
            vals = []
            rest = m.group(4) if not m.group(3) else ""
            if rest:
                for v in split_values(rest):
                    vals.append((value(v, typ, syms), ""))
                tables.append((label, ctype, vals))
                i += 1
                continue
            i += 1
            while i < len(lines):
                body = strip_comment(lines[i]).strip()
                com = lines[i][len(strip_comment(lines[i])):].lstrip(";")
                if not body:
                    i += 1
                    if vals and not lines[i - 1].strip() == "":
                        continue
                    continue
                if not lines[i][:1].isspace():
                    break
                more = body.endswith(",")
                for v in split_values(body):
                    vals.append((value(v, typ, syms), com.strip()))
                i += 1
                if not more:
                    break
            tables.append((label, ctype, vals))
            continue
        i += 1
    return tables, consts


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", required=True)
    ap.add_argument("--chars", required=True, help="Include/char.def")
    ap.add_argument("--lexical", required=True,
                    help="Include/Internal/lexical.def")
    ap.add_argument("files", nargs="+")
    a = ap.parse_args()
    syms = {}
    read_enums(a.chars, syms)
    read_enums(a.lexical, syms)
    out = ["/* generated by Port32/tools/localgen.py from "
           + ", ".join(os.path.basename(f) for f in a.files)
           + " -- do not edit */",
           "#include <geos.h>", ""]
    for f in a.files:
        tables, consts = parse_tables(f, syms)
        out.append("/* " + os.path.basename(f) + " */")
        for name, v in consts.items():
            out.append("const word LOCAL_%s = %d;" % (name, v))
        for label, ctype, vals in tables:
            cname = "local" + label[0].upper() + label[1:]
            out.append("const %s %s[%d] = {" % (ctype, cname, len(vals)))
            fmt = "0x%04x" if ctype == "word" else "0x%02x"
            for n, (v, com) in enumerate(vals):
                c = (" /* %s */" % com.replace("*/", "* /")) if com else ""
                out.append("    " + fmt % (v & (0xffff if ctype == "word"
                                                else 0xff))
                           + ("," if n + 1 < len(vals) else "") + c)
            out.append("};")
        out.append("")
    with open(a.o, "w") as f:
        f.write("\n".join(out))


if __name__ == "__main__":
    sys.exit(main())
