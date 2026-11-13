/*
 * pscriptPDL.c -- the escapes of a PDL printer (the C port of
 * pscriptPDL.asm): DR_PRINT_ESC_SET_PAGE_TRANSFORM and
 * DR_PRINT_ESC_PRINT_GSTRING.
 */
#include "pscript.h"

/* PrintSetPageTransform: dx:si (DR_dsSi) = the TransMatrix.  It sets
 * the page's default transform (SPT) and the paper size (SPS); the
 * carry is clear, as in the asm. */
Boolean PrintSetPageTransform(PState *ps, DriverRegs *r)
{
    MemHandle stream = PSStreamBlock(ps);

    if (EmitTransform(ps, ps->PS_expansionInfo, stream, r->DR_dsSi))
	ps->PS_error = 1;
    else if (EmitPaperSize(ps, ps->PS_expansionInfo, stream))
	ps->PS_error = 1;
    return FALSE;
}

/* PrintGString: cx = GSControl, si = the gstring.  Returns ax = the
 * GSRetType of the translation (with GSC_NEW_PAGE always set). */
Boolean PrintGString(PState *ps, DriverRegs *r)
{
    EPSExportLowFn *f = (EPSExportLowFn *) CallEPSLibrary(ps,
							  TR_EXPORT_LOW);
    GSRetType ret = GSRT_FAULT;

    if (!f || f(ps->PS_expansionInfo, (GStateHandle) r->DR_si,
		PSStreamBlock(ps), (GSControl) (r->DR_cx | GSC_NEW_PAGE),
		&ret) != TE_NO_ERROR)
	ps->PS_error = 1;
    r->DR_ax = ret;
    return FALSE;
}
