/*
 * strDrInt.h -- the stream driver's data, for the drivers that work on
 * a stream directly (Port32; the C form of Include/Internal/
 * strDrInt.def).  The stream driver is Driver/Stream/stream.c.
 *
 * A stream is a block: a StreamData, then the circular buffer.  The
 * token of a stream is the handle of its block (the asm: its segment,
 * or a virtual segment for a movable one); StreamDataOf gives the
 * structure.  The pointers into the buffer (SSD_ptr, SD_max) are
 * offsets from the start of the block, as in the asm, the buffer
 * begins at SD_DATA_OFFSET.  The semaphores are counted as in the
 * asm: the reader's the bytes in the buffer, the writer's the room.
 *
 * A routine notifier (SNM_ROUTINE) is a C function: for an error
 * StreamGeneralNotifyRoutine(SN_data, error, side); for data with a
 * threshold other than 1 the same with (SN_data, count, side); with a
 * threshold of 1 StreamSpecialDataNotifyRoutine (streamC.h): the
 * reader gets each byte (non-zero back: it took it), the writer is
 * asked for one (STREAM_SPECIAL_WRITE_VALUE).  A message notifier
 * (SNM_MESSAGE) is sent StreamNotifyMessageParams: the count or error
 * (cx), the token (dx), the side (bp).
 */
#ifndef STRDRINT_H
#define STRDRINT_H

#include <geos.h>
#include <geosdriver.h>
#include <streamC.h>
#include <Internal/semInt.h>
#include <Internal/streamDr.h>

/* sides, as the registers carry them (StreamRoles of streamC.h) */
#define STREAM_BOTH		(-2)
#define STREAM_READ		(-1)
#define STREAM_WRITE		0

/* StreamNotifyMode, StreamNotifyEvent */
#define SNM_NONE	0
#define SNM_ROUTINE	1
#define SNM_MESSAGE	2
#define SNE_ERROR	0
#define SNE_DATA	1

/* StreamNotifyType: SNT_READER:1, SNT_EVENT:3, SNT_HOW:2 */
#define SNT_READER		0x20
#define SNT_EVENT		0x1c
#define SNT_EVENT_OFFSET	2
#define SNT_HOW			0x03

#define STREAM_ACK	((byte) -1)	/* the notifier waits for an ack */
#define STREAM_NOACK	0

typedef union {
    void	*SND_routine;	/* SNM_ROUTINE */
    optr	SND_message;	/* SNM_MESSAGE */
} StreamNotifierData;

typedef struct {
    byte		SN_type;	/* StreamNotifyMode */
    byte		SN_ack;		/* acknowledgment still pending */
    word		SN_data;	/* message, or the routine's data */
    StreamNotifierData	SN_dest;
} StreamNotifier;

typedef struct {
    StreamNotifier	SSD_error;	/* error event notifier */
    StreamNotifier	SSD_data;	/* data event notifier */
    word		SSD_lastErr;	/* stored error code */
    word		SSD_thresh;	/* notification threshold */
    word		SSD_ptr;	/* reader: head, writer: tail
					 * (offset in the block) */
    Semaphore		SSD_sem;	/* bytes for this side */
    Semaphore		SSD_lock;	/* this side to one thread */
} StreamSideData;

/* StreamState */
#define SS_LINGERING	0x80	/* to go once the data are read */
#define SS_NUKING	0x40	/* to go once everyone is out */
#define SS_RDATA	0x20
#define SS_WDATA	0x10

typedef struct {
    MemHandle		SD_handle;	/* the block */
    StreamSideData	SD_reader;
    StreamSideData	SD_writer;
    word		SD_max;		/* end of the buffer (offset) */
    byte		SD_useCount;	/* threads in the driver */
    byte		SD_state;	/* StreamState */
    Semaphore		SD_closing;	/* the destroyer waits here (the
					 * asm's queue SD_closing) */
    word		SD_unbalanced;	/* unbalanced gets/puts */
} StreamData;

/* the buffer starts after the structure (SD_data) */
#define SD_DATA_OFFSET	((word) ((sizeof(StreamData) + 7) & ~7))

#define StreamDataOf(token)	((StreamData *) MemDeref((MemHandle) (token)))
#define StreamBuffer(sd)	((byte *) (sd))

/* the message of an SNM_MESSAGE notifier */
typedef struct {
    word	SNMP_value;	/* cx: bytes available, or the error */
    StreamToken	SNMP_stream;	/* dx */
    word	SNMP_side;	/* bp: STREAM_READ, STREAM_WRITE */
} StreamNotifyMessageParams;

/* the stream driver's routines for other drivers (stream32.gp exports
 * them; the driver is a library for that) */
/* send a notifier: ack STREAM_ACK if it is to wait for an ack; value,
 * side: cx, bp */
extern void StreamNotify(StreamData *sd, StreamNotifier *n, byte ack,
			 word value, word side);
/* data notification to the writing (reading) side, if over the
 * threshold */
extern void StreamWriteDataNotify(StreamData *sd);
extern void StreamReadDataNotify(StreamData *sd);
/* revoke access (STREAM_LINGER: once the data are read); TRUE if the
 * stream is closing already (STREAM_CLOSING) */
extern Boolean StreamShutdown(StreamToken stream, sword linger);
extern void StreamFree(StreamToken stream);
/* the strategy routine, for a direct call.  DR_STREAM_SET_NOTIFY:
 * the routine in DR_farPtr (SNM_ROUTINE), the message's destination in
 * DR_cx (handle) and DR_dx (chunk) (SNM_MESSAGE); DR_STREAM_READ and
 * DR_STREAM_WRITE: the buffer in DR_dsSi */
extern Boolean StreamStrategy(word function, DriverRegs *regs);

#endif /* STRDRINT_H */
