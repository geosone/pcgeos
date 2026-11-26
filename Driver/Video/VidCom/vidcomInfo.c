/*
 * vidcomInfo.c -- after vidcomInfo.asm: This file contains the code to
 * supply/return info about the specific video device being supported by
 * the driver.
 */
#include "vidcom.h"
#include <ghal.h>
#include <thread.h>
#include <sem.h>

Boolean VidInit(void)
{
    if (!videoSem)
	videoSem = ThreadAllocSem(1);
    return VidDevInit();
}

void VidExit(void)
{
    VidResetXOR();
    VidUnderReset();
    VidDevExit();
    DriverTable.VDI_device = VID_NO_DEVICE;
    if (videoSem)
	ThreadFreeSem(videoSem);
    videoSem = 0;
}

Boolean VidSetDevice(DriverRegs *r)
{
    if (VidDevSet(r->DR_dsSi)) {
	r->DR_ax = (word) DP_INVALID_DEVICE;
	return TRUE;
    }
    DriverTable.VDI_device = 0;
    VidDevSetPtr(NULL);			/* start hidden, with the default */
    r->DR_ax = DP_PRESENT;
    return FALSE;
}
