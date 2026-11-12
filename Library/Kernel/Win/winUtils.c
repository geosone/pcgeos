/*
 * winUtils.c -- helpers of the window system (after winUtils.asm:
 * PWinTree, VWinTree, WinLockFromDI, WinNULLReg, WinSMALLReg,
 * WinCopyReg, WinANDReg, WinORReg, WinNOTReg, WinSetReg, TransRegCoord,
 * WinCalcWinBounds, WinCallVidDriver).
 *
 * The asm kept two special first words in a region chunk, NULL_REG
 * (EOREGREC) and WHOLE_REG (EOREGREC - 1, everything); the C code uses
 * only the null region.  Where the asm made the invalid region WHOLE,
 * it is the visible region here (which is what WHOLE stood for).
 */
#include "winInt.h"

/* ---------------------------------------------------------------------
 * The tree lock: a recursive lock of its own
 * ------------------------------------------------------------------- */

static KSem treeSem;
static Boolean treeSemInit;
static ThreadHandle treeOwner;
static word treeNesting;

void WinTreeP(void)
{
    ThreadHandle cur = KernelThisThread();

    if (!treeSemInit) {
	KSemInit(&treeSem, 1);
	treeSemInit = TRUE;
    }
    if (treeNesting && treeOwner == cur) {
	treeNesting++;
	return;
    }
    KSemP(&treeSem, KSEM_FOREVER);
    treeOwner = cur;
    treeNesting = 1;
}

void WinTreeV(void)
{
    if (!treeNesting || treeOwner != KernelThisThread())
	KernelFatal(UNLOCK_MODULE_BY_WRONG_THREAD);
    if (--treeNesting == 0) {
	treeOwner = 0;
	KSemV(&treeSem);
    }
}

/* WinLockFromDI */
Window *WinLockFromHandle(Handle h, WindowHandle *win)
{
    LMemBlockHeader *b;
    WindowHandle wh;

    if (!h)
	return NULL;
    b = MemLock(h);
    if (!b)
	return NULL;
    if (b->LMBH_lmemType == LMEM_TYPE_WINDOW) {
	*win = h;
	return (Window *) b;
    }
    wh = ((GState *) b)->GS_window;
    if (((GState *) b)->GS_gstring)
	wh = 0;
    MemUnlock(h);
    if (!wh)
	return NULL;
    *win = wh;
    return MemLock(wh);
}

/* ---------------------------------------------------------------------
 * Regions in chunks
 * ------------------------------------------------------------------- */

Region *WinReg(WindowHandle wh, ChunkHandle ch)
{
    return LMemDeref(WIN_REG(wh, ch));
}

Boolean WinRegIsNull(WindowHandle wh, ChunkHandle ch)
{
    return (Boolean) ((word) *WinReg(wh, ch) == EOREGREC);
}

/* WinNULLReg */
void WinRegNull(WindowHandle wh, ChunkHandle ch)
{
    LMemReAlloc(WIN_REG(wh, ch), sizeof(word));
    *(word *) WinReg(wh, ch) = EOREGREC;
}

/* WinSMALLReg: a work region no longer needed */
void WinRegSmall(WindowHandle wh, ChunkHandle ch)
{
    WinRegNull(wh, ch);
}

void WinRegRect(WindowHandle wh, ChunkHandle ch, sword l, sword t, sword r,
		sword b)
{
    word *p;

    if (l > r || t > b) {
	WinRegNull(wh, ch);
	return;
    }
    LMemReAlloc(WIN_REG(wh, ch), 7 * sizeof(word));
    p = (word *) WinReg(wh, ch);
    p[0] = (word) (t - 1);
    p[1] = EOREGREC;
    p[2] = (word) b;
    p[3] = (word) l;
    p[4] = (word) r;
    p[5] = EOREGREC;
    p[6] = EOREGREC;
}

/* TransRegCoord: 0x4000 to 0xbfff take a parameter (graphics.def) */
static word TransRegCoord(word v, const sword *params)
{
    if (v == EOREGREC || !((v & 0xc000) == 0x4000 || (v & 0xc000) == 0x8000))
	return v;
    return (word) ((sword) ((v & 0x1fff) - 0x1000) +
		   (params ? params[((v >> 13) & 7) - 2] : 0));
}

/* the rectangle of the parameters, for a NULL region (rectRegion) */
static const word rectRegion[] = {
    0x7000 - 1, EOREGREC,		/* PARAM_1 - 1 */
    0xb000, 0x5000, 0x9000, EOREGREC,	/* PARAM_3, PARAM_0, PARAM_2 */
    EOREGREC
};

/* WinSetReg, SetupOtherRegLow: a region with its parameters into a
 * chunk */
void WinRegSet(WindowHandle wh, ChunkHandle ch, const Region *reg,
	       const sword *params)
{
    const word *s = reg ? (const word *) reg : rectRegion, *e = s;
    word n, i, *d;

    /* the length: bands up to the final EOREGREC */
    while (*e != EOREGREC) {
	for (e++; *e != EOREGREC; e++)
	    ;
	e++;
    }
    n = (word) (e - s + 1);
    LMemReAlloc(WIN_REG(wh, ch), (word) (n * sizeof(word)));
    d = (word *) WinReg(wh, ch);
    for (i = 0; i < n; i++)
	d[i] = TransRegCoord(s[i], params);
    if (!reg && (params[0] > params[2] || params[1] > params[3]))
	WinRegNull(wh, ch);		/* an empty rectangle */
}

void WinRegCopy(optr src, optr dst)
{
    word size = LMemGetChunkSize(src);

    if (src == dst)
	return;
    LMemReAlloc(dst, size);
    memcpy(LMemDeref(dst), LMemDeref(src), size);
}

/* GrChunkRegOp, through a chunk of its own when dst is a source */
static void Op(RegionOpFlags op, optr a, optr b, optr dst)
{
    MemHandle mh = OptrToHandle(dst);
    optr t;

    if (dst != a && dst != b) {
	GrChunkRegOp(op, a, b, dst);
	return;
    }
    t = ConstructOptr(mh, LMemAlloc(mh, sizeof(word)));
    GrChunkRegOp(op, a, b, t);
    WinRegCopy(t, dst);
    LMemFree(t);
}

void WinRegAnd(optr a, optr b, optr dst)
{
    Op(ROF_AND_OP, a, b, dst);
}

void WinRegOr(optr a, optr b, optr dst)
{
    Op(ROF_OR_OP, a, b, dst);
}

void WinRegNot(optr a, optr dst)
{
    Op(ROF_NOT_OP, a, a, dst);
}

void WinRegSub(optr a, optr b, optr dst, optr tmp)
{
    if ((word) *(word *) LMemDeref(b) == EOREGREC) {
	if (a != dst)
	    WinRegCopy(a, dst);
	return;
    }
    WinRegNot(b, tmp);
    WinRegAnd(a, tmp, dst);
}

/* WinCalcWinBounds */
void WinCalcWinBounds(WindowHandle wh)
{
    Window *w = MemDeref(wh);
    Rectangle r;

    GrGetPtrRegBounds(WinReg(wh, w->W_winReg), &r);
    w = MemDeref(wh);
    w->W_winRect = r;
}

/* WinCallVidDriver */
Boolean WinCallVidDriver(Window *w, word function, DriverRegs *r)
{
    if (!w->W_driverStrategy)
	return TRUE;
    r->DR_esDi = w;
    return ((DriverStrategy *) w->W_driverStrategy)(function, r);
}
