/*
 * processThread.c -- printing a job on a queue's thread (the C port of
 * processThread.asm): the job's information, the printer driver's start
 * and end, the document's orientation on the paper, the page loop, and
 * the job's removal from the queue.
 *
 * The asm's routines shared the stack frame of SpoolerLoop (curJob, a
 * SpoolJobInfo); here they get a pointer to it.  The queue is locked
 * only for short times (LockQueue), as in the asm.
 *
 * Port32: PDL, graphics and text printing (processPDL.c,
 * processGraphics.c, processText.c).  Tiramisu (fax) printing and
 * labels are not built, as in the default spooler.  The asm never sets
 * SPS_ROTATED; neither does this.
 */
#include "spoolLocal.h"
#include <string.h>

/* ---------------------------------------------------------------------
 * Calling the drivers
 * ------------------------------------------------------------------- */

/* CallPrinter: from DRE_SET_DEVICE on, bx is the PState */
Boolean CallPrinter(SpoolJobInfo *j, word function, DriverRegs *r)
{
    r->DR_bx = j->SJI_pstate;
    return j->SJI_pDriver(function, r);
}

Boolean CallPort(SpoolJobInfo *j, word function, DriverRegs *r)
{
    return j->SJI_stream(function, r);
}

static Boolean IsPDL(const SpoolJobInfo *j)
{
    return (j->SJI_printState & SPS_SMARTS) == PS_PDL;
}

static PaperInputOptions PaperInput(SpoolJobInfo *j)
{
    PState *ps = MemLock(j->SJI_pstate);
    PaperInputOptions pio = ps->PS_paperInput;

    MemUnlock(j->SJI_pstate);
    return pio;
}

/* ---------------------------------------------------------------------
 * The job and the queue
 * ------------------------------------------------------------------- */

/* GetJobInfo: the queue's current job into the PState (all of its
 * JobParameters, which the printer driver may have made larger) and
 * into the SpoolJobInfo (a standard JobParameters) */
void GetJobInfo(SpoolJobInfo *j)
{
    QueueInfo *q;
    JobInfoStruct *job;
    word size;

    LockQueue();
    q = QUEUE(j->SJI_qHan);
    q->QI_threadInfo = j;
    job = JOB(q->QI_curJob);
    size = job->JIS_info.JP_size;
    MemReAlloc(j->SJI_pstate, (word) (sizeof(PState) + size),
	       HAF_NO_ERR);
    job = JOB(QUEUE(j->SJI_qHan)->QI_curJob);
    memcpy(PS_JOB_PARAMS((PState *) MemLock(j->SJI_pstate)),
	   &job->JIS_info, size);
    MemUnlock(j->SJI_pstate);
    memcpy(&j->SJI_info, &job->JIS_info, sizeof(JobParameters));
    UnlockQueue();
}

/* StoppedByError: the job ends with PSCT_ERROR */
void StoppedByError(SpoolJobInfo *j)
{
    LockQueue();
    QUEUE(j->SJI_qHan)->QI_error = SI_ERROR;
    UnlockQueue();
}

/* SetNumPhysicalPages: for SpoolInfo (no overflow check, as the asm) */
static void SetNumPhysicalPages(SpoolJobInfo *j)
{
    word n = (word) (j->SJI_xPages * j->SJI_yPages *
		     j->SJI_info.JP_numCopies * j->SJI_info.JP_numPages);

    LockQueue();
    QUEUE(j->SJI_qHan)->QI_numPhysPgs = n;
    UnlockQueue();
}

/* CheckForErrors: TRUE (carry) if the job is to stop; a pause waits
 * until it is over.  *interruption gets the SpoolInterruptions. */
Boolean CheckForErrors(SpoolJobInfo *j, byte *interruption)
{
    byte e;

    for (;;) {
	LockQueue();
	e = QUEUE(j->SJI_qHan)->QI_error;
	UnlockQueue();
	if (e != SI_PAUSE)
	    break;
	TimerSleep(60);
    }
    if (interruption)
	*interruption = e;
    return e != SI_KEEP_GOING;
}

void BumpPageNumber(SpoolJobInfo *j)
{
    QueueInfo *q;

    LockQueue();
    q = QUEUE(j->SJI_qHan);
    q->QI_curPage++;
    q->QI_filePos = j->SJI_fPos;
    UnlockQueue();
}

/* BumpPhysPageNumber: and tell the print job status list */
void BumpPhysPageNumber(SpoolJobInfo *j)
{
    QueueInfo *q;
    word page, id;

    LockQueue();
    q = QUEUE(j->SJI_qHan);
    page = ++q->QI_curPhysPg;
    id = JOB(q->QI_curJob)->JIS_jobID;
    SpoolSendStatusChange(SPOOL_PSCT_NEW_PAGE, id, page);
    UnlockQueue();
}

static void DeleteSpoolFile(const char *fname)
{
    FilePushDir();
    FileSetStandardPath(SP_SPOOL);
    FileDelete(fname);
    FilePopDir();
}

/* RemoveJob: the finished job out of the queue.  TRUE (carry) if it was
 * the last one: the queue stays locked for the thread's end. */
Boolean RemoveJob(SpoolJobInfo *j)
{
    byte e;
    QueueInfo *q;
    ChunkHandle job, ch, next;
    JobInfoStruct *p;

    CheckForErrors(j, &e);
    /* a job saved for later keeps its file when detaching */
    if ((e != SI_DETACH || (j->SJI_info.JP_spoolOpts & SO_SHUTDOWN_ACTION))
	&& (j->SJI_info.JP_spoolOpts & SO_DELETE))
	DeleteSpoolFile(j->SJI_info.JP_fname);

    LockQueue();
    q = QUEUE(j->SJI_qHan);
    job = q->QI_curJob;
    if (q->QI_error == SI_DETACH) {
	/* the jobs canceled by the detach are gone; the rest stay in the
	 * state file (not restored yet: Port32) */
	for (ch = JOB(job)->JIS_next; ch; ch = p->JIS_next) {
	    p = JOB(ch);
	    if (!(p->JIS_info.JP_spoolOpts & SO_SHUTDOWN_ACTION))
		continue;
	    if (p->JIS_info.JP_spoolOpts & SO_DELETE)
		DeleteSpoolFile(p->JIS_info.JP_fname);
	    SendJobRemovedNotification(ch);
	    p = JOB(ch);
	}
	JOB(job)->JIS_next = 0;		/* the thread goes away */
    }
    SendJobRemovedNotification(job);
    next = JOB(job)->JIS_next;
    LMemFreeHandles(queueHandle, job);
    PQ()->PQ_numJobs--;
    q = QUEUE(j->SJI_qHan);
    if (!next) {
	q->QI_curJob = 0;
	return TRUE;
    }
    q->QI_curJob = next;
    q->QI_fileHan = 0;
    q->QI_filePos = 0;
    q->QI_curPage = 0;
    q->QI_curPhysPg = 0;
    q->QI_numPhysPgs = 0;
    q->QI_error = SI_KEEP_GOING;
    UnlockQueue();
    return FALSE;
}

/* ---------------------------------------------------------------------
 * The printer driver
 * ------------------------------------------------------------------- */

/* CalcPageSizeInfo: resolution, scale, band and swath of the mode */
static void CalcPageSizeInfo(SpoolJobInfo *j)
{
    DriverRegs r;
    const PrinterInfo *pi;
    const GraphicsProperties *gp;
    WWFixedAsDWord w;
    dword bytesPerBand, bands;
    word scan, dots;
    byte fmt;

    memset(&r, 0, sizeof(r));
    CallPrinter(j, DR_PRINT_DEVICE_INFO, &r);
    pi = r.DR_dsSi;
    gp = (const GraphicsProperties *) ((const byte *) pi +
	*(const word *) ((const byte *) &pi->PI_lowRes +
			 j->SJI_info.JP_printMode));
    j->SJI_colorFormat = gp->GP_colorFormat;
    j->SJI_colorCorrection = gp->GP_colorCorrection;
    j->SJI_pXres = gp->GP_xres;
    j->SJI_pYres = gp->GP_yres;
    j->SJI_pXscale = GrUDivWWFixed((dword) gp->GP_xres << 16, 72UL << 16);
    j->SJI_pYscale = GrUDivWWFixed((dword) gp->GP_yres << 16, 72UL << 16);
    j->SJI_bandH = gp->GP_bandHeight;

    /* the width in dots; PostScript printers start at the paper's edge */
    dots = j->SJI_printWidth;
    if (IsPDL(j))
	dots = (word) (dots + PAPER_INFO(j).PSR_margins.PCMP_left);
    w = GrMulWWFixed((dword) dots << 16, j->SJI_pXscale);
    j->SJI_bandW = (word) ((w >> 16) + ((w >> 15) & 1));

    scan = j->SJI_bandW;
    fmt = (byte) (j->SJI_colorFormat & BMT_FORMAT);
    if (fmt > BMF_4CMYK || fmt == BMF_MONO) {
	scan = (word) ((scan + 7) >> 3);
	if (fmt != BMF_MONO)
	    scan = (word) (scan << 2);		/* CMY: 4 planes */
    } else if (fmt == BMF_24BIT)
	scan = (word) (scan * 3);
    else if (fmt == BMF_4BIT)
	scan = (word) ((scan + 1) >> 1);
    /* BMF_8BIT: a byte a dot */
    bytesPerBand = (dword) j->SJI_bandH * scan;
    if (!bytesPerBand)
	bytesPerBand = 1;
    bands = SPOOL_SWATH_DATA_SIZE / bytesPerBand + 1;
    j->SJI_swathH = (word) (bands * j->SJI_bandH);

    dots = j->SJI_printHeight;
    if (IsPDL(j))
	dots = (word) (dots + PAPER_INFO(j).PSR_margins.PCMP_top);
    w = GrMulWWFixed((dword) dots << 16, j->SJI_pYscale);
    j->SJI_pageH = (word) ((w >> 16) + ((w >> 15) & 1));
    /* the whole page in one swath if it fits, else whole bands */
    if ((word) j->SJI_pageH < j->SJI_swathH && j->SJI_bandH) {
	bands = (word) j->SJI_pageH / j->SJI_bandH;
	if (!bands)
	    bands = 1;
	j->SJI_swathH = (word) (bands * j->SJI_bandH);
    }
}

/* InitPrinterDriver: the device, retries, smarts, mode and the printable
 * areas; then DR_PRINT_START_JOB.  TRUE on an error (StoppedByError
 * called). */
/* AvailableMode: the graphics mode, or the nearest the device has
 * (better ones first) */
static word AvailableMode(const PrinterInfo *pi, word mode)
{
    static const word order[3][3] = {
	{ PM_GRAPHICS_LOW_RES, PM_GRAPHICS_MED_RES, PM_GRAPHICS_HI_RES },
	{ PM_GRAPHICS_MED_RES, PM_GRAPHICS_HI_RES, PM_GRAPHICS_LOW_RES },
	{ PM_GRAPHICS_HI_RES, PM_GRAPHICS_MED_RES, PM_GRAPHICS_LOW_RES }
    };
    const word *res = &pi->PI_lowRes;
    int i, k = mode == PM_GRAPHICS_LOW_RES ? 0 :
	       mode == PM_GRAPHICS_MED_RES ? 1 : 2;

    for (i = 0; i < 3; i++)
	if (res[order[k][i] / 2])
	    return order[k][i];
    return mode;
}

static Boolean InitPrinterDriver(SpoolJobInfo *j)
{
    DriverRegs r;
    const PrintDriverInfo *pdi;
    const PrinterInfo *pi;
    PrinterResend resend;
    sword w, h;
    QueueInfo *q;

    memset(&r, 0, sizeof(r));
    r.DR_dsSi = j->SJI_info.JP_deviceName;
    CallPrinter(j, DRE_SET_DEVICE, &r);

    memset(&r, 0, sizeof(r));
    CallPrinter(j, DR_PRINT_DRIVER_INFO, &r);
    pdi = r.DR_dsSi;
    resend = pdi ? pdi->PDI_resendOrNot : PR_DONT_RESEND;
    LockQueue();
    q = QUEUE(j->SJI_qHan);
    q->QI_resend = resend;
    q->QI_maxRetry = j->SJI_info.JP_retries;
    q->QI_curRetry = j->SJI_info.JP_retries;
    UnlockQueue();

    memset(&r, 0, sizeof(r));
    CallPrinter(j, DR_PRINT_DEVICE_INFO, &r);
    pi = r.DR_dsSi;
    j->SJI_printState = (byte) (j->SJI_printState |
				((pi ? pi->PI_smarts : 0) & SPS_SMARTS) |
				SPS_FORM_FEED);

    /* Port32: a graphics mode the device has not (its offset 0, as the
     * Okidata's medium resolution) becomes one it has, the next better
     * one first; in DOS16 the print dialog offered only the device's
     * modes, and the spooler took the job's on trust */
    if (pi && j->SJI_info.JP_printMode < PM_FIRST_TEXT_MODE)
	j->SJI_info.JP_printMode = AvailableMode(pi,
						 j->SJI_info.JP_printMode);

    /* the mode, with the paper size */
    memset(&r, 0, sizeof(r));
    r.DR_cx = j->SJI_info.JP_printMode;
    r.DR_ax = (word) PAPER_INFO(j).PSR_width;
    r.DR_si = (word) PAPER_INFO(j).PSR_height;
    CallPrinter(j, DR_PRINT_SET_MODE, &r);

    /* no printer has no printable area: then the user chose a paper
     * that does not exist, and gets half an inch */
    w = (sword) ((word) PAPER_INFO(j).PSR_width -
		 PAPER_INFO(j).PSR_margins.PCMP_right -
		 PAPER_INFO(j).PSR_margins.PCMP_left);
    j->SJI_printWidth = (word) (w > 0 ? w : 36);
    h = (sword) ((word) PAPER_INFO(j).PSR_height -
		 PAPER_INFO(j).PSR_margins.PCMP_bottom -
		 PAPER_INFO(j).PSR_margins.PCMP_top);
    j->SJI_printHeight = (word) (h > 0 ? h : 36);
    j->SJI_docWidth = DOC_INFO(j).PSR_width -
	(word) (DOC_INFO(j).PSR_margins.PCMP_left +
		DOC_INFO(j).PSR_margins.PCMP_right);
    j->SJI_docHeight = DOC_INFO(j).PSR_height -
	(word) (DOC_INFO(j).PSR_margins.PCMP_top +
		DOC_INFO(j).PSR_margins.PCMP_bottom);

    j->SJI_pXscale = 1UL << 16;
    j->SJI_pYscale = 1UL << 16;
    if (j->SJI_info.JP_printMode < PM_FIRST_TEXT_MODE)
	CalcPageSizeInfo(j);
    else
	j->SJI_pageH = PAPER_INFO(j).PSR_margins.PCMP_bottom;

    memset(&r, 0, sizeof(r));
    r.DR_dsSi = &j->SJI_info;
    if (CallPrinter(j, DR_PRINT_START_JOB, &r)) {
	StoppedByError(j);
	return TRUE;
    }
    return FALSE;
}

/* ExitPrinterDriver: the end of the job; the printer-specific data the
 * driver may have changed goes back to the queue */
static void ExitPrinterDriver(SpoolJobInfo *j)
{
    DriverRegs r;
    PState *ps;
    JobParameters *jp;
    JobInfoStruct *job;
    word n;

    memset(&r, 0, sizeof(r));
    CallPrinter(j, DR_PRINT_END_JOB, &r);

    LockQueue();
    job = JOB(QUEUE(j->SJI_qHan)->QI_curJob);
    ps = MemLock(j->SJI_pstate);
    jp = PS_JOB_PARAMS(ps);
    n = (word) (jp->JP_size - offsetof(JobParameters, JP_printerData));
    if (jp->JP_size == job->JIS_info.JP_size &&
	jp->JP_size > offsetof(JobParameters, JP_printerData))
	memcpy(job->JIS_info.JP_printerData, jp->JP_printerData, n);
    MemUnlock(j->SJI_pstate);
    UnlockQueue();
}

/* ProcessStartPage: a form feed unless it was suppressed */
Boolean ProcessStartPage(SpoolJobInfo *j)
{
    DriverRegs r;

    memset(&r, 0, sizeof(r));
    r.DR_cx = (j->SJI_printState & SPS_FORM_FEED) ? C_FF : 0;
    if (CallPrinter(j, DR_PRINT_START_PAGE, &r)) {
	StoppedByError(j);
	return TRUE;
    }
    return FALSE;
}

/* ProcessEndPage: *ret is the page's GSRetType; on GSRT_NEW_PAGE the
 * next element tells whether the document is complete, and *ret becomes
 * its GSRetType.  data is what the GR_NEW_PAGE said (PageEndCommand).
 * Port32: the asm compared bl, the low byte of the PState handle the
 * driver left in bx, with PEC_FORM_FEED; the C printer drivers return
 * no data, so the spooler passes PEC_FORM_FEED (ABI.md section 13w). */
Boolean ProcessEndPage(SpoolJobInfo *j, GSRetType *ret, word data)
{
    DriverRegs r;
    byte cl = C_FF;
    dword pos;
    GStateHandle gs;
    word last;

    j->SJI_printState |= SPS_FORM_FEED;
    if (*ret == GSRT_NEW_PAGE) {
	pos = FilePos(j->SJI_fHan, 0, FILE_POS_RELATIVE);
	gs = GrLoadGString(j->SJI_fHan, GST_STREAM, 0);
	/* Port32: the look leaves the gstate as it was; a drawing
	 * stopped on an element (GSRT_ONE) keeps the translation to the
	 * pen, which moved the next page */
	GrSaveState(j->SJI_gstate);
	*ret = GrDrawGStringAtCP(j->SJI_gstate, gs, GSC_ONE, &last);
	GrRestoreState(j->SJI_gstate);
	GrDestroyGString(gs, 0, GSKT_LEAVE_DATA);
	FilePos(j->SJI_fHan, pos, FILE_POS_START);
	if (data != PEC_FORM_FEED) {
	    cl = 0;
	    j->SJI_printState &= (byte) ~SPS_FORM_FEED;
	}
    }
    memset(&r, 0, sizeof(r));
    r.DR_cx = cl;
    if (CallPrinter(j, DR_PRINT_END_PAGE, &r)) {
	StoppedByError(j);
	return TRUE;
    }
    return FALSE;
}

/* ---------------------------------------------------------------------
 * Where the document goes on the paper
 * ------------------------------------------------------------------- */

/* WillDocumentFit: FALSE (0) if not, TRUE (-1) upright, 1 rotated */
word WillDocumentFit(const PageSizeReport *doc, const PageSizeReport *paper)
{
    dword docX, docY, pageX, pageY;

    docX = doc->PSR_width - (word) (doc->PSR_margins.PCMP_left +
				    doc->PSR_margins.PCMP_right);
    docY = doc->PSR_height - (word) (doc->PSR_margins.PCMP_top +
				     doc->PSR_margins.PCMP_bottom);
    pageX = paper->PSR_width - (word) (paper->PSR_margins.PCMP_left +
				       paper->PSR_margins.PCMP_right);
    pageY = paper->PSR_height - (word) (paper->PSR_margins.PCMP_top +
					paper->PSR_margins.PCMP_bottom);
    if (docY <= pageY && docX <= pageX)
	return (word) TRUE;
    if (docX <= pageY && docY <= pageX)
	return 1;
    return FALSE;
}

#define WWF(i)	((WWFixedAsDWord) ((sdword) (i) * 65536L))

/* half of the low words' difference, as the asm (sar of a word) */
static sword Half(dword a, dword b)
{
    return (sword) ((sword) (word) (a - b) >> 1);
}

static void CenterUpright(SpoolJobInfo *j, GStateHandle gs)
{
    sword x = Half(PAPER_INFO(j).PSR_width, DOC_INFO(j).PSR_width);
    sword y = Half(PAPER_INFO(j).PSR_height, DOC_INFO(j).PSR_height);

    x = (sword) (x - PAPER_INFO(j).PSR_margins.PCMP_left);
    y = (sword) (y - PAPER_INFO(j).PSR_margins.PCMP_top);
    GrApplyTranslation(gs, WWF(x), WWF(y));
}

static void CenterUprightPDL(SpoolJobInfo *j, GStateHandle gs)
{
    GrApplyTranslation(gs,
		       WWF(Half(PAPER_INFO(j).PSR_width,
				DOC_INFO(j).PSR_width)),
		       WWF(Half(PAPER_INFO(j).PSR_height,
				DOC_INFO(j).PSR_height)));
}

/* CalcScaleFactor: paper / document, as 1 / (document / paper) */
static WWFixedAsDWord CalcScaleFactor(dword doc, word paper)
{
    DWFixed dividend, q;
    WWFixed divisor;

    dividend.DWF_int = (sdword) doc;
    dividend.DWF_frac = 0;
    divisor.WWF_int = (sword) paper;
    divisor.WWF_frac = 0;
    GrSDivDWFbyWWF(&dividend, &divisor, &q);
    return GrSDivWWFixed(1UL << 16,
			 ((dword) (word) q.DWF_int << 16) | q.DWF_frac);
}

/* CalcScaledDifference: half of (paper - scale * document), not < 0 */
static sword CalcScaledDifference(WWFixedAsDWord scale, dword doc,
				  word paper)
{
    DWFixed a, b, r;
    word scaled;
    sword d;

    a.DWF_int = (sdword) (scale >> 16);
    a.DWF_frac = (word) scale;
    b.DWF_int = (sdword) doc;
    b.DWF_frac = 0;
    GrMulDWFixed(&a, &b, &r);
    scaled = (word) ((word) r.DWF_int + ((r.DWF_frac & 0x8000) ? 1 : 0));
    d = (sword) ((sword) (paper - scaled) >> 1);
    return d < 0 ? 0 : d;
}

/* the factors of scaling to fit, the same in x and y */
static WWFixedAsDWord GetScaleFactors(SpoolJobInfo *j, Boolean rotated,
				      sword *xOff, sword *yOff)
{
    word pw = rotated ? j->SJI_printHeight : j->SJI_printWidth;
    word ph = rotated ? j->SJI_printWidth : j->SJI_printHeight;
    WWFixedAsDWord y = CalcScaleFactor(j->SJI_docHeight, ph);
    WWFixedAsDWord x = CalcScaleFactor(j->SJI_docWidth, pw);
    WWFixedAsDWord s = (sdword) x <= (sdword) y ? x : y;

    *xOff = CalcScaledDifference(s, j->SJI_docWidth, pw);
    *yOff = CalcScaledDifference(s, j->SJI_docHeight, ph);
    return s;
}

static void UndoDocMargins(SpoolJobInfo *j, GStateHandle gs)
{
    GrApplyTranslation(gs, WWF(-(sword) DOC_INFO(j).PSR_margins.PCMP_left),
		       WWF(-(sword) DOC_INFO(j).PSR_margins.PCMP_top));
}

static void ScaleToFit(SpoolJobInfo *j, GStateHandle gs)
{
    sword x, y;
    WWFixedAsDWord s = GetScaleFactors(j, FALSE, &x, &y);

    GrApplyTranslation(gs, WWF(x), WWF(y));
    GrApplyScale(gs, s, s);
    UndoDocMargins(j, gs);
}

/* the scale is of the printable areas: so the paper margins are added */
static void ScaleToFitPDL(SpoolJobInfo *j, GStateHandle gs)
{
    sword x, y;
    WWFixedAsDWord s = GetScaleFactors(j, FALSE, &x, &y);

    GrApplyTranslation(gs,
		       WWF(x + (sword) PAPER_INFO(j).PSR_margins.PCMP_left),
		       WWF(y + (sword) PAPER_INFO(j).PSR_margins.PCMP_top));
    GrApplyScale(gs, s, s);
    UndoDocMargins(j, gs);
}

static void RotateDoc(SpoolJobInfo *j, GStateHandle gs, Boolean pdl)
{
    sword x, y;

    GrApplyRotation(gs, WWF(-90));
    GrApplyTranslationDWord(gs, 0, -(sdword) DOC_INFO(j).PSR_height);
    x = Half(PAPER_INFO(j).PSR_height, DOC_INFO(j).PSR_width);
    y = (sword) -Half(PAPER_INFO(j).PSR_width, DOC_INFO(j).PSR_height);
    if (!pdl) {
	y = (sword) (y + PAPER_INFO(j).PSR_margins.PCMP_left);
	x = (sword) (x - PAPER_INFO(j).PSR_margins.PCMP_top);
    }
    GrApplyTranslation(gs, WWF(x), WWF(y));
}

static void RotateToFit(SpoolJobInfo *j, GStateHandle gs, Boolean pdl)
{
    sword x, y;
    sdword dx, dy;
    WWFixedAsDWord s;

    GrApplyRotation(gs, WWF(-90));
    s = GetScaleFactors(j, TRUE, &x, &y);
    if (pdl) {
	dx = (sword) (x + PAPER_INFO(j).PSR_margins.PCMP_top);
	dy = (sdword) (sword) (y + PAPER_INFO(j).PSR_margins.PCMP_left) -
	    (sdword) PAPER_INFO(j).PSR_width;
    } else {
	dx = x;
	dy = (sdword) y - j->SJI_printWidth;
    }
    GrApplyTranslationDWord(gs, dx, dy);
    GrApplyScale(gs, s, s);
    UndoDocMargins(j, gs);
}

/* TilePDLDocument: the sheets across and down */
static void TilePDLDocument(SpoolJobInfo *j)
{
    dword w, h;

    w = j->SJI_docWidth + (sdword) ((sword) DOC_INFO(j).PSR_margins.
				    PCMP_left - (sword) PAPER_INFO(j).
				    PSR_margins.PCMP_left);
    h = j->SJI_docHeight + (sdword) ((sword) DOC_INFO(j).PSR_margins.
				     PCMP_top - (sword) PAPER_INFO(j).
				     PSR_margins.PCMP_top);
    j->SJI_xPages = (word) ((w + j->SJI_printWidth - 1) / j->SJI_printWidth);
    j->SJI_yPages = (word) ((h + j->SJI_printHeight - 1) /
			    j->SJI_printHeight);
    j->SJI_printState |= SPS_TILED;
}

static void TileDocument(SpoolJobInfo *j, GStateHandle gs)
{
    sword x = (sword) PAPER_INFO(j).PSR_margins.PCMP_left;
    sword y = (sword) PAPER_INFO(j).PSR_margins.PCMP_top;

    TilePDLDocument(j);
    if (x > (sword) DOC_INFO(j).PSR_margins.PCMP_left)
	x = (sword) DOC_INFO(j).PSR_margins.PCMP_left;
    if (y > (sword) DOC_INFO(j).PSR_margins.PCMP_top)
	y = (sword) DOC_INFO(j).PSR_margins.PCMP_top;
    GrApplyTranslation(gs, WWF(-x), WWF(-y));
}

/* CalcDocOrientation: centred, scaled to fit, rotated or tiled; the
 * result is SJI_defMatrix */
static void CalcDocOrientation(SpoolJobInfo *j)
{
    GStateHandle gs;
    word bits = 0, fit;
    Boolean text = j->SJI_info.JP_printMode >= PM_FIRST_TEXT_MODE;
    SpoolOptions opts = j->SJI_info.JP_spoolOpts;

    j->SJI_xPages = 1;
    j->SJI_yPages = 1;
    gs = GrCreateState(0);
    if (IsPDL(j))
	bits |= OB_PDL;

    fit = WillDocumentFit(&DOC_INFO(j), &PAPER_INFO(j));
    if (fit != FALSE) {
	if (!(opts & SO_FORCE_ROT)) {
	    if (fit == (word) TRUE)
		goto setTransform;
	    if (text)
		goto tileIt;
	}
	bits |= OB_ROTATE;
	goto setTransform;
    }

    /* too big: scale it to fit, or tile it */
    if (!(opts & SO_SCALE))
	goto tileIt;
    bits |= OB_SCALE_TO_FIT;
    if (text)
	goto setTransform;
    /* rotate as well if the paper and the document differ */
    if (opts & SO_FORCE_ROT)
	bits |= OB_ROTATE;
    else if (j->SJI_printWidth > j->SJI_printHeight) {
	if (j->SJI_docWidth < j->SJI_docHeight)
	    bits |= OB_ROTATE;
    } else if (j->SJI_docWidth > j->SJI_docHeight)
	bits |= OB_ROTATE;

setTransform:
    j->SJI_gstate = gs;
    switch (bits) {
    case 0:
	CenterUpright(j, gs);
	break;
    case OB_SCALE_TO_FIT:
	ScaleToFit(j, gs);
	break;
    case OB_ROTATE:
	RotateDoc(j, gs, FALSE);
	break;
    case OB_SCALE_TO_FIT | OB_ROTATE:
	RotateToFit(j, gs, FALSE);
	break;
    case OB_PDL:
	CenterUprightPDL(j, gs);
	break;
    case OB_PDL | OB_SCALE_TO_FIT:
	ScaleToFitPDL(j, gs);
	break;
    case OB_PDL | OB_ROTATE:
	RotateDoc(j, gs, TRUE);
	break;
    default:
	RotateToFit(j, gs, TRUE);
	break;
    }
    goto done;

tileIt:
    if (bits & OB_PDL)
	TilePDLDocument(j);
    else {
	/* most printers wrap text that is too wide: leave a quarter inch */
	if (text) {
	    TilePDLDocument(j);
	    if (j->SJI_xPages != 1)
		j->SJI_printWidth = (word) (j->SJI_printWidth - 12);
	}
	TileDocument(j, gs);
    }

done:
    GrGetTransform(gs, &j->SJI_defMatrix);
    GrDestroyState(gs);
}

/* the page height in device units of a dimension of the document */
static dword DevicePageHeight(SpoolJobInfo *j, dword docDim)
{
    DWFixed a, b, r;

    a.DWF_int = (sdword) (docDim - PAPER_INFO(j).PSR_margins.PCMP_top);
    a.DWF_frac = 0;
    b.DWF_int = (sdword) (sword) (j->SJI_pYscale >> 16);
    b.DWF_frac = (word) j->SJI_pYscale;
    GrMulDWFixed(&a, &b, &r);
    return (dword) r.DWF_int + ((r.DWF_frac & 0x8000) ? 1 : 0);
}

/* AdjustTileForTractor: on tractor paper, one tall page instead of
 * sheets down (in whole bands) */
static void AdjustTileForTractor(SpoolJobInfo *j)
{
    dword h;

    if (!(PaperInput(j) & PIO_TRACTOR) || j->SJI_yPages <= 1)
	return;
    j->SJI_yPages = 0;
    if (j->SJI_info.JP_printMode >= PM_FIRST_TEXT_MODE) {
	j->SJI_pageH = DOC_INFO(j).PSR_height;
	return;
    }
    h = DevicePageHeight(j, (j->SJI_printState & SPS_ROTATED)
			 ? DOC_INFO(j).PSR_width : DOC_INFO(j).PSR_height);
    if (j->SJI_bandH)
	h = (h + j->SJI_bandH - 1) / j->SJI_bandH * j->SJI_bandH;
    j->SJI_pageH = h;
}

/* AdjustForSupressFF: without form feeds, the page is the document */
static void AdjustForSupressFF(SpoolJobInfo *j)
{
    if ((j->SJI_printState & SPS_FORM_FEED) ||
	j->SJI_info.JP_printMode >= PM_FIRST_TEXT_MODE)
	return;
    if ((j->SJI_printState & SPS_TILED) && j->SJI_yPages > 1)
	return;
    j->SJI_yPages = 0;
    j->SJI_pageH = DevicePageHeight(j, (j->SJI_printState & SPS_ROTATED)
				    ? DOC_INFO(j).PSR_width
				    : DOC_INFO(j).PSR_height);
}

/* ---------------------------------------------------------------------
 * The document
 * ------------------------------------------------------------------- */

static void PrintDocument(SpoolJobInfo *j)
{
    GSRetType ret;
    byte e;

    /* PDL, graphics or text */
    int kind = IsPDL(j) ? 0 :
	j->SJI_info.JP_printMode >= PM_FIRST_TEXT_MODE ? 2 : 1;

    if (kind == 2 ? DoTextInit(j) : kind ? DoGraphicsInit(j)
					  : DoPDLInit(j)) {
	SpoolErrorBox(SERROR_NO_MODE_AVAIL, j->SJI_qHan);
	StoppedByError(j);
	return;
    }
    if (CheckForErrors(j, &e))
	goto doneDoc;

    for (;;) {
	/* copies of each page, unless the driver makes them (then the
	 * JobParameters of the PState say 1) */
	{
	    PState *ps = MemLock(j->SJI_pstate);
	    j->SJI_numCopies = PS_JOB_PARAMS(ps)->JP_numCopies;
	    MemUnlock(j->SJI_pstate);
	}
	for (;;) {
	    if ((PaperInput(j) & PIO_MANUAL) &&
		SpoolErrorBox(SERROR_MANUAL_PAPER_FEED, 0) != SPOOL_IC_OK)
		ret = GSRT_FAULT;
	    else {
		ret = kind == 2 ? PrintTextPage(j) :
		      kind ? PrintGraphicsPage(j) : PrintPDLPage(j);
		if (CheckForErrors(j, &e))
		    goto doneDoc;
		/* uncollated copies: the page again */
		if (!(j->SJI_info.JP_spoolOpts & SO_COLLATE) &&
		    (sbyte) --j->SJI_numCopies > 0) {
		    FilePos(j->SJI_fHan, j->SJI_fPos, FILE_POS_START);
		    continue;
		}
	    }
	    break;
	}
	if (ret == GSRT_COMPLETE) {
	    /* collated copies: the document again */
	    if (!(j->SJI_info.JP_spoolOpts & SO_COLLATE) ||
		(sbyte) --j->SJI_info.JP_numCopies <= 0)
		break;
	    j->SJI_fPos = 0;
	    FilePos(j->SJI_fHan, 0, FILE_POS_START);
	    continue;
	}
	if (ret == GSRT_FAULT) {
	    StoppedByError(j);
	    break;
	}
	/* the file is past the GR_NEW_PAGE: the next page starts here */
	j->SJI_fPos = FilePos(j->SJI_fHan, 0, FILE_POS_RELATIVE);
	BumpPageNumber(j);
    }
doneDoc:
    if (kind == 2)
	DoTextCleanup(j);
    else if (kind)
	DoGraphicsCleanup(j);
    else
	DoPDLCleanup(j);
}

/* PrintFile: the job's spool file through the port to the printer */
void PrintFile(SpoolJobInfo *j)
{
    word portError;

    if (!j->SJI_info.JP_fname[0])
	return;
    FileSetStandardPath(SP_SPOOL);
    j->SJI_fHan = FileOpen(j->SJI_info.JP_fname,
			   FILE_DENY_NONE | FILE_ACCESS_RW);
    if (!j->SJI_fHan) {
	SpoolErrorBox(SERROR_NO_SPOOL_FILE, j->SJI_qHan);
	StoppedByError(j);
	return;
    }
    j->SJI_fPos = 0;
    j->SJI_printState = 0;
    if (InitPrinterPort(j, &portError)) {
	StoppedByError(j);
	if (portError != PE_PORT_NOT_OPENED)
	    ClosePrinterPort(j);
    } else {
	if (!InitPrinterDriver(j)) {
	    CalcDocOrientation(j);
	    AdjustTileForTractor(j);
	    AdjustForSupressFF(j);
	    SetNumPhysicalPages(j);
	    PrintDocument(j);
	}
	ExitPrinterDriver(j);
	ClosePrinterPort(j);
    }
    FileClose(j->SJI_fHan, TRUE);
    j->SJI_fHan = 0;
}
