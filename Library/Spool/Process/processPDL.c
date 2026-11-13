/*
 * processPDL.c -- printing on a page description language printer (the
 * C port of processPDL.asm): the spool file's pages go to the printer
 * driver as graphics strings (DR_PRINT_ESC_PRINT_GSTRING), which it
 * translates (PostScript: the EPS library).  A page larger than the
 * paper is tiled: printed once per sheet, moved by the printable area.
 */
#include "spoolLocal.h"
#include <string.h>

/* DoPDLInit: a GState that keeps the current transformation; 0: OK */
word DoPDLInit(SpoolJobInfo *j)
{
    j->SJI_gstate = GrCreateState(0);
    return 0;
}

void DoPDLCleanup(SpoolJobInfo *j)
{
    GrDestroyState(j->SJI_gstate);
    j->SJI_gstate = 0;
}

/* AskForNextPDLPage: on manual feed the user puts in the next sheet */
static word AskForNextPDLPage(SpoolJobInfo *j)
{
    PState *ps = MemLock(j->SJI_pstate);
    PaperInputOptions pio = ps->PS_paperInput;

    MemUnlock(j->SJI_pstate);
    if (!(pio & PIO_MANUAL))
	return SPOOL_IC_OK;
    return SpoolErrorBox(SERROR_MANUAL_PAPER_FEED, 0);
}

/* PrintPDLPage: one page of the document, on as many sheets as it is
 * tiled over; the GSRetType of the page */
GSRetType PrintPDLPage(SpoolJobInfo *j)
{
    DriverRegs r;
    GSRetType ret;
    byte e;

    GrApplyTransform(j->SJI_gstate, &j->SJI_defMatrix);
    j->SJI_curyPage = j->SJI_yPages;
    for (;;) {					/* down */
	GrGetTransform(j->SJI_gstate, &j->SJI_yloopTM);
	j->SJI_curxPage = j->SJI_xPages;
	for (;;) {				/* across */
	    BumpPhysPageNumber(j);
	    GrGetTransform(j->SJI_gstate, &j->SJI_xloopTM);
	    if (ProcessStartPage(j)) {
		ret = GSRT_FAULT;
		goto doneDocPage;
	    }
	    GrGetTransform(j->SJI_gstate, &j->SJI_oldMatrix);
	    memset(&r, 0, sizeof(r));
	    r.DR_dsSi = &j->SJI_oldMatrix;
	    if (CallPrinter(j, DR_PRINT_ESC_SET_PAGE_TRANSFORM, &r)) {
		ret = GSRT_FAULT;
		goto doneDocPage;
	    }
	    j->SJI_gstring = GrLoadGString(j->SJI_fHan, GST_STREAM, 0);
	    memset(&r, 0, sizeof(r));
	    r.DR_cx = GSC_NEW_PAGE;
	    r.DR_si = j->SJI_gstring;
	    if (CallPrinter(j, DR_PRINT_ESC_PRINT_GSTRING, &r)) {
		GrDestroyGString(j->SJI_gstring, 0, GSKT_LEAVE_DATA);
		ret = GSRT_FAULT;
		goto doneDocPage;
	    }
	    ret = (GSRetType) r.DR_ax;
	    GrDestroyGString(j->SJI_gstring, 0, GSKT_LEAVE_DATA);
	    j->SJI_gstring = 0;
	    if (CheckForErrors(j, NULL))
		ret = GSRT_FAULT;
	    if (ProcessEndPage(j, &ret, PEC_FORM_FEED))
		ret = GSRT_FAULT;
	    if (ret == GSRT_FAULT)
		goto doneDocPage;

	    if ((sword) --j->SJI_curxPage <= 0)
		break;
	    /* the next sheet to the right: the same page again */
	    FilePos(j->SJI_fHan, j->SJI_fPos, FILE_POS_START);
	    GrSetTransform(j->SJI_gstate, &j->SJI_xloopTM);
	    if (j->SJI_printState & SPS_ROTATED)
		GrApplyTranslation(j->SJI_gstate, 0,
				   -((sdword) j->SJI_printWidth << 16));
	    else
		GrApplyTranslation(j->SJI_gstate,
				   -((sdword) j->SJI_printWidth << 16), 0);
	    if (AskForNextPDLPage(j) == SPOOL_IC_DISMISS) {
		ret = GSRT_FAULT;
		goto exit;
	    }
	}
	if ((sword) --j->SJI_curyPage <= 0)
	    break;
	/* the next row of sheets */
	FilePos(j->SJI_fHan, j->SJI_fPos, FILE_POS_START);
	GrSetTransform(j->SJI_gstate, &j->SJI_yloopTM);
	if (j->SJI_printState & SPS_ROTATED)
	    GrApplyTranslation(j->SJI_gstate,
			       -((sdword) j->SJI_printHeight << 16), 0);
	else
	    GrApplyTranslation(j->SJI_gstate, 0,
			       -((sdword) j->SJI_printHeight << 16));
	if (AskForNextPDLPage(j) == SPOOL_IC_DISMISS) {
	    ret = GSRT_FAULT;
	    goto exit;
	}
    }

doneDocPage:
    /* the user is told, unless the job was canceled or is detaching */
    if (ret == GSRT_FAULT) {
	CheckForErrors(j, &e);
	if (e != SI_ABORT && e != SI_DETACH)
	    SpoolErrorBox(SERROR_CANNOT_CONVERT_PAGE, j->SJI_qHan);
    }
exit:
    GrSetNullTransform(j->SJI_gstate);
    return ret;
}
