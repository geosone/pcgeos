/*
 * vidcomPalette.c -- DR_VID_SET_PALETTE and DR_VID_GET_PALETTE (after
 * vidcomPalette.asm).  The drivers on a true color surface draw with
 * the RGB of the attributes; the palette is kept for what asks for it.
 */
#include "vidcom.h"

RGBValue vidPalette[256];

/* dx:si the RGBValues, al the first entry, cx the count */
void VidSetPalette(DriverRegs *r)
{
    const RGBValue *p = r->DR_dsSi;
    word first = (byte) r->DR_ax, n = (word) r->DR_cx;

    if (!p)
	return;
    if (first + n > 256)
	n = (word) (256 - first);
    memcpy(&vidPalette[first], p, n * sizeof(RGBValue));
}

void VidGetPalette(DriverRegs *r)
{
    RGBValue *p = r->DR_dsSi;
    word first = (byte) r->DR_ax, n = (word) r->DR_cx;

    if (!p)
	return;
    if (first + n > 256)
	n = (word) (256 - first);
    memcpy(p, &vidPalette[first], n * sizeof(RGBValue));
}
