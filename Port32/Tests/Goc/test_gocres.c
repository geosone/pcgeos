/*
 * test_gocres.c -- runs the checks of gocres.goc (goc -cg output).
 *
 * The classes of the UI library and the kernel entry points the test
 * object file refers to are stubbed: only the tables are checked, no
 * message is sent.
 */
#include <geos.h>
#include <object.h>
#include <stdio.h>

ClassStruct GenProcessClass, GenApplicationClass, GenPrimaryClass,
    GenTriggerClass, GenGlyphClass;
Handle __goc_resource_handles[8];

dword CObjMessage(void) { return 0; }

extern int GocTestCheck(void);

int main(void)
{
    int fails = GocTestCheck();

    printf("%s: %d failure(s)\n", fails ? "FAIL" : "ok", fails);
    return fails != 0;
}
