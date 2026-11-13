/*
 * bufferClearOutput.c -- PrClearOutputBuffer (the C port of
 * bufferClearOutput.asm).
 */
#include "printcom.h"
#include <string.h>

void PrClearOutputBuffer(PState *ps)
{
    memset(PS_OUTPUT_BUFFER(ps), 0, PRINT_OUTPUT_BUFFER_SIZE);
}
