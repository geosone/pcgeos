/*
 * processGraphics.c -- printing in the graphics modes (the C port of
 * processGraphics.asm): each page of the spool file is drawn into a
 * bitmap one swath high, in a temporary VM file of SP_SPOOL, and the
 * swath goes to the printer driver (DR_PRINT_SWATH); then the page is
 * moved up by a swath and drawn again, until the page is done.  A page
 * larger than the paper is tiled, as in processPDL.c.
 *
 * Port32: the asm's labels, Tiramisu (fax) printing, the dual-threaded
 * printing and the non-spooling build are not ported.  The bitmap of a
 * swath has the format of the mode (monochrome, CMYK or CMY: the planes
 * of the color printers, drawn by the memory video driver, vidmem); it
 * is dithered as the asm asked (BM_CLUSTERED_DITHER); the color
 * correction table of the mode (GP_colorCorrection) is not used
 * (graphicsBitmapCreate.c).
 */
#include "spoolLocal.h"
#include <vm.h>
#include <win.h>
#include <hugearr.h>
#include <string.h>

#define SPOOL_FILE_VM_ATTRS	VMA_SINGLE_THREAD_ACCESS

/* CreateBitmapFile: TRUE if the file could not be made */
static Boolean CreateBitmapFile(SpoolJobInfo *j)
{
    j->SJI_bmFileName[0] = 0;
    j->SJI_bmFileHan = VMOpen(j->SJI_bmFileName,
			      VMAF_FORCE_READ_WRITE | VMAF_FORCE_DENY_WRITE |
			      VMAF_USE_BLOCK_LEVEL_SYNCHRONIZATION,
			      VMO_TEMP_FILE, 0);
    if (!j->SJI_bmFileHan)
	return TRUE;
    VMSetAttributes(j->SJI_bmFileHan, SPOOL_FILE_VM_ATTRS,
		    (VMAttributes) ~SPOOL_FILE_VM_ATTRS);
    return FALSE;
}

/* DoGraphicsInit: the bitmap file and a swath's bitmap to draw into; 0
 * if all is well */
word DoGraphicsInit(SpoolJobInfo *j)
{
    BMFormat fmt = (BMFormat) (j->SJI_colorFormat & BMT_FORMAT);
    Boolean err;

    FilePushDir();
    FileSetStandardPath(SP_SPOOL);
    err = CreateBitmapFile(j);
    FilePopDir();
    if (err)
	return 1;
    j->SJI_bmHan = GrCreateBitmap(fmt, j->SJI_bandW, j->SJI_swathH,
				  j->SJI_bmFileHan,
				  ConstructOptr(GeodeGetProcessHandle(), 0),
				  &j->SJI_gstate);
    if (!j->SJI_bmHan) {
	VMClose(j->SJI_bmFileHan, FALSE);
	j->SJI_bmFileHan = 0;
	return 1;
    }
    j->SJI_bmWinHan = GrGetWinHandle(j->SJI_gstate);
    GrSetBitmapMode(j->SJI_gstate, BM_CLUSTERED_DITHER,
		    (MemHandle) j->SJI_colorCorrection);
    GrBeginUpdate(j->SJI_gstate);
    GrEndUpdate(j->SJI_gstate);
    return 0;
}

/* DoGraphicsCleanup: the bitmap and its file go */
void DoGraphicsCleanup(SpoolJobInfo *j)
{
    if (j->SJI_gstate)
	GrDestroyBitmap(j->SJI_gstate, BMD_LEAVE_DATA);
    j->SJI_gstate = 0;
    if (j->SJI_bmFileHan)
	VMClose(j->SJI_bmFileHan, FALSE);
    j->SJI_bmFileHan = 0;
    FilePushDir();
    FileSetStandardPath(SP_SPOOL);
    FileDelete(j->SJI_bmFileName);
    FilePopDir();
}

/* AdjustBitmapHeight: the bitmap's height (B_height in the directory)
 * for the last swath of a page, and back */
static void AdjustBitmapHeight(SpoolJobInfo *j, word height)
{
    byte *dir;

    HugeArrayLockDir(j->SJI_bmFileHan, j->SJI_bmHan, (void **) &dir);
    ((Bitmap *) (dir + sizeof(HugeArrayDirectory)))->B_height = height;
    HugeArrayDirty(dir);
    HugeArrayUnlockDir(dir);
}

/* TranslateThePage: by x, y dots (swapped on a rotated page) */
static void TranslateThePage(SpoolJobInfo *j, sword x, sword y)
{
    sword t;

    if (j->SJI_printState & SPS_ROTATED) {
	t = x;
	x = y;
	y = t;
    }
    WinApplyTranslation(j->SJI_bmWinHan, (WWFixedAsDWord) ((sdword) x << 16),
			(WWFixedAsDWord) ((sdword) y << 16),
			WIF_DONT_INVALIDATE);
}

/* AskForNextPage: on manual feed the user puts in the next sheet */
static word AskForNextPage(SpoolJobInfo *j)
{
    PState *ps = MemLock(j->SJI_pstate);
    PaperInputOptions pio = ps->PS_paperInput;

    MemUnlock(j->SJI_pstate);
    if (!(pio & PIO_MANUAL))
	return SPOOL_IC_OK;
    return SpoolErrorBox(SERROR_MANUAL_PAPER_FEED, 0);
}

/* the swaths of one sheet: the GSRetType of the page */
static GSRetType PrintSwaths(SpoolJobInfo *j)
{
    DriverRegs r;
    GSRetType ret;
    dword left;

    j->SJI_curScan = 0;
    for (;;) {
	GrClearBitmap(j->SJI_gstate);
	j->SJI_gstring = GrLoadGString(j->SJI_fHan, GST_STREAM, 0);
	GrSaveState(j->SJI_gstate);
	ret = GrDrawGString(j->SJI_gstate, j->SJI_gstring, 0, 0,
			    GSC_NEW_PAGE, NULL);
	GrRestoreState(j->SJI_gstate);
	GrDestroyGString(j->SJI_gstring, 0, GSKT_LEAVE_DATA);
	j->SJI_gstring = 0;

	memset(&r, 0, sizeof(r));
	r.DR_dx = j->SJI_bmFileHan;
	r.DR_cx = j->SJI_bmHan;
	if (CallPrinter(j, DR_PRINT_SWATH, &r))
	    return GSRT_FAULT;

	j->SJI_curScan += j->SJI_swathH;
	if (j->SJI_curScan >= j->SJI_pageH)
	    return ret;
	/* the last swath of the page may be lower */
	left = j->SJI_pageH - j->SJI_curScan;
	if (left < j->SJI_swathH) {
	    GrClearBitmap(j->SJI_gstate);
	    AdjustBitmapHeight(j, (word) left);
	}
	/* the page a swath up: before the page's transformation */
	WinGetTransform(j->SJI_bmWinHan, &j->SJI_oldMatrix);
	WinSetNullTransform(j->SJI_bmWinHan, WIF_DONT_INVALIDATE);
	TranslateThePage(j, 0, (sword) -j->SJI_swathH);
	WinApplyTransform(j->SJI_bmWinHan, &j->SJI_oldMatrix,
			  WIF_DONT_INVALIDATE);
	FilePos(j->SJI_fHan, j->SJI_fPos, FILE_POS_START);
	if (CheckForErrors(j, NULL))
	    return GSRT_FAULT;
	if (ret == GSRT_FAULT)
	    return ret;
    }
}

/* PrintGraphicsPage: one page of the document, on as many sheets as it
 * is tiled over; the GSRetType of the page */
GSRetType PrintGraphicsPage(SpoolJobInfo *j)
{
    WindowHandle w = j->SJI_bmWinHan;
    GSRetType ret;
    Boolean err;

    WinApplyScale(w, j->SJI_pXscale, j->SJI_pYscale, WIF_DONT_INVALIDATE);
    WinApplyTransform(w, &j->SJI_defMatrix, WIF_DONT_INVALIDATE);
    j->SJI_curyPage = j->SJI_yPages;
    for (;;) {					/* down */
	WinGetTransform(w, &j->SJI_yloopTM);
	j->SJI_curxPage = j->SJI_xPages;
	for (;;) {				/* across */
	    BumpPhysPageNumber(j);
	    WinGetTransform(w, &j->SJI_xloopTM);
	    if (ProcessStartPage(j)) {
		ret = GSRT_FAULT;
		goto done;
	    }
	    ret = PrintSwaths(j);
	    /* PEC_FORM_FEED, as for PDL printers (ABI.md section 13w) */
	    err = ProcessEndPage(j, &ret, PEC_FORM_FEED);
	    AdjustBitmapHeight(j, j->SJI_swathH);
	    if (err || ret == GSRT_FAULT) {
		ret = GSRT_FAULT;
		goto done;
	    }
	    if ((sword) --j->SJI_curxPage <= 0)
		break;
	    /* the next sheet to the right: the same page again */
	    FilePos(j->SJI_fHan, j->SJI_fPos, FILE_POS_START);
	    WinSetTransform(w, &j->SJI_xloopTM, WIF_DONT_INVALIDATE);
	    TranslateThePage(j, (sword) -j->SJI_printWidth, 0);
	    if (AskForNextPage(j) == SPOOL_IC_DISMISS) {
		ret = GSRT_FAULT;
		goto done;
	    }
	}
	if ((sword) --j->SJI_curyPage <= 0)
	    break;
	/* the next row of sheets */
	FilePos(j->SJI_fHan, j->SJI_fPos, FILE_POS_START);
	WinSetTransform(w, &j->SJI_yloopTM, WIF_DONT_INVALIDATE);
	/* the asm swapped the row's translation on a rotated page before
	 * TranslateThePage swapped it again: down the page either way */
	if (j->SJI_printState & SPS_ROTATED)
	    TranslateThePage(j, (sword) -j->SJI_printHeight, 0);
	else
	    TranslateThePage(j, 0, (sword) -j->SJI_printHeight);
	if (AskForNextPage(j) == SPOOL_IC_DISMISS) {
	    ret = GSRT_FAULT;
	    goto done;
	}
    }
done:
    WinSetNullTransform(w, WIF_DONT_INVALIDATE);
    return ret;
}
