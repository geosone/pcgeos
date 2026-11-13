/*
 * streamHexToASCII.c -- a number in decimal to the stream (the C port of
 * streamHexToASCII.asm).
 */
#include "printcom.h"

Boolean HexToAsciiStreamWrite(PState *ps, word n)
{
    char buf[5];
    int i = 5;

    do {
	buf[--i] = (char) ('0' + n % 10);
	n /= 10;
    } while (n);
    return PrintStreamWrite(ps, buf + i, (word) (5 - i));
}
