/*
 * fileFile.c -- operations on files and directories by name (after
 * fileFile.asm).  Every routine sets the error for ThreadGetError.
 *
 * Not yet: extended attributes, links, FileCopy/FileMove, the
 * notification of file changes.
 */
#include "kernelC.h"
#include "ghal.h"

#include <string.h>

static word Fail(word e)
{
    KernelSetError(e);
    return e;
}

static word Status(ghal_status s, const char *host)
{
    return Fail(FileErrorOf(s, host));
}

word FileDelete(const char *name)
{
    char host[PATH_BUFFER_SIZE];

    if (!FileHostPath(name, host, sizeof(host)))
	return ThreadGetError();
    return Status(ghal_fs_delete(host), host);
}

/* the new name is in the directory of the old one unless it has a path */
word FileRename(const char *oldName, const char *newName)
{
    char from[PATH_BUFFER_SIZE], to[PATH_BUFFER_SIZE];
    const char *sep;

    if (!FileHostPath(oldName, from, sizeof(from)))
	return ThreadGetError();
    if (strchr(newName, '\\') || strchr(newName, '/')) {
	if (!FileHostPath(newName, to, sizeof(to)))
	    return ThreadGetError();
    } else {
	char rel[PATH_BUFFER_SIZE];
	sep = strrchr(oldName, '\\');
	if (!sep || (strrchr(oldName, '/') && strrchr(oldName, '/') > sep))
	    sep = strrchr(oldName, '/');
	if (sep) {
	    size_t n = (size_t) (sep - oldName) + 1;
	    if (n + strlen(newName) >= sizeof(rel))
		return Fail(ERROR_PATH_TOO_LONG);
	    memcpy(rel, oldName, n);
	    strcpy(rel + n, newName);
	} else {
	    if (strlen(newName) >= sizeof(rel))
		return Fail(ERROR_PATH_TOO_LONG);
	    strcpy(rel, newName);
	}
	if (!FileHostPath(rel, to, sizeof(to)))
	    return ThreadGetError();
    }
    return Status(ghal_fs_rename(from, to), from);
}

FileAttrs FileGetAttributes(const char *path)
{
    char host[PATH_BUFFER_SIZE];
    ghal_fs_info info;
    ghal_status s;

    if (!FileHostPath(path, host, sizeof(host)))
	return 0;
    s = ghal_fs_stat(host, &info);
    Status(s, host);
    if (s != GHAL_OK)
	return 0;
    /* the GHAL attribute bits are the DOS ones */
    return (FileAttrs) (info.attrs & (FA_RDONLY | FA_HIDDEN | FA_SYSTEM |
				      FA_SUBDIR | FA_ARCHIVE));
}

word FileSetAttributes(const char *path, FileAttrs attr)
{
    char host[PATH_BUFFER_SIZE];

    if (attr & (FA_SUBDIR | FA_VOLUME | FA_LINK))
	return Fail(ERROR_ACCESS_DENIED);
    if (!FileHostPath(path, host, sizeof(host)))
	return ThreadGetError();
    return Status(ghal_fs_set_attrs(host, attr), host);
}

word FileCreateDir(const char *name)
{
    char host[PATH_BUFFER_SIZE];

    if (!FileHostPath(name, host, sizeof(host)))
	return ThreadGetError();
    return Status(ghal_fs_mkdir(host), host);
}

word FileDeleteDir(const char *name)
{
    char host[PATH_BUFFER_SIZE], cur[PATH_BUFFER_SIZE];

    if (!FileHostPath(name, host, sizeof(host)) ||
	!FileHostPath("", cur, sizeof(cur)))
	return ThreadGetError();
    if (!strcmp(host, cur))
	return Fail(ERROR_IS_CURRENT_DIRECTORY);
    return Status(ghal_fs_rmdir(host), host);
}
