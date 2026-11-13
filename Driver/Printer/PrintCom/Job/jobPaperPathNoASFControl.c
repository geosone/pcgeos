/*
 * jobPaperPathNoASFControl.c -- PrintSetPaperPath (the C port of
 * jobPaperPathNoASFControl.asm): tractor or cut sheet feeder, nothing
 * sent; the margins of the path.
 */
#include "printcom.h"

Boolean PrSetPaperPath(PState *ps, PaperInputOptions in,
		       PaperOutputOptions out)
{
    const PrinterInfo *pi = ps->PS_deviceInfo;

    (void) out;
    ps->PS_paperInput = in;
    ps->PS_paperOutput = (PaperOutputOptions) (PS_REVERSE << POO_SORTED_OFFSET);
    ps->PS_currentMargins = (!(in & PIO_TRACTOR) && (in & PIO_ASF))
	? pi->PI_marginASF : pi->PI_marginTractor;
    return FALSE;
}

/* PrintSetPaperPath: al = PaperInputOptions, ah = PaperOutputOptions */
Boolean PrintSetPaperPath(PState *ps, DriverRegs *r)
{
    return PrSetPaperPath(ps, (PaperInputOptions) (r->DR_ax & 0xff),
			  (PaperOutputOptions) ((r->DR_ax >> 8) & 0xff));
}
