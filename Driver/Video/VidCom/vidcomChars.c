/*
 * vidcomChars.c -- DR_VID_PUTSTRING (after vidcomChars.asm: VidPutString,
 * PutStringLow, KernChar; the position of vidcomFont.asm).
 *
 * Each character's picture (CharData: rows of bits, MSB first) is drawn
 * at the pen plus its offsets in the text attributes, a run of set bits
 * at a time; the pen moves by the character's width, the space padding
 * (TM_PAD_SPACES) and the kerning (TM_TRACK_KERN, TM_PAIR_KERN: the
 * track kerning and the pair's value, a width only when it stays
 * positive), in 1/256 pixel.  A character outside the font, or not in
 * it, is the default character.
 *
 * Not yet: characters the font driver builds (CHAR_NOT_BUILT,
 * CHAR_NOT_LOADED: the default character instead), region and
 * greyscale characters (FBF_IS_REGION, FBF_IS_GREY), rotated text
 * (GS_complexOpcode), control characters (TM_DRAW_CONTROL_CHARS),
 * GS_textDrawOffset.
 */
#include "vidcom.h"
#include <Internal/fontDr.h>
#include <char.h>

static CharTableEntry *Entry(const FontBuf *fb, byte ch)
{
    CharTableEntry *cte = FB_CHAR_TABLE(fb);

    if (ch < fb->FB_firstChar || ch > fb->FB_lastChar ||
	cte[ch - fb->FB_firstChar].CTE_dataOffset <= CHAR_MISSING)
	ch = fb->FB_defaultChar;
    return &cte[ch - fb->FB_firstChar];
}

/* KernChar: the pair's value (BBFixed) in 1/256 */
static sdword PairKern(const FontBuf *fb, byte prev, byte ch)
{
    const KernPair *kp = (const KernPair *) ((const byte *) fb +
					     fb->FB_kernPairPtr);
    const byte *kv = (const byte *) fb + fb->FB_kernValuePtr;
    word i;

    for (i = 0; i < fb->FB_kernCount; i++)
	if (kp[i].KP_char1 == ch && kp[i].KP_char2 == prev)
	    return ((sdword) (sbyte) kv[2 * i + 1] << 8) + kv[2 * i];
    return 0;
}

/* a character's picture with its top left at x, y */
static void DrawChar(Window *w, const FontBuf *fb, const CharTableEntry *cte,
		     sword x, sword y, const VidDrawState *ds)
{
    const CharData *cd;
    word row, bpr;

    if (cte->CTE_dataOffset <= CHAR_MISSING ||
	(cte->CTE_flags & CTF_NO_DATA))
	return;
    cd = (const CharData *) ((const byte *) fb + cte->CTE_dataOffset);
    bpr = (word) ((cd->CD_pictureWidth + 7) >> 3);
    x = (sword) (x + cd->CD_xoff);
    y = (sword) (y + cd->CD_yoff);
    for (row = 0; row < cd->CD_numRows; row++) {
	const byte *bits = cd->CD_data + row * bpr;
	word i = 0;
	while (i < cd->CD_pictureWidth) {
	    word start;
	    while (i < cd->CD_pictureWidth &&
		   !(bits[i >> 3] & (0x80 >> (i & 7))))
		i++;
	    if (i == cd->CD_pictureWidth)
		break;
	    start = i;
	    while (i < cd->CD_pictureWidth &&
		   (bits[i >> 3] & (0x80 >> (i & 7))))
		i++;
	    VidFillClipped(w, (sword) (x + start), (sword) (y + row),
			   (sword) (x + i - 1), (sword) (y + row), ds);
	}
    }
}

void VidPutString(DriverRegs *r)
{
    GState *gs = r->DR_dsSi;
    Window *w = r->DR_esDi;
    VidPutStringArgs *a = r->DR_bpPtr;
    const FontBuf *fb;
    const char *s;
    sdword x, y;
    word n;
    byte prev = 0;
    CharTableFlags pf = 0;
    VidDrawState ds;

    if (!gs || !w || !a || !a->VPS_font)
	return;
    fb = a->VPS_font;
    x = ((sdword) (sword) (word) r->DR_ax << 8) | (byte) r->DR_dx;
    y = ((sdword) (sword) (word) r->DR_bx << 8) | (byte) (r->DR_dx >> 8);
    VidSetupDraw(gs, &gs->GS_textAttr, w, &ds);
    VidDevCondHidePtr(w->W_maskRect.R_left, w->W_maskRect.R_top,
		      w->W_maskRect.R_right, w->W_maskRect.R_bottom);
    for (s = a->VPS_string, n = a->VPS_numChars; n && *s; n--, s++) {
	byte ch = (byte) *s;
	const CharTableEntry *cte;
	sdword adv;

	if (ch == C_OPTHYPHEN) {	/* drawn only as the last one */
	    if (!(gs->GS_textMode & TM_DRAW_OPTIONAL_HYPHENS) ||
		(n != 1 && s[1]))
		continue;
	    ch = C_HYPHEN;
	}
	cte = Entry(fb, ch);
	adv = ((sdword) cte->CTE_width.WBF_int << 8) +
	      cte->CTE_width.WBF_frac;
	if (gs->GS_textMode & (TM_TRACK_KERN | TM_PAIR_KERN)) {
	    sdword k = ((sdword) gs->GS_trackKernValue.BBF_int << 8) +
		       gs->GS_trackKernValue.BBF_frac;
	    if ((pf & CTF_IS_FIRST_KERN) &&
		(cte->CTE_flags & CTF_IS_SECOND_KERN))
		k += PairKern(fb, prev, ch);
	    if (adv + k >= 0)
		adv += k;
	    else
		adv = 0;
	    pf = cte->CTE_flags;
	}
	DrawChar(w, fb, cte, (sword) (x >> 8), (sword) (y >> 8), &ds);
	if (ch == C_SPACE && (gs->GS_textMode & TM_PAD_SPACES))
	    adv += ((sdword) gs->GS_textSpacePad.WBF_int << 8) +
		   gs->GS_textSpacePad.WBF_frac;
	x += adv;
	prev = ch;
    }
    VidDevCondShowPtr();
    a->VPS_string = s;
    r->DR_ax = (word) (x >> 8);
    r->DR_bx = (word) (y >> 8);
    r->DR_dx = (word) (((y & 0xff) << 8) | (x & 0xff));
}
