/*
 * geodesHandle.c -- after geodesHandle.asm: GeodeFind.
 */
#include "kernelC.h"
#include "geosmod.h"
#include <string.h>

GeodeHandle GeodeFind(const char *name, word numChars, GeodeAttrs attrMatch,
		      GeodeAttrs attrNoMatch)
{
    GeodeHandle gh;

    GeodeLock();
    gh = GeodeFindLow(name, numChars, attrMatch, attrNoMatch);
    GeodeUnlock();
    return gh;
}
