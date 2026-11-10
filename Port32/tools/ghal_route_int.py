#!/usr/bin/env python3
"""
ghal_route_int.py -- replace software interrupts in asm code by GHAL macros.

Plan phase S0 (Port32/PLAN.md): every host service the kernel and the
loaders call goes through a macro from Include/Internal/ghal.def.  The dos
backend expands each macro to the very `int` it replaces, so the build output
stays byte-identical (check with geocmp.py compare --code).

Only code lines are touched: `;` comments and COMMENT x...x blocks are skipped
(same parser as ghal_inventory.py).  Line endings and encoding are kept.

Usage:
    ghal_route_int.py [--dry-run] PATH...
"""
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ghal_inventory import asm_code_lines  # noqa: E402

# interrupt operand (lower case) -> GHAL macro
ROUTES = {
    "21h": "GHAL_DosCall",
    "10h": "GHAL_VideoBios", "0x10": "GHAL_VideoBios",
    "video_bios": "GHAL_VideoBios",
    "13h": "GHAL_DiskBios",
    "15h": "GHAL_SysBios", "apm_command_irq": "GHAL_SysBios",
    "16h": "GHAL_KbdBios",
    "19h": "GHAL_BootstrapBios",
    "1ah": "GHAL_ClockBios", "timer_rtc_bios_int": "GHAL_ClockBios",
    "25h": "GHAL_DosAbsRead",
    "26h": "GHAL_DosAbsWrite",
    "2fh": "GHAL_MultiplexCall",
    "67h": "GHAL_EmsCall", "emm_int": "GHAL_EmsCall",
}

INT_RX = re.compile(r"^([ \t]+)int([ \t]+)([A-Za-z0-9_]+)\b")


def route(path, dry_run):
    raw = open(path, "rb").read()
    text = raw.decode("latin-1")
    lines = text.splitlines(keepends=True)
    count = 0
    for n, code in asm_code_lines(text):
        m = INT_RX.match(code)
        if not m:
            continue
        macro = ROUTES.get(m.group(3).lower())
        if not macro:
            continue
        line = lines[n - 1]
        lines[n - 1] = m.group(1) + macro + line[m.end():]
        count += 1
    if count and not dry_run:
        open(path, "wb").write("".join(lines).encode("latin-1"))
    return count


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("paths", nargs="+")
    args = ap.parse_args()
    total = 0
    for top in args.paths:
        walk = [(top, [], [os.path.basename(top)])] if os.path.isfile(top) \
            else os.walk(top)
        for dirpath, _, files in walk:
            for fn in sorted(files):
                if os.path.splitext(fn)[1].lower() not in (".asm", ".def"):
                    continue
                path = dirpath if os.path.isfile(dirpath) \
                    else os.path.join(dirpath, fn)
                n = route(path, args.dry_run)
                if n:
                    print(f"{n:4d} {path}")
                    total += n
    print(f"{total} interrupts routed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
