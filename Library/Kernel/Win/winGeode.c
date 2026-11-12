/*
 * winGeode.c -- the window variables of each geode (after winGeode.asm:
 * WinGeodeSetPtrImage, WinGeodeGetInputObj, WinGeodeSetInputObj,
 * WinGeodeGetParentObj, WinGeodeSetParentObj, WinGeodeGetFlags,
 * WinGeodeSetFlags, WinGeodeSetActiveWin, WinSysSetActiveGeode (in
 * winNotification.c), ReadGeodeWinVars, WriteGeodeWinVars).
 *
 * Each geode has a GeodeWinVars in its geode private data
 * (Geodes/geodesPriv.c): the object that gets its input, the parent
 * object (its field), its pointer image (PIL_GEODE), its flags and its
 * active window.  The words are allocated once, the first time they
 * are needed (the asm's WinInitSys).  Geode 0 is the current process.
 */
#include "winInt.h"

static word gwvOffset;
static KSem gwvSem = { 1, 0 };

#define GWV_WORDS	((word) ((sizeof(GeodeWinVars) + 1) / sizeof(word)))

typedef union {
    GeodeWinVars	v;
    word		w[GWV_WORDS];
} GWVBuf;

static word Offset(void)
{
    if (!gwvOffset) {
	KSemP(&gwvSem, KSEM_FOREVER);
	if (!gwvOffset)
	    gwvOffset = GeodePrivAlloc(0, GWV_WORDS);
	KSemV(&gwvSem);
    }
    return gwvOffset;
}

/* ReadGeodeWinVars */
void WinReadGeodeVars(GeodeHandle gh, GeodeWinVars *v)
{
    GWVBuf b;
    word off = Offset();

    memset(&b, 0, sizeof(b));
    if (off)
	GeodePrivRead(gh, off, GWV_WORDS, b.w);
    *v = b.v;
}

/* WriteGeodeWinVars */
void WinWriteGeodeVars(GeodeHandle gh, const GeodeWinVars *v)
{
    GWVBuf b;
    word off = Offset();

    memset(&b, 0, sizeof(b));
    b.v = *v;
    if (off)
	(void) GeodePrivWrite(gh, off, GWV_WORDS, b.w);
}

void WinGeodeSetPtrImage(GeodeHandle gh, optr ptrCh)
{
    GeodeWinVars v;
    Boolean changed = TRUE;

    if (!gh)
	gh = GeodeGetProcessHandle();
    WinPtrImageP();
    if (OptrToHandle(ptrCh) || OptrToChunk(ptrCh) != PIV_UPDATE) {
	WinReadGeodeVars(gh, &v);
	changed = (Boolean) (v.GWV_ptrImage != ptrCh);
	if (changed) {
	    v.GWV_ptrImage = ptrCh;
	    WinWriteGeodeVars(gh, &v);
	}
    }
    if (changed && gh == WinPtrFinalGeode())
	ImSetPtrImage(PIL_GEODE, ptrCh);
    WinPtrImageV();
}

optr WinGeodeGetInputObj(GeodeHandle gh)
{
    GeodeWinVars v;

    WinReadGeodeVars(gh, &v);
    return v.GWV_inputObj;
}

optr WinGeodeGetParentObj(GeodeHandle gh)
{
    GeodeWinVars v;

    WinReadGeodeVars(gh, &v);
    return v.GWV_parentObj;
}

word WinGeodeGetFlags(GeodeHandle gh)
{
    GeodeWinVars v;

    WinReadGeodeVars(gh, &v);
    return v.GWV_flags;
}

void WinGeodeSetInputObj(GeodeHandle gh, optr iobj)
{
    GeodeWinVars v;

    WinPtrImageP();
    WinReadGeodeVars(gh, &v);
    v.GWV_inputObj = iobj;
    WinWriteGeodeVars(gh, &v);
    WinPtrImageV();
}

void WinGeodeSetParentObj(GeodeHandle gh, optr pObj)
{
    GeodeWinVars v;

    WinPtrImageP();
    WinReadGeodeVars(gh, &v);
    v.GWV_parentObj = pObj;
    WinWriteGeodeVars(gh, &v);
    WinPtrImageV();
}

void WinGeodeSetFlags(GeodeHandle gh, word flags)
{
    GeodeWinVars v;

    WinPtrImageP();
    WinReadGeodeVars(gh, &v);
    v.GWV_flags = flags;
    WinWriteGeodeVars(gh, &v);
    WinPtrImageV();
}

/* the window whose pointer images show while the pointer is over the
 * geode's windows (the window the mouse is grabbed for), or 0 */
void WinGeodeSetActiveWin(GeodeHandle gh, WindowHandle win)
{
    GeodeWinVars v;

    if (!gh)
	gh = GeodeGetProcessHandle();
    WinTreeP();
    WinPtrImageP();
    WinReadGeodeVars(gh, &v);
    if (v.GWV_activeWin != win) {
	v.GWV_activeWin = win;
	WinWriteGeodeVars(gh, &v);
	if (win != WinPtrFinalWin())
	    WinUpdatePtrImages();
    }
    WinPtrImageV();
    WinTreeV();
}
