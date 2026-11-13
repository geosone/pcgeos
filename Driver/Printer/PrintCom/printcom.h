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
#include <char.h>
#include <print.h>
#include <driver.h>
#include <geosdriver.h>
#include <streamC.h>
#include <Internal/streamDr.h>
#include <Internal/printDr.h>
#include <Internal/spoolDr.h>

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

/* a custom routine of a job (PI_customEntry, PI_customExit: indices into
 * printCustomRoutines, 0 none; the device tables declare it) */
typedef Boolean PrintCustomRoutine(PState *ps);
/* a custom entry of jobStartPCL.c that went on at InitPState, past the
 * reset (customPaintJetXL300PCL.c), returns this: no error */
#define PR_CUSTOM_AT_INIT_PSTATE	2
extern PrintCustomRoutine *const printCustomRoutines[];

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

/* ---------------------------------------------------------------------
 * The raster (dot matrix) drivers: the PrintCom routines they share,
 * called by name as in the asm.  A driver of these gives its constants
 * and control codes in printcomDriver.h (beside it), which the PrintCom
 * files include, as the asm included the driver's Constant.def.
 * ------------------------------------------------------------------- */

/* the output buffer at the start of the print buffer (GPB_outputBuffer),
 * the band buffer after it (GPB_bandBuffer) */
#define PRINT_OUTPUT_BUFFER_SIZE	96
#define PS_OUTPUT_BUFFER(ps)	((byte *) (ps)->PS_bufSeg)
#define PS_BAND_BUFFER(ps)	((byte *) (ps)->PS_bufSeg + \
				 PRINT_OUTPUT_BUFFER_SIZE)

/* the driver's tables (Port32/tools/dmdevinfo2c.py): the font entries'
 * control codes by FE_command, the printerFontInfo resource (PI_draft and
 * PI_nlq are offsets into it), the tables of the ISO substitutions and
 * translations */
extern const byte *const printFontCommands[];
extern const byte *const printFontInfoBase;
extern const SpoolTransTables printTransTables;

/* Cursor/ */
extern word PrConvertToDriverCoordinates(WWFixedAsDWord points);
extern WWFixedAsDWord PrConvertFromDriverCoordinates(word units);
extern Boolean PrLineFeed(PState *ps, word units);
extern Boolean PrFormFeed(PState *ps);
extern Boolean Pr1ScanlineFeed(PState *ps);
extern Boolean SendLineFeed(PState *ps);
extern Boolean PrSetCursor(PState *ps, WWFixedAsDWord x, WWFixedAsDWord y);

/* Job/ */
extern Boolean PrintResetPrinterAndWait(PState *ps);
extern Boolean PrSetPaperPath(PState *ps, PaperInputOptions in,
			      PaperOutputOptions out);
extern const PrinterMargins *PrJobMargins(PState *ps);

/* Text/, Styles/ */
extern Boolean PrSetFont(PState *ps, FontID id, word size, byte pitch);
extern Boolean PrSetStyles(PState *ps, PrintTextStyle style);
/* the routines that set and reset each style bit (bit 0 first): the
 * driver's styles composite (printcomEpsonStyles.c, ...) */
typedef Boolean PrintStyleRoutine(PState *ps);
extern PrintStyleRoutine *const printSetStyle[16], *const printResetStyle[16];
extern PrintTextStyle PrTestStyles(PState *ps, PrintTextStyle style);
extern Boolean PrText(PState *ps, const char *text, word count);
extern Boolean PrintSetSymbolSet(PState *ps);
extern Boolean PrintLoadSymbolSet(PState *ps);

/* Color/ */
extern Boolean PrSetColorRGB(PState *ps, byte r, byte g, byte b);
extern byte PrMapRGBtoCMYKIndex(byte r, byte g, byte b);
extern Boolean PrSetRibbonColor(PState *ps, byte color);
extern Boolean SetFirstCMYK(PState *ps);
extern Boolean SetNextCMYK(PState *ps);

/* Buffer/ */
extern Boolean PrCreatePrintBuffers(PState *ps);
extern void PrDestroyPrintBuffers(PState *ps);
extern void PrLoadBandBuffer(PState *ps);
extern word PrScanBandBuffer(PState *ps, word *bytes);
extern Boolean PrSendOutputBuffer(PState *ps, word *count);
extern void PrClearOutputBuffer(PState *ps);

/* Graphics/ */
extern void LoadSwathHeader(PState *ps, VMFileHandle file,
			    VMBlockHandle block);
extern void DerefFirstScanline(PState *ps);
extern const byte *DerefAScanline(PState *ps);
extern void ReleaseScanlines(PState *ps);
extern word PrLoadPstateVars(PState *ps, word *left);
extern Boolean PrPrintABand(PState *ps);
extern Boolean PrSendGraphicControlCode(PState *ps, const byte *code,
					word columns);
extern Boolean PrPrintLowBand(PState *ps);
extern Boolean PrPrintMediumBand(PState *ps);
extern Boolean PrPrintHighBand(PState *ps);
extern Boolean PrRotate8Lines(PState *ps, word bytes);
extern Boolean PrRotate4LinesZeroTop(PState *ps, word bytes);
extern Boolean PrRotate4LinesZeroBottom(PState *ps, word bytes);
extern Boolean PrSend8HiresLines(PState *ps, word *bandStart);
/* 24 pins (rotate24.c, rotate2pass24.c): columns -1 all, 0 even, 1 odd */
extern Boolean PrRotate24Columns(PState *ps, word bytes, int columns);
extern Boolean PrRotate24Lines(PState *ps, word bytes);
extern Boolean PrSend24HiresLines(PState *ps, word *bandStart);

/* ---------------------------------------------------------------------
 * The PCL drivers (DeskJet, LaserJet): positions in 1/300", numbers in
 * the commands in decimal.
 * ------------------------------------------------------------------- */

/* Stream/streamHexToASCII.c, streamPCLCommand.c: a zero-terminated code,
 * each '#' the number */
extern Boolean HexToAsciiStreamWrite(PState *ps, word n);
extern Boolean HexToAsciiStreamWrite0(PState *ps, word n, word digits);
extern Boolean WriteNumCommand(PState *ps, const byte *code, word n);
/* Cursor/cursorSetCursorPCL.c: the cursor to PS_cursorPos */
extern Boolean PrMoveCursor(PState *ps);
/* Text/textSetFontPCL.c: the font with its styles */
extern Boolean PrSetFontStyle(PState *ps, FontID id, word size, byte pitch,
			      PrintTextStyle style);
/* Text/Font/fontPCLInfo.c */
extern const FontID pclFontIDTab[];
extern const word pclTypefaceTab[];
/* Graphics/graphicsPCLTIFF.c: a scan line (NULL: white) */
extern Boolean PrSendScanline(PState *ps, const byte *line);

/* PCL 4 (LaserJet): Job/jobCopiesPCL4.c */
extern Boolean PrintSetCopies(PState *ps, word copies);
/* the HPFontControls of PUID_initMemory */
#define HPFC_DELETE_ALL_SOFT_FONTS	0
#define HPFC_DELETE_TEMP_SOFT_FONTS	1
#define HPFC_DELETE_SOFT_FONT		2
/* Text/textSetFontPCL4.c: the resident font of PS_curFont */
extern Boolean PrintSetFontWayInt(PState *ps);
/* Styles/stylesSetPCL4.c: underline as PS_asciiStyle says */
extern Boolean PrintSetStylesInt(PState *ps);
/* Text/textInitFontPCL4.c */
extern void PrintSetURWMono12(PState *ps);
extern Boolean PrInitFont(PState *ps);
/* Text/Font/fontTopLevelPCL4.c: the soft fonts of a page */
extern Boolean FontInit(PState *ps);
extern Boolean FontExit(PState *ps);
extern Boolean FontAddFace(PState *ps);
extern Boolean FontAddChars(PState *ps, const byte *text, word count);
extern Boolean FontSendSpace(PState *ps);
/* Graphics/graphicsAdjustForResolution.c: 300 dpi dots of a mode's */
extern word PrAdjustForResolution(PState *ps, word dots);

#endif /* PRINTCOM_H */
