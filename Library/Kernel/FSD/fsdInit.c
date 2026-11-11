/*
 * fsdInit.c -- the primary file system driver (after LoadFSDriver of
 * fsdInit.asm).
 *
 * DOS16 GEOS loads the driver that "[system] primaryFSD" of the init
 * file names, or else one chosen by the DOS version.  In Port32 HostFS
 * is the primary driver on every host, and primaryFSD chooses the layout
 * of its host names (Port32/hostfs/hostfs.h):
 *	mslf.geo			DOS names with long names (MSLF), or
 *					DOS names where the host has none
 *	ms3, ms4, ms7, dri, os2,
 *	ntfat (.geo, also ...ec.geo)	DOS 8.3 names
 *	hostfs.geo			long names
 * Without the key, or with another driver, the layout is the host's:
 * long names, on dos32 DOS names with or without LFN as DOS has it.
 * Reading the init file does not depend on the layout: HostFS finds a
 * name in any of them.
 *
 * The drives of the primary driver: DOS16's driver made one for each DOS
 * drive.  HostFS serves the top directory as drive C, and
 *	[hostfs]
 *	drives = D=/srv/geos E=/mnt/cd
 * gives it more: a letter, "=", a host directory, separated by blanks
 * or line ends (a host path cannot hold a blank).  [paths] may then
 * name directories on them.
 *
 * Not yet: the drivers of "[system] fs" and the secondary drivers
 * (network, CD-ROM).
 */
#include "kernelC.h"
#include "hostfs.h"

#include <initfile.h>
#include <string.h>

static const struct {
    const char	*name;
    int		layout;
} layouts[] = {
    { "mslf", HOSTFS_NAMES_DOS_LFN },
    { "ms3", HOSTFS_NAMES_DOS },
    { "ms4", HOSTFS_NAMES_DOS },
    { "ms7", HOSTFS_NAMES_DOS },
    { "dri", HOSTFS_NAMES_DOS },
    { "os2", HOSTFS_NAMES_DOS },
    { "ntfat", HOSTFS_NAMES_DOS },
    { "hostfs", HOSTFS_NAMES_LONG },
};

/* the layout for the driver file `name' (a path, any case, "ec" or not) */
static int LayoutOf(const char *name)
{
    char base[16];
    const char *p;
    word i, n = 0;

    for (p = name; *p; p++)
	if (*p == '\\' || *p == '/' || *p == ':')
	    name = p + 1;
    while (name[n] && name[n] != '.' && n < sizeof(base) - 1) {
	char c = name[n];
	base[n++] = (char) (c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    }
    base[n] = 0;
    for (i = 0; i < sizeof(layouts) / sizeof(layouts[0]); i++) {
	word ln = (word) strlen(layouts[i].name);
	if (!strcmp(base, layouts[i].name) ||
	    (n == ln + 2 && !strncmp(base, layouts[i].name, ln) &&
	     !strcmp(base + ln, "ec")))
	    return layouts[i].layout;
    }
    return HOSTFS_NAMES_AUTO;
}

#define TOP_DRIVE	2		/* C: (filePath.c) */

/* one "X=hostpath" of [hostfs] drives */
static void HostDrive(const char *item, word len)
{
    char path[256], name[2];
    word number;
    FSDDrive *old;
    void *priv;
    char c = item[0];

    if (c >= 'a' && c <= 'z')
	c = (char) (c - 'a' + 'A');
    if (len < 3 || c < 'A' || c > 'Z' || item[1] != '=' ||
	(size_t) (len - 2) >= sizeof(path))
	return;
    number = (word) (c - 'A');
    if (number == TOP_DRIVE)
	return;
    memcpy(path, item + 2, len - 2);
    path[len - 2] = 0;
    priv = HostFSDriveData(path);
    if (!priv)
	return;
    old = FSDDriveOf(number);
    if (old && old->FDR_fsd == &hostFSDriver)
	HostFSFreeDriveData(old->FDR_private);
    name[0] = c;
    name[1] = 0;
    if (!FSDInitDrive(number, name, DS_PRESENT | DRIVE_FIXED,
		      MEDIA_FIXED_DISK, &hostFSDriver, priv))
	HostFSFreeDriveData(priv);
}

static void HostDrives(void)
{
    MemHandle mh;
    word n, i, start;
    const char *t;

    if (InitFileReadStringBlock("hostfs", "drives", &mh, IFRF_READ_ALL, &n))
	return;
    t = MemLock(mh);
    for (i = 0; i < n; ) {
	while (i < n && (byte) t[i] <= ' ')
	    i++;
	start = i;
	while (i < n && (byte) t[i] > ' ')
	    i++;
	if (i > start)
	    HostDrive(t + start, (word) (i - start));
    }
    MemFree(mh);
}

void FSDLoadPrimary(void)
{
    char name[64];
    word n;
    int layout = HOSTFS_NAMES_AUTO;

    if (!InitFileReadStringBuffer("system", "primaryFSD", name,
				  sizeof(name), &n))
	layout = LayoutOf(name);
    if (layout == HOSTFS_NAMES_DOS_LFN && !HostFSHostLongNames())
	layout = HOSTFS_NAMES_DOS;
    HostFSSetDosNames(layout);
    HostDrives();
}
