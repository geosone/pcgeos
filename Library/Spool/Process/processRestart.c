/*
 * processRestart.c -- the spooler's start (the C port of SpoolBirth of
 * processRestart.asm).
 *
 * Port32: the state file of a detach (spoolsta.dat) is not written yet,
 * so there are no jobs to restart: the spool files of an earlier session
 * are deleted, as the asm did when there was no state file (the
 * spool???.dat files and the spool???.ps files of print-to-file).  Fax
 * files are not built (SpoolNukeFaxFiles).
 */
#include "spoolLocal.h"
#include <string.h>

static void SpoolNukeFiles(const char *pattern)
{
    FileEnumParams p;
    MemHandle mh = 0;
    FileLongName *names;
    word n, i;

    memset(&p, 0, sizeof(p));
    p.FEP_searchFlags = FILE_ENUM_ALL_FILE_TYPES | FESF_CALLBACK;
    p.FEP_returnAttrs = (void *) FESRT_NAME;
    p.FEP_returnSize = sizeof(FileLongName);
    p.FEP_bufSize = FE_BUFSIZE_UNLIMITED;
    p.FEP_callback = (void *) FESC_WILDCARD;
    p.FEP_cbData1 = (uintptr_t) pattern;
    p.FEP_cbData2 = TRUE;
    n = FileEnum(&p, &mh, NULL);
    if (!mh)
	return;
    names = MemLock(mh);
    for (i = 0; i < n; i++)
	FileDelete(names[i]);
    MemFree(mh);
}

void SpoolBirth(void)
{
    FilePushDir();
    FileSetStandardPath(SP_SPOOL);
    FileDelete("spoolsta.dat");
    SpoolNukeFiles("spool???.dat");
    SpoolNukeFiles("spool???.ps");
    FilePopDir();
}
