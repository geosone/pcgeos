/*
 * fileFile.c -- operations on files and directories by name (after
 * fileFile.asm).  Every routine sets the error for ThreadGetError.
 *
 * Not yet: links, FileCopy/FileMove, the notification of file changes.
 */
#include "kernelC.h"
#include "ghal.h"
#include "hostfs.h"

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
    char host[HOST_PATH_SIZE];

    if (!FileHostPath(name, host, sizeof(host)))
	return ThreadGetError();
    return Status(ghal_fs_delete(host), host);
}

/* the new name is in the directory of the old one unless it has a
 * path.  A GEOS file or a directory with @DIRNAME.000 gets the new name
 * in its header; with DOS names (hostfs) its host name stays when it
 * stays in its directory, as on DOS16. */
word FileRename(const char *oldName, const char *newName)
{
    char from[HOST_PATH_SIZE], dirName[HOST_PATH_SIZE];
    char rel[PATH_BUFFER_SIZE];
    const char *sep;
    ghal_fs_info info;
    ghal_status s;
    FileTarget *t;
    Boolean dir, geos, hasDirName, sameDir;
    word e = 0;
    size_t n;

    if (!FileHostPath(oldName, from, sizeof(from)))
	return ThreadGetError();
    s = ghal_fs_stat(from, &info);
    if (s != GHAL_OK)
	return Status(s, from);
    if (strchr(newName, '\\') || strchr(newName, '/')) {
	if (strlen(newName) >= sizeof(rel))
	    return Fail(ERROR_PATH_TOO_LONG);
	strcpy(rel, newName);
    } else {
	sep = strrchr(oldName, '\\');
	if (!sep || (strrchr(oldName, '/') && strrchr(oldName, '/') > sep))
	    sep = strrchr(oldName, '/');
	n = sep ? (size_t) (sep - oldName) + 1 : 0;
	if (n + strlen(newName) >= sizeof(rel))
	    return Fail(ERROR_PATH_TOO_LONG);
	memcpy(rel, oldName, n);
	strcpy(rel + n, newName);
    }
    dir = (info.attrs & GHAL_FS_ATTR_DIR) != 0;
    hasDirName = dir && HostFSJoin(dirName, sizeof(dirName), from,
				   HOSTFS_DIRNAME_FILE) &&
		 ghal_fs_stat(dirName, NULL) == GHAL_OK;
    {
	unsigned char hdr[GFH_SIZE];
	geos = !dir && HostFSReadHeader(from, hdr) == GHAL_OK;
    }
    t = ghal_mem_alloc(sizeof(FileTarget), 0);
    if (!t)
	return Fail(ERROR_INSUFFICIENT_MEMORY);
    if (!FileResolveNew(rel, dir ? HOSTFS_DIR : geos ? HOSTFS_GEOS
				 : HOSTFS_NATIVE, t)) {
	e = ThreadGetError();
	goto done;
    }
    if (t->FT_exists && strcmp(t->FT_host, from)) {
	e = ERROR_FILE_EXISTS;
	goto done;
    }
    n = strlen(t->FT_dir);
    sameDir = !strncmp(from, t->FT_dir, n) && from[n] == HostFSSep() &&
	      !strchr(from + n + 1, HostFSSep());
    if (!(HostFSDosNames() && sameDir && (geos || hasDirName)) &&
	strcmp(t->FT_host, from)) {
	s = ghal_fs_rename(from, t->FT_host);
	if (s != GHAL_OK) {
	    e = FileErrorOf(s, from);
	    goto done;
	}
	strcpy(from, t->FT_host);
    }
    if (geos)
	s = HostFSSetName(from, t->FT_name);
    else if (hasDirName || t->FT_dirName) {
	if (!HostFSJoin(dirName, sizeof(dirName), from, HOSTFS_DIRNAME_FILE))
	    s = GHAL_ERR_INVAL;
	else if (hasDirName)
	    s = HostFSSetName(dirName, t->FT_name);
	else
	    s = HostFSWriteDirName(from, t->FT_name, FileNow());
    }
    e = FileErrorOf(s, NULL);
done:
    ghal_mem_free(t);
    return Fail(e);
}

FileAttrs FileGetAttributes(const char *path)
{
    char host[HOST_PATH_SIZE];
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
    char host[HOST_PATH_SIZE];

    if (attr & (FA_SUBDIR | FA_VOLUME | FA_LINK))
	return Fail(ERROR_ACCESS_DENIED);
    if (!FileHostPath(path, host, sizeof(host)))
	return ThreadGetError();
    return Status(ghal_fs_set_attrs(host, attr), host);
}

/* with DOS names a directory whose name is no DOS name gets a
 * generated one and a @DIRNAME.000 with its GEOS name (dosPath.asm) */
word FileCreateDir(const char *name)
{
    FileTarget *t = ghal_mem_alloc(sizeof(FileTarget), 0);
    ghal_status s;
    word e;

    if (!t)
	return Fail(ERROR_INSUFFICIENT_MEMORY);
    if (!FileResolveNew(name, HOSTFS_DIR, t))
	e = ThreadGetError();
    else if (t->FT_exists)
	e = ERROR_FILE_EXISTS;
    else {
	s = ghal_fs_mkdir(t->FT_host);
	if (s == GHAL_OK && t->FT_dirName) {
	    s = HostFSWriteDirName(t->FT_host, t->FT_name, FileNow());
	    if (s != GHAL_OK)
		ghal_fs_rmdir(t->FT_host);
	}
	e = FileErrorOf(s, t->FT_host);
    }
    ghal_mem_free(t);
    return Fail(e);
}

/* a directory holding only its @DIRNAME.000 is empty */
word FileDeleteDir(const char *name)
{
    char host[HOST_PATH_SIZE], cur[HOST_PATH_SIZE], dn[HOST_PATH_SIZE];
    char entry[HOSTFS_NAME_MAX];
    ghal_dir *d;
    ghal_status s;
    Boolean other = FALSE, hasDirName = FALSE;

    if (!FileHostPath(name, host, sizeof(host)) ||
	!FileHostPath("", cur, sizeof(cur)))
	return ThreadGetError();
    if (!strcmp(host, cur))
	return Fail(ERROR_IS_CURRENT_DIRECTORY);
    s = ghal_fs_opendir(host, &d);
    if (s != GHAL_OK)
	return Status(s, host);
    while (ghal_fs_readdir(d, entry, sizeof(entry), NULL) == GHAL_OK) {
	if (HostFSIsDirNameFile(entry))
	    hasDirName = TRUE;
	else
	    other = TRUE;
    }
    ghal_fs_closedir(d);
    if (other)
	return Fail(ERROR_DIRECTORY_NOT_EMPTY);
    if (hasDirName && HostFSJoin(dn, sizeof(dn), host, HOSTFS_DIRNAME_FILE))
	ghal_fs_delete(dn);
    return Status(ghal_fs_rmdir(host), host);
}
