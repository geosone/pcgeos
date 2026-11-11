#!/usr/bin/env python3
"""vmcheck.py -- check the structure of a GEOS VM file (16-bit format).

    vmcheck.py [--strict] FILE...

--strict: also as the kernel leaves them (unassigned entries cleared,
VMH_usedSize right).

A file may start with a GEOS file header (256 bytes, signature
"\\xc7E\\xc1S", as the tools and DOS16 GEOS write them); positions are
relative to the data after it.  Checked: the VMFileHeader, the VM header,
the lists of unassigned and assigned (free space) entries, the counts,
and that used blocks and free space lie in the file without overlapping.
Exit code 1 if a file is broken.  An independent reading of
vmemConstant.def, used to check the files of the C kernel (Port32 VMem).
"""
import struct
import sys

GFH_SIG = b"\xc7E\xc1S"
GFH_SIZE = 256
VM_FILE_SIG = 0xADEB
VM_HDR_SIG = 0xFB
TABLE = 32
STRICT = False
ENTRY = 12


def check(path):
    data = open(path, "rb").read()
    if data[:4] == GFH_SIG:
        data = data[GFH_SIZE:]
    errs = []

    def err(msg):
        errs.append(msg)

    if len(data) < 24:
        return ["too short"]
    sig, hsize, hpos = struct.unpack_from("<HHI", data, 0)
    if sig != VM_FILE_SIG:
        return ["bad file signature %#x" % sig]
    if hpos + hsize > len(data) or hsize < TABLE + ENTRY:
        return ["header (%d bytes at %d) outside the file" % (hsize, hpos)]
    hdr = data[hpos:hpos + hsize]
    (hsig, assigned, last_assigned, unassigned, last_handle, n_assigned,
     n_unassigned, n_used, n_resident, n_extra, map_block, threshold,
     used_size, attrs, cflags, db_map) = struct.unpack_from(
        "<HHHHHHHHHHHhIBBH", hdr, 0)
    if hsig != VM_HDR_SIG:
        err("bad header signature %#x" % hsig)
    if last_handle > hsize or (last_handle - TABLE) % ENTRY or \
            last_handle < TABLE + ENTRY:
        return errs + ["bad VMH_lastHandle %d" % last_handle]
    handles = range(TABLE, last_handle, ENTRY)

    def used(h):
        return hdr[h + 2] & 1

    def u16(off):
        return struct.unpack_from("<H", hdr, off)[0]

    def u32(off):
        return struct.unpack_from("<I", hdr, off)[0]

    # the header's own entry repeats the VMFileHeader
    if not used(TABLE):
        err("header entry not used")
    if u16(TABLE + 6) != hsize or u32(TABLE + 8) != hpos:
        err("header entry (%d at %d) differs from the file header"
            % (u16(TABLE + 6), u32(TABLE + 8)))

    def valid(h):
        return h in handles

    seen = {}
    # unassigned list
    h, count = unassigned, 0
    while h:
        if not valid(h) or used(h) or h in seen:
            err("bad unassigned entry %#x" % h)
            break
        seen[h] = "unassigned"
        if STRICT and (u32(h + 4) or u32(h + 8)):
            # the kernel clears them (VMUnassignBlk); the tools do not
            err("unassigned entry %#x not cleared" % h)
        count += 1
        h = u16(h)
    if count != n_unassigned:
        err("VMH_numUnassigned %d, list has %d" % (n_unassigned, count))
    # assigned list: sorted, linked both ways
    spans = []
    h, prev, count, lastpos = assigned, 0, 0, -1
    while h:
        if not valid(h) or used(h) or h in seen:
            err("bad assigned entry %#x" % h)
            break
        seen[h] = "assigned"
        if u16(h + 2) != prev:
            err("assigned entry %#x: prev %#x, expected %#x"
                % (h, u16(h + 2), prev))
        size, pos = u32(h + 4), u32(h + 8)
        if not size:
            err("assigned entry %#x without space" % h)
        if pos <= lastpos:
            err("assigned list not sorted at %#x" % h)
        lastpos = pos
        spans.append((pos, size, "free %#x" % h))
        count += 1
        prev, h = h, u16(h)
    if prev != last_assigned:
        err("VMH_lastAssigned %#x, list ends at %#x" % (last_assigned, prev))
    if count != n_assigned:
        err("VMH_numAssigned %d, list has %d" % (n_assigned, count))
    # used entries
    nused = 0
    for h in handles:
        if used(h):
            nused += 1
            if h in seen:
                err("used entry %#x on a list" % h)
            size, pos = u16(h + 6), u32(h + 8)
            if size:
                spans.append((pos, size, "block %#x" % h))
        elif h not in seen:
            err("free entry %#x on no list" % h)
    if nused != n_used:
        err("VMH_numUsed %d, table has %d" % (n_used, nused))
    hdr_entry = u16(TABLE + 6)
    real_used = sum(u16(h + 6) for h in handles if used(h))
    if STRICT and used_size != real_used:
        err("VMH_usedSize %d, blocks have %d" % (used_size, real_used))
    if map_block and (not valid(map_block) or not used(map_block)):
        err("map block %#x not used" % map_block)
    # space: inside the file, after the file header, no overlaps
    spans.sort()
    end = 24
    for pos, size, what in spans:
        if pos < end:
            err("%s at %d overlaps (ends of previous: %d)" % (what, pos, end))
        if pos + size > len(data):
            err("%s at %d+%d beyond the end %d" % (what, pos, size,
                                                   len(data)))
        end = max(end, pos + size)
    return errs


def main():
    global STRICT
    bad = 0
    args = sys.argv[1:]
    if args and args[0] == "--strict":
        STRICT = True
        args = args[1:]
    for path in args:
        errs = check(path)
        print("%s: %s" % (path, "ok" if not errs else "BROKEN"))
        for e in errs:
            print("  " + e)
        bad |= bool(errs)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
