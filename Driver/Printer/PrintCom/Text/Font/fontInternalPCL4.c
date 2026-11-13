/*
 * fontInternalPCL4.c -- the soft font list (the C port of
 * fontInternalPCL4.asm): the entry of the current font, a new entry.
 *
 * Port32: a font's style leaves out underline (PTS_UNDERLINE; the asm
 * masked TS_UNDERLINE, bit 0 of the PrintTextStyle): the printer
 * underlines (stylesSetPCL4.c), so an underlined run uses the font of
 * its plain one instead of a second copy.
 */
#include "fontPCL4.h"
#include <string.h>

/* the TestFontEntry of PS_curFont */
static void CurrentFont(PState *ps, TestFontEntry *t)
{
    memset(t, 0, sizeof(*t));
    t->TFE_fontID = ps->PS_curFont.FE_fontID;
    t->TFE_style = (PrintTextStyle) (ps->PS_asciiStyle & ~PTS_UNDERLINE);
    t->TFE_pointsize = ps->PS_curFont.FE_size;
    t->TFE_optFontEntry = ps->PS_curOptFont;
    /* track kerning (BBFixed points) in dots */
    t->TFE_optFontEntry.OFE_trackKern =
	PointsToPCL((sdword) ps->PS_curOptFont.OFE_trackKern);
}

static Boolean SameFont(const TestFontEntry *a, const TestFontEntry *b)
{
    const OptFontEntry *x = &a->TFE_optFontEntry, *y = &b->TFE_optFontEntry;

    return (Boolean) (a->TFE_fontID == b->TFE_fontID &&
		      a->TFE_style == b->TFE_style &&
		      a->TFE_pointsize == b->TFE_pointsize &&
		      x->OFE_color.RGB_red == y->OFE_color.RGB_red &&
		      x->OFE_color.RGB_green == y->OFE_color.RGB_green &&
		      x->OFE_color.RGB_blue == y->OFE_color.RGB_blue &&
		      x->OFE_spacePad == y->OFE_spacePad &&
		      x->OFE_trackKern == y->OFE_trackKern &&
		      x->OFE_fontWeight == y->OFE_fontWeight &&
		      x->OFE_fontWidth == y->OFE_fontWidth);
}

SoftFontEntry *IsFontInPrinter(PState *ps, HPFontInfo *fi)
{
    TestFontEntry t;
    word i;

    CurrentFont(ps, &t);
    for (i = 0; i < fi->HPFI_numFonts; i++)
	if (SameFont(&fi->HPFI_fontEntries[i].SFE_test, &t))
	    return &fi->HPFI_fontEntries[i];
    return NULL;
}

SoftFontEntry *AddFontEntry(PState *ps, HPFontInfo *fi, word tag)
{
    SoftFontEntry *sfe = &fi->HPFI_fontEntries[fi->HPFI_numFonts++];

    memset(sfe, 0, sizeof(*sfe));
    CurrentFont(ps, &sfe->SFE_test);
    sfe->SFE_orientation = 0;		/* HPO_PORTRAIT */
    sfe->SFE_flags = SFF_ON_PAGE;
    sfe->SFE_size = HP_FONT_SIZE;
    sfe->SFE_fontTag = tag;
    return sfe;
}
