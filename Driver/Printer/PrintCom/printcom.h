/*
 * printcom.h -- the common part of the printer drivers in C (the C form
 * of printcomConstant.def, printcomMacro.def and the routines of
 * the PrintCom asm files that a driver includes).
 *
 * A printer driver in C is the common files it needs (beside their asm
 * in PrintCom/) plus its own.  As in DOS16, the common code calls the
 * driver's routines by name:
 *
 *   PrintInit, PrintExit, PrintInitStream     its admin routines
 *   printDevices[], printNumDevices           its devices (DefPrinter)
 *   printDriverInfo                           its PrintDriverInfo
 *   printEscTable[], printNumEscapes          its escapes
 *   the DR_PRINT_* module routines            (printcomEntry.c)
 *
 * Port32 ABI (Port32/ABI.md section 7.6): a routine gets the locked
 * PState (bp in the asm) and the registers; it returns the carry.  The
 * device info is the driver's own data, so DR_PRINT_DEVICE_INFO and
 * DR_PRINT_DRIVER_INFO return pointers in DR_dsSi (dx 0), and
 * PS_deviceInfo is a pointer.
 */
#ifndef PRINTCOM_H
#define PRINTCOM_H

#include <geos.h>
#include <file.h>
#include <geode.h>
#include <library.h>
#include <resource.h>
#include <heap.h>
#include <lmem.h>
#include <chunkarr.h>
#include <graphics.h>
#include <gstring.h>
#include <font.h>
#include <print.h>
#include <driver.h>
#include <geosdriver.h>
#include <streamC.h>
#include <Internal/streamDr.h>
#include <Internal/printDr.h>

/* a module routine of the jump tables */
typedef Boolean PrintRoutine(PState *ps, DriverRegs *r);

/* a device: its name (the DEIT_nameTable string) and its info */
typedef struct {
    const char		*PDE_name;
    const PrinterInfo	*PDE_info;
} PrintDeviceEntry;

/* an escape: its code and its routine */
typedef struct {
    word		PEE_code;
    PrintRoutine	*PEE_routine;
} PrintEscEntry;

/* the driver's own data */
extern const PrintDeviceEntry	printDevices[];
extern const word		printNumDevices;
extern const PrintDriverInfo	printDriverInfo;
extern const PrintEscEntry	printEscTable[];
extern const word		printNumEscapes;

/* the strategy routine (printcomEntry.c) */
extern Boolean PrintStrategy(word function, DriverRegs *r);

/* the driver's admin routines */
extern Boolean PrintInit(DriverRegs *r);
extern Boolean PrintExit(DriverRegs *r);
extern Boolean PrintInitStream(PState *ps);

/* the routines of the jump tables (printcomTables.asm) */
extern PrintRoutine PrintGetCursor, PrintSetCursor, PrintGetLineSpacing,
    PrintSetLineSpacing, PrintSetFont, PrintGetColorFormat, PrintSetColor,
    PrintGetStyles, PrintSetStyles, PrintTestStyles, PrintText, PrintRaw,
    PrintStyleRun, PrintSwath, PrintStartPage, PrintEndPage,
    PrintGetPrintArea, PrintGetMargins, PrintGetPaperPath,
    PrintSetPaperPath, PrintStartJob, PrintEndJob, PrintGetMainUI,
    PrintGetOptionsUI, PrintEvalUI, PrintStuffUI;

/* printcomNoStyles.c */
extern Boolean PrintClearStyles(PState *ps);

/* printcomStream.c: TRUE (carry) on an error, PS_error set */
extern Boolean PrintStreamWrite(PState *ps, const void *buf, word size);
extern Boolean PrintStreamWriteByte(PState *ps, byte b);
/* a control code: a count byte, then the bytes */
extern Boolean SendCodeOut(PState *ps, const byte *code);

/* the PrintDriverUIData in JP_printerData */
#define PS_UI_DATA(ps)	((PrintDriverUIData *) PS_JOB_PARAMS(ps)->JP_printerData)

#endif /* PRINTCOM_H */
