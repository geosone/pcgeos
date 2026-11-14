/*
 * diskHigh.c -- disks (after diskHigh.asm, diskTable.asm and diskC.asm).
 *
 * A disk is the medium in a drive.  DiskRegisterDisk asks the drive's
 * file system driver for the ID of what it holds (DR_FS_DISK_ID) and
 * returns the disk with that ID, or registers a new one (DR_FS_DISK_INIT
 * gives its volume name).  The asm kernel's DiskHandle is the offset of
 * the DiskDesc in its FSInfoResource; here it is an even number, the
 * index in the disk table, so it can never be a StandardPath (odd).  A
 * standard path given as a disk stands for the disk of the GEOS top
 * directory.
 *
 * DiskSave and DiskRestore keep a disk in the DOS16 FSSavedDisk layout
 * (Include/Internal/fsd.def): links and state files hold it.
 *
 * Not yet: renaming, formatting and copying disks, removable media (the
 * user asked to insert a disk).
 */
#include "kernelC.h"
#include <Internal/fsDriver.h>
#include <system.h>

#include <string.h>

#define MAX_DISKS	64

static FSDDisk *disks[MAX_DISKS];

FSDDisk *FSDDiskOf(DiskHandle disk)
{
    dword i;

    if (disk & DISK_IS_STD_PATH_MASK)
	disk = FileTopDisk();
    i = disk / 2;
    if (!disk || (disk & 1) || i > MAX_DISKS)
	return NULL;
    return disks[i - 1];
}

static word Fail(word e)
{
    KernelSetError(e);
    return e;
}

/* FSDGenNameless (fsdDisk.asm): a disk without a volume name is
 * "Unnamed" and the next number (KS_UNNAMED, assocNum); a DOS16 volume
 * name is never empty, and DiskSave relies on that */
static void GenNameless(FSDDisk *d)
{
    static dword assocNum;
    char num[12];

    d->FDK_flags |= FDK_NAMELESS;
    UtilHex32ToAscii(num, ++assocNum, 0);
    strcpy(d->FDK_volume, "Unnamed");
    strncat(d->FDK_volume, num, VOLUME_NAME_LENGTH - strlen(d->FDK_volume));
}

static DiskHandle Register(word driveNumber)
{
    FSDDrive *drive = FSDDriveOf(driveNumber);
    FSDDisk *d;
    dword id;
    word e, i, free = MAX_DISKS;

    if (!drive || !(drive->FDR_status & DS_PRESENT)) {
	Fail(ERROR_INVALID_VOLUME);
	return 0;
    }
    e = drive->FDR_fsd->FSD_diskID(drive, &id);
    if (e) {
	Fail(e);
	return 0;
    }
    for (i = 0; i < MAX_DISKS; i++) {
	if (!disks[i]) {
	    if (free == MAX_DISKS)
		free = i;
	    continue;
	}
	if (disks[i]->FDK_drive == drive && disks[i]->FDK_id == id) {
	    KernelSetError(0);
	    return disks[i]->FDK_handle;
	}
    }
    if (free == MAX_DISKS) {
	Fail(ERROR_TOO_MANY_OPEN_FILES);
	return 0;
    }
    d = ghal_mem_alloc(sizeof(FSDDisk), GHAL_MEM_ZERO);
    if (!d) {
	Fail(ERROR_INSUFFICIENT_MEMORY);
	return 0;
    }
    d->FDK_handle = (DiskHandle) (2 * (free + 1));
    d->FDK_drive = drive;
    d->FDK_id = id;
    e = drive->FDR_fsd->FSD_diskInit(d);
    if (e) {
	ghal_mem_free(d);
	Fail(e);
	return 0;
    }
    if (!d->FDK_volume[0])
	GenNameless(d);
    disks[free] = d;
    KernelSetError(0);
    return d->FDK_handle;
}

DiskHandle DiskRegisterDisk(word driveNumber)
{
    return Register(driveNumber);
}

DiskHandle DiskRegisterDiskSilently(word driveNumber)
{
    return Register(driveNumber);
}

word DiskGetDrive(DiskHandle disk)
{
    FSDDisk *d = FSDDiskOf(disk);

    return d ? d->FDK_drive->FDR_number : 0;
}

void DiskGetVolumeName(DiskHandle disk, char *buffer)
{
    FSDDisk *d = FSDDiskOf(disk);

    strcpy(buffer, d ? d->FDK_volume : "");
}

word DiskGetVolumeInfo(DiskHandle dh, DiskInfoStruct *info)
{
    FSDDisk *d = FSDDiskOf(dh);

    if (!d)
	return Fail(ERROR_INVALID_VOLUME);
    return Fail(d->FDK_drive->FDR_fsd->FSD_diskInfo(d, info));
}

dword DiskGetVolumeFreeSpace(DiskHandle dh)
{
    DiskInfoStruct info;

    return DiskGetVolumeInfo(dh, &info) ? 0 : (dword) info.DIS_freeSpace;
}

word DiskSetVolumeName(DiskHandle dh, const char *name)
{
    (void) name;
    return Fail(FSDDiskOf(dh) ? ERROR_UNSUPPORTED_FUNCTION
			      : ERROR_INVALID_VOLUME);
}

Boolean DiskCheckWritable(DiskHandle disk)
{
    FSDDisk *d = FSDDiskOf(disk);

    return d && (d->FDK_flags & FDK_WRITABLE);
}

Boolean DiskCheckInUse(DiskHandle disk)
{
    FSDDisk *d = FSDDiskOf(disk);

    return d && d->FDK_inUse;
}

Boolean DiskCheckUnnamed(DiskHandle disk)
{
    FSDDisk *d = FSDDiskOf(disk);

    return d && (d->FDK_flags & FDK_NAMELESS);
}

DiskHandle DiskForEach(Boolean (*callback)(DiskHandle disk))
{
    word i;

    for (i = 0; i < MAX_DISKS; i++)
	if (disks[i] && callback(disks[i]->FDK_handle))
	    return disks[i]->FDK_handle;
    return 0;
}

/* the disk with volume name `fname' (case does not matter) */
DiskHandle DiskFind(const char *fname, DiskFindResult *code)
{
    DiskHandle found = 0;
    word i, n = 0;

    for (i = 0; i < MAX_DISKS; i++) {
	const char *a = fname, *b;
	if (!disks[i])
	    continue;
	b = disks[i]->FDK_volume;
	while (*a && *b && ((*a ^ *b) & ~0x20) == 0)
	    a++, b++;
	if (!*a && !*b) {
	    if (!found)
		found = disks[i]->FDK_handle;
	    n++;
	}
    }
    *code = n == 0 ? DFR_NOT_FOUND : n == 1 ? DFR_UNIQUE : DFR_NOT_UNIQUE;
    KernelSetError(0);
    return found;
}

/* ---------------------------------------------------------------------
 * Saved disks (DiskSave, DiskRestore): FSSavedDisk, little-endian and
 * packed as in DOS16 --
 *	FSSD_name	char[VOLUME_NAME_LENGTH + 1]	the volume name
 *	FSSD_flags	DiskFlags (DF_NAMELESS 4, DF_WRITABLE 2,
 *			DF_ALWAYS_VALID 1)
 *	FSSD_media	MediaType
 *	FSSD_ifsName	char[GEODE_NAME_SIZE]	the driver; 0 the primary
 *	FSSD_id		dword
 *	FSSD_private	word	the offset of the driver's data
 *	FSSD_driveName	"name:", null-terminated
 * A standard path is FSSavedStdPath: a 0 byte, the StandardPath word.
 * The file system drivers here save nothing of their own.
 * ------------------------------------------------------------------- */

#define FSSD_NAME	0
#define FSSD_FLAGS	(VOLUME_NAME_LENGTH + 1)
#define FSSD_MEDIA	(FSSD_FLAGS + 1)
#define FSSD_IFS_NAME	(FSSD_MEDIA + 1)
#define FSSD_ID		(FSSD_IFS_NAME + GEODE_NAME_SIZE)
#define FSSD_PRIVATE	(FSSD_ID + 4)
#define FSSD_DRIVE_NAME	(FSSD_PRIVATE + 2)
#define FSSSP_SIZE	3
#define DF_NAMELESS_BIT	0x04
#define DF_WRITABLE_BIT	0x02
#define DF_ALWAYS_VALID_BIT 0x01

Boolean DiskSave(DiskHandle disk, void *buffer, word *bufSizePtr)
{
    byte *b = buffer;
    FSDDisk *d;
    FSDDrive *top;
    word need, n;

    if (disk & DISK_IS_STD_PATH_MASK) {
	if (*bufSizePtr < FSSSP_SIZE) {
	    *bufSizePtr = FSSSP_SIZE;
	    return FALSE;
	}
	b[0] = 0;
	b[1] = (byte) disk;
	b[2] = (byte) (disk >> 8);
	*bufSizePtr = FSSSP_SIZE;
	return TRUE;
    }
    d = FSDDiskOf(disk);
    if (!d) {
	*bufSizePtr = 0;		/* cannot be saved */
	return FALSE;
    }
    n = (word) strlen(d->FDK_drive->FDR_name);
    need = (word) (FSSD_DRIVE_NAME + n + 2);
    if (*bufSizePtr < need) {
	*bufSizePtr = need;
	return FALSE;
    }
    memset(b, 0, FSSD_DRIVE_NAME);
    strncpy((char *) b + FSSD_NAME, d->FDK_volume, VOLUME_NAME_LENGTH);
    b[FSSD_FLAGS] = (byte) (((d->FDK_flags & FDK_WRITABLE) ?
			     DF_WRITABLE_BIT : 0) |
			    ((d->FDK_flags & FDK_NAMELESS) ?
			     DF_NAMELESS_BIT : 0) |
			    ((d->FDK_drive->FDR_status & DS_MEDIA_REMOVABLE) ?
			     0 : DF_ALWAYS_VALID_BIT));
    b[FSSD_MEDIA] = d->FDK_drive->FDR_media;
    top = FSDDiskOf(FileTopDisk()) ? FSDDiskOf(FileTopDisk())->FDK_drive
				     : NULL;	/* the primary's */
    if (!top || top->FDR_fsd != d->FDK_drive->FDR_fsd) {
	const char *fn = d->FDK_drive->FDR_fsd->FSD_name;
	word i;
	for (i = 0; i < GEODE_NAME_SIZE; i++)
	    b[FSSD_IFS_NAME + i] = (byte) (*fn ? *fn++ : ' ');
    }
    b[FSSD_ID] = (byte) d->FDK_id;
    b[FSSD_ID + 1] = (byte) (d->FDK_id >> 8);
    b[FSSD_ID + 2] = (byte) (d->FDK_id >> 16);
    b[FSSD_ID + 3] = (byte) (d->FDK_id >> 24);
    b[FSSD_PRIVATE] = (byte) need;
    b[FSSD_PRIVATE + 1] = (byte) (need >> 8);
    memcpy(b + FSSD_DRIVE_NAME, d->FDK_drive->FDR_name, n);
    b[FSSD_DRIVE_NAME + n] = ':';
    b[FSSD_DRIVE_NAME + n + 1] = 0;
    *bufSizePtr = need;
    return TRUE;
}

/* the disk of the saved data: the known disk of its drive with its ID,
 * else the one the drive holds (a fixed drive's, whatever its ID; a
 * removable drive's if its ID matches, else the callback asks for it).
 * 0 with the thread's error the DiskRestoreError if it cannot be. */
DiskHandle DiskRestore(void *buffer,
		       DiskRestoreError (*callback)(const char *driveName,
						    const char *diskName,
						    void **bufferPtr,
						    DiskRestoreError error))
{
    const byte *b = buffer;
    const char *dn;
    FSDDrive *drive;
    DiskHandle dh;
    DiskRestoreError r;
    dword id;
    sword dr;
    word i;

    if (!b[0]) {
	KernelSetError(0);
	return (DiskHandle) (b[1] | (b[2] << 8));
    }
    for (;;) {
	dn = (const char *) b + FSSD_DRIVE_NAME;
	id = (dword) b[FSSD_ID] | ((dword) b[FSSD_ID + 1] << 8) |
	     ((dword) b[FSSD_ID + 2] << 16) | ((dword) b[FSSD_ID + 3] << 24);
	for (i = 0; dn[i] && dn[i] != ':'; i++)
	    ;
	dr = FSDDriveByName(dn, i);
	drive = dr < 0 ? NULL : FSDDriveOf((word) dr);
	if (!drive) {
	    KernelSetError(DRE_DRIVE_NO_LONGER_EXISTS);
	    return 0;
	}
	for (i = 0; i < MAX_DISKS; i++)
	    if (disks[i] && disks[i]->FDK_drive == drive &&
		disks[i]->FDK_id == id) {
		KernelSetError(0);
		return disks[i]->FDK_handle;
	    }
	dh = Register((word) dr);
	if (dh && (!(drive->FDR_status & DS_MEDIA_REMOVABLE) ||
		   FSDDiskOf(dh)->FDK_id == id)) {
	    KernelSetError(0);
	    return dh;
	}
	if (!dh) {
	    KernelSetError(DRE_COULDNT_CREATE_NEW_DISK_HANDLE);
	    return 0;
	}
	if (!callback) {
	    KernelSetError(DRE_REMOVABLE_DRIVE_DOESNT_HOLD_DISK);
	    return 0;
	}
	r = callback(dn, (const char *) b + FSSD_NAME, (void **) &b,
		     DRE_REMOVABLE_DRIVE_DOESNT_HOLD_DISK);
	if (r != DRE_DISK_IN_DRIVE) {
	    KernelSetError(r);
	    return 0;
	}
    }
}
