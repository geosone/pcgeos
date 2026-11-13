/*
 * jobPaperCapsl.c -- PrintSetPaperPath (the C port of jobPaperCapsl.asm):
 * the paper input (manual or a tray) as CSI n q, n the
 * PaperInputOptions byte as it came, the margins of the sheet feeder.
 */
#include "printcom.h"
#include "printcomDriver.h"

/* the PaperInputOptions the asm stored by the index it was given */
static const byte inputConversionTab[5] = {
    ASF_TRAY1 << PIO_ASF_OFFSET, MF_MANUAL1 << PIO_MANUAL_OFFSET,
    ASF_TRAY1 << PIO_ASF_OFFSET, ASF_TRAY2 << PIO_ASF_OFFSET,
    ASF_TRAY3 << PIO_ASF_OFFSET
};

Boolean PrSetPaperPath(PState *ps, PaperInputOptions in,
		       PaperOutputOptions out)
{
    /* xlatb: the asm read past the table for an index above 4 */
    ps->PS_paperInput = in < 5 ? inputConversionTab[in] : in;
    ps->PS_paperOutput = out;
    if (SendCodeOut(ps, pr_codes_CSIcode) ||
	HexToAsciiStreamWrite(ps, (word) in) ||
	PrintStreamWriteByte(ps, 'q'))
	return TRUE;
    ps->PS_currentMargins = ps->PS_deviceInfo->PI_marginASF;
    return FALSE;
}

/* PrintSetPaperPath: al = PaperInputOptions, ah = PaperOutputOptions */
Boolean PrintSetPaperPath(PState *ps, DriverRegs *r)
{
    return PrSetPaperPath(ps, (PaperInputOptions) (r->DR_ax & 0xff),
			  (PaperOutputOptions) ((r->DR_ax >> 8) & 0xff));
}
