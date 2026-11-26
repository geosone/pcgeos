/*
 * graphicsFontC.c -- after graphicsFontC.asm: GrCheckFontAvailID,
 * GrCheckFontAvailName.
 */
#include "graphicsInt.h"
#include <ctype.h>
#include <stdlib.h>

/* GrCheckFontAvailReal: the font by ID or (FEF_STRING) name, of the
 * type; FID_INVALID if not there */
static FontID CheckFontAvail(FontEnumFlags flags, word family, FontID id,
			     const char *name)
{
    word n, i;
    FontsAvailEntry *fa;
    FontID r = FID_INVALID;

    if (!fontBlkHandle)
	return FID_INVALID;
    FontDrLockInfoBlock();
    fa = GrFontChunk(FONTS_AVAIL_HANDLE, &n);
    n = (word) (n / sizeof(FontsAvailEntry));
    for (i = 0; i < n && r == FID_INVALID; i++) {
	FontInfo *fi = GrFontChunk(fa[i].FAE_infoHandle, NULL);
	FontEnumStruct e;
	const char *a, *b;
	if (!GrCheckFontType(fi, flags, family))
	    continue;
	if (!(flags & FEF_STRING)) {
	    if (fi->FI_fontID == id)
		r = id;
	    continue;
	}
	GrEnumAddFont(&e, fi, 0);
	for (a = e.FES_name, b = name; *a && *b; a++, b++)
	    if ((flags & FEF_DOWNCASE) ?
		tolower((byte) *a) != tolower((byte) *b) : *a != *b)
		break;
	if (!*a && !*b)
	    r = fi->FI_fontID;
    }
    FontDrUnlockInfoBlock();
    return r;
}

FontID GrCheckFontAvailID(FontEnumFlags flags, word family, FontID id)
{
    return CheckFontAvail((FontEnumFlags) (flags & ~FEF_STRING), family, id,
			  NULL);
}

FontID GrCheckFontAvailName(FontEnumFlags flags, word family,
			    const char *name)
{
    return CheckFontAvail((FontEnumFlags) (flags | FEF_STRING), family, 0,
			  name);
}
