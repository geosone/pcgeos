/*
 * printcomEntry.c -- the strategy routine of a printer driver (the C
 * port of printcomEntry.asm and printcomTables.asm: DriverStrategy,
 * PrintEscape, PrintSuspend, PrintUnsuspend and the jump tables).
 *
 * As in DOS16:
 *   - from DRE_SET_DEVICE on, bx is the PState's handle; it is locked
 *     around the call and PS_error is cleared first;
 *   - a function with the high bit set is an escape of the driver's
 *     table; an unknown one returns di 0 and no carry;
 *   - the carry is the routine's (the asm read PS_error into the carry
 *     and then popped the routine's flags over it).
 */
#include "printcom.h"

static Boolean PrintSuspend(DriverRegs *r)
{
    (void) r;
    return FALSE;
}

static Boolean PrintUnsuspend(DriverRegs *r)
{
    (void) r;
    return FALSE;
}

extern Boolean PrintTestDevice(DriverRegs *r);
extern Boolean PrintSetDevice(PState *ps, DriverRegs *r);
extern Boolean PrintGetDriverInfo(DriverRegs *r);
extern Boolean PrintDeviceInfo(PState *ps, DriverRegs *r);
extern Boolean PrintSetMode(PState *ps, DriverRegs *r);
extern Boolean PrintSetStream(PState *ps, DriverRegs *r);
extern Boolean PrintHomeCursor(PState *ps, DriverRegs *r);

/* modHanJumpTable/modOffJumpTable: DR_PRINT_GET_CURSOR on */
static PrintRoutine *const moduleTable[] = {
    PrintGetCursor,		/* DR_PRINT_GET_CURSOR */
    PrintSetCursor,		/* DR_PRINT_SET_CURSOR */
    PrintGetLineSpacing,	/* DR_PRINT_GET_LINE_SPACING */
    PrintSetLineSpacing,	/* DR_PRINT_SET_LINE_SPACING */
    PrintSetFont,		/* DR_PRINT_SET_FONT */
    PrintGetColorFormat,	/* DR_PRINT_GET_COLOR_FORMAT */
    PrintSetColor,		/* DR_PRINT_SET_COLOR */
    PrintGetStyles,		/* DR_PRINT_GET_STYLES */
    PrintSetStyles,		/* DR_PRINT_SET_STYLES */
    PrintTestStyles,		/* DR_PRINT_TEST_STYLES */
    PrintText,			/* DR_PRINT_TEXT */
    PrintRaw,			/* DR_PRINT_RAW */
    PrintStyleRun,		/* DR_PRINT_STYLE_RUN */
    PrintSwath,			/* DR_PRINT_SWATH */
    PrintStartPage,		/* DR_PRINT_START_PAGE */
    PrintEndPage,		/* DR_PRINT_END_PAGE */
    PrintGetPrintArea,		/* DR_PRINT_GET_PRINT_AREA */
    PrintGetMargins,		/* DR_PRINT_GET_MARGINS */
    PrintGetPaperPath,		/* DR_PRINT_GET_PAPER_PATH */
    PrintSetPaperPath,		/* DR_PRINT_SET_PAPER_PATH */
    PrintStartJob,		/* DR_PRINT_START_JOB */
    PrintEndJob,		/* DR_PRINT_END_JOB */
    PrintGetMainUI,		/* DR_PRINT_GET_MAIN_UI */
    PrintGetOptionsUI,		/* DR_PRINT_GET_OPTIONS_UI */
    PrintEvalUI,		/* DR_PRINT_EVAL_UI */
    PrintStuffUI		/* DR_PRINT_STUFF_UI */
};

#define NUM_MODULE_ROUTINES	(sizeof(moduleTable) / sizeof(moduleTable[0]))

/* PrintEscape: an unknown escape is di 0, no carry */
static Boolean PrintEscape(word function, PState *ps, DriverRegs *r)
{
    word i;

    for (i = 0; i < printNumEscapes; i++)
	if (printEscTable[i].PEE_code == function)
	    return printEscTable[i].PEE_routine(ps, r);
    r->DR_di = 0;
    return FALSE;
}

/* the resident routines: residentJumpTable */
static Boolean PrintResident(word function, PState *ps, DriverRegs *r)
{
    switch (function) {
    case DR_INIT:
	return PrintInit(r);
    case DR_EXIT:
	return PrintExit(r);
    case DR_SUSPEND:
	return PrintSuspend(r);
    case DR_UNSUSPEND:
	return PrintUnsuspend(r);
    case DRE_TEST_DEVICE:
	return PrintTestDevice(r);
    case DRE_SET_DEVICE:
	return PrintSetDevice(ps, r);
    case DR_PRINT_DRIVER_INFO:
	return PrintGetDriverInfo(r);
    case DR_PRINT_DEVICE_INFO:
	return PrintDeviceInfo(ps, r);
    case DR_PRINT_SET_MODE:
	return PrintSetMode(ps, r);
    case DR_PRINT_SET_STREAM:
	return PrintSetStream(ps, r);
    case DR_PRINT_HOME_CURSOR:
	return PrintHomeCursor(ps, r);
    }
    return TRUE;
}

Boolean PrintStrategy(word function, DriverRegs *r)
{
    MemHandle h = 0;
    PState *ps = NULL;
    Boolean carry;

    if (function >= DR_PRINT_FIRST_PSTATE_NEEDED) {
	h = (MemHandle) r->DR_bx;
	ps = MemLock(h);
	if (!ps)
	    return TRUE;
	ps->PS_error = 0;
    }
    if (function & 0x8000)
	carry = PrintEscape(function, ps, r);
    else if (function <= DR_PRINT_HOME_CURSOR)
	carry = PrintResident(function, ps, r);
    else if ((word) ((function - DR_PRINT_GET_CURSOR) / 2) <
	     NUM_MODULE_ROUTINES && !(function & 1))
	carry = moduleTable[(function - DR_PRINT_GET_CURSOR) / 2](ps, r);
    else
	carry = TRUE;
    if (h)
	MemUnlock(h);
    return carry;
}
