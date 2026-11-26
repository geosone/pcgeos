/*
 * heapC.c -- after heapC.asm: MemPtrToHandle.
 */
#include "kernelC.h"
#include "ghal.h"
#include <string.h>

typedef struct {
    const byte *ptr;
    MemHandle found;
} PtrSearch;

static int FindPtr(Handle h, HandleEntry *he, void *ctx)
{
    PtrSearch *s = ctx;
    const byte *start = he->HE_addr;

    if (he->HE_type == HT_MEM && start && s->ptr >= start &&
	s->ptr < start + (he->HE_size ? he->HE_size : 1)) {
	s->found = h;
	return 1;
    }
    return 0;
}

MemHandle MemPtrToHandle(void *ptr)
{
    PtrSearch s;

    s.ptr = ptr;
    s.found = 0;
    HandleEnum(FindPtr, &s);
    return s.found;
}
