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


VMBF_LMEM = 0x01
LMF_NO_HANDLES = 0x0080
LMEM_EMPTY = 0xffff


def check_lmem(blk, what, err):
    """an LMem block in the DOS16 layout (lmemCode.asm): 16-byte header,
    handle table at LMBH_offset, chunks with a size word (data + 2) in
    slots rounded to 4 after the table, free chunks on LMBH_freeList"""
    if len(blk) < 16:
        err("%s: LMem block of %d bytes" % (what, len(blk)))
        return
    (handle, offset, flags, ltype, bsize, nh, free, total_free) = \
        struct.unpack_from("<8H", blk, 0)
    if flags & LMF_NO_HANDLES:
        return
    if bsize > len(blk) or bsize < offset:
        err("%s: LMBH_blockSize %d (%d in the file)" % (what, bsize, len(blk)))
        return
    end = offset + 2 * nh
    if offset < 16 or offset & 1 or end > bsize:
        err("%s: handle table %d+%d outside the block" % (what, offset, nh))
        return
    slots = []
    for i in range(nh):
        e = struct.unpack_from("<H", blk, offset + 2 * i)[0]
        if e in (0, LMEM_EMPTY):
            continue
        size = struct.unpack_from("<H", blk, e - 2)[0] if e >= 2 else 0
        if e < end + 2 or e & 1 or size < 2 or e - 2 + size > bsize:
            err("%s: chunk %#x (%d bytes at %d) outside the heap"
                % (what, offset + 2 * i, size, e))
            continue
        slots.append((e - 2, (size + 3) & ~3, "chunk %#x" % (offset + 2 * i)))
    free_sum, f, n = 0, free, 0
    while f and n < 4096:
        if f < end + 2 or f + 2 > bsize:
            err("%s: free chunk at %d outside the heap" % (what, f))
            break
        size = struct.unpack_from("<H", blk, f - 2)[0]
        slots.append((f - 2, size, "free chunk"))
        free_sum += size
        f = struct.unpack_from("<H", blk, f)[0]
        n += 1
    if free_sum != total_free:
        err("%s: LMBH_totalFree %d, free chunks have %d"
            % (what, total_free, free_sum))
    slots.sort()
    last = end
    for pos, size, name in slots:
        if pos < last or pos + size > bsize:
            err("%s: %s at %d overlaps or leaves the block" % (what, name, pos))
        last = max(last, pos + size)


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
                if hdr[h + 3] & VMBF_LMEM and pos + size <= len(data):
                    check_lmem(data[pos:pos + size], "block %#x" % h, err)
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
