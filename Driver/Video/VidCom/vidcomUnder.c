/*
 * vidcomUnder.c -- save under areas (after vidcomUnder.asm:
 * VidCheckUnder, VidInfoUnder, VidRequestUnder, VidSaveUnder,
 * VidRestoreUnder, VidNukeUnder, RemoveSaveUnder, FindSaveUnder,
 * SaveScreen, RestoreScreen; vidcomUtils.asm: CheckSaveUnderCollision).
 *
 * A window opened with WPF_SAVE_UNDER (a menu, say) gets the screen
 * under it saved, so that closing it can put the pixels back instead
 * of having the windows below redraw.  Up to eight areas, each a bit of
 * WinUnderFlags; the pixels are kept in locked blocks of the heap
 * owned by the driver, up to SU_BLOCKS of whole rows of at most
 * SU_BLOCK_SIZE bytes (the asm used one block or video memory), so a new area never needs
 * another one removed unless all eight bits are in use
 * (DR_VID_REQUEST_UNDER then names the oldest).
 *
 * The window system marks the windows below an area with its bit in
 * W_savedUnderMask.  Drawing to such a window that touches the area
 * clears the bit there (the asm's collision called
 * WinMaskOutSaveUnder): the saved pixels no longer show what the window
 * would show, and the window system exposes it again when the area
 * closes (Library/Kernel/Win/winCreDest.c).
 *
 * DR_VID_RESTORE_UNDER in C takes the region to restore (screen
 * coordinates, what of the closing window was visible) at ds:si, the
 * asm used the window's W_maskReg; NULL restores the whole area.
 */
#include "vidcom.h"
#include <heap.h>
#include <Internal/grWinInt.h>

#define SU_COUNT	8
#define SU_BLOCKS	64
#define SU_BLOCK_SIZE	60000

typedef struct {
    WinUnderFlags	SU_flags;	/* 0: free */
    Handle		SU_window;
    Handle		SU_parent;
    Rectangle		SU_rect;	/* screen, inclusive */
    word		SU_rowsPer;	/* rows in a block */
    MemHandle		SU_blocks[SU_BLOCKS];	/* locked */
    dword		*SU_rows[SU_BLOCKS];	/* 0x00RRGGBB */
} SaveUnder;

static SaveUnder suTable[SU_COUNT];	/* in the order they were made */
static word suCount;

/* VidCheckUnder: the areas the rectangle touches */
static WinUnderFlags Check(sword l, sword t, sword r, sword b)
{
    WinUnderFlags f = 0;
    word i;

    for (i = 0; i < suCount; i++)
	if (l <= suTable[i].SU_rect.R_right &&
	    r >= suTable[i].SU_rect.R_left &&
	    t <= suTable[i].SU_rect.R_bottom &&
	    b >= suTable[i].SU_rect.R_top)
	    f |= suTable[i].SU_flags;
    return f;
}

/* FindSaveUnder: the area of a window */
static SaveUnder *Find(const Window *w)
{
    word i;

    for (i = 0; i < suCount; i++)
	if (suTable[i].SU_window == w->W_header.LMBH_handle)
	    return &suTable[i];
    return NULL;
}

/* RemoveSaveUnder */
static void Remove(SaveUnder *s)
{
    word i, at = (word) (s - suTable);

    for (i = 0; i < SU_BLOCKS && s->SU_blocks[i]; i++)
	MemFree(s->SU_blocks[i]);
    memmove(&suTable[at], &suTable[at + 1],
	    (size_t) (suCount - at - 1) * sizeof(SaveUnder));
    suCount--;
    memset(&suTable[suCount], 0, sizeof(SaveUnder));
}

static sword Clamp(sword v, word size)
{
    if (v < 0)
	return 0;
    return v >= (sword) size ? (sword) (size - 1) : v;
}

/* the saved pixel row y (screen) */
static dword *Row(const SaveUnder *s, sword y)
{
    word n = (word) (y - s->SU_rect.R_top);
    word w = (word) (s->SU_rect.R_right - s->SU_rect.R_left + 1);

    return s->SU_rows[n / s->SU_rowsPer] + (n % s->SU_rowsPer) * w;
}

/* VidSaveUnder: carry if not possible; ax the area's bit */
static Boolean Save(DriverRegs *r)
{
    const Window *w = r->DR_esDi;
    SaveUnder *s;
    WinUnderFlags used = 0, f;
    sword l, t, rt, b, x, y;
    word i, rowBytes, rows, n;

    if (suCount == SU_COUNT)
	return TRUE;
    if ((word) r->DR_ax == 0x8000) {
	l = w->W_winRect.R_left;
	t = w->W_winRect.R_top;
	rt = w->W_winRect.R_right;
	b = w->W_winRect.R_bottom;
    } else {
	l = (sword) r->DR_ax;
	t = (sword) r->DR_bx;
	rt = (sword) r->DR_cx;
	b = (sword) r->DR_dx;
    }
    l = Clamp(l, DriverTable.VDI_pageW);
    rt = Clamp(rt, DriverTable.VDI_pageW);
    t = Clamp(t, DriverTable.VDI_pageH);
    b = Clamp(b, DriverTable.VDI_pageH);
    if (l > rt || t > b ||
	(dword) (rt - l + 1) * sizeof(dword) > SU_BLOCK_SIZE)
	return TRUE;
    rowBytes = (word) ((rt - l + 1) * sizeof(dword));
    s = &suTable[suCount];
    memset(s, 0, sizeof(*s));
    s->SU_rowsPer = (word) (SU_BLOCK_SIZE / rowBytes);
    rows = (word) (b - t + 1);
    for (i = 0, n = 0; n < rows; i++, n = (word) (n + s->SU_rowsPer)) {
	word k = (word) (rows - n < s->SU_rowsPer ? rows - n : s->SU_rowsPer);
	if (i == SU_BLOCKS ||
	    !(s->SU_blocks[i] = MemAlloc((word) (k * rowBytes),
					 HF_SWAPABLE | HF_SHARABLE,
					 HAF_LOCK))) {
	    word j;
	    for (j = 0; j < i; j++)
		MemFree(s->SU_blocks[j]);
	    memset(s, 0, sizeof(*s));
	    return TRUE;
	}
	HandleModifyOwner(s->SU_blocks[i], GeodeGetCodeProcessHandle());
	s->SU_rows[i] = MemDeref(s->SU_blocks[i]);
    }
    for (i = 0; i < suCount; i++)
	used |= suTable[i].SU_flags;
    for (f = 1; used & f; f = (WinUnderFlags) (f << 1))
	;
    suCount++;
    s->SU_flags = f;
    s->SU_window = w->W_header.LMBH_handle;
    s->SU_parent = w->W_parent;
    s->SU_rect.R_left = l;
    s->SU_rect.R_top = t;
    s->SU_rect.R_right = rt;
    s->SU_rect.R_bottom = b;
    /* SaveScreen: not the pointer, not the XOR region */
    VidDevCondHidePtr(l, t, rt, b);
    VidCheckXORCollision(l, t, rt, b);
    for (y = t; y <= b; y++) {
	dword *p = Row(s, y);
	for (x = l; x <= rt; x++)
	    *p++ = VidDevGetPixel(x, y);
    }
    VidShowXOR();
    VidDevCondShowPtr();
    r->DR_ax = f;
    return FALSE;
}

/* the saved pixels from l to rt on line y */
static void RestoreSpan(const SaveUnder *s, sword y, sword l, sword rt)
{
    const dword *p;
    sword x;

    if (y < s->SU_rect.R_top || y > s->SU_rect.R_bottom)
	return;
    if (l < s->SU_rect.R_left)
	l = s->SU_rect.R_left;
    if (rt > s->SU_rect.R_right)
	rt = s->SU_rect.R_right;
    p = Row(s, y) + (l - s->SU_rect.R_left);
    for (x = l; x <= rt; x++)
	VidDevPutPixel(x, y, *p++, MM_COPY);
}

/* RestoreScreen: within the region (NULL: all of it) */
static void Restore(const SaveUnder *s, const Region *reg)
{
    const Rectangle *b = &s->SU_rect;
    sword y;

    VidDevCondHidePtr(b->R_left, b->R_top, b->R_right, b->R_bottom);
    VidCheckXORCollision(b->R_left, b->R_top, b->R_right, b->R_bottom);
    if (!reg) {
	for (y = b->R_top; y <= b->R_bottom; y++)
	    RestoreSpan(s, y, b->R_left, b->R_right);
    } else {
	/* the rows of the region: the bottom of each, its ons and
	 * offs, EOREGREC */
	const sword *p = (const sword *) reg;
	sword top = (sword) 0x8000;
	while ((word) *p != EOREGREC) {
	    sword bottom = *p++;
	    const sword *row = p;
	    for (y = top < b->R_top ? b->R_top : top;
		 y <= bottom && y <= b->R_bottom; y++)
		for (p = row; (word) *p != EOREGREC; p += 2)
		    RestoreSpan(s, y, p[0], p[1]);
	    for (p = row; (word) *p != EOREGREC; p += 2)
		;
	    p++;
	    top = (sword) (bottom + 1);
	}
    }
    VidShowXOR();
    VidDevCondShowPtr();
}

/* the functions of save under areas (DR_VID_SAVE_UNDER ...
 * DR_VID_INFO_UNDER); carry */
Boolean VidUnder(word function, DriverRegs *r)
{
    SaveUnder *s;
    word i;

    switch (function) {
    case DR_VID_SAVE_UNDER:
	return Save(r);
    case DR_VID_RESTORE_UNDER:
	if ((s = Find(r->DR_esDi)) != NULL) {
	    Restore(s, r->DR_dsSi);
	    Remove(s);
	}
	return FALSE;
    case DR_VID_NUKE_UNDER:
	if ((s = Find(r->DR_esDi)) != NULL)
	    Remove(s);
	return FALSE;
    case DR_VID_REQUEST_UNDER:		/* all bits used: the oldest */
	if (suCount < SU_COUNT)
	    return FALSE;
	r->DR_ax = suTable[0].SU_flags;
	r->DR_bx = suTable[0].SU_window;
	return TRUE;
    case DR_VID_CHECK_UNDER:
	r->DR_ax = Check((sword) r->DR_ax, (sword) r->DR_bx,
			 (sword) r->DR_cx, (sword) r->DR_dx);
	return FALSE;
    case DR_VID_INFO_UNDER:
	for (i = 0; i < suCount; i++)
	    if (suTable[i].SU_flags & (WinUnderFlags) r->DR_ax) {
		r->DR_ax = (word) suTable[i].SU_rect.R_left;
		r->DR_bx = (word) suTable[i].SU_rect.R_top;
		r->DR_cx = (word) suTable[i].SU_rect.R_right;
		r->DR_dx = (word) suTable[i].SU_rect.R_bottom;
		r->DR_di = suTable[i].SU_window;
		return FALSE;
	    }
	r->DR_ax = r->DR_bx = r->DR_cx = r->DR_dx = r->DR_di = 0;
	return FALSE;
    }
    return FALSE;
}

/* the areas over a point (DR_VID_MOVEPTR returns them for WinMovePtr) */
WinUnderFlags VidUnderAt(sword x, sword y)
{
    return Check(x, y, x, y);
}

/* CheckSaveUnderCollision: drawing to window w in b (NULL: anywhere)
 * touches the areas whose bits are in its W_savedUnderMask: those bits
 * cleared */
void VidUnderCollision(Window *w, const Rectangle *b)
{
    word i;

    if (!w || !w->W_savedUnderMask)
	return;
    for (i = 0; i < suCount; i++) {
	const Rectangle *u = &suTable[i].SU_rect;
	if ((w->W_savedUnderMask & suTable[i].SU_flags) &&
	    (!b || (b->R_left <= u->R_right && b->R_right >= u->R_left &&
		    b->R_top <= u->R_bottom && b->R_bottom >= u->R_top)))
	    w->W_savedUnderMask &= (WinUnderFlags) ~suTable[i].SU_flags;
    }
}

/* DR_EXIT */
void VidUnderReset(void)
{
    while (suCount)
	Remove(&suTable[suCount - 1]);
}
