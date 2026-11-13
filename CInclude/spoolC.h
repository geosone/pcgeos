/*
 * spoolC.h -- the spool library's routines and types for C (the routines
 * of spool.goh and spool.def without the controller classes, which need
 * the UI library).
 *
 * Port32 (Port32/ABI.md section 13w): one C routine per library routine,
 * named as in spool.goh; the register versions of the asm (SpoolAddJob:
 * dx:si, ...) are these routines too.  SpoolAddJob, which the asm had
 * only as a register routine for the PrintControl, takes the
 * JobParameters and returns the job ID.
 */
#ifndef SPOOLC_H
#define SPOOLC_H

#include <geos.h>
#include <file.h>
#include <thread.h>
#include <print.h>
#include <Internal/spoolInt.h>

#ifndef MAXIMUM_PRINTER_NAME_LENGTH
#define MAXIMUM_PRINTER_NAME_LENGTH	64	/* includes the NULL */
#endif

/* operation status codes of the queue routines */
typedef enum /* word */ {
    SPOOL_OPERATION_SUCCESSFUL,
    SPOOL_JOB_NOT_FOUND,
    SPOOL_QUEUE_EMPTY,
    SPOOL_QUEUE_NOT_EMPTY,
    SPOOL_QUEUE_NOT_FOUND,
    SPOOL_CANT_VERIFY_PORT,
    SPOOL_OPERATION_FAILED
} SpoolOpStatus;

/* the time stamp of a print job */
typedef struct {
    byte	STS_second;
    byte	STS_minute;
    byte	STS_hour;
} SpoolTimeStruct;

/* the name of a spool file: "spool000.dat" */
typedef struct {
    char	SFN_base[5];
    char	SFN_num[3];
    char	SFN_ext[5];
} SpoolFileName;

typedef enum /* word */ {
    SIT_JOB_INFO	= 0,
    SIT_QUEUE_INFO	= 2,
    SIT_JOB_PARAMETERS	= 4
} SpoolInfoType;

/* JS_printing */
typedef ByteEnum SpoolJobPrinting;
#define SJP_NOT_PRINTING		0x00
#define SJP_PRINTING_NO_PAGE_INFO	0xff	/* TRUE */
#define SJP_PRINTING			0xfe	/* TRUE-1 */

/* SIT_JOB_INFO returns a block with this (spool.def) */
typedef struct {
    char		JS_fname[13];
    char		JS_parent[FILE_LONGNAME_LENGTH+1];
    char		JS_documentName[FILE_LONGNAME_LENGTH+1];
    word		JS_numPages;
    SpoolTimeStruct	JS_time;
    SpoolJobPrinting	JS_printing;
    word		JS_curPage;	/* current physical page, iff
					 * JS_printing is SJP_PRINTING */
    word		JS_totalPage;	/* physical pages of the job, iff
					 * JS_printing is SJP_PRINTING */
} JobStatus;

typedef struct {
    Boolean		SIQP_activityQuery;
    PrintPortInfo	*SIQP_portInfo;
    MemHandle		SIQP_retBlock;
    word		SIQP_retNumJobs;
} SpoolInfoQueueParams;

typedef struct {
    word		SIJP_jobID;
    MemHandle		SIJP_retBlock;
} SpoolInfoJobParams;

typedef union {
    SpoolInfoQueueParams	SIP_queueParams;
    SpoolInfoJobParams		SIP_jobParams;
} SpoolInfoParams;

/* the printers of the ini file ([printer] printers) */
typedef struct {
    char		PSI_stringBuf[GEODE_MAX_DEVICE_NAME_SIZE];
    word		PSI_stringLength;
    PrinterDriverType	PSI_driverType;
} PrinterStringInfo;

/* the printers */
extern word SpoolGetNumPrinters(PrinterDriverType driverType,
				byte localOnlyFlag);
extern Boolean SpoolGetPrinterString(PrinterStringInfo *retInfo,
				     word printerNum);
extern void SpoolGetPrinterInfo(void);
extern word SpoolCreatePrinter(const char *printerName,
			       PrinterDriverType driverType);
extern void SpoolDeletePrinter(word printerNum);
extern int SpoolGetDefaultPrinter(void);
extern void SpoolSetDefaultPrinter(word printerNum);

/* the jobs */
extern FileHandle SpoolCreateSpoolFile(char *fileNameBuf);
extern word SpoolAddJob(const JobParameters *params);
extern SpoolOpStatus SpoolDelJob(word jobID);
extern SpoolOpStatus SpoolInfo(SpoolInfoType infoType,
			       SpoolInfoParams *params);
extern SpoolOpStatus SpoolHurryJob(word jobID);
extern SpoolOpStatus SpoolDelayJob(word jobID);
extern SpoolOpStatus SpoolModifyPriority(word jobID,
					 ThreadPriority priority);
extern SpoolOpStatus SpoolVerifyPrinterPort(PrintPortInfo *portInfo);

/* ---------------------------------------------------------------------
 * Page sizes (libPaper)
 * ------------------------------------------------------------------- */

#define MAX_PAPER_STRING_LENGTH 	48	/* includes NULL-terminator */
#define MAX_PRE_DEFINED_PAPER_SIZES	128
#define MAX_USER_DEFINED_PAPER_SIZES	128
#define MAX_PAPER_SIZES			(MAX_PRE_DEFINED_PAPER_SIZES + \
					 MAX_USER_DEFINED_PAPER_SIZES)

typedef struct {
    word	NPSI_numSizes;
    word	NPSI_defaultSize;	/* always 0 */
} NumPaperSizesInfo;

typedef struct {
    word	PSI_width;		/* points */
    word	PSI_height;
    PageLayout	PSI_defaultLayout;
} PaperSizeInfo;

typedef struct {
    word	PSOI_numOrdered;
    word	PSOI_numUnused;
} PaperSizeOrderInfo;

extern void SpoolGetDefaultPageSizeInfo(PageSizeReport *psr);
extern void SpoolSetDefaultPageSizeInfo(PageSizeReport *psr);
extern void SpoolGetNumPaperSizes(NumPaperSizesInfo *sizesInfo,
				  PageType pageType);
/* a paper size number: an index into the order, or (0xff << 8) | size */
extern word SpoolGetPaperString(char *stringBuf, word paperSizeNum,
				PageType pageType);
extern void SpoolGetPaperSize(PaperSizeInfo *sizeInfo, word paperSizeNum,
			      PageType pageType);
/* the paper size number, or -1 */
extern word SpoolConvertPaperSize(word width, word height,
				  PageType pageType);
/* Port32: with the PageType, which the asm's C stub did not pass */
extern void SpoolGetPaperSizeOrder(byte *orderBuf, byte *userDefBuf,
				   PaperSizeOrderInfo *numBuf,
				   PageType pageType);
extern void SpoolSetPaperSizeOrder(byte *orderArray, word numEntries,
				   PageType pageType);
/* the new size's number (128-255), 0 on failure */
extern word SpoolCreatePaperSize(char *paperSizeString, word width,
				 word height, PageLayout layout,
				 PageType pageType);
/* FALSE on success */
extern Boolean SpoolDeletePaperSize(word paperSizeNum, PageType pageType);

#endif /* SPOOLC_H */
