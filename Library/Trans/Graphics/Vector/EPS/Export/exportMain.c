/*
 * exportMain.c -- the export routines of the PostScript translation
 * library (the C port of exportMain.asm).
 *
 * Not ported yet: TransExport (through the file stream driver),
 * EPSExportBitmap, and the export UI.
 */
#include "exportInt.h"

#include <file.h>
#include <string.h>

/* a stream handle of zero is the asm's "tst di / jz errorFileWrite" */

TransError EPSExportHeader(MemHandle options, MemHandle stream)
{
    PSExportBlock *o;
    Boolean err = FALSE;
    FileHandle fh;

    if (!stream)
	return TE_EXPORT_ERROR;
    o = MemLock(options);
    o->PSEB_status |= PSES_OPEN_PROLOG;
    if (!(o->PSEB_public.PSEO_common.GEO_flags & GEF_NO_COMMENTS))
	err = EmitHeaderComments(o, stream);
    if (!err && !(o->PSEB_public.PSEO_common.GEO_flags & GEF_NO_HEADER))
	err = SendToStream(stream, beginPSProlog, beginPSPrologSize, NULL);
    MemUnlock(options);
    if (err)
	return TE_EXPORT_ERROR;

    /* SYSTEM/patch.ps, if there is one, goes after the prolog */
    FilePushDir();
    FileSetStandardPath(SP_SYSTEM);
    fh = FileOpen("patch.ps", FILE_ACCESS_R | FILE_DENY_NONE);
    if (fh) {
	dword size = FileSize(fh);
	MemHandle mh = size && size < 0xfff0 ?
	    MemAlloc((word) size, HF_DYNAMIC, HAF_LOCK) : 0;
	if (mh) {
	    void *buf = MemDeref(mh);
	    if (FileRead(fh, buf, (word) size, TRUE) == (word) size)
		err = SendToStream(stream, buf, (word) size, NULL);
	    MemFree(mh);
	} else if (size)
	    err = TRUE;
	FileClose(fh, TRUE);
    }
    FilePopDir();
    return err ? TE_EXPORT_ERROR : TE_NO_ERROR;
}

/* the prolog or the page setup left open: closed before the graphics */
static Boolean CloseOpenSection(PSExportBlock *o, MemHandle stream)
{
    if (o->PSEB_status & PSES_OPEN_PROLOG) {
	o->PSEB_status &= ~PSES_OPEN_PROLOG;
	return SendToStream(stream, PS(endProlog), NULL);
    }
    if (o->PSEB_status & PSES_OPEN_PAGE_SETUP) {
	o->PSEB_status &= ~PSES_OPEN_PAGE_SETUP;
	return EmitEndPageSetup(stream);
    }
    return FALSE;
}

TransError EPSExportLow(MemHandle options, GStateHandle gstring,
			MemHandle stream, GSControl flags, GSRetType *ret)
{
    PSExportBlock *o;
    TGSLocals *tgs;
    MemHandle tgsBlock;
    GSRetType r;

    if (ret)
	*ret = GSRT_FAULT;
    if (!stream)
	return TE_EXPORT_ERROR;
    o = MemLock(options);
    if (CloseOpenSection(o, stream)) {
	MemUnlock(options);
	return TE_EXPORT_ERROR;
    }
    /* the asm kept the locals on the stack; they are a block here */
    tgsBlock = MemAlloc(sizeof(TGSLocals), HF_DYNAMIC, HAF_LOCK);
    if (!tgsBlock) {
	MemUnlock(options);
	return TE_EXPORT_ERROR;
    }
    tgs = MemDeref(tgsBlock);
    r = TranslateGString(tgs, gstring, stream, o, flags);
    MemFree(tgsBlock);
    MemUnlock(options);
    if (ret)
	*ret = r;
    return r == GSRT_FAULT ? TE_EXPORT_ERROR : TE_NO_ERROR;
}

TransError EPSExportRaw(MemHandle options, const void *data, word size,
			MemHandle stream)
{
    (void) options;
    if (!stream || SendToStream(stream, data, size, NULL))
	return TE_EXPORT_ERROR;
    return TE_NO_ERROR;
}

TransError EPSExportTrailer(MemHandle options, MemHandle stream)
{
    PSExportBlock *o;
    Boolean err = FALSE;

    if (!stream)
	return TE_EXPORT_ERROR;
    o = MemLock(options);
    if (o->PSEB_status & PSES_OPEN_PROLOG)
	err = SendToStream(stream, PS(endProlog), NULL);
    if (!err) {
	if (o->PSEB_public.PSEO_flags & PSEF_EPS_FILE)
	    err = SendToStream(stream, PS(epsTrailer), NULL);
	else
	    err = SendToStream(stream, PS(printTrailer), NULL);
    }
    MemUnlock(options);
    return err ? TE_EXPORT_ERROR : TE_NO_ERROR;
}

/* EPSExportBeginPage: leaves the page setup open, so that the caller can
 * add to it (a default transform, say); EPSExportLow or EPSExportEndPage
 * closes it */
TransError EPSExportBeginPage(MemHandle options, MemHandle stream)
{
    PSExportBlock *o;
    Boolean err = FALSE;

    if (!stream)
	return TE_EXPORT_ERROR;
    o = MemLock(options);
    if (o->PSEB_status & PSES_OPEN_PROLOG) {
	o->PSEB_status &= ~PSES_OPEN_PROLOG;
	err = EmitDocSetup(o, stream);
    }
    if (!err) {
	err = EmitPageSetup(o, stream);
	o->PSEB_status |= PSES_OPEN_PAGE_SETUP;
    }
    MemUnlock(options);
    /* the asm returned TE_NO_ERROR here even after a write error */
    return err ? TE_EXPORT_ERROR : TE_NO_ERROR;
}

TransError EPSExportEndPage(MemHandle options, MemHandle stream)
{
    PSExportBlock *o;
    Boolean err = FALSE;

    if (!stream)
	return TE_EXPORT_ERROR;
    o = MemLock(options);
    o->PSEB_curPage++;
    if (o->PSEB_status & PSES_OPEN_PAGE_SETUP) {
	o->PSEB_status &= ~PSES_OPEN_PAGE_SETUP;
	err = SendToStream(stream, PS(endPageSetup), NULL);
    }
    if (!err)
	err = SendToStream(stream, PS(emitEP), NULL) ||
	    SendToStream(stream, PS(emitEndDict), NULL) ||
	    SendToStream(stream, PS(emitRestore), NULL) ||
	    SendToStream(stream, PS(pageTrailer), NULL);
    MemUnlock(options);
    return err ? TE_EXPORT_ERROR : TE_NO_ERROR;
}

/* EPSNormalizeFilename: GEO_fileName ends in ".PS" (print files), ".EPS"
 * or ".EPI" (with a preview).  Port32 fixes of the asm: it fell through
 * from the forced .PS into the EPS check, compared the same character
 * three times there, and returned no TransError. */
TransError EPSNormalizeFilename(MemHandle options)
{
    PSExportBlock *o = MemLock(options);
    char *name = o->PSEB_public.PSEO_common.GEO_fileName;
    word len = 0, max = sizeof(o->PSEB_public.PSEO_common.GEO_fileName);
    char *dot = NULL, *ext;
    const char *want;
    int i;

    while (len < max && name[len])
	len++;
    for (i = 1; i <= 4 && i <= (int) len; i++)
	if (name[len - i] == '.') {
	    dot = name + len - i;
	    break;
	}
    if (!dot) {
	if (len + 5 > max) {
	    MemUnlock(options);
	    return TE_ERROR;
	}
	dot = name + len;
	*dot = '.';
	dot[1] = 0;
    }
    ext = dot + 1;
    if (!(o->PSEB_public.PSEO_flags & PSEF_EPS_FILE))
	want = "PS";
    else if (o->PSEB_public.PSEO_flags & PSEF_INCLUDE_PREVIEW)
	want = "EPI";
    else
	want = "EPS";
    for (i = 0; want[i]; i++)
	if ((ext[i] & ~0x20) != want[i])
	    break;
    if (want[i] || ext[i]) {
	if ((word) (ext - name) + 4 > max) {
	    MemUnlock(options);
	    return TE_ERROR;
	}
	for (i = 0; want[i]; i++)
	    ext[i] = want[i];
	ext[i] = 0;
    }
    MemUnlock(options);
    return TE_NO_ERROR;
}
