/*
 * winWindows.c -- the window tree (after winWindows.asm: WinTInsert,
 * WinTLocateInsertionPoint, FindExistingLayer, FindLayerInsertPos,
 * FindWindowInsertPos, WinTInsertHere, WinTRemove, WinTValidate,
 * WinCalcUnivReg, WinMove, WinMoveHere, WinResize, WinNULLVisReg,
 * WinChangePriority, WinLocatePoint, WinScroll, WinSetMaskReg,
 * ShiftRegion).
 *
 * The children of a window are in order from the front: the first
 * child is on top.  Within its parent a window goes behind every
 * sibling of a smaller layer priority value, then within its layer
 * (W_layerID) behind those of a smaller window priority; on equal
 * values in front, or behind with WPF_PLACE_BEHIND
 * (WPF_PLACE_LAYER_BEHIND for a new layer).
 *
 * The regions (window.def):
 *   W_univReg  W_winReg in the parent's universe, without the regions
 *              of the siblings in front of it
 *   W_childReg the sum of the children's W_winReg
 *   W_visReg   W_univReg without W_childReg
 *
 * The asm validated only the windows a change could touch (the change
 * bounds) and kept the siblings' sum in the parent while it went
 * along.  The C code recalculates the whole tree of the window that
 * changed, from its root, with the same results: every window's new
 * visible region goes to WinCalcVisReg, which washes out and exposes
 * what became visible.
 *
 * Save-under areas are not kept: the video drivers in C refuse them
 * (DR_VID_SAVE_UNDER), so the window system never has one.
 */
#include "winInt.h"
#include "../Graphics/graphicsInt.h"

/* ---------------------------------------------------------------------
 * Inserting and removing
 * ------------------------------------------------------------------- */

static Window *L(WindowHandle wh)
{
    return MemLock(wh);
}

/* WinTLocateInsertionPoint: the sibling to go in front of, or 0 */
static WindowHandle LocateInsertionPoint(WindowHandle wh,
					 WindowHandle parent, word passFlags)
{
    Window *w = MemDeref(wh), *p = MemDeref(parent), *s;
    WindowHandle cur, first = 0, next;
    Handle layer = w->W_layerID;
    byte prio = w->W_priority, sp;

    /* FindExistingLayer */
    for (cur = p->W_firstChild; cur; cur = next) {
	s = L(cur);
	next = s->W_nextSibling;
	if (s->W_layerID == layer && cur != wh) {
	    MemUnlock(cur);
	    first = cur;
	    break;
	}
	MemUnlock(cur);
    }
    if (!first) {			/* FindLayerInsertPos */
	for (cur = p->W_firstChild; cur; cur = next) {
	    s = L(cur);
	    next = s->W_nextSibling;
	    sp = s->W_priority;
	    MemUnlock(cur);
	    if ((sp & WPD_LAYER) < (prio & WPD_LAYER))
		continue;
	    if ((sp & WPD_LAYER) > (prio & WPD_LAYER) ||
		!(passFlags & WPF_PLACE_LAYER_BEHIND))
		return cur;
	}
	return 0;
    }
    for (cur = first; cur; cur = next) {	/* FindWindowInsertPos */
	s = L(cur);
	next = s->W_nextSibling;
	sp = s->W_priority;
	if (s->W_layerID != layer) {
	    MemUnlock(cur);
	    return cur;
	}
	MemUnlock(cur);
	if ((sp & WPD_WIN) < (prio & WPD_WIN))
	    continue;
	if ((sp & WPD_WIN) > (prio & WPD_WIN) ||
	    !(passFlags & WPF_PLACE_BEHIND))
	    return cur;
    }
    return 0;
}

/* WinTInsert: wh and parent locked; prio 0 parts keep the window's */
void WinTInsert(WindowHandle wh, WindowHandle parent, byte prio,
		word passFlags)
{
    Window *w = MemDeref(wh), *p, *s;
    WindowHandle before, prev;

    if (!(prio & WPD_LAYER))
	prio |= (byte) (w->W_priority & WPD_LAYER);
    if (!(prio & WPD_WIN))
	prio |= (byte) (w->W_priority & WPD_WIN);
    w->W_priority = prio;
    before = LocateInsertionPoint(wh, parent, passFlags);
    p = MemDeref(parent);
    /* WinTInsertHere */
    if (before) {
	s = L(before);
	prev = s->W_prevSibling;
	s->W_prevSibling = wh;
	MemUnlock(before);
    } else {
	prev = p->W_lastChild;
	p->W_lastChild = wh;
    }
    if (prev) {
	s = L(prev);
	s->W_nextSibling = wh;
	MemUnlock(prev);
    } else
	p->W_firstChild = wh;
    w = MemDeref(wh);
    w->W_prevSibling = prev;
    w->W_nextSibling = before;
    w->W_parent = parent;
}

/* WinTRemove: out of its parent's children (the window's links stay) */
void WinTRemove(WindowHandle wh)
{
    Window *w = L(wh), *p, *s;
    WindowHandle prev = w->W_prevSibling, next = w->W_nextSibling,
		 parent = w->W_parent;

    MemUnlock(wh);
    if (!parent)
	return;
    p = L(parent);
    if (prev) {
	s = L(prev);
	s->W_nextSibling = next;
	MemUnlock(prev);
    } else
	p->W_firstChild = next;
    if (next) {
	s = L(next);
	s->W_prevSibling = prev;
	MemUnlock(next);
    } else
	p->W_lastChild = prev;
    MemUnlock(parent);
}

/* ---------------------------------------------------------------------
 * Validation
 * ------------------------------------------------------------------- */

/*
 * The branch at wh, its universe calculated: the children's universes
 * (the parent's W_temp2Reg is what of its universe the siblings so far
 * leave, W_childReg their sum), then the window's visible region.
 */
static void ValidateBranch(WindowHandle wh)
{
    Window *w = L(wh), *c;
    WindowHandle ch, next;

    WinRegCopy(WIN_REG(wh, w->W_univReg), WIN_REG(wh, w->W_temp2Reg));
    w = MemDeref(wh);
    WinRegNull(wh, w->W_childReg);
    w = MemDeref(wh);
    for (ch = w->W_firstChild; ch; ch = next) {
	c = L(ch);
	next = c->W_nextSibling;
	/* WinCalcUnivReg */
	WinRegAnd(WIN_REG(ch, c->W_winReg), WIN_REG(wh, w->W_temp2Reg),
		  WIN_REG(ch, c->W_univReg));
	c = MemDeref(ch);
	w = MemDeref(wh);
	WinRegSub(WIN_REG(wh, w->W_temp2Reg), WIN_REG(ch, c->W_winReg),
		  WIN_REG(wh, w->W_temp2Reg), WIN_REG(wh, w->W_temp1Reg));
	w = MemDeref(wh);
	c = MemDeref(ch);
	WinRegOr(WIN_REG(wh, w->W_childReg), WIN_REG(ch, c->W_winReg),
		 WIN_REG(wh, w->W_childReg));
	MemUnlock(ch);
	w = MemDeref(wh);
    }
    WinRegSmall(wh, w->W_temp2Reg);
    w = MemDeref(wh);
    WinRegSmall(wh, w->W_temp1Reg);
    w = MemDeref(wh);
    for (ch = w->W_firstChild; ch; ch = next) {
	c = L(ch);
	next = c->W_nextSibling;
	MemUnlock(ch);
	ValidateBranch(ch);
    }
    WinCalcVisReg(wh);
    MemUnlock(wh);
}

/* WinTValidate from the root of the tree */
void WinTValidateTree(WindowHandle wh)
{
    WindowHandle root = wh, up;
    Window *w;

    for (;;) {
	w = L(root);
	up = w->W_parent;
	MemUnlock(root);
	if (!up)
	    break;
	root = up;
    }
    w = L(root);
    WinRegCopy(WIN_REG(root, w->W_winReg), WIN_REG(root, w->W_univReg));
    MemUnlock(root);
    ValidateBranch(root);
}

/* WinNULLVisReg over a branch: all of it is exposed again */
void WinNullVisBranch(WindowHandle wh)
{
    Window *w = L(wh);
    WindowHandle ch, next;

    WinRegNull(wh, w->W_visReg);
    w = MemDeref(wh);
    w->W_grFlags &= (WinGrFlags) ~(WGF_MASK_VALID | WGF_XFORM_VALID);
    w->W_grRegFlags &= (WinGrRegFlags) ~(WGRF_PATH_VALID |
					 WGRF_WIN_PATH_VALID);
    for (ch = w->W_firstChild; ch; ch = next) {
	Window *c = L(ch);
	next = c->W_nextSibling;
	MemUnlock(ch);
	WinNullVisBranch(ch);
    }
    MemUnlock(wh);
}

static void SetPattPos(Window *w)
{
    w->W_ditherX = (word) w->W_winRect.R_left;
    w->W_ditherY = (word) w->W_winRect.R_top;
    w->W_pattPos = (word) (((w->W_winRect.R_top & 7) << 8) |
			   (w->W_winRect.R_left & 7));
}

/* WinMoveHere: the window and its children by dx, dy */
void WinMoveBranch(WindowHandle wh, sword dx, sword dy)
{
    Window *w = L(wh);
    WindowHandle ch, next;

    if (dx || dy) {
	GrMoveReg(WinReg(wh, w->W_winReg), dx, dy);
	WinCalcWinBounds(wh);
	w = MemDeref(wh);
	SetPattPos(w);
    }
    for (ch = w->W_firstChild; ch; ch = next) {
	Window *c = L(ch);
	next = c->W_nextSibling;
	MemUnlock(ch);
	WinMoveBranch(ch, dx, dy);
    }
    MemUnlock(wh);
}

/* ---------------------------------------------------------------------
 * Moving, resizing, priority
 * ------------------------------------------------------------------- */

/* WinChangeEnter: the tree lock and the window, unless it is closing */
static Window *ChangeEnter(Handle h, WindowHandle *wh)
{
    Window *w;

    WinTreeP();
    w = WinLockFromHandle(h, wh);
    if (w && (w->W_regFlags & WRF_CLOSED)) {
	MemUnlock(*wh);
	w = NULL;
    }
    if (!w)
	WinTreeV();
    return w;
}

void WinMove(WindowHandle win, sword xMove, sword yMove, WinPassFlags flags)
{
    WindowHandle wh;
    Window *w = ChangeEnter(win, &wh), *p;

    if (!w)
	return;
    w->W_curState = 0;
    if (flags & WPF_ABS) {		/* from the parent's origin */
	if (w->W_parent) {
	    p = L(w->W_parent);
	    xMove = (sword) (xMove + p->W_winRect.R_left);
	    yMove = (sword) (yMove + p->W_winRect.R_top);
	    MemUnlock(w->W_parent);
	}
	xMove = (sword) (xMove - w->W_winRect.R_left);
	yMove = (sword) (yMove - w->W_winRect.R_top);
    }
    MemUnlock(wh);
    WinMoveBranch(wh, xMove, yMove);
    WinNullVisBranch(wh);
    WinTValidateTree(wh);
    WinTreeV();
}

void WinResize(WindowHandle win, const Region *reg, word axParam,
	       word bxParam, word cxParam, word dxParam, WinPassFlags flags)
{
    WindowHandle wh;
    Window *w = ChangeEnter(win, &wh), *p;
    sword params[4], ox, oy, nx, ny;

    if (!w)
	return;
    w->W_curState = 0;
    ox = w->W_winRect.R_left;
    oy = w->W_winRect.R_top;
    nx = ox;
    ny = oy;
    if (flags & WPF_ABS) {		/* from the parent's origin */
	nx = ny = 0;
	if (w->W_parent) {
	    p = L(w->W_parent);
	    nx = p->W_winRect.R_left;
	    ny = p->W_winRect.R_top;
	    MemUnlock(w->W_parent);
	}
    }
    params[0] = (sword) axParam;
    params[1] = (sword) bxParam;
    params[2] = (sword) cxParam;
    params[3] = (sword) dxParam;
    WinRegSet(wh, w->W_winReg, reg, params);
    w = MemDeref(wh);
    GrMoveReg(WinReg(wh, w->W_winReg), nx, ny);
    WinCalcWinBounds(wh);
    w = MemDeref(wh);
    SetPattPos(w);
    {					/* the children: as the origin */
	WindowHandle ch, next;
	sword dx = (sword) (w->W_winRect.R_left - ox),
	      dy = (sword) (w->W_winRect.R_top - oy);
	for (ch = w->W_firstChild; ch; ch = next) {
	    Window *c = L(ch);
	    next = c->W_nextSibling;
	    MemUnlock(ch);
	    WinMoveBranch(ch, dx, dy);
	}
    }
    MemUnlock(wh);
    WinNullVisBranch(wh);
    WinTValidateTree(wh);
    WinTreeV();
}

/*
 * WinChangePriority: with WPF_LAYER the children of the window with
 * layer layerID get the layer priority of flags and go together to
 * their new place; else the window goes to its new place (and into
 * layer layerID, if not 0).
 */
void WinChangePriority(WindowHandle win, WinPassFlags flags, word layerID)
{
    WindowHandle wh, ch, next, moved[64], parent;
    Window *w = ChangeEnter(win, &wh), *c;
    word n = 0, i;
    byte prio = (byte) (flags & WPF_PRIORITY);

    if (!w)
	return;
    if (flags & WPF_LAYER) {
	for (ch = w->W_firstChild; ch; ch = next) {
	    c = L(ch);
	    next = c->W_nextSibling;
	    if (c->W_layerID == (Handle) layerID && n < 64) {
		if (prio & WPD_LAYER)
		    c->W_priority = (byte) ((c->W_priority & WPD_WIN) |
					    (prio & WPD_LAYER));
		moved[n++] = ch;
	    }
	    MemUnlock(ch);
	}
	MemUnlock(wh);
	for (i = 0; i < n; i++)
	    WinTRemove(moved[i]);
	/* the first by the layer rules, the rest after it in order */
	for (i = 0; i < n; i++) {
	    c = L(moved[i]);
	    w = L(wh);
	    if (i == 0)
		WinTInsert(moved[i], wh, c->W_priority,
			   (word) (flags & WPF_PLACE_LAYER_BEHIND));
	    else {
		Window *pv = L(moved[i - 1]);
		WindowHandle after = pv->W_nextSibling;
		c->W_prevSibling = moved[i - 1];
		c->W_nextSibling = after;
		c->W_parent = wh;
		pv->W_nextSibling = moved[i];
		if (after) {
		    Window *a = L(after);
		    a->W_prevSibling = moved[i];
		    MemUnlock(after);
		} else
		    w->W_lastChild = moved[i];
		MemUnlock(moved[i - 1]);
	    }
	    MemUnlock(wh);
	    MemUnlock(moved[i]);
	}
	if (n)
	    WinTValidateTree(wh);
	WinTreeV();
	return;
    }
    parent = w->W_parent;
    if (layerID)
	w->W_layerID = (Handle) layerID;
    w->W_curState = 0;
    MemUnlock(wh);
    if (parent) {
	WinTRemove(wh);
	w = L(wh);
	L(parent);
	WinTInsert(wh, parent, prio, (word) flags);
	MemUnlock(parent);
	MemUnlock(wh);
	WinTValidateTree(wh);
    }
    WinTreeV();
}

/* ---------------------------------------------------------------------
 * WinLocatePoint
 * ------------------------------------------------------------------- */

/*
 * The window of the tree at root whose W_winReg holds the screen point
 * x, y, the deepest and frontmost, or 0.  The C API has no
 * WinLocatePoint; the input manager and the tests use this one.
 */
WindowHandle WinLocatePointC(WindowHandle root, sword x, sword y)
{
    Window *w;
    WindowHandle ch, next, found = 0;
    Rectangle b;

    WinTreeP();
    w = L(root);
    if (!(w->W_regFlags & WRF_CLOSED) &&
	GrTestPointInReg(WinReg(root, w->W_winReg), (word) x, (word) y, &b)) {
	found = root;
	for (ch = w->W_firstChild; ch; ch = next) {
	    WindowHandle in;
	    Window *c = L(ch);
	    next = c->W_nextSibling;
	    MemUnlock(ch);
	    in = WinLocatePointC(ch, x, y);
	    if (in) {
		found = in;
		break;
	    }
	}
    }
    MemUnlock(root);
    WinTreeV();
    return found;
}

/* ---------------------------------------------------------------------
 * Scrolling
 * ------------------------------------------------------------------- */

/*
 * WinScroll: the document moved by xMove, yMove (document units) in the
 * window: the move rounded to whole pixels, applied to W_TMatrix as a
 * translation, and what is on the screen moved with it (BltCommon,
 * BLTM_MOVE: what comes into view becomes invalid).  An update going
 * on has its region moved too (ShiftRegion).  scrollAmt: the
 * translation applied, in document units (with no move at all, the
 * asm returned the move in device units; so does this).
 */
void WinScroll(WindowHandle win, WWFixedAsDWord xMove, WWFixedAsDWord yMove,
	       PointWWFixed *scrollAmt)
{
    WindowHandle wh;
    Window *w;
    WWFixedAsDWord tx = xMove, ty = yMove;
    sword sx, sy, dx, dy;

    WinTreeP();
    w = WinLockFromHandle(win, &wh);
    if (!w) {
	WinTreeV();
	return;
    }
    if (w->W_TMatrix.TM_flags & TM_SCALED) {
	tx = GrMulWWFixed(tx, *(const WWFixedAsDWord *) &w->W_TMatrix.TM_11);
	ty = GrMulWWFixed(ty, *(const WWFixedAsDWord *) &w->W_TMatrix.TM_22);
    }
    /* the source: the window's corner less the move, rounded */
    {
	sdword v = ((sdword) w->W_winRect.R_left << 16) - (sdword) tx;
	sx = (sword) ((v >> 16) + ((v >> 15) & 1));
	v = ((sdword) w->W_winRect.R_top << 16) - (sdword) ty;
	sy = (sword) ((v >> 16) + ((v >> 15) & 1));
    }
    dx = (sword) (w->W_winRect.R_left - sx);
    dy = (sword) (w->W_winRect.R_top - sy);
    if (dx || dy) {
	tx = (WWFixedAsDWord) ((sdword) dx << 16);
	ty = (WWFixedAsDWord) ((sdword) dy << 16);
	if (w->W_TMatrix.TM_flags & TM_SCALED) {
	    tx = GrSDivWWFixed(tx,
			       *(const WWFixedAsDWord *) &w->W_TMatrix.TM_11);
	    ty = GrSDivWWFixed(ty,
			       *(const WWFixedAsDWord *) &w->W_TMatrix.TM_22);
	}
	GrMatTranslate(&w->W_TMatrix, (sdword) tx, (sdword) ty);
	w->W_TMatrix.TM_flags &= (TransFlags) ~TF_INV_VALID;
	GrMatCalcInverse(&w->W_TMatrix);
	w->W_TMatrix.TM_flags |= TM_TRANSLATED;
	w->W_grFlags &= (WinGrFlags) ~WGF_XFORM_VALID;
	if (!(w->W_regFlags & WRF_CLOSED)) {
	    word width = (word) (w->W_winRect.R_right - w->W_winRect.R_left + 1);
	    word height = (word) (w->W_winRect.R_bottom - w->W_winRect.R_top +
				  1);
	    sword left = w->W_winRect.R_left, top = w->W_winRect.R_top;

	    /* ShiftRegion: the update region moved, within the visible */
	    GrMoveReg(WinReg(wh, w->W_updateReg), dx, dy);
	    WinRegAnd(WIN_REG(wh, w->W_updateReg), WIN_REG(wh, w->W_visReg),
		      WIN_REG(wh, w->W_updateReg));
	    w = MemDeref(wh);
	    /* WinSetMaskReg: the visible region less the invalid */
	    WinRegSub(WIN_REG(wh, w->W_visReg), WIN_REG(wh, w->W_invalReg),
		      WIN_REG(wh, w->W_maskReg), WIN_REG(wh, w->W_temp1Reg));
	    w = MemDeref(wh);
	    WinRegSmall(wh, w->W_temp1Reg);
	    w = MemDeref(wh);
	    WinSetupMaskFlags(w);
	    GrBltCommon(wh, sx, sy, left, top, width, height, BLTM_MOVE);
	    w = MemDeref(wh);
	    w->W_grFlags &= (WinGrFlags) ~WGF_MASK_VALID;
	    w->W_grRegFlags &= (WinGrRegFlags) ~(WGRF_PATH_VALID |
						 WGRF_WIN_PATH_VALID);
	}
    }
    if (scrollAmt) {
	*(WWFixedAsDWord *) &scrollAmt->PF_x = tx;
	*(WWFixedAsDWord *) &scrollAmt->PF_y = ty;
    }
    MemUnlock(wh);
    WinTreeV();
}
