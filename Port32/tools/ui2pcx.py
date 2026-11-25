#!/usr/bin/env python3
"""ui2pcx: the PCX a cvtpcx moniker (.ui) was made of.

Some pictures beside the convert scripts (Library/SpecUI/*/Art,
Library/User/Art, ...) were checked in only as the .ui file cvtpcx
wrote; their PCX is gone or was replaced.  This writes it back: the
moniker's first bitmap (4-bit or 8-bit, packbits or not, with its
mask) as a PCX, the masked-out pixels in the mask colour (cvtpcx -m),
at an offset (-x, -y), so that cvtpcx with the same options makes the
same .ui.

    ui2pcx.py [-8] [-m pixel] [-x n] [-y n] [-W n -H n] [-p base.pcx]
              file.ui out.pcx

-8: an 8-bit PCX with the bitmap's own palette, or the standard GEOS
palette if it has none (cvtpcx then writes the bitmap without one),
else a 16-colour one (4 planes).
-W, -H: the size of a new PCX (else just the bitmap at its offset); a
bitmap reaching beyond it is cut (one cvtpcx read beyond its PCX).
-p: the bitmap goes into this PCX (one of a grid of pictures), of the
same kind; its masked-out pixels too.
"""
import os
import re
import struct
import sys

HDR = "<BBBBHHHHHH48sBBHH58s"


def geos_palette():
    """The 256 colours cvtpcx compares a PCX's palette with."""
    src = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                       "..", "..", "Tools", "misc", "cvtpcx", "cvtpcx.c")
    text = open(src, encoding="latin-1").read()
    m = re.search(r"GeosColorTable\[\]\s*=\s*\{(.*?)\};", text, re.S)
    text = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)
    vals = [int(x, 0) for x in re.findall(r"0x[0-9a-fA-F]+|\d+", text)]
    assert len(vals) == 768, len(vals)
    return bytes(vals)


def unpack(data):
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
    return out


def read_bitmap(path):
    text = open(path, encoding="latin-1").read().replace("\r", "")
    m = re.search(r"C?Bitmap\s*<+\s*(\d+)\s*,\s*(\d+)\s*,\s*(\w+)\s*,"
                  r"\s*BMF_(4|8)BIT or mask BMT_MASK([^\n]*)", text)
    if not m:
        sys.exit(f"{path}: no 4-bit or 8-bit bitmap with a mask")
    w, h, comp, bpp = int(m.group(1)), int(m.group(2)), m.group(3), \
        int(m.group(4))
    rest = text[m.end():]
    rest = rest[:rest.index("}")]
    pal = None
    if "BMT_PALETTE" in m.group(5):
        n = int(re.match(r"\s*word\s+(\d+)", rest).group(1))
        rgb = re.findall(r"RGBValue\s*<\s*(0x[0-9a-fA-F]+)\s*,\s*"
                         r"(0x[0-9a-fA-F]+)\s*,\s*(0x[0-9a-fA-F]+)\s*>",
                         rest)[:n]
        pal = bytes(int(c, 16) for t in rgb for c in t)
        pal += bytes(768 - len(pal))
        end = 0
        for _ in range(n):
            end = rest.index("RGBValue", end) + 8
        rest = rest[rest.index(">", end) + 1:]
    data = [int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]{1,2})", rest)]
    mbytes, pbytes = (w + 7) // 8, (w * bpp + 7) // 8
    if comp == "BMC_PACKBITS":
        data = unpack(data)
    line = mbytes + pbytes
    if len(data) != line * h:
        sys.exit(f"{path}: {len(data)} bytes, not {h} lines of {line}")
    rows = []
    for y in range(h):
        mk = data[y * line:y * line + mbytes]
        px = data[y * line + mbytes:(y + 1) * line]
        if bpp == 4:
            val = [(px[x // 2] >> (4 if x % 2 == 0 else 0)) & 15
                   for x in range(w)]
        else:
            val = px
        rows.append([val[x] if mk[x // 8] & (0x80 >> (x % 8)) else None
                     for x in range(w)])
    return w, h, rows, pal


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
    f = struct.unpack(HDR, b[:128])
    planes, bits, bpl = f[12], f[3], f[13]
    W, H = f[6] - f[4] + 1, f[7] - f[5] + 1
    if (planes, bits) not in ((4, 1), (1, 8)):
        sys.exit(f"{path}: neither 16 colours (4 planes) nor 8 bits")
    data, i = bytearray(), 128
    while len(data) < H * planes * bpl:
        c = b[i]
        i += 1
        if c >= 0xc0:
            data += bytes([b[i]]) * (c & 0x3f)
            i += 1
        else:
            data.append(c)
    if planes == 4:
        pix = [[sum(((data[(y * 4 + p) * bpl + x // 8] >> (7 - x % 8)) & 1)
                    << p for p in range(4)) for x in range(W)]
               for y in range(H)]
        tail = b""
    else:
        pix = [list(data[y * bpl:y * bpl + W]) for y in range(H)]
        tail = b[-769:]
    return b[:128], tail, planes == 1, W, H, bpl, pix


def main():
    args, mask, xo, yo, base, eight, W, H = sys.argv[1:], 13, 0, 0, None, \
        False, 0, 0
    while args and args[0].startswith("-"):
        o = args[0][1]
        if o == "8":
            eight = True
            args.pop(0)
            continue
        v = args[0][2:] or args.pop(1)
        args.pop(0)
        if o == "m":
            mask = int(v)
        elif o == "x":
            xo = int(v)
        elif o == "y":
            yo = int(v)
        elif o == "W":
            W = int(v)
        elif o == "H":
            H = int(v)
        elif o == "p":
            base = v
    if len(args) != 2:
        sys.exit(__doc__)
    w, h, rows, own = read_bitmap(args[0])
    if base:
        hdr, tail, eight, W, H, bpl, pix = read_pcx(base)
    else:
        W, H = W or w + xo, H or h + yo
        if eight:
            bpl = W + (W & 1)
            pal = own or geos_palette()
            hdr = struct.pack(HDR, 10, 5, 1, 8, 0, 0, W - 1, H - 1, 72, 72,
                              pal[:48], 0, 1, bpl, 1, bytes(58))
            tail = b"\x0c" + pal
        else:
            bpl = (W + 7) // 8
            bpl += bpl & 1
            hdr = struct.pack(HDR, 10, 5, 1, 1, 0, 0, W - 1, H - 1, 96, 96,
                              geos_palette()[:48], 0, 4, bpl, 1, bytes(58))
            tail = b""
        pix = [[mask] * W for _ in range(H)]
    for y in range(min(h, H - yo)):
        for x in range(min(w, W - xo)):
            pix[y + yo][x + xo] = mask if rows[y][x] is None else rows[y][x]
    body = bytearray()
    for y in range(H):
        if eight:
            line = bytearray(pix[y]) + bytes(bpl - W)
        else:
            line = bytearray()
            for p in range(4):
                plane = bytearray(bpl)
                for x in range(W):
                    if pix[y][x] >> p & 1:
                        plane[x // 8] |= 0x80 >> (x % 8)
                line += plane
        body += pcx_rle(line)
    open(args[1], "wb").write(hdr + body + tail)


if __name__ == "__main__":
    main()
