/*
 * pscript.h -- the PostScript printer driver in C (the C form of
 * pscriptConstant.def and pscriptInclude.def).
 *
 * The driver is the common printer driver code (PrintCom/printcom.h)
 * plus the routines of the PScript C files.  It translates through the
 * PostScript translation library (eps.geo, CInclude/xlatPS.h), which it
 * loads per job and calls by entry number (TR_*), as the asm did.
 */
#ifndef PSCRIPT_H
#define PSCRIPT_H

#include "printcom.h"
#include <xlatPS.h>

/* margins (points): PostScript can use most of the page */
#define PR_MARGIN_LEFT		18
#define PR_MARGIN_RIGHT		18
#define PR_MARGIN_TRACTOR	18
#define PR_MARGIN_TOP		18
#define PR_MARGIN_BOTTOM	18

/* after the device's GraphicsProperties (PI_hiRes) */
typedef struct {
    PSFontList	PSIS_fonts;	/* fonts available on printer */
    PSLevel	PSIS_level;	/* PostScript level and extensions */
    word	PSIS_plen;	/* length of prolog section */
    word	PSIS_prolog;	/* offset from the PrinterInfo of the
				 * PostScript code for the prolog */
} PSInfoStruct;

#define PS_INFO_STRUCT(pi) ((const PSInfoStruct *) ((const byte *) (pi) + \
	(pi)->PI_hiRes + sizeof(GraphicsProperties)))

/* PI_customEntry, PI_customExit: the custom job routines.  The asm
 * stored their offsets; these are numbers (psdevinfo2c.py). */
enum {
    PSC_NONE,
    PSC_PrintEnterLJ4PScript,
    PSC_PrintExitLJ4PScript,
    PSC_PrintEnterIBM4019PScript,
    PSC_PrintEnterHostIntegration,
    PSC_PrintExitHostIntegration
};

/* pscriptControlCodes.c: a count byte, then the bytes (SendCodeOut) */
extern const byte pr_codes_PJLUEL[], pr_codes_PJLCRLF[],
    pr_codes_PJLEnterPostScript[], pr_codes_PJLMsgPScript[],
    pr_codes_PJLMsgClear[], pr_codes_IBMSelectTraySIC[],
    pr_codes_IBMEnterPostScriptSIC[];

/* pscriptUtils.c */
extern void *CallEPSLibrary(PState *ps, word routine);
extern TransError EPSRaw(PState *ps, MemHandle options, const void *data,
			 word size, MemHandle stream);
extern Boolean EmitTransform(PState *ps, MemHandle options,
			     MemHandle stream, const TransMatrix *tm);
extern Boolean EmitPaperSize(PState *ps, MemHandle options,
			     MemHandle stream);
extern char *UWordToAscii(char *p, word v);

/* the stream block of the job (GEO_hFile of the options) */
extern MemHandle PSStreamBlock(PState *ps);

#endif /* PSCRIPT_H */
