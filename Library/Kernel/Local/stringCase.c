/*
 * stringCase.c -- upper and lower case (after stringCase.asm, SBCS): the
 * tables of stringCaseTables.asm, made C at build time, map the
 * characters from MIN_UPCASE ('a') and MIN_DOWNCASE ('A') on.
 */
#include "localInt.h"

static wchar MapChar(wchar ch, const byte *table, word min)
{
    if ((ch & 0xff) < min)
	return ch;
    /* the asm replaces the low byte only */
    return (wchar) ((ch & 0xff00) | table[(ch & 0xff) - min]);
}

wchar LocalUpcaseChar(wchar ch)
{
    return MapChar(ch, localUpcaseTable, LOCAL_MIN_UPCASE);
}

wchar LocalDowncaseChar(wchar ch)
{
    return MapChar(ch, localDowncaseTable, LOCAL_MIN_DOWNCASE);
}

/* ConvertString: up to size characters (0: to the null), in place; the
 * null is converted too and ends it */
static void ConvertString(char *str, word size, const byte *table, word min)
{
    byte c;

    do {
	c = (byte) *str;
	if (c >= min)
	    c = table[c - min];
	*str++ = (char) c;
    } while (c && --size);
}

void LocalUpcaseString(char *str, word size)
{
    ConvertString(str, size, localUpcaseTable, LOCAL_MIN_UPCASE);
}

void LocalDowncaseString(char *str, word size)
{
    ConvertString(str, size, localDowncaseTable, LOCAL_MIN_DOWNCASE);
}
