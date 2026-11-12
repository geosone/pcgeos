/*
 * winState.c -- the state of a window (after winState.asm: WinSetInfo,
 * WinGetInfo, WinGetWinScreenBounds).
 *
 * WinGetInfo writes at data (the C stub's layout, sizes of Port32):
 *   WIT_PRIVATE_DATA  WinPrivateData
 *   WIT_COLOR         4 bytes: red/index, WinColorFlags, blue, green
 *   WIT_INPUT_OBJ, WIT_EXPOSURE_OBJ  optr
 *   WIT_STRATEGY      the strategy routine (a pointer)
 *   WIT_FLAGS         word: WinRegFlags, WinPtrFlags << 8
 *   WIT_LAYER_ID, WIT_PARENT_WIN, WIT_FIRST_CHILD_WIN,
 *   WIT_LAST_CHILD_WIN, WIT_PREV_SIBLING_WIN, WIT_NEXT_SIBLING_WIN
 *                     Handle
 *   WIT_PRIORITY      word (WinPriorityData)
 * WinSetInfo takes (win.h): WIT_PRIVATE_DATA a pointer to the
 * WinPrivateData, WIT_COLOR red | flags << 8 | green << 16 | blue << 24,
 * WIT_INPUT_OBJ and WIT_EXPOSURE_OBJ the optr, WIT_STRATEGY the
 * routine; the other types do nothing.  The asm flushed the old
 * object's queues when an optr changed; not yet (winCreDest.c).
 */
#include "winInt.h"
#include <stdint.h>

void WinSetInfo(WindowHandle win, WinInfoType type, optr data)
{
    WindowHandle wh;
    Window *w;

    WinTreeP();
    w = WinLockFromHandle(win, &wh);
    if (!w) {
	WinTreeV();
	return;
    }
    switch (type) {
    case WIT_PRIVATE_DATA:
	if (data)
	    w->W_privData = *(const WinPrivateData *) (uintptr_t) data;
	break;
    case WIT_COLOR:
	w->W_colorRGB.RGB_red = (byte) data;
	w->W_color = (WinColorFlags) (data >> 8);
	w->W_colorRGB.RGB_green = (byte) (data >> 16);
	w->W_colorRGB.RGB_blue = (byte) (data >> 24);
	break;
    case WIT_INPUT_OBJ:
	w->W_inputObj = data;
	break;
    case WIT_EXPOSURE_OBJ:
	w->W_exposureObj = data;
	break;
    case WIT_STRATEGY:
	w->W_driverStrategy = (void *) (uintptr_t) data;
	break;
    default:
	break;
    }
    MemUnlock(wh);
    WinTreeV();
}

void WinGetInfo(WindowHandle win, WinInfoType type, void *data)
{
    WindowHandle wh;
    Window *w;
    byte *b = data;
    word v;

    WinTreeP();
    w = WinLockFromHandle(win, &wh);
    if (!w) {
	WinTreeV();
	return;
    }
    switch (type) {
    case WIT_PRIVATE_DATA:
	*(WinPrivateData *) data = w->W_privData;
	break;
    case WIT_COLOR:
	b[0] = w->W_colorRGB.RGB_red;
	b[1] = w->W_color;
	b[2] = w->W_colorRGB.RGB_blue;
	b[3] = w->W_colorRGB.RGB_green;
	break;
    case WIT_INPUT_OBJ:
	memcpy(data, &w->W_inputObj, sizeof(optr));
	break;
    case WIT_EXPOSURE_OBJ:
	memcpy(data, &w->W_exposureObj, sizeof(optr));
	break;
    case WIT_STRATEGY:
	memcpy(data, &w->W_driverStrategy, sizeof(void *));
	break;
    case WIT_FLAGS:
	v = (word) (w->W_regFlags | (w->W_ptrFlags << 8));
	memcpy(data, &v, sizeof(v));
	break;
    case WIT_LAYER_ID:
	memcpy(data, &w->W_layerID, sizeof(Handle));
	break;
    case WIT_PARENT_WIN:
	memcpy(data, &w->W_parent, sizeof(Handle));
	break;
    case WIT_FIRST_CHILD_WIN:
	memcpy(data, &w->W_firstChild, sizeof(Handle));
	break;
    case WIT_LAST_CHILD_WIN:
	memcpy(data, &w->W_lastChild, sizeof(Handle));
	break;
    case WIT_PREV_SIBLING_WIN:
	memcpy(data, &w->W_prevSibling, sizeof(Handle));
	break;
    case WIT_NEXT_SIBLING_WIN:
	memcpy(data, &w->W_nextSibling, sizeof(Handle));
	break;
    case WIT_PRIORITY:
	v = w->W_priority;
	memcpy(data, &v, sizeof(v));
	break;
    }
    MemUnlock(wh);
    WinTreeV();
}

/* the whole coordinate space for a GState without a window */
void WinGetWinScreenBounds(WindowHandle win, Rectangle *bounds)
{
    WindowHandle wh;
    Window *w;

    WinTreeP();
    w = WinLockFromHandle(win, &wh);
    if (!w) {
	bounds->R_left = bounds->R_top = (sword) -0x4000;
	bounds->R_right = bounds->R_bottom = 0x4000;
    } else {
	*bounds = w->W_winRect;
	MemUnlock(wh);
    }
    WinTreeV();
}
