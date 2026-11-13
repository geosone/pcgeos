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
#include <Internal/streamDr.h>

#define CUPS_NUM_UNITS              4
#define CUPS_BUFFER_SIZE            4096

/* DR_STREAM_GET_ERROR: what went wrong with the unit (after
 * STREAM_FIRST_DEV_ERROR, streamC.h) */
#define CUPS_ERR_NO_QUEUE           256 /* no queue named, no default */
#define CUPS_ERR_REFUSED            257 /* CUPS refused the job */
#define CUPS_ERR_HOST               258 /* CUPS is gone or broke */

#endif /* CUPS_DR_H */
