/*
 * heapHandle.c -- the handle table of the C kernel (after heapHandle.asm).
 *
 * A handle is the index of its entry times HANDLE_GRANULE (16), so 0 is
 * the null handle and handles keep their 16-bit alignment.  The table
 * grows in pages of HANDLE_PAGE entries; a page never moves, so an entry
 * stays where it is for the life of its handle.  Free handles are kept
 * on a free list (lowest handles are reused last, like the asm kernel's
 * list).  All changes are made inside ghal_crit.  Port32/ABI.md section 2,
 * Port32/PLAN.md P4.
 */
#include "kernelC.h"
#include "ghal.h"

#define HANDLE_PAGE	1024			/* entries per page */
#define HANDLE_MAX_PAGES 16384			/* 16M handles */

static HandleEntry *pages[HANDLE_MAX_PAGES];
static dword pageCount;
static Handle freeList;				/* via HE_next */
static dword used;

static HandleEntry *Entry(dword index)
{
    return &pages[index / HANDLE_PAGE][index % HANDLE_PAGE];
}

/* add a page to the free list (index 0 of page 0 is never used) */
static int GrowTable(void)
{
    HandleEntry *page;
    dword i, first;

    if (pageCount == HANDLE_MAX_PAGES)
	return 0;
    page = ghal_mem_alloc(HANDLE_PAGE * sizeof(HandleEntry), GHAL_MEM_ZERO);
    if (!page)
	return 0;
    pages[pageCount] = page;
    first = pageCount == 0 ? 1 : 0;
    /* push in reverse, so the lowest handle comes off the list first */
    for (i = HANDLE_PAGE; i-- > first; ) {
	page[i].HE_type = HT_FREE;
	page[i].HE_next = freeList;
	freeList = (Handle) ((pageCount * HANDLE_PAGE + i) * HANDLE_GRANULE);
    }
    pageCount++;
    return 1;
}

Handle HandleAlloc(byte type, GeodeHandle owner)
{
    Handle h;
    HandleEntry *he;

    ghal_crit_enter();
    /* interrupt code takes only what is free (no memory allocation) */
    if (freeList == 0 && (ghal_irq_active() || !GrowTable())) {
	ghal_crit_leave();
	return 0;
    }
    h = freeList;
    he = Entry(h / HANDLE_GRANULE);
    freeList = he->HE_next;
    he->HE_type = type;
    he->HE_flags = 0;
    he->HE_lockCount = 0;
    he->HE_allocFlags = 0;
    he->HE_owner = owner;
    he->HE_next = 0;
    he->HE_otherInfo = 0;
    he->HE_size = 0;
    he->HE_addr = NULL;
    used++;
    ghal_crit_leave();
    return h;
}

/* entry of h if it is a valid handle value, else NULL */
static HandleEntry *Lookup(Handle h)
{
    dword index = h / HANDLE_GRANULE;

    if (h == 0 || h % HANDLE_GRANULE != 0 || index >= pageCount * HANDLE_PAGE)
	return NULL;
    return Entry(index);
}

void HandleFree(Handle h)
{
    HandleEntry *he = Lookup(h);

    if (!he)
	KernelFatal(ILLEGAL_HANDLE);
    if (he->HE_type == HT_FREE)
	KernelFatal(FREEING_FREE_HANDLE);
    ghal_crit_enter();
    he->HE_type = HT_FREE;
    he->HE_addr = NULL;
    he->HE_next = freeList;
    freeList = h;
    used--;
    ghal_crit_leave();
}

HandleEntry *HandleEntryOf(Handle h)
{
    HandleEntry *he = Lookup(h);

    if (!he)
	KernelFatal(ILLEGAL_HANDLE);
    if (he->HE_type == HT_FREE)
	KernelFatal(HANDLE_FREE);
    return he;
}

HandleEntry *MemEntryOf(Handle h)
{
    HandleEntry *he = HandleEntryOf(h);

    if (he->HE_type != HT_MEM)
	KernelFatal(NOT_MEM_HANDLE);
    return he;
}

int HandleEnum(int (*fn)(Handle h, HandleEntry *he, void *ctx), void *ctx)
{
    dword i, n = pageCount * HANDLE_PAGE;
    int r;

    for (i = 1; i < n; i++) {
	HandleEntry *he = Entry(i);
	if (he->HE_type != HT_FREE &&
	    (r = fn((Handle) (i * HANDLE_GRANULE), he, ctx)) != 0)
	    return r;
    }
    return 0;
}

dword HandleCountUsed(void)
{
    return used;
}
