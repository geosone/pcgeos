/*
 * winSaveUnder.c -- save under areas in the window system (after
 * winWindows.asm: WinGetOverlyingSaveUnders,
 * WinClearSaveUndersOverlappingParent, WinClearSaveUnders,
 * WinPerformObscure, WinPerformVis, CorrectForSaveUnder; winUtils.asm:
 * WinClearSaveUndersLow; winCreDest.asm: WinRequestUnder and the save
 * under parts of WinOpen and WinClose; winIndividual.asm:
 * WinMaskOutSaveUnder).
 *
 * A window opened with WPF_SAVE_UNDER asks the video driver to save the
 * screen under it (DR_VID_SAVE_UNDER, vidcomUnder.c); the area's bit is
 * in its W_saveUnder.  Closing it puts the pixels back
 * (DR_VID_RESTORE_UNDER, within what of it was visible) instead of
 * having the windows below redraw.
 *
 * The asm left the regions of the windows below alone while the area
 * was up ("in hopes that it will soon go away") and cut it out of a
 * window only when drawing to it collided with the area.  The C window
 * system calculates the regions as always (the windows below lose the
 * area) and keeps the bits only for the exposure:
 *   - opening marks the windows its area overlaps (W_savedUnderMask and
 *     W_savedUnderFlags), the window's own branch not;
 *   - drawing to a marked window that touches the area (the driver's
 *     collision check), or invalidating there, clears its mask bit: the
 *     saved pixels no longer show what the window would;
 *   - closing restores the pixels, and validation does not wash out or
 *     expose the restored part of the windows whose mask bit is still
 *     set (WinCalcVisReg: the asm's (mask 1, flags 1), "we got lucky");
 *     the others are exposed as usual.  Then the bits go.
 * An area is given up (DR_VID_NUKE_UNDER, the bits cleared, nothing to
 * expose since the regions are right) when:
 *   - a window opens over it (not one of the siblings below the new
 *     window), or the driver needs its bit for a new area;
 *   - a window moves, is resized, changes priority or closes within the
 *     bounds of its parent that the area overlaps (as the asm, which
 *     says this may give up more areas than needed);
 *   - its own window moves, is resized or changes priority.
 */
#include "winInt.h"

WinUnderFlags winUnderClosing;		/* the area being closed */
optr winUnderRestored;			/* and its restored region */

static WindowHandle Root(WindowHandle wh)
{
    for (;;) {
	Window *w = MemLock(wh);
	WindowHandle up = w->W_parent;
	MemUnlock(wh);
	if (!up)
	    return wh;
	wh = up;
    }
}

/* the bits f out of the branch */
static void ClearBits(WindowHandle wh, WinUnderFlags f)
{
    Window *w = MemLock(wh);
    WindowHandle ch, next;

    w->W_savedUnderMask &= (WinUnderFlags) ~f;
    w->W_savedUnderFlags &= (WinUnderFlags) ~f;
    for (ch = w->W_firstChild; ch; ch = next) {
	Window *c = MemLock(ch);
	next = c->W_nextSibling;
	MemUnlock(ch);
	ClearBits(ch, f);
    }
    MemUnlock(wh);
}

/* WinPerformObscure with WIN_OPEN_SAVE_UNDER: the windows of the branch
 * the rectangle overlaps, not the branch skip */
static void MarkBits(WindowHandle wh, WindowHandle skip, WinUnderFlags f,
		     const Rectangle *r)
{
    Window *w;
    WindowHandle ch, next;

    if (wh == skip)
	return;
    w = MemLock(wh);
    if (w->W_winRect.R_left <= r->R_right &&
	w->W_winRect.R_right >= r->R_left &&
	w->W_winRect.R_top <= r->R_bottom &&
	w->W_winRect.R_bottom >= r->R_top) {
	w->W_savedUnderMask |= f;
	w->W_savedUnderFlags |= f;
    }
    for (ch = w->W_firstChild; ch; ch = next) {
	Window *c = MemLock(ch);
	next = c->W_nextSibling;
	MemUnlock(ch);
	MarkBits(ch, skip, f, r);
    }
    MemUnlock(wh);
}

/* DR_VID_CHECK_UNDER: the areas a rectangle (screen) overlaps */
WinUnderFlags WinCheckUnder(Window *w, const Rectangle *b)
{
    DriverRegs r;

    memset(&r, 0, sizeof(r));
    r.DR_ax = (word) b->R_left;
    r.DR_bx = (word) b->R_top;
    r.DR_cx = (word) b->R_right;
    r.DR_dx = (word) b->R_bottom;
    if (WinCallVidDriver(w, DR_VID_CHECK_UNDER, &r))
	return 0;
    return (WinUnderFlags) r.DR_ax;
}

/* WinClearSaveUndersLow: give up the areas f; any is a window of the
 * tree */
void WinClearSaveUnders(WindowHandle any, WinUnderFlags f)
{
    WinUnderFlags bit;
    Window *w;

    if (!f)
	return;
    for (bit = 1; bit; bit = (WinUnderFlags) (bit << 1)) {
	DriverRegs r;
	WindowHandle sh;
	if (!(f & bit))
	    continue;
	memset(&r, 0, sizeof(r));
	r.DR_ax = bit;
	w = MemLock(any);
	(void) WinCallVidDriver(w, DR_VID_INFO_UNDER, &r);
	MemUnlock(any);
	sh = (WindowHandle) r.DR_di;
	if (!sh)
	    continue;
	w = MemLock(sh);
	memset(&r, 0, sizeof(r));
	(void) WinCallVidDriver(w, DR_VID_NUKE_UNDER, &r);
	w->W_saveUnder = 0;
	MemUnlock(sh);
    }
    ClearBits(Root(any), f);
}

/* WinGetOverlyingSaveUnders: the areas over the window's bounds, not
 * those of the siblings below it */
WinUnderFlags WinOverlyingUnders(WindowHandle wh)
{
    Window *w = MemLock(wh);
    WinUnderFlags f = WinCheckUnder(w, &w->W_winRect);
    WindowHandle s = w->W_nextSibling, next;

    MemUnlock(wh);
    for (; s && f; s = next) {
	Window *sw = MemLock(s);
	f &= (WinUnderFlags) ~sw->W_saveUnder;
	next = sw->W_nextSibling;
	MemUnlock(s);
    }
    return f;
}

/* before a change of the window (WinChangePrep and
 * WinClearSaveUndersOverlappingParent): its own area, then those over
 * its parent's bounds (its own at the root) */
void WinUnderChange(WindowHandle wh)
{
    Window *w = MemLock(wh);
    WindowHandle parent = w->W_parent;
    Rectangle b;

    if (w->W_saveUnder) {
	WinUnderFlags own = w->W_saveUnder;
	MemUnlock(wh);
	WinClearSaveUnders(wh, own);
	w = MemLock(wh);
    }
    b = w->W_winRect;
    if (parent) {
	Window *p = MemLock(parent);
	b = p->W_winRect;
	MemUnlock(parent);
    }
    {
	WinUnderFlags f = WinCheckUnder(w, &b);
	MemUnlock(wh);
	WinClearSaveUnders(wh, f);
    }
}

/* after opening a window with an area: the windows under it marked */
void WinUnderOpened(WindowHandle wh)
{
    Window *w = MemLock(wh);
    WinUnderFlags f = w->W_saveUnder;
    DriverRegs r;
    Rectangle b;

    if (f) {
	memset(&r, 0, sizeof(r));
	r.DR_ax = f;
	(void) WinCallVidDriver(w, DR_VID_INFO_UNDER, &r);
	b.R_left = (sword) r.DR_ax;
	b.R_top = (sword) r.DR_bx;
	b.R_right = (sword) r.DR_cx;
	b.R_bottom = (sword) r.DR_dx;
	MemUnlock(wh);
	if (r.DR_di)
	    MarkBits(Root(wh), wh, f, &b);
	return;
    }
    MemUnlock(wh);
}

/* WinMaskOutSaveUnder for an invalidation: what W_temp1Reg (screen)
 * touches is no longer what was saved */
void WinUnderInval(WindowHandle wh)
{
    Window *w = MemDeref(wh);
    Rectangle b;

    if (!w->W_savedUnderMask || WinRegIsNull(wh, w->W_temp1Reg))
	return;
    GrGetPtrRegBounds(WinReg(wh, w->W_temp1Reg), &b);
    w = MemDeref(wh);
    w->W_savedUnderMask &= (WinUnderFlags) ~WinCheckUnder(w, &b);
}

/*
 * WinClose of a window with an area, still in the tree: the areas over
 * it given up, the pixels restored within
 * its visible region, which W_temp1Reg keeps for the validation of the
 * parent's tree (WinCalcVisReg).  Returns the area's bit.
 */
WinUnderFlags WinUnderClosing(WindowHandle wh, WindowHandle parent)
{
    Window *w = MemLock(wh);
    WinUnderFlags f = w->W_saveUnder;
    DriverRegs r;

    MemUnlock(wh);
    if (!f)
	return 0;
    WinClearSaveUnders(parent,
		       (WinUnderFlags) (WinOverlyingUnders(wh) & ~f));
    w = MemLock(wh);
    WinRegCopy(WIN_REG(wh, w->W_visReg), WIN_REG(wh, w->W_temp1Reg));
    w = MemDeref(wh);
    memset(&r, 0, sizeof(r));
    r.DR_dsSi = WinReg(wh, w->W_temp1Reg);
    (void) WinCallVidDriver(w, DR_VID_RESTORE_UNDER, &r);
    w = MemDeref(wh);
    w->W_saveUnder = 0;
    winUnderClosing = f;
    winUnderRestored = WIN_REG(wh, w->W_temp1Reg);	/* stays locked */
    return f;
}

/* after the validation: the bits of the closed area go */
void WinUnderClosed(WindowHandle wh, WindowHandle parent, WinUnderFlags f)
{
    winUnderClosing = 0;
    winUnderRestored = 0;
    WinRegNull(wh, ((Window *) MemDeref(wh))->W_temp1Reg);
    MemUnlock(wh);
    ClearBits(Root(parent), f);
}
