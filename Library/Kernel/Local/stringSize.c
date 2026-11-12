/*
 * stringSize.c -- the length of a string (after stringSize.asm, SBCS: the
 * size in bytes is the number of characters).
 */
#include "localInt.h"

word LocalStringLength(const char *str)
{
    const char *p = str;

    while (*p)
	p++;
    return (word) (p - str);
}

word LocalStringSize(const char *str)
{
    return LocalStringLength(str);
}
