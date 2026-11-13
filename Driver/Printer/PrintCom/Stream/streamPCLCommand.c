/*
 * streamPCLCommand.c -- a PCL command with a number (the C port of
 * streamPCLCommand.asm): the bytes of a zero-terminated control code,
 * each '#' the number in decimal.
 */
#include "printcom.h"

Boolean WriteNumCommand(PState *ps, const byte *code, word n)
{
    for (; *code; code++)
	if (*code == '#' ? HexToAsciiStreamWrite(ps, n)
			 : PrintStreamWriteByte(ps, *code))
	    return TRUE;
    return FALSE;
}
