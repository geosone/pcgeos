/*
 * streamHexToASCIILeading0.c -- HexToAsciiStreamWrite0 (the C port of
 * streamHexToASCIILeading0.asm): a number in decimal ASCII to the
 * printer, with leading zeros to at least the digits given.  The asm's
 * HexToAsciiStreamWrite took the digits in cx; the C name differs from
 * streamHexToASCII.c's, which has no digits.
 */
#include "printcom.h"

Boolean HexToAsciiStreamWrite0(PState *ps, word n, word digits)
{
    char buf[8];
    int i = 8;

    do {
	buf[--i] = (char) ('0' + n % 10);
	n /= 10;
    } while (n);
    while (i > 0 && 8 - i < (int) digits)
	buf[--i] = '0';
    return PrintStreamWrite(ps, buf + i, (word) (8 - i));
}
