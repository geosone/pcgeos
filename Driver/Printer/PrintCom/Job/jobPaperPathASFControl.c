/*
 * jobPaperPathASFControl.c -- PrintSetPaperPath (the C port of
 * jobPaperPathASFControl.asm): tractor or cut sheet feeder, and the
 * margins of the path.
 */
#include "printcom.h"
#include "printcomDriver.h"

Boolean PrSetPaperPath(PState *ps, PaperInputOptions in,
		       PaperOutputOptions out)
{
    const PrinterInfo *pi = ps->PS_deviceInfo;
    const PrinterMargins *m;

    (void) out;
    ps->PS_paperInput = in;
    ps->PS_paperOutput = (PaperOutputOptions) (PS_REVERSE << POO_SORTED_OFFSET);
    if (!(in & PIO_TRACTOR) && (in & PIO_ASF)) {
	/* Port32: the bin only after a code (none for the text only
	 * drivers; the asm sent the bare 1 or 2) */
	if (SendCodeOut(ps, pr_codes_EnableASF) ||
	    SendCodeOut(ps, pr_codes_ASFControl) ||
	    (pr_codes_ASFControl[0] &&
	     PrintStreamWriteByte(ps, (byte) ((in & PIO_ASF) ==
					      (ASF_TRAY1 << PIO_ASF_OFFSET) ?
					      1 : 2))))
	    return TRUE;
	m = &pi->PI_marginASF;
    } else {
	if (SendCodeOut(ps, pr_codes_DisableASF))
	    return TRUE;
	m = &pi->PI_marginTractor;
    }
    ps->PS_currentMargins = *m;
    return FALSE;
}

/* PrintSetPaperPath: al = PaperInputOptions, ah = PaperOutputOptions */
Boolean PrintSetPaperPath(PState *ps, DriverRegs *r)
{
    return PrSetPaperPath(ps, (PaperInputOptions) (r->DR_ax & 0xff),
			  (PaperOutputOptions) ((r->DR_ax >> 8) & 0xff));
}
