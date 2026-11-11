/*
 * fileFile.c -- operations on files and directories by name (after
 * fileFile.asm), over DR_FS_PATH_OP of the disk's driver.  Every routine
 * sets the error for ThreadGetError.
 *
 * Not yet: links, FileCopy/FileMove, the notification of file changes.
 */
#include "kernelC.h"

#include <string.h>

static word Fail(word e)
{
    KernelSetError(e);
    return e;
}

word FileDelete(const char *name)
{
    char dp[FILE_DISK_PATH_SIZE];
    FSDDisk *d = FileResolve(name, dp, sizeof(dp));

    if (!d)
	return ThreadGetError();
    return Fail(FSD_OF(d)->FSD_deleteFile(d, dp));
}

/* the new name is in the directory of the old one unless it has a
 * path; both must be on the same disk */
word FileRename(const char *oldName, const char *newName)
{
    char from[FILE_DISK_PATH_SIZE], to[FILE_DISK_PATH_SIZE];
    char rel[PATH_BUFFER_SIZE];
    const char *sep;
    FSDDisk *d, *d2;
    size_t n;

    if (strchr(newName, '\\') || strchr(newName, '/') ||
	strchr(newName, ':')) {
	if (strlen(newName) >= sizeof(rel))
	    return Fail(ERROR_PATH_TOO_LONG);
	strcpy(rel, newName);
    } else {
	sep = strrchr(oldName, '\\');
	if (!sep || (strrchr(oldName, '/') && strrchr(oldName, '/') > sep))
	    sep = strrchr(oldName, '/');
	if (!sep)
	    sep = strchr(oldName, ':');
	n = sep ? (size_t) (sep - oldName) + 1 : 0;
	if (n + strlen(newName) >= sizeof(rel))
	    return Fail(ERROR_PATH_TOO_LONG);
	memcpy(rel, oldName, n);
	strcpy(rel + n, newName);
    }
    d = FileResolve(oldName, from, sizeof(from));
    if (!d)
	return ThreadGetError();
    d2 = FileResolve(rel, to, sizeof(to));
    if (!d2)
	return ThreadGetError();
    if (d2 != d)
	return Fail(ERROR_DIFFERENT_DEVICE);
    return Fail(FSD_OF(d)->FSD_rename(d, from, to));
}

FileAttrs FileGetAttributes(const char *path)
{
    char dp[FILE_DISK_PATH_SIZE];
    FSDDisk *d = FileResolve(path, dp, sizeof(dp));
    FileAttrs a = 0;

    if (!d)
	return 0;
    Fail(FSD_OF(d)->FSD_getAttrs(d, dp, &a));
    return a;
}

word FileSetAttributes(const char *path, FileAttrs attr)
{
    char dp[FILE_DISK_PATH_SIZE];
    FSDDisk *d;

    if (attr & (FA_SUBDIR | FA_VOLUME | FA_LINK))
	return Fail(ERROR_ACCESS_DENIED);
    d = FileResolve(path, dp, sizeof(dp));
    if (!d)
	return ThreadGetError();
    return Fail(FSD_OF(d)->FSD_setAttrs(d, dp, attr));
}

word FileCreateDir(const char *name)
{
    char dp[FILE_DISK_PATH_SIZE];
    FSDDisk *d = FileResolve(name, dp, sizeof(dp));

    if (!d)
	return ThreadGetError();
    return Fail(FSD_OF(d)->FSD_createDir(d, dp));
}

/* not the current directory (the same file as "") */
word FileDeleteDir(const char *name)
{
    char dp[FILE_DISK_PATH_SIZE], cp[FILE_DISK_PATH_SIZE];
    FSDDisk *d = FileResolve(name, dp, sizeof(dp)), *cd;
    dword id, cid;

    if (!d)
	return ThreadGetError();
    cd = FileResolve("", cp, sizeof(cp));
    if (cd == d &&
	!FSD_OF(d)->FSD_pathExtAttrs(d, dp, FEA_FILE_ID, &id, sizeof(id),
				     FALSE) &&
	!FSD_OF(d)->FSD_pathExtAttrs(d, cp, FEA_FILE_ID, &cid, sizeof(cid),
				     FALSE) &&
	id == cid)
	return Fail(ERROR_IS_CURRENT_DIRECTORY);
    return Fail(FSD_OF(d)->FSD_deleteDir(d, dp));
}
