/*
 * graphicsCanonBJC.c -- PrintSwath (the C port of graphicsCanonBJC.asm):
 * a swath to a Canon BJC in its raster mode, a scan line at a time: each
 * plane (yellow, cyan, magenta, black; black alone of a monochrome
 * swath) PackBits compressed in ESC ( A with its color letter, then a
 * raster skip of a line.  A plane without ink is not sent.
 */
#include "printcom.h"
#include "printcomDriver.h"

/* PrCompactScanline: PackBits of the line up to its last byte not 0
 * into the band buffer, as the asm counted its runs; the size, 0 if the
 * line is white */
static word PrCompactScanline(PState *ps, const byte *in)
{
    word n = ps->PS_bandBWidth;
    byte *out = (byte *) ps->PS_bufSeg, *start = out, *cp;
    byte prev, cur;
    int count;

    while (n && !in[n - 1])
	n--;
    if (!n)
	return 0;
    for (;;) {
	/* initPrev */
	if (!n--)
	    break;
	prev = *in++;
    gotPrev:
	count = 1;
	cp = out++;
	if (!n--) {
	    *cp = (byte) (count - 1);		/* finishDiff */
	    *out++ = prev;
	    break;
	}
	cur = *in++;
	if (cur == prev) {
	sameLoop:
	    for (;;) {
		if (++count == 0x80) {		/* endSame */
		    *cp = (byte) -(count - 1);
		    *out++ = prev;
		    goto next;
		}
		if (!n--) {			/* finishSame */
		    *cp = (byte) -(count - 1);
		    *out++ = prev;
		    goto done;
		}
		cur = *in++;
		if (cur != prev)
		    break;
	    }
	    *cp = (byte) -(count - 1);
	    *out++ = prev;
	    prev = cur;
	    goto gotPrev;
	}
	for (;;) {				/* diffLoop */
	    *out++ = prev;
	    prev = cur;
	    if (++count == 0x80) {		/* endDiff */
		*cp = (byte) (count - 1);
		*out++ = prev;
		goto next;
	    }
	    if (!n--) {				/* finishDiff */
		*cp = (byte) (count - 1);
		*out++ = prev;
		goto done;
	    }
	    cur = *in++;
	    if (cur == prev)
		break;
	}
	*cp = (byte) (count - 2);
	cp = out++;
	count = 1;
	goto sameLoop;
    next:
	;
    }
done:
    return (word) (out - start);
}

/* PrPrintAScan: the plane of PS_curColorNumber */
static Boolean PrPrintAScan(PState *ps, const byte *line)
{
    word n = line ? PrCompactScanline(ps, line) : 0;

    if (!n)
	return FALSE;
    return SendCodeOut(ps, pr_codes_SetGraphics) ||
	PrintStreamWriteByte(ps, (byte) (n + 1)) ||
	PrintStreamWriteByte(ps, (byte) ((n + 1) >> 8)) ||
	PrintStreamWriteByte(ps, canonBJColorTable[ps->PS_curColorNumber]) ||
	PrintStreamWrite(ps, (const byte *) ps->PS_bufSeg, n) ||
	PrintStreamWriteByte(ps, C_CR);
}

/* PrintSwath: dx = the VM file, cx = the bitmap's block */
Boolean PrintSwath(PState *ps, DriverRegs *r)
{
    word lines, left, c;
    Boolean err = FALSE;
    Boolean mono = (Boolean) ((ps->PS_printerType & PT_COLOR) == BMF_MONO);

    LoadSwathHeader(ps, (VMFileHandle) r->DR_dx, (VMBlockHandle) r->DR_cx);
    (void) PrLoadPstateVars(ps, &left);
    PrCreatePrintBuffers(ps);
    DerefFirstScanline(ps);
    lines = ps->PS_swath.B_height;
    while (!err && lines--) {
	if (mono) {
	    ps->PS_curColorNumber = 0;
	    {
		const byte *line = DerefAScanline(ps);
		ps->PS_curColorNumber = 3;	/* "K" */
		err = PrPrintAScan(ps, line);
	    }
	} else
	    for (c = 0; !err && c < 4; c++) {
		ps->PS_curColorNumber = c;
		err = PrPrintAScan(ps, DerefAScanline(ps));
	    }
	if (!err)
	    err = PrLineFeed(ps, 1);
	ps->PS_newScanNumber++;
    }
    PrDestroyPrintBuffers(ps);
    return err;
}
