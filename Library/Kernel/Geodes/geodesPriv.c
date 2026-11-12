/*
 * geodesPriv.c -- geode private data (after geodesPriv.asm:
 * GeodePrivAlloc, GeodePrivFree, GeodePrivRead, GeodePrivWrite,
 * GeodePrivExit, FindContiguousWords).
 *
 * A library that needs a few words for each geode (the window system's
 * GeodeWinVars, say) allocates a range of them once, for all geodes, and
 * reads and writes them per geode.  The map has a word per slot: the
 * geode that allocated it, or 0 for a free one.  An offset is the byte
 * offset of the range plus 2 (FIRST_GEODE_PRIV_OFFSET), so it is never
 * 0, as in the asm.
 *
 * Each geode's words are a block of the kernel's heap (malloc) that
 * the core block points to; it grows on writes and goes when the geode
 * is freed.  Words never written read as 0.  The asm kept the map in
 * the kernel's own private data block and each geode's words in a
 * block owned by the geode.
 */
#include "kernelC.h"

#include <stdlib.h>
#include <string.h>

#define FIRST_GEODE_PRIV_OFFSET		2
#define PRIV_DATA_SLOT_INCREMENT	8

static KSem privSem = { 1, 0 };
static word *privMap;			/* owner per slot, 0: free */
static word privMapWords;

static void PrivP(void)
{
    KSemP(&privSem, KSEM_FOREVER);
}

static void PrivV(void)
{
    KSemV(&privSem);
}

/* FindContiguousWords: n free slots for gh; the first, or -1 */
static long FindFree(word n, GeodeHandle gh)
{
    word i, run = 0;

    for (i = 0; i < privMapWords; i++) {
	run = privMap[i] ? 0 : (word) (run + 1);
	if (run == n) {
	    word first = (word) (i + 1 - n), j;
	    for (j = first; j <= i; j++)
		privMap[j] = gh;
	    return first;
	}
    }
    return -1;
}

word GeodePrivAlloc(GeodeHandle gh, word numWords)
{
    long first;

    if (!numWords)
	return 0;
    PrivP();
    while ((first = FindFree(numWords, gh)) < 0) {
	word grow = (word) ((numWords + PRIV_DATA_SLOT_INCREMENT - 1) /
			    PRIV_DATA_SLOT_INCREMENT * PRIV_DATA_SLOT_INCREMENT);
	word *m;
	if ((dword) privMapWords + grow > 0x7fff ||
	    !(m = realloc(privMap, (size_t) (privMapWords + grow) *
			  sizeof(word)))) {
	    PrivV();
	    return 0;
	}
	memset(m + privMapWords, 0, grow * sizeof(word));
	privMap = m;
	privMapWords = (word) (privMapWords + grow);
    }
    PrivV();
    return (word) (first * sizeof(word) + FIRST_GEODE_PRIV_OFFSET);
}

void GeodePrivFree(word offset, word numWords)
{
    word i = (word) ((offset - FIRST_GEODE_PRIV_OFFSET) / sizeof(word));

    PrivP();
    for (; numWords && i < privMapWords; numWords--, i++)
	privMap[i] = 0;
    PrivV();
}

static GeodeCore *Core(GeodeHandle *gh)
{
    if (!*gh)
	*gh = GeodeGetProcessHandle();
    return GeodeCoreOf(*gh);
}

void GeodePrivRead(GeodeHandle gh, word offset, word numWords, word *dest)
{
    GeodeCore *c;
    word at = (word) ((offset - FIRST_GEODE_PRIV_OFFSET) / sizeof(word));
    word i;

    GeodeLock();
    c = Core(&gh);
    for (i = 0; i < numWords; i++)
	dest[i] = c && (word) (at + i) < c->GC_privWords ?
	    c->GC_privData[at + i] : 0;
    GeodeUnlock();
}

Boolean GeodePrivWrite(GeodeHandle gh, word offset, word numWords,
		       word *src)
{
    GeodeCore *c;
    word at = (word) ((offset - FIRST_GEODE_PRIV_OFFSET) / sizeof(word));
    Boolean ok = FALSE;

    GeodeLock();
    c = Core(&gh);
    if (c) {
	dword need = (dword) at + numWords;
	if (need > c->GC_privWords) {
	    word *d = realloc(c->GC_privData, (size_t) need * sizeof(word));
	    if (d) {
		memset(d + c->GC_privWords, 0,
		       (size_t) (need - c->GC_privWords) * sizeof(word));
		c->GC_privData = d;
		c->GC_privWords = (word) need;
	    }
	}
	if (need <= c->GC_privWords) {
	    memcpy(c->GC_privData + at, src, numWords * sizeof(word));
	    ok = TRUE;
	}
    }
    GeodeUnlock();
    return ok;
}

/* a geode goes: its ranges in the map and its words */
void GeodePrivExit(GeodeHandle gh)
{
    GeodeCore *c = GeodeCoreOf(gh);
    word i;

    PrivP();
    for (i = 0; i < privMapWords; i++)
	if (privMap[i] == gh)
	    privMap[i] = 0;
    PrivV();
    if (c) {
	free(c->GC_privData);
	c->GC_privData = NULL;
	c->GC_privWords = 0;
    }
}
