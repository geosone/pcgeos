/*
 * threadPrivate.c -- words of private data in every thread (after
 * threadPrivate.asm).
 *
 * ThreadPrivAlloc gives a geode a range of words that each thread has for
 * itself, first fit; ThreadPrivFree gives it back, and the words of a
 * geode that exits go with it.  The asm kept them at the end of the
 * ThreadPrivateData at the bottom of each stack (read at ss:offset); a
 * Port32 thread has them in its KThread, read with ThreadPrivData.  An
 * offset is 2 * (index + 1), never 0 (0 means no room, as the C stub
 * returned).
 */
#include "kernelC.h"
#include "ghal.h"

#include <thread.h>

/* the owner geode of each word (kTPD.TPD_heap), 0 for free */
static GeodeHandle owners[THREAD_PRIV_WORDS];

#define OFFSET_OF(i)	((word) (2 * ((i) + 1)))
#define INDEX_OF(off)	((word) ((off) / 2 - 1))

word ThreadPrivAlloc(word wordsRequested, GeodeHandle owner)
{
    word i, n;

    if (!wordsRequested || wordsRequested > THREAD_PRIV_WORDS)
	return 0;
    ghal_crit_enter();
    for (i = 0; i + wordsRequested <= THREAD_PRIV_WORDS; i++) {
	for (n = 0; n < wordsRequested && !owners[i + n]; n++)
	    ;
	if (n == wordsRequested) {
	    for (n = 0; n < wordsRequested; n++)
		owners[i + n] = owner;
	    ghal_crit_leave();
	    return OFFSET_OF(i);
	}
    }
    ghal_crit_leave();
    return 0;
}

void ThreadPrivFree(word range, word wordsRequested)
{
    word i = INDEX_OF(range), n;

    if (range < 2 || range & 1 || i + wordsRequested > THREAD_PRIV_WORDS)
	KernelFatal(THREAD_PRIVATE_DATA_NOT_ALLOCATED);
    for (n = 0; n < wordsRequested; n++) {	/* (the asm checked first) */
	EC_CHECK(owners[i + n], THREAD_PRIVATE_DATA_NOT_ALLOCATED);
	EC_CHECK(owners[i + n] == owners[i],
		 THREAD_PRIVATE_DATA_OWNED_BY_SOMEONE_ELSE);
    }
    ghal_crit_enter();
    for (n = 0; n < wordsRequested; n++)
	owners[i + n] = 0;
    ghal_crit_leave();
}

void ThreadPrivExit(GeodeHandle gh)
{
    word i;

    ghal_crit_enter();
    for (i = 0; i < THREAD_PRIV_WORDS; i++)
	if (owners[i] == gh)
	    owners[i] = 0;
    ghal_crit_leave();
}

word *ThreadPrivData(word offset)
{
    word i = INDEX_OF(offset);

    if (offset < 2 || offset & 1 || i >= THREAD_PRIV_WORDS)
	KernelFatal(THREAD_PRIVATE_DATA_NOT_ALLOCATED);
    return &ThreadOf(KernelThisThread())->T_priv[i];
}
