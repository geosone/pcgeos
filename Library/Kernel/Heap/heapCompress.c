/*
 * heapCompress.c -- LZG compression (after heapCompress.asm): bitmaps
 * (BMC_LZG), compressed resources and VM blocks use it.
 *
 * The format, as the asm writes it: a flag byte for each eight items,
 * MSB first, 1 for a literal byte, 0 for a code byte (and what follows
 * it):
 *   1ddddddd             3 bytes from d back (a small pair)
 *   011nnnnn             n zeros; n = 0 ends the data
 *   010nnnnn             n literal bytes; n = 0: the next byte is n
 *   00llllhh dddddddd    l bytes from hhdddddddd back; l = 0: the next
 *                        byte is l
 * The window is 1024 bytes.  A match is 3 to 254 bytes, a run of zeros
 * 2 to 30 (the asm's string compares stop one short of their counts).
 *
 * The compress "stack" was a block the asm switched its stack to, with
 * the hash tables in it; here it is the block of the tables.  The
 * compressor matches the asm's output except where the asm read past
 * the end of the data (its last bytes), which only changes which of two
 * equal matches it takes.
 */
#include "kernelC.h"

#include <string.h>

#define LZG_POSITION_BITS	10
#define LZG_MIN_MATCH		3
#define LZG_MAX_MATCH		15		/* in the two-byte pair */
#define LZG_MAX_LITERAL		7
#define LZG_MAX_LIT_STRING	0x1f
#define LZG_MAX_LONG_LIT	0xff
#define LZG_MAX_RUN		0x1f
#define LZG_SMALL_PAIR_FLAG	0x80
#define LZG_LITERALS_FLAG	0x40
#define LZG_RUN_LENGTH_FLAG	0x20
#define LZG_END_MARKER		(LZG_LITERALS_FLAG | LZG_RUN_LENGTH_FLAG)
#define LZG_STRINGS		256
#define LZG_DICTIONARY		(1 << LZG_POSITION_BITS)
#define LZG_CHAIN_MASK		(LZG_DICTIONARY - 1)
#define LZG_NONE		0xffffffffUL

typedef struct {
    dword	strings[LZG_STRINGS];	/* the last position of a hash */
    dword	chains[LZG_DICTIONARY];	/* the one before, by position */
} LZGTables;

typedef struct {
    LZGTables	*t;
    const byte	*src;
    dword	n;			/* bytes of the source */
    byte	*dst, *out;
    byte	*flags;			/* the flag byte being filled */
    int		flagCount;
    dword	matchPos, literalPos;
} LZGState;

MemHandle _pascal LZGAllocCompressStack(GeodeHandle stackOwner)
{
    if (!stackOwner)
	return MemAlloc(sizeof(LZGTables), HF_DYNAMIC, HAF_NO_ERR);
    return MemAllocSetOwner(stackOwner, sizeof(LZGTables), HF_DYNAMIC,
			    HAF_NO_ERR);
}

void _pascal LZGFreeCompressStack(MemHandle compressStack)
{
    MemFree(compressStack);
}

/* CalcHashIndex */
static word Hash(const LZGState *s, dword at)
{
    byte b0 = s->src[at];
    byte b1 = at + 1 < s->n ? s->src[at + 1] : 0;
    byte b2 = at + 2 < s->n ? s->src[at + 2] : 0;
    byte lo = (byte) ((b0 << 1) | (b0 >> 7));
    byte hi = (byte) ((b1 >> 1) | (b1 << 7));

    return (byte) ((lo ^ hi) ^ b2);
}

/* InsertString */
static void Insert(LZGState *s, word h, dword at)
{
    s->t->chains[at & LZG_CHAIN_MASK] = s->t->strings[h];
    s->t->strings[h] = at;
}

/* Set(Non)LiteralByteFlag */
static void Flag(LZGState *s, byte bit)
{
    if (--s->flagCount < 0) {
	s->flagCount = 7;
	s->flags = s->out++;
	*s->flags = 0;
    }
    *s->flags = (byte) ((*s->flags << 1) | bit);
}

/* SearchDictionary: the longest match at si (at most left); 0 none */
static dword Search(LZGState *s, word h, dword si, dword left)
{
    dword low = si > LZG_DICTIONARY - 1 ? si - (LZG_DICTIONARY - 1) : 0;
    dword p = s->t->strings[h], best = 0;
    dword limit = left < 0xff ? left : 0xff;

    while (p != LZG_NONE && p >= low) {
	if (best < limit && s->src[si + best] == s->src[p + best]) {
	    dword k = 0;
	    while (k < limit && s->src[si + k] == s->src[p + k])
		k++;
	    if (k == 0xff)
		k = 0xfe;			/* repe cmpsb, one short */
	    if (k > best) {
		best = k;
		s->matchPos = p;
	    }
	}
	p = s->t->chains[p & LZG_CHAIN_MASK];
    }
    return best < left ? best : left;
}

/* WriteLiteral: the bytes from literalPos to si */
static void WriteLiteral(LZGState *s, dword si)
{
    const byte *p = s->src + s->literalPos;
    dword n = si - s->literalPos;

    while (n) {
	dword k;
	if (n <= LZG_MAX_LITERAL) {
	    while (n--) {
		Flag(s, 1);
		*s->out++ = *p++;
	    }
	    return;
	}
	Flag(s, 0);
	if (n <= LZG_MAX_LIT_STRING) {
	    *s->out++ = (byte) (n | LZG_LITERALS_FLAG);
	    k = n;
	} else {
	    k = n <= LZG_MAX_LONG_LIT ? n : LZG_MAX_LONG_LIT;
	    *s->out++ = LZG_LITERALS_FLAG;
	    *s->out++ = (byte) k;
	}
	memcpy(s->out, p, k);
	s->out += k;
	p += k;
	n -= k;				/* more after 255 of them */
    }
}

int _pascal LZGCompress(byte *compressBuffer, byte *data, int dataSize,
			MemHandle compressStack)
{
    LZGState s;
    dword si = 0, left = (dword) dataSize;
    word i;

    memset(&s, 0, sizeof(s));
    s.t = MemLock(compressStack);
    if (!s.t)
	return 0;
    s.src = data;
    s.n = left;
    s.dst = s.out = compressBuffer;
    for (i = 0; i < LZG_STRINGS; i++)
	s.t->strings[i] = LZG_NONE;

    s.literalPos = si;
    while (left) {
	word h;
	dword len;

	if (left >= 2 && !data[si] && !data[si + 1]) {	/* WriteRunLength */
	    dword k = 0;
	    if (s.literalPos != si)
		WriteLiteral(&s, si);
	    Flag(&s, 0);
	    while (k < LZG_MAX_RUN && si + k < s.n && !data[si + k])
		k++;
	    if (k == LZG_MAX_RUN)
		k--;				/* repe scasb, one short */
	    if (k > left)
		k = left;
	    *s.out++ = (byte) (k | LZG_LITERALS_FLAG | LZG_RUN_LENGTH_FLAG);
	    si += k;
	    left -= k;
	    s.literalPos = si;
	    continue;
	}
	h = Hash(&s, si);
	len = s.t->strings[h] == LZG_NONE ? 0 : Search(&s, h, si, left);
	if (len < LZG_MIN_MATCH) {
	    Insert(&s, h, si);
	    si++;
	    left--;
	    continue;
	}
	{					/* WritePair */
	    dword d = si - s.matchPos;
	    if (s.literalPos != si)
		WriteLiteral(&s, si);
	    Flag(&s, 0);
	    if (len == LZG_MIN_MATCH && d <= 0x7f)
		*s.out++ = (byte) (d | LZG_SMALL_PAIR_FLAG);
	    else if (len <= LZG_MAX_MATCH) {
		*s.out++ = (byte) ((d >> 8) | (len << 2));
		*s.out++ = (byte) d;
	    } else {
		*s.out++ = (byte) (d >> 8);
		*s.out++ = (byte) d;
		*s.out++ = (byte) len;
	    }
	    left -= len;
	    while (len--) {
		Insert(&s, Hash(&s, si), si);
		si++;
	    }
	    s.literalPos = si;
	}
    }
    if (s.literalPos != si)
	WriteLiteral(&s, si);
    Flag(&s, 0);
    *s.out++ = LZG_END_MARKER;
    *s.flags = (byte) (*s.flags << s.flagCount);
    MemUnlock(compressStack);
    return (int) (s.out - s.dst);
}

/* LZGUncompressLow: one stream from src to dst, at most cap bytes
 * written (the rest skipped); the bytes of the source in *used; the
 * bytes it holds */
dword LZGUncompressBounded(byte *dst, dword cap, const byte *src,
			   dword *used)
{
    const byte *p = src;
    dword o = 0;
    byte flags = 0;
    int nflags = 0;

#define PUT(b) do { if (o < cap) dst[o] = (b); o++; } while (0)
#define BACK(d) (o >= (d) && o - (d) < cap ? dst[o - (d)] : 0)
    for (;;) {
	byte c;
	if (!nflags) {
	    flags = *p++;
	    nflags = 8;
	}
	c = (byte) (flags & 0x80);
	flags = (byte) (flags << 1);
	nflags--;
	if (c) {
	    PUT(*p);
	    p++;
	    continue;
	}
	c = *p++;
	if (c & LZG_SMALL_PAIR_FLAG) {
	    dword d = c & 0x7f, k;
	    for (k = 0; k < 3; k++)
		PUT(BACK(d));
	} else if (c & LZG_LITERALS_FLAG) {
	    dword n = c & 0x1f;
	    if (c & LZG_RUN_LENGTH_FLAG) {
		if (!n)
		    break;
		while (n--)
		    PUT(0);
	    } else {
		if (!n)
		    n = *p++;
		while (n--) {
		    PUT(*p);
		    p++;
		}
	    }
	} else {
	    dword d = ((dword) (c & 3) << 8) | *p++;
	    dword n = c >> 2;
	    if (!n)
		n = *p++;
	    while (n--)
		PUT(BACK(d));
	}
    }
#undef PUT
#undef BACK
    if (used)
	*used = (dword) (p - src);
    return o;
}

int _pascal LZGUncompress(byte *dataBuffer, byte *compressedData)
{
    return (int) LZGUncompressBounded(dataBuffer, 0xffffffffUL,
				      compressedData, NULL);
}

int _pascal LZGGetUncompressedSize(byte *compressedData)
{
    dword n = LZGUncompressBounded(NULL, 0, compressedData, NULL);

    return n > 0xffff ? -1 : (int) n;
}
