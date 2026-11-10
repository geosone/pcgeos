#!/usr/bin/env python3
"""
geocmp.py -- byte-compare GEOS build outputs, ignoring build timestamps.

Used for plan phase P2 (Port32/PLAN.md): routing the asm kernel through the
GHAL must leave the real16 / pm16 builds byte-identical.  A plain `cmp`
does not work because every link stamps the output with the build time:

  * GEOS2 file header (all GEOS files): creation date/time at 200..203
  * .geo: GH_geodeSerial ("time stamp (SWAT uniqueness)",
    Include/Internal/geodeStr.def) at 256 + 42 = 298..299
  * .sym/.ldf: the user-info field (68..167) holds ":<serial>:" text

These fields are zeroed before hashing; everything else must match.

Usage:
    geocmp.py snapshot INSTALLED_DIR MANIFEST.json
    geocmp.py compare  MANIFEST.json INSTALLED_DIR

`compare` exits 1 if any file differs, is missing or is new.
"""
import argparse
import hashlib
import json
import os
import sys

EXTS = (".geo", ".exe", ".com", ".sym", ".ldf", ".vm")
GEOS2_MAGIC = b"\xc7\x45\xc1\x53"

# (start, end) byte ranges to mask, per extension; applied to GEOS2 files only
COMMON = [(200, 204)]                   # create_date + create_time
MASKS = {
    ".geo": COMMON + [(298, 300)],      # + GH_geodeSerial
    ".sym": COMMON + [(68, 168)],       # + info field with serial
    ".ldf": COMMON + [(68, 168)],
    ".vm":  COMMON,
}


def masked_digest(path):
    with open(path, "rb") as f:
        data = bytearray(f.read())
    ext = os.path.splitext(path)[1].lower()
    if data[:4] == GEOS2_MAGIC:
        for start, end in MASKS.get(ext, COMMON):
            data[start:min(end, len(data))] = bytes(max(0, min(end, len(data)) - start))
    return hashlib.sha256(data).hexdigest(), len(data)


def scan(root):
    result = {}
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames.sort()
        for name in sorted(filenames):
            if name.lower().endswith(EXTS):
                full = os.path.join(dirpath, name)
                rel = os.path.relpath(full, root)
                digest, size = masked_digest(full)
                result[rel] = {"sha256": digest, "size": size}
    return result


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("snapshot")
    s.add_argument("installed")
    s.add_argument("manifest")
    c = sub.add_parser("compare")
    c.add_argument("manifest")
    c.add_argument("installed")
    args = ap.parse_args()

    if args.cmd == "snapshot":
        files = scan(args.installed)
        with open(args.manifest, "w", encoding="utf-8") as f:
            json.dump(files, f, indent=1, sort_keys=True)
        print(f"{len(files)} files recorded in {args.manifest}")
        return 0

    with open(args.manifest, encoding="utf-8") as f:
        ref = json.load(f)
    cur = scan(args.installed)
    changed = sorted(k for k in ref if k in cur and ref[k] != cur[k])
    missing = sorted(k for k in ref if k not in cur)
    new = sorted(k for k in cur if k not in ref)
    for title, items in (("changed", changed), ("missing", missing), ("new", new)):
        for k in items:
            print(f"{title:8s} {k}")
    print(f"{len(ref)} reference files: {len(changed)} changed, "
          f"{len(missing)} missing, {len(new)} new")
    return 1 if changed or missing or new else 0


if __name__ == "__main__":
    sys.exit(main())
