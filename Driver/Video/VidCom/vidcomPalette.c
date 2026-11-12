/*
 * vidcomPalette.c -- DR_VID_SET_PALETTE and DR_VID_GET_PALETTE (after
 * vidcomPalette.asm).  The drivers on a true color surface draw with
 * the RGB of the attributes; the palette is kept for what asks for it.
 * A driver with a palette of its own sets vidDevPalette (VidMem: the
 * bitmap's).
 */
#include "vidcom.h"

/* until the kernel sets it: the 16 standard colors (C_BLACK ...
 * C_WHITE) */
RGBValue vidPalette[256] = {
    {0x00, 0x00, 0x00}, {0x00, 0x00, 0xaa}, {0x00, 0xaa, 0x00},
    {0x00, 0xaa, 0xaa}, {0xaa, 0x00, 0x00}, {0xaa, 0x00, 0xaa},
    {0xaa, 0x55, 0x00}, {0xaa, 0xaa, 0xaa}, {0x55, 0x55, 0x55},
    {0x55, 0x55, 0xff}, {0x55, 0xff, 0x55}, {0x55, 0xff, 0xff},
    {0xff, 0x55, 0x55}, {0xff, 0x55, 0xff}, {0xff, 0xff, 0x55},
    {0xff, 0xff, 0xff}
};

Boolean (*vidDevPalette)(DriverRegs *r, Boolean set);

/* dx:si the RGBValues, al the first entry, cx the count */
void VidSetPalette(DriverRegs *r)
{
    const RGBValue *p = r->DR_dsSi;
    word first = (byte) r->DR_ax, n = (word) r->DR_cx;

    if (vidDevPalette && vidDevPalette(r, TRUE))
	return;
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

    if (vidDevPalette && vidDevPalette(r, FALSE))
	return;
    if (!p)
	return;
    if (first + n > 256)
	n = (word) (256 - first);
    memcpy(p, &vidPalette[first], n * sizeof(RGBValue));
}
