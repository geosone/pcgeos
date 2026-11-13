/*
 * bufferOkiSendOutput.c -- PrSendOutputBuffer (the C port of
 * bufferOkiSendOutput.asm): the output buffer to the printer with each
 * ETX doubled, the Okidata graphics' escape (ETX STX ends them).
 */
#include "printcom.h"

Boolean PrSendOutputBuffer(PState *ps, word *count)
{
    const byte *b = PS_OUTPUT_BUFFER(ps), *p;
    word n = *count, k;

    *count = 0;
    while (n) {
	for (p = b, k = 0; k < n && *p != C_ETX; p++, k++)
	    ;
	if (k == n)
	    return PrintStreamWrite(ps, b, n);
	k++;				/* the ETX, then once more */
	if (PrintStreamWrite(ps, b, k) || PrintStreamWriteByte(ps, C_ETX))
	    return TRUE;
	b += k;
	n = (word) (n - k);
    }
    return FALSE;
}
