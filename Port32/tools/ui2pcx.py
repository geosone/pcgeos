#!/usr/bin/env python3
"""ui2pcx: the 16-colour PCX a cvtpcx -g moniker (.ui) was made of.

Some pictures in Library/SpecUI/CommonUI/CArt were checked in only as
the .ui file cvtpcx wrote; their 4-bit PCX is gone.  This writes it
back: the moniker's 4-bit packbits bitmap (with its mask) as a 4-plane
PCX, the masked-out pixels in the mask colour (cvtpcx -m), at an offset
(-x, -y), so that cvtpcx with the same options makes the same .ui.

    ui2pcx.py [-m pixel] [-x n] [-y n] [-p base.pcx] file.ui out.pcx

-p: the bitmap goes into this 16-colour PCX (one of a grid of
pictures, edited after the rest), its masked-out pixels too.
"""
import re
import struct
import sys

# the standard GEOS palette, colours 0-15
PALETTE = [
    (0x00, 0x00, 0x00), (0x00, 0x00, 0xaa), (0x00, 0xaa, 0x00),
    (0x00, 0xaa, 0xaa), (0xaa, 0x00, 0x00), (0xaa, 0x00, 0xaa),
    (0xaa, 0x55, 0x00), (0xaa, 0xaa, 0xaa), (0x55, 0x55, 0x55),
    (0x55, 0x55, 0xff), (0x55, 0xff, 0x55), (0x55, 0xff, 0xff),
    (0xff, 0x55, 0x55), (0xff, 0x55, 0xff), (0xff, 0xff, 0x55),
    (0xff, 0xff, 0xff)]


def read_bitmap(path):
    text = open(path, encoding="latin-1").read()
    m = re.search(r"Bitmap\s*<\s*(\d+)\s*,\s*(\d+)\s*,\s*(\w+)\s*,"
                  r"\s*BMF_4BIT or mask BMT_MASK\s*>", text)
    if not m:
        sys.exit(f"{path}: no 4-bit bitmap with a mask")
    w, h, comp = int(m.group(1)), int(m.group(2)), m.group(3)
    rest = text[m.end():]
    rest = rest[:rest.index("}")]
    data = [int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{1,2})", rest)]
    mbytes, pbytes = (w + 7) // 8, (w * 4 + 7) // 8
    if comp == "BMC_PACKBITS":
        out, i = [], 0
        while i < len(data):
            n = data[i]
            i += 1
            if n < 128:
                out += data[i:i + n + 1]
                i += n + 1
            elif n > 128:
                out += [data[i]] * (257 - n)
                i += 1
        data = out
    line = mbytes + pbytes
    if len(data) != line * h:
        sys.exit(f"{path}: {len(data)} bytes, not {h} lines of {line}")
    rows = []
    for y in range(h):
        mk = data[y * line:y * line + mbytes]
        px = data[y * line + mbytes:(y + 1) * line]
        rows.append([((px[x // 2] >> (4 if x % 2 == 0 else 0)) & 15)
                     if mk[x // 8] & (0x80 >> (x % 8)) else None
                     for x in range(w)])
    return w, h, rows


def pcx_rle(b):
    out, i = bytearray(), 0
    while i < len(b):
        n = 1
        while i + n < len(b) and b[i + n] == b[i] and n < 63:
            n += 1
        if n > 1 or b[i] >= 0xc0:
            out += bytes([0xc0 | n, b[i]])
        else:
            out.append(b[i])
        i += n
    return out


def read_pcx(path):
    b = open(path, "rb").read()
    f = struct.unpack("<BBBBHHHHHH48sBBHH58s", b[:128])
    if f[12] != 4 or f[3] != 1:
        sys.exit(f"{path}: not a 16-colour (4 plane) PCX")
    W, H, bpl = f[6] - f[4] + 1, f[7] - f[5] + 1, f[13]
    data, i = bytearray(), 128
    while len(data) < H * 4 * bpl:
        c = b[i]
        i += 1
        if c >= 0xc0:
            data += bytes([b[i]]) * (c & 0x3f)
            i += 1
        else:
            data.append(c)
    pix = [[sum(((data[(y * 4 + p) * bpl + x // 8] >> (7 - x % 8)) & 1) << p
                for p in range(4)) for x in range(W)] for y in range(H)]
    return b[:128], W, H, bpl, pix


def main():
    args, mask, xo, yo, base = sys.argv[1:], 13, 0, 0, None
    while args and args[0].startswith("-"):
        o, v = args[0][1], args[0][2:] or args.pop(1)
        args.pop(0)
        if o == "m":
            mask = int(v)
        elif o == "x":
            xo = int(v)
        elif o == "y":
            yo = int(v)
        elif o == "p":
            base = v
    if len(args) != 2:
        sys.exit(__doc__)
    w, h, rows = read_bitmap(args[0])
    if base:
        hdr, W, H, bpl, pix = read_pcx(base)
    else:
        W, H = w + xo, h + yo
        bpl = (W + 7) // 8
        bpl += bpl & 1
        pix = [[mask] * W for _ in range(H)]
    for y in range(h):
        for x in range(w):
            pix[y + yo][x + xo] = mask if rows[y][x] is None else rows[y][x]
    if not base:
        hdr = struct.pack("<BBBBHHHHHH48sBBHH58s", 10, 5, 1, 1, 0, 0, W - 1,
                          H - 1, 96, 96,
                          bytes(c for rgb in PALETTE for c in rgb), 0, 4, bpl,
                          1, bytes(58))
    body = bytearray()
    for y in range(H):
        line = bytearray()
        for p in range(4):
            plane = bytearray(bpl)
            for x in range(W):
                if pix[y][x] >> p & 1:
                    plane[x // 8] |= 0x80 >> (x % 8)
            line += plane
        body += pcx_rle(line)
    open(args[1], "wb").write(hdr + body)


if __name__ == "__main__":
    main()
