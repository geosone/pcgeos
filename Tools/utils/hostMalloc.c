/*
 * hostMalloc.c -- what the tools ask of their own malloc (malloc.c),
 * for the builds that use the host's (Tools/CMakeLists.txt).
 */
#include <stddef.h>

/*
 * The size of a block; 0 for one that malloc did not allocate.  The
 * parsers ask to tell their first, static stack from a later one: saying
 * 0 makes them allocate a new one and copy, which is always right.
 */
unsigned
malloc_size(void *ptr)
{
    (void)ptr;
    return 0;
}

/* Errors of malloc are fatal or not: the host's just returns NULL. */
void
malloc_noerr(int noerr)
{
    (void)noerr;
}
