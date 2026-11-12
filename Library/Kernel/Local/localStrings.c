/*
 * localStrings.c -- the strings of the localization code (the LocalStrings
 * resource of localStrings.asm; LockStringsDS, UnlockStrings and
 * StoreResourceString of dateTimeFields.asm, SetResourceString of
 * dateTimeFormat.asm, LocalGetStringAsData and LocalWriteStringAsData of
 * numericFormats.asm).
 *
 * The defaults are the asm's chunks, made C at build time
 * (localDefaultStrings[], Port32/tools/localgen.py); the copies here are
 * what the Set routines and the init file change.  A lock that nests,
 * as MemThreadGrab of the asm, guards them.
 */
#include "kernelC.h"
#include "localInt.h"

#include <initfile.h>
#include <string.h>

const char localizationCategory[] = "localization";

static char strings[LS_COUNT][LOCAL_STRING_SIZE];
static Boolean loaded;
/* MemThreadGrab of the resource: it nests for the thread that has it */
static KSem stringsSem = { 1, 0 };
static ThreadHandle stringsOwner;
static word stringsNesting;

/* the defaults the first time */
static void Load(void)
{
    word i;

    for (i = 0; i < LS_COUNT; i++) {
	strncpy(strings[i], localDefaultStrings[i], LOCAL_STRING_SIZE - 1);
	strings[i][LOCAL_STRING_SIZE - 1] = 0;
    }
    loaded = TRUE;
}

void LocalLockStrings(void)
{
    ThreadHandle me = KernelThisThread();

    if (stringsNesting && stringsOwner == me) {
	stringsNesting++;
	return;
    }
    KSemP(&stringsSem, KSEM_FOREVER);
    stringsOwner = me;
    stringsNesting = 1;
    if (!loaded)
	Load();
}

void LocalUnlockStrings(void)
{
    EC_CHECK(stringsNesting && stringsOwner == KernelThisThread(),
	     BAD_UNLOCK);
    if (!--stringsNesting) {
	stringsOwner = 0;
	KSemV(&stringsSem);
    }
}

char *LocalString(word id)
{
    EC_CHECK(id < LS_COUNT, LOCAL_BAD_STRING);
    return strings[id];
}

void LocalGetString(word id, char *buf, word size)
{
    LocalLockStrings();
    strncpy(buf, LocalString(id), size - 1);
    buf[size - 1] = 0;
    LocalUnlockStrings();
}

void LocalSetString(word id, const char *str)
{
    LocalLockStrings();
    strncpy(LocalString(id), str, LOCAL_STRING_SIZE - 1);
    LocalUnlockStrings();
}

byte LocalStringChar(word id)
{
    byte c;

    LocalLockStrings();
    c = (byte) LocalString(id)[0];
    LocalUnlockStrings();
    return c;
}

Boolean LocalReadStringAsData(const char *key, word id, word size)
{
    char buf[LOCAL_STRING_SIZE];
    word n;

    if (size > sizeof(buf) - 1)
	size = sizeof(buf) - 1;
    if (InitFileReadDataBuffer(localizationCategory, key, buf, size, &n))
	return FALSE;
    buf[n < size ? n : size] = 0;
    LocalSetString(id, buf);
    return TRUE;
}

void LocalWriteStringAsData(const char *key, word id)
{
    char buf[LOCAL_STRING_SIZE];

    LocalGetString(id, buf, sizeof(buf));
    InitFileWriteData(localizationCategory, key, buf, (word) strlen(buf));
}
