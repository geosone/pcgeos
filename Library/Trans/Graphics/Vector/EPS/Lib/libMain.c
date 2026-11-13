/*
 * libMain.c -- the library side of the PostScript translation library
 * (the C port of the parts of libMain.asm the export needs, and of the
 * entry the asm took from the translation library skeleton).
 */
#include <geos.h>
#include <heap.h>
#include <library.h>
#include <xlatPS.h>

Boolean TransLibraryEntry(LibraryCallType ty, GeodeHandle client)
{
    (void) ty;
    (void) client;
    return FALSE;
}

static void Copy(char *d, const char *s)
{
    while ((*d++ = *s++) != 0)
	;
}

/* TransGetExportOptions: a block with the default options, for an
 * options UI block of zero; with a UI block, none yet (as the asm) */
MemHandle TransGetExportOptions(MemHandle ui)
{
    MemHandle mh;
    PSExportBlock *b;

    if (ui)
	return 0;
    mh = MemAlloc(sizeof(PSExportBlock), HF_DYNAMIC | HF_SHARABLE,
		  HAF_LOCK | HAF_ZERO_INIT);
    if (!mh)
	return 0;
    b = MemDeref(mh);
    b->PSEB_public.PSEO_common.GEO_pages = 1;
    b->PSEB_public.PSEO_common.GEO_copies = 1;
    Copy(b->PSEB_public.PSEO_common.GEO_appName, "PC/GEOS");
    Copy(b->PSEB_public.PSEO_common.GEO_docName, "untitled");
    Copy(b->PSEB_public.PSEO_common.GEO_fileName, "untitled.eps");
    b->PSEB_public.PSEO_common.GEO_docH = 792;
    b->PSEB_public.PSEO_common.GEO_docW = 612;
    b->PSEB_public.PSEO_flags = PSEF_EPS_FILE;
    b->PSEB_public.PSEO_fonts = PSFL_STANDARD_13;
    b->PSEB_curPage = 1;
    MemUnlock(mh);
    return mh;
}
