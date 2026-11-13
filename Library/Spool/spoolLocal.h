/*
 * spoolLocal.h -- the spool library in C: the C form of spoolConstant.def,
 * spoolVariable.def, spoolGlobal.def and Process/processConstant.def, as
 * far as the C files beside the asm need them.
 *
 * Port32 (Port32/ABI.md section 13w): the spooler is a library with a
 * process (spool32.gp), as in DOS16, but without its UI so far (the
 * PrintControl, the PageSizeControl, the printer control panel and the
 * error boxes need the UI library).  What it does:
 *   - the queues and their threads (Process/processQueue.c,
 *     processUtils.c, processLoop.c, processLockQueue.c);
 *   - printing a spool file on a PDL printer (processThread.c,
 *     processPDL.c); graphics and text modes come with the printer
 *     drivers that need them;
 *   - the ports: file (processFile.c, filestr.geo), custom
 *     (processCustom.c, e.g. cups.geo) and nothing (processNothing.c);
 *     serial and parallel need their stream drivers;
 *   - the printers of the ini file (Lib/libPrinter.c) and the spool
 *     files (Lib/libMisc.c).
 * The build options of the default spooler: no labels, no printing
 * dialog, no dual-threaded printing, no non-spooled printing.
 */
#ifndef SPOOLLOCAL_H
#define SPOOLLOCAL_H

#include <geos.h>
#include <heap.h>
#include <lmem.h>
#include <chunkarr.h>
#include <file.h>
#include <fileEnum.h>
#include <geode.h>
#include <driver.h>
#include <geosdriver.h>
#include <initfile.h>
#include <sem.h>
#include <thread.h>
#include <timer.h>
#include <timedate.h>
#include <system.h>
#include <char.h>
#include <graphics.h>
#include <gstring.h>
#include <font.h>
#include <print.h>
#include <streamC.h>
#include <spoolC.h>
#include <Internal/streamDr.h>
#include <Internal/printDr.h>

/* ---------------------------------------------------------------------
 * processConstant.def
 * ------------------------------------------------------------------- */

#define SPOOL_FILE_BUFFER_SIZE		1024
#define SPOOL_PARALLEL_BUFFER_SIZE	1024
#define SPOOL_INVALID_JOB_ID		0xffff

/* SpoolPrintState */
#define SPS_TIRAMISU_PRINTING	0x20	/* fax printing */
#define SPS_FORM_FEED		0x10	/* FormFeedFlag */
#define SPS_TILED		0x08	/* TiledFlag */
#define SPS_ROTATED		0x04	/* RotatedFlag */
#define SPS_SMARTS		0x03	/* PrinterSmart */

/* OrientBits: the index of CalcDocOrientation's routines */
#define OB_PDL			0x08
#define OB_ROTATE		0x04
#define OB_SCALE_TO_FIT		0x02

/* SpoolInterruptions (QI_error) */
#define SI_KEEP_GOING		0
#define SI_PAUSE		1	/* pause while an error is up */
#define SI_ABORT		2	/* abort the current job */
#define SI_DETACH		3	/* detaching while printing */
#define SI_ERROR		4	/* encountered an error */

/* PortErrors */
#define PE_PORT_NOT_OPENED	0
#define PE_PORT_INIT_ERROR	1

/* PrintStatusChangeType of gcnlist.goh (MSG_PRINT_STATUS_CHANGE), for
 * the C files */
#define SPOOL_PSCT_NEW_PAGE	0	/* data: the physical page */
#define SPOOL_PSCT_COMPLETE	1
#define SPOOL_PSCT_CANCELED	2
#define SPOOL_PSCT_ERROR	3

/* SpoolJobAction */
#define SJA_HURRY		0
#define SJA_DELAY		2
#define SJA_PRIORITY		4

/* spoolConstant.def: SpoolerState */
#define SS_DETACHING		0x8000
#define SS_DETACH_DIALOG	0x4000

/* the heap space a queue's thread reserves (spoolConstant.def) */
#define SPOOL_STANDARD_RESERVATION	50

/* the answers of SpoolErrorBox: InteractionCommand values of
 * Objects/gInterC.h, which needs the UI */
#define SPOOL_IC_DISMISS	1
#define SPOOL_IC_OK		5

/* the swath of graphics printing: 256K (processConstant.def) */
#define SPOOL_SWATH_DATA_SIZE	0x40000UL

/* ---------------------------------------------------------------------
 * The print queue: an LMem block of the spooler, shared by all threads
 * (LockQueue).  Its chunks are the queues (one per port, each with its
 * thread) and their jobs.
 * ------------------------------------------------------------------- */

typedef struct {
    ChunkHandle		JIS_next;	/* next job in the queue */
    ChunkHandle		JIS_queue;	/* the queue it is in */
    SpoolTimeStruct	JIS_time;	/* time stamp */
    word		JIS_jobID;
    JobParameters	JIS_info;	/* JP_size bytes, as passed */
} JobInfoStruct;

#define JIS_SIZE(jpSize) \
    ((word) (offsetof(JobInfoStruct, JIS_info) + (jpSize)))

struct _SpoolJobInfo;

typedef struct {
    ChunkHandle		QI_next;	/* next queue */
    ThreadHandle	QI_thread;	/* the queue's thread */
    char		QI_device[MAX_DEVICE_NAME_SIZE];
    PrintPortInfo	QI_portInfo;
    ChunkHandle		QI_curJob;	/* the current job */
    struct _SpoolJobInfo *QI_threadInfo;	/* the thread's job */
    FileHandle		QI_fileHan;	/* gstring file */
    dword		QI_filePos;	/* where the current page starts */
    word		QI_curPage;	/* current page number */
    byte		QI_error;	/* SpoolInterruptions */
    word		QI_timeout;	/* unused */
    byte		QI_maxRetry;	/* retries to begin with */
    byte		QI_curRetry;	/* retries left */
    PrinterResend	QI_resend;
    GeodeHandle		QI_portHan;	/* port driver */
    GeodeHandle		QI_pdHan;	/* printer driver */
    word		QI_numPhysPgs;	/* physical pages of the job */
    word		QI_curPhysPg;	/* the current physical page */
} QueueInfo;

typedef struct {
    LMemBlockHeader	PQ_header;
    word		PQ_numQueues;	/* active queues */
    word		PQ_numJobs;	/* jobs in all queues */
    ChunkHandle		PQ_firstQueue;
} PrintQueue;

/* the job a queue's thread prints: on its stack, as the asm's curJob */
typedef struct _SpoolJobInfo {
    ChunkHandle		SJI_qHan;	/* our queue */
    JobParameters	SJI_info;	/* copied from the job */
    FileHandle		SJI_fHan;	/* spool file */
    dword		SJI_fPos;	/* where the current page starts */
    DriverStrategy	*SJI_stream;	/* port driver strategy */
    GeodeHandle		SJI_sHan;	/* port driver */
    DriverStrategy	*SJI_pDriver;	/* printer driver strategy */
    GeodeHandle		SJI_pdHan;	/* printer driver */
    GeodeHandle		SJI_prev_pdHan;	/* the previous job's */
    GStateHandle	SJI_gstring;	/* the spool file's gstring */
    MemHandle		SJI_pstate;	/* PState + JobParameters */
    GStateHandle	SJI_gstate;	/* to draw to */
    word		SJI_pXres, SJI_pYres;	/* resolution (dpi) */
    WWFixedAsDWord	SJI_pXscale, SJI_pYscale;
    word		SJI_bandW;	/* band width (pixels) */
    word		SJI_bandH;	/* band height (pixels) */
    word		SJI_swathH;	/* swath height (pixels) */
    dword		SJI_pageH;	/* page height (pixels) */
    dword		SJI_curScan;
    byte		SJI_numCopies;	/* copies left (uncollated) */
    BMFormat		SJI_colorFormat;
    word		SJI_colorCorrection;
    TransMatrix		SJI_oldMatrix;
    TransMatrix		SJI_defMatrix;	/* CalcDocOrientation's */
    TransMatrix		SJI_xloopTM;
    TransMatrix		SJI_yloopTM;
    byte		SJI_printState;	/* SpoolPrintState */
    word		SJI_xPages;	/* sheets across */
    word		SJI_curxPage;
    word		SJI_yPages;	/* sheets down */
    word		SJI_curyPage;
    word		SJI_printWidth;	/* printable area of the paper */
    word		SJI_printHeight;
    dword		SJI_docWidth;	/* printable area of the document */
    dword		SJI_docHeight;
} SpoolJobInfo;

#define PAPER_INFO(j)	((j)->SJI_info.JP_paperSizeInfo)
#define DOC_INFO(j)	((j)->SJI_info.JP_docSizeInfo)

/* ---------------------------------------------------------------------
 * spoolVariable.def
 * ------------------------------------------------------------------- */

extern MemHandle	queueHandle;	/* the PrintQueue (0: none) */
extern SemaphoreHandle	queueSemaphore;
extern word		spoolStateFlags;	/* SpoolerState */
extern word		nextJobID;
extern word		customQueueID;
extern ThreadLockHandle	errorThreadLock;
extern word		errorBoxCount;

/* the queue, while it is locked */
#define PQ()		((PrintQueue *) MemDeref(queueHandle))
#define QUEUE(ch)	((QueueInfo *) LMemDerefHandles(queueHandle, ch))
#define JOB(ch)		((JobInfoStruct *) LMemDerefHandles(queueHandle, ch))

/* ---------------------------------------------------------------------
 * The routines
 * ------------------------------------------------------------------- */

/* processLockQueue.c: TRUE (carry) if there is no queue; the semaphore
 * is held either way, as in the asm */
extern Boolean LockQueue(void);
extern void UnlockQueue(void);

/* processUtils.c */
extern void AllocPrintQueue(void);
extern ChunkHandle FindRightQueue(const PrintPortInfo *port);
extern void InsertJobIntoQueue(ChunkHandle job, ChunkHandle queue);
extern ChunkHandle MapJobIDtoHandle(word jobID);
extern void SendJobAddedNotification(ChunkHandle job);
extern void SendJobRemovedNotification(ChunkHandle job);
extern ChunkHandle AllocDeviceQueue(ChunkHandle job);

/* processQueue.c */
extern word SpoolAddJobInternal(const JobParameters *params, word jobID);
extern Boolean SpoolQueueCheckJobsWantingWarning(void);

/* processLoop.c */
extern word SpoolThreadBirth(word queue);

/* processThread.c */
extern void GetJobInfo(SpoolJobInfo *j);
extern void PrintFile(SpoolJobInfo *j);
extern void StoppedByError(SpoolJobInfo *j);
extern Boolean RemoveJob(SpoolJobInfo *j);
extern Boolean CheckForErrors(SpoolJobInfo *j, byte *interruption);
extern void BumpPageNumber(SpoolJobInfo *j);
extern void BumpPhysPageNumber(SpoolJobInfo *j);
extern word WillDocumentFit(const PageSizeReport *doc,
			    const PageSizeReport *paper);
extern Boolean ProcessStartPage(SpoolJobInfo *j);
extern Boolean ProcessEndPage(SpoolJobInfo *j, GSRetType *ret,
			      word data);
extern Boolean CallPrinter(SpoolJobInfo *j, word function, DriverRegs *r);
extern Boolean CallPort(SpoolJobInfo *j, word function, DriverRegs *r);

/* processPDL.c */
extern word DoPDLInit(SpoolJobInfo *j);
extern void DoPDLCleanup(SpoolJobInfo *j);
extern GSRetType PrintPDLPage(SpoolJobInfo *j);

/* processPort.c */
extern Boolean LoadPortDriver(SpoolJobInfo *j);
extern Boolean InitPrinterPort(SpoolJobInfo *j, word *portError);
extern void ClosePrinterPort(SpoolJobInfo *j);

/* the port types (processFile.c, processCustom.c, processNothing.c):
 * init returns TRUE and a PortErrors on an error */
extern Boolean InitFilePort(SpoolJobInfo *j, word *portError);
extern void ExitFilePort(SpoolJobInfo *j);
extern Boolean InitCustomPort(SpoolJobInfo *j, word *portError);
extern void ExitCustomPort(SpoolJobInfo *j);
extern Boolean InitNothingPort(SpoolJobInfo *j, word *portError);
extern void ExitNothingPort(SpoolJobInfo *j);

/* processError.c: InteractionCommand of the answer */
extern word SpoolErrorBox(word error, ChunkHandle queue);

/* processRestart.c */
extern void SpoolBirth(void);

/* processApp.goc: messages to the spooler's process, notifications */
extern void SpoolSendCreateThread(ChunkHandle job);
extern void SpoolSendJobNotification(Boolean added, word jobID,
				     PrinterPortType type, word portNum);
extern void SpoolSendStatusChange(word type, word jobID, word data);
extern void SpoolSendPrintersChanged(void);

/* Lib/libPrinter.c */
extern PrinterDriverType ReadPrinterDriverType(const char *printer);

#endif /* SPOOLLOCAL_H */
