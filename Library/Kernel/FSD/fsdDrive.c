/*
 * fsdDrive.c -- the drives of the system (after fsdDrive.asm and the
 * drive routines of Disk/diskHigh.asm).
 *
 * A drive has a number (0 is A:), a name, a status and the file system
 * driver that runs it (Port32/include/fsdriver.h).  A driver's drives
 * are made with FSDInitDrive (FSDInitDrive in fsdDrive.asm, called by
 * the driver as it initializes); the kernel makes the drive of the GEOS
 * top directory (FileSetTopHostPath, filePath.c).
 *
 * The drive API (DriveGetStatus, ...) is in Drive/driveHigh.c.
 *
 * Not yet: drive locks (DR_FS_DRIVE_LOCK), aliases, removing a drive.
 */
#include "kernelC.h"
#include "fsdriver.h"

#include <string.h>

static FSDDrive *drives[DRIVE_MAX_DRIVES];

Boolean FSDInitDrive(word number, const char *name, byte status,
		     MediaType media, const FSDriver *fsd, void *priv)
{
    FSDDrive *d;

    if (number >= DRIVE_MAX_DRIVES)
	return FALSE;
    d = drives[number];
    if (!d) {
	d = ghal_mem_alloc(sizeof(FSDDrive), GHAL_MEM_ZERO);
	if (!d)
	    return FALSE;
	drives[number] = d;
    }
    d->FDR_number = number;
    strncpy(d->FDR_name, name, DRIVE_NAME_MAX_LENGTH);
    d->FDR_name[DRIVE_NAME_MAX_LENGTH] = 0;
    d->FDR_status = status;
    d->FDR_extStatus = 0;
    d->FDR_media = media;
    d->FDR_fsd = fsd;
    d->FDR_private = priv;
    return TRUE;
}

FSDDrive *FSDDriveOf(word number)
{
    return number < DRIVE_MAX_DRIVES ? drives[number] : NULL;
}

sword FSDDriveByName(const char *name, word n)
{
    word i, j;

    for (i = 0; i < DRIVE_MAX_DRIVES; i++) {
	const char *dn;
	if (!drives[i])
	    continue;
	dn = drives[i]->FDR_name;
	for (j = 0; j < n; j++) {
	    char a = name[j], b = dn[j];
	    if (a >= 'a' && a <= 'z')
		a = (char) (a - 'a' + 'A');
	    if (b >= 'a' && b <= 'z')
		b = (char) (b - 'a' + 'A');
	    if (a != b)
		break;
	}
	if (j == n && !dn[n])
	    return (sword) i;
    }
    return -1;
}
