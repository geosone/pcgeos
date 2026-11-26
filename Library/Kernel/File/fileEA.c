/*
 * fileEA.c -- Port32: what the extended attribute routines of fileFile.c
 * and fileIO.c share, over
 * FSHOF_GET/SET_EXT_ATTRIBUTES and FSPOF_GET/SET_EXT_ATTRIBUTES of the
 * disk's driver: what they are and where they are kept is the driver's
 * (HostFS: the GeosFileHeader, Driver/IFS/HostFS/hostfsDriver.c).
 */
#include "kernelC.h"
#include "fileMacro.h"

#include <string.h>

word FileEADone(word e)
{
    KernelSetError(e);
    return e;
}

Boolean FileDiskAttr(FSDDisk *d, FileExtendedAttribute attr,
			void **bufferP, word *sizeP, FileExtAttrDesc *rest,
			MemHandle *mhP, word *e)
{
    FileExtAttrDesc *a = *bufferP, *r = rest;
    word i, n = 0, disks = 0;

    *mhP = 0;
    *e = 0;
    if (attr == FEA_DISK) {
	if (*sizeP < sizeof(DiskHandle))
	    *e = ERROR_ATTR_SIZE_MISMATCH;
	else
	    *(DiskHandle *) *bufferP = d->FDK_handle;
	return TRUE;
    }
    if (attr != FEA_MULTIPLE)
	return FALSE;
    for (i = 0; i < *sizeP; i++)
	disks += a[i].FEAD_attr == FEA_DISK;
    if (!disks)
	return FALSE;
    if (*sizeP - disks > EA_REST) {
	*mhP = MemAlloc((*sizeP - disks) * sizeof(FileExtAttrDesc),
			HF_SWAPABLE, HAF_LOCK);
	if (!*mhP) {
	    *e = ERROR_INSUFFICIENT_MEMORY;
	    return TRUE;
	}
	r = MemDeref(*mhP);
    }
    for (i = 0; i < *sizeP; i++) {
	if (a[i].FEAD_attr != FEA_DISK)
	    r[n++] = a[i];
	else if (a[i].FEAD_size < sizeof(DiskHandle))
	    *e = ERROR_ATTR_SIZE_MISMATCH;
	else
	    *(DiskHandle *) a[i].FEAD_value = d->FDK_handle;
    }
    if (*e || !n) {
	if (*mhP)
	    MemFree(*mhP);
	*mhP = 0;
	return TRUE;
    }
    *bufferP = r;
    *sizeP = n;
    return FALSE;
}

/* ---------------------------------------------------------------------
 * All the attributes that can be set (FSHOF/FSPOF_GET_ALL_EXT_ATTRIBUTES,
 * as the DOS driver's dosvceaAllAttrs): a locked block of
 * FileExtAttrDesc with the values after them; a file that is no GEOS
 * file has only its modification time and FileAttrs.
 * ------------------------------------------------------------------- */

