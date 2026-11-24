/*
 * test_geosport.c -- base types of GEOS in C (CInclude/geosport.h,
 * reached through CInclude/geos.h with GEOS_PORT32 defined).
 *
 * Same widths on every host: 32/64-bit Linux, dos32 (Open Watcom 386),
 * later Windows (LLP64) and GonWare.
 */
#include <geos.h>
#include <stdio.h>

#define CHECK_SIZE(type, n) \
    typedef char check_##type[(sizeof(type) == (n)) ? 1 : -1]

CHECK_SIZE(byte, 1);
CHECK_SIZE(sbyte, 1);
CHECK_SIZE(word, 2);
CHECK_SIZE(sword, 2);
CHECK_SIZE(dword, 4);
CHECK_SIZE(sdword, 4);
CHECK_SIZE(wchar, 2);
CHECK_SIZE(Boolean, 2);
CHECK_SIZE(Handle, 4);
CHECK_SIZE(MemHandle, 4);
CHECK_SIZE(ChunkHandle, 2);
CHECK_SIZE(optr, 8);
CHECK_SIZE(Message, 4);
CHECK_SIZE(WWFixedAsDWord, 4);
CHECK_SIZE(WWFixed, 4);

static int fails;

static void check(int ok, const char *what)
{
    if (!ok) {
        fails++;
        printf("FAIL %s\n", what);
    }
}

int main(void)
{
    MemHandle h = 0x89abcdefu;
    ChunkHandle ch = 0xfedc;
    optr o = ConstructOptr(h, ch);

    check(OptrToHandle(o) == h, "OptrToHandle");
    check(OptrToChunk(o) == ch, "OptrToChunk");
    check(HandleToOptr(h) == ConstructOptr(h, 0), "HandleToOptr");
    check(OptrToChunk(HandleToOptr(h)) == NullChunk, "HandleToOptr chunk");
    check(ConstructOptr(NullHandle, NullChunk) == NullOptr, "NullOptr");
    check((sword) -1 < 0 && (sdword) -1 < 0, "signed types");
    check(TRUE != FALSE, "TRUE/FALSE");
    check(NULL == (void *) 0, "NULL");
    printf("%s: %d failure(s)\n", fails ? "FAIL" : "ok", fails);
    return fails != 0;
}
