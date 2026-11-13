/*
 * jobPaperPCL.c -- the paper path of the PCL drivers (the C port of
 * jobPaperPCL.asm without PCL4MODE): the input path (manual 2, the
 * trays 1, 1, 4, 6), the margins of cut sheets.
 */
#include "printcom.h"
#include "printcomDriver.h"

static const byte asfTable[4] = { 1, 1, 4, 6 };

Boolean PrSetPaperPath(PState *ps, PaperInputOptions in,
		       PaperOutputOptions out)
{
    byte path;

    ps->PS_paperInput = in;
    ps->PS_paperOutput = out;
    if (in & PIO_MANUAL)
	path = 2;
    else
	path = asfTable[(in >> PIO_ASF_OFFSET) & ASF_TRAY3];
    if (WriteNumCommand(ps, pr_codes_SetInputPath, path))
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
