#!/usr/bin/env python3
"""
ghal_inventory.py -- find every place where GEOS touches the host directly
(DOS, BIOS, DPMI, ports, interrupt vectors, interrupt flag, ...).

This is step 1 of plan phase P2 (Port32/PLAN.md): the inventory tells us
which GHAL entry points we need and where the code has to be routed through
them.  It is meant to be re-run (and later checked in CI) whenever upstream
is merged, so new host contacts show up immediately.

Usage:
    ghal_inventory.py [--root DIR] [--markdown FILE] [--json FILE]
                      [--check BASELINE.json]

--check exits non-zero if any area/category count grew compared to the
baseline (new host contacts that are not routed through the GHAL yet).
"""
import argparse
import json
import os
import re
import sys
from collections import defaultdict

# Directories that are not GEOS target code.
SKIP_DIRS = {"Installed", "Tools", "TechDocs", "Port32", ".git", "bin",
             "FontData", "DOSAppl"}
ASM_EXT = {".asm", ".def", ".ui"}
C_EXT = {".c", ".h", ".goc", ".goh"}

I = re.IGNORECASE

# Category -> list of regexes, applied to comment-stripped code lines.
ASM_RULES = {
    "dos_int21": [
        r"^\s*int\s+(21h|0x21|21)\b",
        r"^\s*call\s+(DOSUtilInt21|FileInt21|SysCallDOS)\b",
    ],
    "dos_other": [
        r"^\s*int\s+(2[02-9a-e]h|0x2[02-9a-e])\b",   # 20h,22h..2Eh (not 21h, 2Fh)
        r"^\s*int\s+(2fh|0x2f)\b",                    # multiplex
    ],
    "dpmi": [
        r"^\s*int\s+(31h|0x31)\b",
    ],
    "bios": [
        r"^\s*int\s+(1[0-9a-f]h|0x1[0-9a-f])\b",
        r"^\s*int\s+(VIDEO_BIOS|TIMER_RTC_BIOS_INT|KEYBOARD_BIOS\w*|DISK_BIOS\w*)\b",
    ],
    "ems_xms": [
        r"^\s*int\s+(67h|0x67|EMM_INT)\b",
        r"\bXMS(Call|Entry|Driver)\w*",
    ],
    "port_io": [
        r"^\s*(in|out)\s+(al|ax|eax|dx|[0-9a-fx]+h?|[A-Z_][A-Z_0-9]*)\s*,",
        r"^\s*(insb|insw|outsb|outsw|ins|outs)\b",
    ],
    "irq_vector": [
        r"\bSys(Catch|Reset)(Device)?Interrupt\b",
        r"\b(GPMI(Get|Set)(Interrupt|Exception)Handler)\b",
    ],
    "int_flag": [
        r"^\s*(INT_OFF|INT_ON|cli|sti)\b",
    ],
    "bios_lock": [
        r"\bSys(Lock|Unlock)BIOS\w*\b",
        r"\b(Lock|Unlock)DOS\b",
    ],
    "gpmi": [
        r"\bGPMI[A-Z]\w*",
        r"\bGPMIVectorTable\b",
    ],
    "hostif": [
        r"\bHostIf(Call|Detect)\w*",
        r"^\s*int\s+(0a0h|0xa0|HOST_API_INTERRUPT)\b",
    ],
    "breakpoint": [
        r"^\s*int\s+3\b",
        r"^\s*int3\b",
    ],
}

C_RULES = {
    "dos_int21": [r"\b(intdos|intdosx|bdos)\s*\(", r"\b_dos_\w+\s*\("],
    "bios": [r"\b_bios_\w+\s*\("],
    "c_int86": [r"\bint86x?\s*\("],
    "port_io": [r"\b(inp|outp|inpw|outpw|inportb|outportb)\s*\("],
    "int_flag": [r"\b(_disable|_enable)\s*\("],
    "inline_asm": [r"\b(_asm|__asm)\b", r"#\s*pragma\s+aux\b"],
    "bios_lock": [r"\bSys(Lock|Unlock)BIOS\w*\s*\("],
    "hostif": [r"\bHostIf(Call|Detect)\w*\s*\("],
}

ASM_RX = {k: [re.compile(p, I) for p in v] for k, v in ASM_RULES.items()}
C_RX = {k: [re.compile(p, I) for p in v] for k, v in C_RULES.items()}
COMMENT_BLOCK = re.compile(r"^\s*COMMENT\s+(\S)", I)


def asm_code_lines(text):
    """Yield (lineno, code) with esp comments removed (`;` and COMMENT x...x)."""
    in_block = None
    for n, line in enumerate(text.splitlines(), 1):
        if in_block:
            if in_block in line:
                in_block = None
            continue
        m = COMMENT_BLOCK.match(line)
        if m:
            delim = m.group(1)
            rest = line[m.end():]
            if delim not in rest:
                in_block = delim
            continue
        code = line.split(";", 1)[0]
        if code.strip():
            yield n, code


def c_code_lines(text):
    """Yield (lineno, code) with C comments removed (good enough heuristics)."""
    in_block = False
    for n, line in enumerate(text.splitlines(), 1):
        out = []
        i = 0
        while i < len(line):
            if in_block:
                j = line.find("*/", i)
                if j < 0:
                    i = len(line)
                else:
                    in_block = False
                    i = j + 2
            else:
                j = line.find("/*", i)
                k = line.find("//", i)
                if k >= 0 and (j < 0 or k < j):
                    out.append(line[i:k])
                    break
                if j < 0:
                    out.append(line[i:])
                    break
                out.append(line[i:j])
                in_block = True
                i = j + 2
        code = "".join(out)
        if code.strip():
            yield n, code


def area_of(rel):
    """Group a path into an area: Library/Kernel/<Sub>, Driver/<Class>/<Name>, ..."""
    p = rel.split("/")
    if p[0] == "Library" and len(p) > 2 and p[1] == "Kernel":
        return "/".join(p[:3]) if len(p) > 3 else "Library/Kernel"
    if p[0] == "Driver" and len(p) > 3:
        return "/".join(p[:3])
    if p[0] in ("Library", "Appl", "Driver") and len(p) > 2:
        return "/".join(p[:2])
    if len(p) > 1:
        return p[0]
    return "(root)"


def scan(root):
    hits = []          # (area, category, relpath, lineno, code)
    for dirpath, dirnames, filenames in os.walk(root):
        rel_dir = os.path.relpath(dirpath, root)
        top = rel_dir.split(os.sep)[0]
        if rel_dir == ".":
            dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        elif top in SKIP_DIRS:
            dirnames[:] = []
            continue
        for fn in filenames:
            ext = os.path.splitext(fn)[1].lower()
            if ext in ASM_EXT:
                rules, lines = ASM_RX, asm_code_lines
            elif ext in C_EXT:
                rules, lines = C_RX, c_code_lines
            else:
                continue
            path = os.path.join(dirpath, fn)
            rel = os.path.relpath(path, root).replace(os.sep, "/")
            with open(path, "rb") as f:
                text = f.read().decode("latin-1")
            area = area_of(rel)
            for n, code in lines(text):
                for cat, rxs in rules.items():
                    if any(rx.search(code) for rx in rxs):
                        hits.append((area, cat, rel, n, code.strip()))
    return hits


CORE_PREFIXES = ("Library/Kernel", "Loader", "Loader32")


def summarize(hits):
    by_area = defaultdict(lambda: defaultdict(int))
    for area, cat, *_ in hits:
        by_area[area][cat] += 1
    return {a: dict(c) for a, c in by_area.items()}


def write_markdown(hits, summary, out, ref):
    cats = sorted({c for a in summary.values() for c in a})
    total = defaultdict(int)
    for a in summary.values():
        for c, v in a.items():
            total[c] += v

    def row(name, counts):
        return "| " + name + " | " + " | ".join(
            str(counts.get(c, "")) for c in cats) + " | " + \
            str(sum(counts.values())) + " |"

    w = out.write
    w("# GHAL-Inventar: direkte Host-Kontakte\n\n")
    w(f"Erzeugt von `Port32/tools/ghal_inventory.py` auf `{ref}`.\n")
    w("Kommentare (`;`, `COMMENT x…x`, C-Kommentare) sind ausgenommen; "
      "nicht gescannt: " + ", ".join(f"`{d}/`" for d in sorted(SKIP_DIRS)) + ".\n\n")
    w("## Kategorien\n\n")
    w("| Kategorie | Bedeutung | GHAL-Ziel |\n|---|---|---|\n")
    legend = [
        ("dos_int21", "DOS-Aufrufe (`int 21h`, `DOSUtilInt21`, `FileInt21`, `intdos`)", "`ghal_fs`/`ghal_boot`, bzw. IFS/DOS-Treiber"),
        ("dos_other", "andere DOS-Interrupts (20h, 22h–2Eh, Multiplex 2Fh)", "`ghal_boot`/`ghal_exit`/Treiber"),
        ("dpmi", "DPMI `int 31h`", "`ghal_mem`/`ghal_irq` (dos32/pm16)"),
        ("bios", "BIOS 10h–1Fh (`VIDEO_BIOS` …)", "Treiber bzw. `ghal_display`/`ghal_time`"),
        ("ems_xms", "EMS/XMS", "entfällt (Swap) bzw. `ghal_mem`"),
        ("port_io", "Port-I/O `in`/`out`", "nur in DOS-Treibern; Kernel → `ghal_time`/`ghal_irq`"),
        ("irq_vector", "Interrupt-/Exception-Vektoren", "`ghal_irq`"),
        ("int_flag", "`INT_OFF`/`INT_ON`/`cli`/`sti`", "`ghal_crit`"),
        ("bios_lock", "`SysLockBIOS`/DOS-Sperre", "`ghal_hostlock`"),
        ("gpmi", "GPMI (PM-Branch)", "DOS-Seite von `ghal_mem`/`ghal_irq`"),
        ("hostif", "HostIf (Basebox)", "Host-Dienste der GHAL"),
        ("breakpoint", "`int 3`", "`ghal_log` (Breakpoint)"),
        ("c_int86", "C: `int86`", "wie die jeweilige Interrupt-Klasse"),
        ("inline_asm", "C: Inline-Assembler / `#pragma aux`", "nach C bzw. GHAL"),
    ]
    for c, d, g in legend:
        if c in total:
            w(f"| `{c}` | {d} | {g} |\n")
    w("\n## Gesamt\n\n")
    w("| | " + " | ".join(f"`{c}`" for c in cats) + " | Summe |\n")
    w("|---|" + "---|" * (len(cats) + 1) + "\n")
    w(row("**alle**", total) + "\n\n")

    def table(title, areas):
        w(f"## {title}\n\n")
        w("| Bereich | " + " | ".join(f"`{c}`" for c in cats) + " | Summe |\n")
        w("|---|" + "---|" * (len(cats) + 1) + "\n")
        for a in sorted(areas, key=lambda a: -sum(summary[a].values())):
            w(row(f"`{a}`", summary[a]) + "\n")
        w("\n")

    core = [a for a in summary if a.startswith(CORE_PREFIXES)]
    drivers = [a for a in summary if a.startswith("Driver")]
    rest = [a for a in summary if a not in core and a not in drivers]
    table("GHAL-Kern: Kernel und Loader", core)
    table("Treiber", drivers)
    table("Libraries, Apps, Include", rest)

    w("## Einzelstellen im GHAL-Kern (Kernel, Loader, Loader32)\n\n")
    w("Ohne `int_flag` (zu zahlreich; siehe Tabelle). Format: `Datei:Zeile` Code.\n\n")
    cur = None
    for area, cat, rel, n, code in sorted(
            (h for h in hits if h[0].startswith(CORE_PREFIXES)
             and h[1] != "int_flag"),
            key=lambda h: (h[0], h[1], h[2], h[3])):
        key = (area, cat)
        if key != cur:
            w(f"\n**{area} – `{cat}`**\n\n")
            cur = key
        w(f"- `{rel}:{n}` `{code[:90]}`\n")
    w("\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    ap.add_argument("--root", default=os.path.join(os.path.dirname(__file__), "..", ".."))
    ap.add_argument("--markdown")
    ap.add_argument("--json")
    ap.add_argument("--check")
    ap.add_argument("--ref", default="")
    args = ap.parse_args()

    root = os.path.abspath(args.root)
    hits = scan(root)
    summary = summarize(hits)
    if args.markdown:
        with open(args.markdown, "w", encoding="utf-8") as f:
            write_markdown(hits, summary, f, args.ref or root)
    if args.json:
        with open(args.json, "w", encoding="utf-8") as f:
            json.dump(summary, f, indent=1, sort_keys=True)
    if args.check:
        with open(args.check, encoding="utf-8") as f:
            base = json.load(f)
        grown = []
        for a, cs in summary.items():
            for c, v in cs.items():
                b = base.get(a, {}).get(c, 0)
                if v > b:
                    grown.append(f"{a}: {c} {b} -> {v}")
        if grown:
            print("New host contacts not routed through the GHAL:")
            print("\n".join("  " + g for g in sorted(grown)))
            return 1
        print("No new host contacts.")
    if not (args.markdown or args.json or args.check):
        total = defaultdict(int)
        for cs in summary.values():
            for c, v in cs.items():
                total[c] += v
        for c, v in sorted(total.items(), key=lambda x: -x[1]):
            print(f"{v:6d}  {c}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
