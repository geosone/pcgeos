/*
 * graphicsPrintSwathPCL4.c -- PrintSwath of the PCL 4 printers (the C port
 * of graphicsPrintSwathPCL4.asm): the resolution, then the swath as one
 * raster graphic (PrSendBitmap) or as the runs of its lines
 * (PrSendBitmapCompressed), by the printer's memory (PUID_amountMemory,
 * the mode's share of it) and whether its fonts are deleted
 * (PUID_initMemory).
 */
#include "printcom.h"
#include "printcomDriver.h"

extern Boolean PrSendBitmap(PState *ps);
extern Boolean PrSendBitmapCompressed(PState *ps);

/* by the memory (0..3) and PUID_initMemory + 1 (0: none deleted, 1: all,
 * 2: the temporary ones); TRUE: compressed */
static const byte printMethods[12] = {
    1, 1, 1, 0,
    1, 0, 0, 0,
    1, 1, 0, 0
};

/* PrintSwath: dx = the VM file, cx = the bitmap's block */
Boolean PrintSwath(PState *ps, DriverRegs *r)
{
    word left, mem;
    byte init;
    Boolean err;

    LoadSwathHeader(ps, (VMFileHandle) r->DR_dx, (VMBlockHandle) r->DR_cx);
    PrLoadPstateVars(ps, &left);
    ps->PS_curColorNumber = 0;
    if (WriteNumCommand(ps, pr_codes_SetGraphicRes,
			pr_graphic_Res_Values[ps->PS_mode / 2]))
	return TRUE;
    /* (((HI - mode) / 2) * 128 + memory + 128) / 256 - 1 */
    mem = (word) ((((PM_GRAPHICS_HI_RES - ps->PS_mode) >> 1) << 7) +
		  PS_UI_DATA(ps)->PUID_amountMemory + 0x80);
    mem = (word) ((mem >> 8) - 1);
    init = (byte) (PS_UI_DATA(ps)->PUID_initMemory + 1);
    if (mem < 4 && init < 3 && printMethods[mem + init * 4])
	err = PrSendBitmapCompressed(ps);
    else
	err = PrSendBitmap(ps);
    ReleaseScanlines(ps);
    return err;
}
