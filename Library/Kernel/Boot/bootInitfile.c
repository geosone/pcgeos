/*
 * bootInitfile.c -- after bootInitfile.asm: BootLoadDriver,
 * BootLoadKeyboardDriver.
 */
#include "kernelC.h"
#include "gapi.h"
#include <initfile.h>
#include <system.h>
#include <driver.h>
#include <Internal/kbdDr.h>
#include <Internal/videoDr.h>
#include <Internal/powerDr.h>
#include <string.h>

/* LoadDriver of bootInitfile.asm: a driver from a standard path */
GeodeHandle BootLoadDriver(const char *name, StandardPath sp,
			      word major, word minor)
{
    GeodeLoadError e;
    GeodeHandle gh;

    FilePushDir();
    FileSetStandardPath(sp);
    gh = GeodeUseDriver(name, major, minor, &e);
    FilePopDir();
    return gh;
}

void BootLoadKeyboardDriver(void)
{
    char name[FILE_LONGNAME_BUFFER_SIZE];
    word n;
    GeodeHandle gh;

    if (InitFileReadStringBuffer("keyboard", "driver", name, sizeof(name),
				 &n))
	return;
    gh = BootLoadDriver(name, SP_KEYBOARD_DRIVERS, KEYBOARD_PROTO_MAJOR,
		    KEYBOARD_PROTO_MINOR);
    if (!gh)
	gh = BootLoadDriver("kbd.geo", SP_KEYBOARD_DRIVERS, KEYBOARD_PROTO_MAJOR,
			KEYBOARD_PROTO_MINOR);
    if (!gh)
	KernelFatal(CANNOT_LOAD_KEYBOARD_DRIVER);
    GeodeSetDefaultDriver(GDDT_KEYBOARD, gh);
}
