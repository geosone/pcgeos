/*
 * vidcomFont.c -- after vidcomFont.asm: This file contains routines for
 * dealing with outline fonts.
 */
#include "vidcom.h"
#include <Internal/fontDr.h>
#include <char.h>

/* VidBuildChar: the font driver builds ch into the font */
const FontBuf *VidBuildChar(GState *gs, const FontBuf *fb, byte ch)
{
    DriverRegs r;

    memset(&r, 0, sizeof(r));
    r.DR_dx = ch;
    r.DR_esDi = (void *) fb;
    r.DR_bpPtr = gs;
    if (GrCallFontDriverID(fb->FB_maker, DR_FONT_GEN_CHAR, &r) ||
	!r.DR_esDi)
	return fb;
    return r.DR_esDi;
}
