/*
 * printcomStream.c -- writing to the stream (the C port of
 * printcomStream.asm: Stream/streamWrite.asm, streamWriteByte.asm and
 * streamSendCodeOut.asm).
 *
 * DR_STREAM_WRITE and DR_STREAM_WRITE_BYTE with STREAM_BLOCK to the
 * PState's stream; an error sets PS_error to 1 and returns the carry.
 */
#include "printcom.h"
#include <string.h>
#include "Stream/streamWrite.c"
#include "Stream/streamWriteByte.c"
#include "Stream/streamSendCodeOut.c"
