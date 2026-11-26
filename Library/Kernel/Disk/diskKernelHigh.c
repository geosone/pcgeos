/*
 * diskKernelHigh.c -- after diskKernelHigh.asm: DiskCheckWritable.
 */
#include "kernelC.h"
#include <Internal/fsDriver.h>
#include <system.h>
#include <string.h>

Boolean DiskCheckWritable(DiskHandle disk)
{
    FSDDisk *d = FSDDiskOf(disk);

    return d && (d->FDK_flags & FDK_WRITABLE);
}
