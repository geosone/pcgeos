/*
 * streamSendCodeOut.c -- after streamSendCodeOut.asm: SendCodeOut.
 * Included by the printcom*.c files whose asm includes it.
 */
#include "printcom.h"
#include <string.h>

/* SendCodeOut: a count byte, then that many bytes; none is no write */
Boolean SendCodeOut(PState *ps, const byte *code)
{
    if (!code[0])
	return FALSE;
    return PrintStreamWrite(ps, code + 1, code[0]);
}
