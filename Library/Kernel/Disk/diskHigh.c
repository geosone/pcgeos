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
 * Not yet: DiskSave/DiskRestore, renaming, formatting and copying disks,
 * removable media (the user asked to insert a disk).
 */
#include "kernelC.h"
#include <Internal/fsDriver.h>

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

    return d && !d->FDK_volume[0];
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
