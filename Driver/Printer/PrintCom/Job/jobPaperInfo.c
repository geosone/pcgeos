/*
 * jobPaperInfo.c -- PrintGetPaperPath, PrintGetMargins and
 * PrintGetPrintArea (the C port of jobPaperInfo.asm).
 */
#include "printcom.h"

/* PrintGetPaperPath: al = PI_paperInput, ah = PI_paperOutput */
Boolean PrintGetPaperPath(PState *ps, DriverRegs *r)
{
    r->DR_ax = (word) (ps->PS_deviceInfo->PI_paperInput |
		       (ps->PS_deviceInfo->PI_paperOutput << 8));
    return FALSE;
}

/* the margins of the job's paper path (tractor, else cut sheet) */
const PrinterMargins *PrJobMargins(PState *ps)
{
    return (PS_UI_DATA(ps)->PUID_paperInput & PIO_TRACTOR)
	? &ps->PS_deviceInfo->PI_marginTractor
	: &ps->PS_deviceInfo->PI_marginASF;
}

/* PrintGetMargins: ax, si, cx, dx = left, top, right, bottom */
Boolean PrintGetMargins(PState *ps, DriverRegs *r)
{
    const PrinterMargins *m = PrJobMargins(ps);

    r->DR_ax = m->PM_left;
    r->DR_si = m->PM_top;
    r->DR_cx = m->PM_right;
    r->DR_dx = m->PM_bottom;
    return FALSE;
}

/* PrintGetPrintArea: the printable area of the paper (PS_customWidth,
 * at most PI_paperWidth, by PS_customHeight) inside the margins */
Boolean PrintGetPrintArea(PState *ps, DriverRegs *r)
{
    const PrinterMargins *m = PrJobMargins(ps);
    word w = ps->PS_customWidth;

    if (w > ps->PS_deviceInfo->PI_paperWidth)
	w = ps->PS_deviceInfo->PI_paperWidth;
    r->DR_ax = m->PM_left;
    r->DR_si = m->PM_top;
    r->DR_cx = (word) (w - m->PM_right);
    r->DR_dx = (word) (ps->PS_customHeight - m->PM_bottom);
    return FALSE;
}
