/*
 * cupsDr.h -- the CUPS port driver (cups.geo): the stream driver calls
 * it answers, as numbered in Include/Internal/streamDr.def, and its own
 * options.
 *
 * The spooler opens it as a custom port (PPT_CUSTOM,
 * Library/Spool/Process/processCustom.asm):
 *   STREAM_ESC_LOAD_OPTIONS   ds:si = the printer's name, its ini
 *                             category: [<printer>] hostQueue (the CUPS
 *                             queue; default: the host's default printer),
 *                             hostDuplex (TRUE: both sides);
 *   DR_STREAM_OPEN            bx = a block with CPP_info (a queue name
 *                             there wins over hostQueue), dx = buffer size;
 *                             returns bx = the unit;
 *   DR_STREAM_WRITE(_BYTE)    the printer driver's output (PostScript);
 *   DR_STREAM_CLOSE           ax = STREAM_LINGER prints the job,
 *                             STREAM_DISCARD drops it.
 */
#ifndef CUPS_DR_H
#define CUPS_DR_H

#include <driver.h>

#define DR_STREAM_GET_DEVICE_MAP    8
#define DR_STREAM_OPEN              10
#define DR_STREAM_CLOSE             12
#define DR_STREAM_SET_NOTIFY        14
#define DR_STREAM_GET_ERROR         16
#define DR_STREAM_SET_ERROR         18
#define DR_STREAM_FLUSH             20
#define DR_STREAM_SET_THRESHOLD     22
#define DR_STREAM_READ              24
#define DR_STREAM_READ_BYTE         26
#define DR_STREAM_WRITE             28
#define DR_STREAM_WRITE_BYTE        30
#define DR_STREAM_QUERY             32

#define STREAM_ESC_LOAD_OPTIONS     0x8100
#define STREAM_ESC_RESTART_OUTPUT   0x8101

#define CUPS_NUM_UNITS              4
#define CUPS_BUFFER_SIZE            4096

/* DR_STREAM_GET_ERROR: what went wrong with the unit (after
 * STREAM_FIRST_DEV_ERROR, streamC.h) */
#define CUPS_ERR_NO_QUEUE           256 /* no queue named, no default */
#define CUPS_ERR_REFUSED            257 /* CUPS refused the job */
#define CUPS_ERR_HOST               258 /* CUPS is gone or broke */

#endif /* CUPS_DR_H */
