/*
 * driveHigh.c -- the drive API (after driveHigh.asm and driveC.asm), on
 * the drive table of FSD/fsdDrive.c.
 *
 * Not yet: drive locks.
 */
#include "kernelC.h"
#include "fsdriver.h"

#include <string.h>

word DriveGetStatus(word driveNumber)
{
    FSDDrive *d = FSDDriveOf(driveNumber);

    return d ? d->FDR_status : 0;
}

word DriveGetExtStatus(word driveNumber)
{
    FSDDrive *d = FSDDriveOf(driveNumber);

    return d ? (word) (d->FDR_status | d->FDR_extStatus) : 0;
}

MediaType DriveGetDefaultMedia(word driveNumber)
{
    FSDDrive *d = FSDDriveOf(driveNumber);

    return d ? d->FDR_media : MEDIA_NONEXISTENT;
}

Boolean DriveTestMediaSupport(word driveNumber, MediaType media)
{
    FSDDrive *d = FSDDriveOf(driveNumber);

    return d && d->FDR_media == media;
}

char *DriveGetName(word driveNumber, char *buffer, word bufferSize)
{
    FSDDrive *d = FSDDriveOf(driveNumber);
    size_t n;

    if (!d)
	return NULL;
    n = strlen(d->FDR_name);
    if (n + 1 > bufferSize)
	return NULL;
    memcpy(buffer, d->FDR_name, n + 1);
    return buffer + n;		/* at the null, as driveC.asm */
}
