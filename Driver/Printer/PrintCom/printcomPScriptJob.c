/*
 * printcomPScriptJob.c -- the job routines of the PostScript driver (the
 * C port of printcomPScriptJob.asm and what it includes:
 * Job/jobPaperInfo.asm, Job/jobPaperPathNoASFControl.asm,
 * Job/jobStartPScript.asm, Job/jobEndPScript.asm and the custom routines
 * of Job/Custom: customLJ4PScript.asm, customIBM4019PScript.asm and
 * customHostIntegration.asm).
 *
 * A job loads the translation library (eps.geo, from the import/export
 * drivers' directory), gets its default options, fills them from the
 * JobParameters and writes the header, the device's prolog and
 * SYSTEM/devpatch.ps.  The end writes the trailer and frees it all.
 *
 * Port32 changes:
 *   - The asm wrote devpatch.ps with FileWrite to the stream block's
 *     handle, as if it were a file; it goes through TR_EXPORT_RAW here.
 *   - The host integration printer (customHostIntegration.asm) printed
 *     through a temp file and the ps2pdf library of DOSBox.  In Port32
 *     the host prints through the CUPS port driver (Driver/Stream/Cups),
 *     so the device prints to its port as the others do.
 */
#include "pscript.h"

#include <string.h>

static const char epsFileName[] = "eps.geo";
static const char patchFile[] = "devpatch.ps";

/* ---------------------------------------------------------------------
 * jobPaperInfo.asm
 * ------------------------------------------------------------------- */

/* PrintGetPaperPath: al = PI_paperInput, ah = PI_paperOutput */
Boolean PrintGetPaperPath(PState *ps, DriverRegs *r)
{
    const PrinterInfo *pi = ps->PS_deviceInfo;

    r->DR_ax = pi->PI_paperInput | ((word) pi->PI_paperOutput << 8);
    return FALSE;
}

static const PrinterMargins *Margins(PState *ps)
{
    if (PS_UI_DATA(ps)->PUID_paperInput & PIO_TRACTOR)
	return &ps->PS_deviceInfo->PI_marginTractor;
    return &ps->PS_deviceInfo->PI_marginASF;
}

/* PrintGetMargins: ax left, si top, cx right, dx bottom; the tractor
 * margins if the job's paper comes from a tractor */
Boolean PrintGetMargins(PState *ps, DriverRegs *r)
{
    const PrinterMargins *m = Margins(ps);

    r->DR_ax = m->PM_left;
    r->DR_si = m->PM_top;
    r->DR_cx = m->PM_right;
    r->DR_dx = m->PM_bottom;
    return FALSE;
}

/* PrintGetPrintArea: ax left, si top, cx right, dx bottom (points) */
Boolean PrintGetPrintArea(PState *ps, DriverRegs *r)
{
    const PrinterMargins *m = Margins(ps);
    word w = ps->PS_customWidth;

    if (w > ps->PS_deviceInfo->PI_paperWidth)
	w = ps->PS_deviceInfo->PI_paperWidth;
    r->DR_ax = m->PM_left;
    r->DR_si = m->PM_top;
    r->DR_cx = (word) (w - m->PM_right);
    r->DR_dx = (word) (ps->PS_customHeight - m->PM_bottom);
    return FALSE;
}

/* ---------------------------------------------------------------------
 * jobPaperPathNoASFControl.asm
 * ------------------------------------------------------------------- */

static void SetPaperPath(PState *ps, PaperInputOptions in)
{
    ps->PS_paperInput = in;
    ps->PS_paperOutput = (PaperOutputOptions) (PS_REVERSE <<
					       POO_SORTED_OFFSET);
    if (!(in & PIO_TRACTOR) && (in & PIO_ASF))
	ps->PS_currentMargins = ps->PS_deviceInfo->PI_marginASF;
    else
	ps->PS_currentMargins = ps->PS_deviceInfo->PI_marginTractor;
}

/* PrintSetPaperPath: al = PaperInputOptions (ah, the output, is not
 * used: the paper comes out face up) */
Boolean PrintSetPaperPath(PState *ps, DriverRegs *r)
{
    SetPaperPath(ps, (PaperInputOptions) r->DR_ax);
    return FALSE;
}

/* ---------------------------------------------------------------------
 * The custom routines
 * ------------------------------------------------------------------- */

static Boolean SendCodes(PState *ps, const byte *const *codes)
{
    for (; *codes; codes++)
	if (SendCodeOut(ps, *codes))
	    return TRUE;
    return FALSE;
}

/* customLJ4PScript.asm: PJL around the job */
static Boolean PrintEnterLJ4PScript(PState *ps)
{
    static const byte *const codes[] = {
	pr_codes_PJLUEL, pr_codes_PJLCRLF, pr_codes_PJLMsgPScript,
	pr_codes_PJLEnterPostScript, NULL
    };
    return SendCodes(ps, codes);
}

static Boolean PrintExitLJ4PScript(PState *ps)
{
    static const byte *const codes[] = {
	pr_codes_PJLUEL, pr_codes_PJLCRLF, pr_codes_PJLMsgClear,
	pr_codes_PJLUEL, NULL
    };
    return SendCodes(ps, codes);
}

/* customIBM4019PScript.asm: the tray, then PostScript */
static Boolean PrintEnterIBM4019PScript(PState *ps)
{
    byte in = PS_UI_DATA(ps)->PUID_paperInput, tray;

    if (SendCodeOut(ps, pr_codes_IBMSelectTraySIC))
	return TRUE;
    if (in == (ASF_TRAY2 << PIO_ASF_OFFSET))
	tray = 0x90;				/* the bottom tray */
    else if (in == (MF_MANUAL1 << PIO_MANUAL_OFFSET))
	tray = 0x80;				/* manual feed */
    else if (in == (ASF_TRAY3 << PIO_ASF_OFFSET))
	tray = 0x98;				/* the envelope tray */
    else
	tray = 0x88;				/* the upper tray */
    if (PrintStreamWriteByte(ps, tray))
	return TRUE;
    return SendCodeOut(ps, pr_codes_IBMEnterPostScriptSIC);
}

static Boolean CustomRoutine(PState *ps, word which)
{
    switch (which) {
    case PSC_PrintEnterLJ4PScript:
	return PrintEnterLJ4PScript(ps);
    case PSC_PrintExitLJ4PScript:
	return PrintExitLJ4PScript(ps);
    case PSC_PrintEnterIBM4019PScript:
	return PrintEnterIBM4019PScript(ps);
    case PSC_PrintEnterHostIntegration:	/* see the top of the file */
    case PSC_PrintExitHostIntegration:
    default:
	return FALSE;
    }
}

/* ---------------------------------------------------------------------
 * printcomPScriptJob.asm
 * ------------------------------------------------------------------- */

/* AppendToProlog: the device's prolog, then SYSTEM/devpatch.ps */
static Boolean AppendToProlog(PState *ps, MemHandle options,
			      MemHandle stream)
{
    const PrinterInfo *pi = ps->PS_deviceInfo;
    const PSInfoStruct *psis = PS_INFO_STRUCT(pi);
    FileHandle fh;
    Boolean err = FALSE;

    if (psis->PSIS_plen &&
	EPSRaw(ps, options, (const byte *) pi + psis->PSIS_prolog,
	       psis->PSIS_plen, stream) != TE_NO_ERROR)
	return TRUE;

    FilePushDir();
    FileSetStandardPath(SP_SYSTEM);
    fh = FileOpen(patchFile, FILE_ACCESS_R | FILE_DENY_NONE);
    if (fh) {
	dword size = FileSize(fh);
	MemHandle mh = size && size < 0xfff0 ?
	    MemAlloc((word) size, HF_DYNAMIC, HAF_LOCK) : 0;
	if (mh) {
	    void *buf = MemDeref(mh);
	    if (FileRead(fh, buf, (word) size, TRUE) != (word) size ||
		EPSRaw(ps, options, buf, (word) size, stream) != TE_NO_ERROR)
		err = TRUE;
	    MemFree(mh);
	} else if (size)
	    err = TRUE;
	FileClose(fh, TRUE);
    }
    FilePopDir();
    return err;
}

/* CreatePostScriptBlock: the stream block the library writes to */
static MemHandle CreatePostScriptBlock(PState *ps)
{
    MemHandle h = MemAlloc(sizeof(EPSExportLowStreamStruct), HF_DYNAMIC,
			   HAF_LOCK | HAF_NO_ERR);
    EPSExportLowStreamStruct *s = MemDeref(h);

    s->ESPELSS_token = ps->PS_streamToken;
    s->ESPELSS_strategy = ps->PS_streamStrategy;
    MemUnlock(h);
    return h;
}

static void CopyName(char *dst, word dsize, const char *src, word ssize)
{
    word i;

    for (i = 0; i + 1 < dsize && i < ssize && src[i]; i++)
	dst[i] = src[i];
    dst[i] = 0;
}

/* ---------------------------------------------------------------------
 * jobStartPScript.asm
 * ------------------------------------------------------------------- */

Boolean PrintStartJob(PState *ps, DriverRegs *r)
{
    const PrinterInfo *pi = ps->PS_deviceInfo;
    const PSInfoStruct *psis = PS_INFO_STRUCT(pi);
    JobParameters *jp = PS_JOB_PARAMS(ps);
    TransGetExportOptionsFn *getOpts;
    EPSExportOptStreamFn *header;
    GeodeLoadError e;
    PSExportBlock *o;
    MemHandle options, stream;
    word copies;

    (void) r;
    ps->PS_printerType = pi->PI_type;
    ps->PS_printerSmart = pi->PI_smarts;
    if (jp->JP_portInfo.PPI_type != PPT_FILE && pi->PI_customEntry &&
	CustomRoutine(ps, pi->PI_customEntry))
	return TRUE;

    SetPaperPath(ps, PS_UI_DATA(ps)->PUID_paperInput);
    ps->PS_cursorPos.P_x = 0;
    ps->PS_cursorPos.P_y = 0;
    ps->PS_epsLibrary = 0;			/* in case the load fails */
    ps->PS_expansionInfo = 0;

    FilePushDir();
    FileSetStandardPath(SP_IMPORT_EXPORT_DRIVERS);
    ps->PS_epsLibrary = GeodeUseLibrary(epsFileName, 0, 0, &e);
    FilePopDir();
    if (!ps->PS_epsLibrary)
	return TRUE;

    getOpts = (TransGetExportOptionsFn *) CallEPSLibrary(ps,
						TR_GET_EXPORT_OPTIONS);
    options = getOpts ? getOpts(0) : 0;
    if (!options)
	return TRUE;
    ps->PS_expansionInfo = options;
    stream = CreatePostScriptBlock(ps);

    o = MemLock(options);
    o->PSEB_public.PSEO_flags &= ~PSEF_EPS_FILE;
    o->PSEB_public.PSEO_common.GEO_hFile = (FileHandle) stream;
    o->PSEB_public.PSEO_common.GEO_pages = jp->JP_numPages;
    /* several copies: collated, the spooler makes them; else PostScript */
    copies = jp->JP_numCopies;
    if (copies > 1) {
	jp->JP_numCopies = 1;
	if (jp->JP_spoolOpts & SO_COLLATE) {
	    jp->JP_numCopies = (byte) copies;
	    copies = 1;
	}
    }
    o->PSEB_public.PSEO_common.GEO_copies = copies;
    o->PSEB_public.PSEO_common.GEO_docH = jp->JP_docSizeInfo.PSR_height;
    o->PSEB_public.PSEO_common.GEO_docW = jp->JP_docSizeInfo.PSR_width;
    if (jp->JP_spoolOpts & SO_COLLATE)
	o->PSEB_public.PSEO_common.GEO_flags |= GEF_COLLATE;
    CopyName(o->PSEB_public.PSEO_common.GEO_appName,
	     sizeof(o->PSEB_public.PSEO_common.GEO_appName),
	     jp->JP_parent, sizeof(jp->JP_parent));
    CopyName(o->PSEB_public.PSEO_common.GEO_docName,
	     sizeof(o->PSEB_public.PSEO_common.GEO_docName),
	     jp->JP_documentName, sizeof(jp->JP_documentName));
    o->PSEB_public.PSEO_fonts = psis->PSIS_fonts;
    o->PSEB_public.PSEO_level = psis->PSIS_level;
    MemUnlock(options);

    header = (EPSExportOptStreamFn *) CallEPSLibrary(ps, TR_EXPORT_HEADER);
    if (!header || header(options, stream) != TE_NO_ERROR)
	return TRUE;
    return AppendToProlog(ps, options, stream);
}

/* ---------------------------------------------------------------------
 * jobEndPScript.asm
 * ------------------------------------------------------------------- */

Boolean PrintEndJob(PState *ps, DriverRegs *r)
{
    const PrinterInfo *pi = ps->PS_deviceInfo;

    (void) r;
    if (ps->PS_epsLibrary) {
	EPSExportOptStreamFn *trailer = (EPSExportOptStreamFn *)
	    CallEPSLibrary(ps, TR_EXPORT_TRAILER);
	if (ps->PS_expansionInfo) {
	    MemHandle stream = PSStreamBlock(ps);
	    if (trailer && trailer(ps->PS_expansionInfo, stream) !=
		TE_NO_ERROR)
		ps->PS_error = 1;
	    if (stream)
		MemFree(stream);
	    MemFree(ps->PS_expansionInfo);
	    ps->PS_expansionInfo = 0;
	}
	GeodeFreeLibrary(ps->PS_epsLibrary);
	ps->PS_epsLibrary = 0;
    }
    if (PS_JOB_PARAMS(ps)->JP_portInfo.PPI_type != PPT_FILE &&
	pi->PI_customExit)
	return CustomRoutine(ps, pi->PI_customExit);
    return PrintClearStyles(ps);
}
