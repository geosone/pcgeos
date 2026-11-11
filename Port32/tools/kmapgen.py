#!/usr/bin/env python3
"""
kmapgen.py -- turn a keyboard map of the asm keyboard driver
(Driver/Keyboard/**/kmap*.def, SBCS) into C for the C driver.

    kmapgen.py kmapGermanExt.def out.c [symbol]

The output defines `const KeyboardMap symbol' (default kbdKeyboardMap)
with the tables of the map (CInclude/Internal/kbdMap.h).  Values stay
symbolic (VC_*, C_*, KEY_*, KD_*, ...): the C compiler resolves them from
input.h, char.h, localize.h, kbdMap.h and Driver/Keyboard/keyboard.h, so
the generator only changes the syntax:

    byte a, b, c       values of the tables, in order
    x or y, mask EVB_Z x | y, EV_Z
    <C_BSW x> <>       x, 0
    'c' '''' '\\'      the character's code
    ExtendedDef <...>  one ExtendedDef; AccentDef <<...>> one AccentDef

The map is a sequence of labelled parts (kmapUS.def):
KbdHeader KbdConvHeader <...>, KbdExtendedScanCodes, KbdExtendedScanTable,
KbdKeyDefTable (up to KBD_MAX_SCAN), KbdExtendedDefTable,
KbdAccentables, KbdAccentTable.
"""
import re
import sys

PARTS = ["KbdExtendedScanCodes", "KbdExtendedScanTable", "KbdKeyDefTable",
         "KbdExtendedDefTable", "KbdAccentables", "KbdAccentTable"]


def strip(text):
    """Comments, COMMENT blocks and CRs out; quotes kept."""
    text = text.replace("\r", "")
    # COMMENT <c> ... <c>
    out, i = [], 0
    while True:
        m = re.search(r"(?im)^\s*COMMENT\s+(\S)", text[i:])
        if not m:
            out.append(text[i:])
            break
        out.append(text[i:i + m.start()])
        delim = m.group(1)
        end = text.find(delim, i + m.end())
        i = len(text) if end < 0 else end + 1
    text = "".join(out)
    lines = []
    for line in text.split("\n"):
        res, q = [], None
        for ch in line:
            if q:
                res.append(ch)
                if ch == q:
                    q = None
            elif ch in "'\"":
                q = ch
                res.append(ch)
            elif ch == ";":
                break
            else:
                res.append(ch)
        lines.append("".join(res))
    # line continuation with a backslash at the end
    return re.sub(r"\\[ \t]*\n", " ", "\n".join(lines))


def split_top(s, sep=","):
    """Split at sep outside quotes and <>."""
    parts, depth, q, cur = [], 0, None, []
    i = 0
    while i < len(s):
        ch = s[i]
        if q:
            cur.append(ch)
            if ch == q:
                if i + 1 < len(s) and s[i + 1] == q:   # '' inside quotes
                    cur.append(s[i + 1])
                    i += 1
                else:
                    q = None
        elif ch in "'\"":
            q = ch
            cur.append(ch)
        elif ch == "<":
            depth += 1
            cur.append(ch)
        elif ch == ">":
            depth -= 1
            cur.append(ch)
        elif ch == sep and depth == 0:
            parts.append("".join(cur).strip())
            cur = []
        else:
            cur.append(ch)
        i += 1
    if "".join(cur).strip():
        parts.append("".join(cur).strip())
    return parts


def value(v):
    """One asm value as a C expression."""
    v = v.strip()
    if not v or v == "<>":
        return "0"
    m = re.fullmatch(r"<\s*C_(?:BSW|CTRL|UI)\s+(.*)>", v, re.S)
    if m:
        return value(m.group(1))
    m = re.fullmatch(r"<(.*)>", v, re.S)
    if m:
        return value(m.group(1))
    m = re.fullmatch(r"(['\"])(.*)\1", v, re.S)
    if m:
        body = m.group(2).replace(m.group(1) * 2, m.group(1))
        if body == "\\\\":
            body = "\\"
        if len(body) != 1:
            raise ValueError("character %r" % v)
        return "0x%02x" % ord(body)

    def num(t):
        t = t.group(0)
        if re.fullmatch(r"[0-9][0-9a-fA-F]*[hH]", t):
            return "0x" + t[:-1]
        if re.fullmatch(r"[01]+[bB]", t):
            return str(int(t[:-1], 2))
        return t
    v = re.sub(r"\bmask\s+EVB_", "EV_", v)
    v = re.sub(r"\b(0[xX][0-9a-fA-F]+|[0-9][0-9a-fA-F]*[hH]|[01]+[bB]|[0-9]+)\b",
               num, v)
    v = re.sub(r"\bor\b", "|", v)
    v = re.sub(r"\band\b", "&", v)
    v = re.sub(r"\bnot\b", "~", v)
    v = re.sub(r"\bshl\b", "<<", v)
    if "'" in v or '"' in v or re.search(r"\bmask\b", v):
        raise ValueError("value %r" % v)
    return "(%s)" % v.strip() if " " in v.strip() else v.strip()


def bytes_of(text):
    vals = []
    for line in text.split("\n"):
        m = re.match(r"\s*(?:[A-Za-z_]\w*\s+)?byte\s+(.*)", line)
        if m:
            vals += [value(x) for x in split_top(m.group(1))]
    return vals


def table_of(text, name, width):
    """A table given as `byte' lines or as `name <...>,<...>'."""
    if re.search(r"\b%s\s*<" % name, text):
        vals = []
        for e in structs(text, name):
            v = [value(x) for x in split_top(e)]
            vals += (v + ["0"] * width)[:width]
        return vals
    return bytes_of(text)


def structs(text, name):
    """The <...> initialisers after `name' (ExtendedDef, AccentDef)."""
    i = text.find(name)
    if i < 0:
        raise ValueError("no " + name)
    body = text[i + len(name):]
    out, depth, start = [], 0, None
    q = None
    for k, ch in enumerate(body):
        if q:
            if ch == q:
                q = None
            continue
        if ch in "'\"":
            q = ch
        elif ch == "<":
            if depth == 0:
                start = k + 1
            depth += 1
        elif ch == ">":
            depth -= 1
            if depth == 0:
                out.append(body[start:k])
    return out


def section(text, label, nxt):
    i = re.search(r"\b%s\b" % label, text)
    if not i:
        raise ValueError("no " + label)
    rest = text[i.end():]
    j = re.search(r"\b%s\b" % nxt, rest) if nxt else None
    return rest[:j.start()] if j else rest


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    src, dst = sys.argv[1], sys.argv[2]
    sym = sys.argv[3] if len(sys.argv) > 3 else "kbdKeyboardMap"
    text = strip(open(src, encoding="latin-1").read())

    hdr = re.search(r"KbdHeader\s+KbdConvHeader\s*<(.*?)>", text, re.S)
    hv = split_top(hdr.group(1)) if hdr else []
    hv += ["0"] * (4 - len(hv))
    flags = value(hv[3]) if hv[3] not in ("", "0") else "0"
    flags = re.sub(r"\bmask\s+", "", flags)

    ext_codes = bytes_of(section(text, PARTS[0], PARTS[1]))
    ext_scan = table_of(section(text, PARTS[1], PARTS[2]), "ExtendedScanDef", 3)
    keydefs = table_of(section(text, PARTS[2], "KBD_MAX_SCAN"), "KeyDef", 4)
    extdefs = structs(section(text, PARTS[3], PARTS[4]), "ExtendedDef")
    accentables = bytes_of(section(text, PARTS[4], PARTS[5]))
    accents = structs(section(text, PARTS[5], None), "AccentDef")

    if len(keydefs) % 4:
        sys.exit("%s: %d KeyDef bytes" % (src, len(keydefs)))
    ext_codes = (ext_codes + ["0"] * 4)[:4]
    ext_scan = (ext_scan + ["0"] * 48)[:48]
    accentables = (accentables + ["0"] * 16)[:16]

    o = []
    o.append("/* Generated by Port32/tools/kmapgen.py from %s -- do not edit */"
             % src.split("Driver/Keyboard/")[-1])
    o.append('#include "keyboard.h"\n')
    o.append("static const byte extScanCodes[4] = { %s };" % ", ".join(ext_codes))
    o.append("static const ExtendedScanDef extScanTab[%d] = {" % KBD_NUM_EXTSCANMAPS)
    for k in range(0, 48, 3):
        lo, hi, mapped = ext_scan[k:k + 3]
        o.append("    { (word) ((%s) | ((%s) << 8)), %s }," % (lo, hi, mapped))
    o.append("};")
    o.append("static const KeyDef keyDefTab[%d] = {" % (len(keydefs) // 4))
    for k in range(0, len(keydefs), 4):
        o.append("    { %s },	/* scan 0x%02x */" % (", ".join(keydefs[k:k + 4]),
                                                    k // 4 + 1))
    o.append("};")
    o.append("static const ExtendedDef extDefTab[%d] = {" % max(len(extdefs), 1))
    for n, e in enumerate(extdefs):
        vals = [value(x) for x in split_top(e)]
        vals = (vals + ["0"] * 8)[:8]
        o.append("    { %s },	/* %d */" % (", ".join(vals), n))
    if not extdefs:
        o.append("    { 0 }")
    o.append("};")
    o.append("static const byte accentables[16] = { %s };" % ", ".join(accentables))
    o.append("static const AccentDef accentTab[16] = {")
    for e in accents[:16]:
        inner = re.sub(r"^\s*<(.*)>\s*$", r"\1", e, flags=re.S)
        vals = [value(x) for x in split_top(inner)]
        vals = (vals + ["0"] * 8)[:8]
        o.append("    { { %s } }," % ", ".join(vals))
    o.append("};\n")
    o.append("const KeyboardMap %s = {" % sym)
    o.append("    %s, %s, %s," % (value(hv[0]), value(hv[2]), flags))
    o.append("    extScanCodes, extScanTab, keyDefTab, %d, extDefTab," %
             (len(keydefs) // 4))
    o.append("    accentables, accentTab")
    o.append("};")
    open(dst, "w").write("\n".join(o) + "\n")


KBD_NUM_EXTSCANMAPS = 16

if __name__ == "__main__":
    main()
