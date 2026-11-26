/*
 * printcomPScriptJob.c -- the job routines of the PostScript driver
 * (after printcomPScriptJob.asm: AppendToProlog, CreatePostScriptBlock).
 * As the asm, it includes the parts beside their asm files: the custom
 * routines of Job/Custom (customLJ4PScript.c, customIBM4019PScript.c,
 * customHostIntegration.c), Job/jobStartPScript.c and
 * Job/jobEndPScript.c.  Job/jobPaperInfo.c and
 * Job/jobPaperPathNoASFControl.c, which the asm includes too, are built
 * on their own, as for the other drivers.
 *
 * A job loads the translation library (eps.geo, from the import/export
 * drivers' directory), gets its default options, fills them from the
 * JobParameters and writes the header, the device's prolog and
 * SYSTEM/devpatch.ps.  The end writes the trailer and frees it all.
 *
 * Port32 changes:
 *   - The asm wrote devpatch.ps with FileWrite to the stream block's
 *     handle, as if it were a file; it goes through TR_EXPORT_RAW here.
 *   - The host integration printer: see customHostIntegration.c.
 */
#include "pscript.h"

#include <string.h>

static const char patchFile[] = "devpatch.ps";

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

#include "Job/Custom/customLJ4PScript.c"
#include "Job/Custom/customIBM4019PScript.c"
#include "Job/Custom/customHostIntegration.c"

/* the custom routine of PI_customEntry or PI_customExit (the asm called
 * the near pointer) */
static Boolean CustomRoutine(PState *ps, word which)
{
    switch (which) {
    case PSC_PrintEnterLJ4PScript:
	return PrintEnterLJ4PScript(ps);
    case PSC_PrintExitLJ4PScript:
	return PrintExitLJ4PScript(ps);
    case PSC_PrintEnterIBM4019PScript:
	return PrintEnterIBM4019PScript(ps);
    case PSC_PrintEnterHostIntegration:
	return PrintEnterHostIntegration(ps);
    case PSC_PrintExitHostIntegration:
	return PrintExitHostIntegration(ps);
    default:
	return FALSE;
    }
}

#include "Job/jobStartPScript.c"
#include "Job/jobEndPScript.c"
