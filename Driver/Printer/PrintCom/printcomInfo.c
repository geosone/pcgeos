/*
 * printcomInfo.c -- the info routines of a printer driver (the C port of
 * printcomInfo.asm): PrintGetDriverInfo, PrintDeviceInfo,
 * PrintTestDevice, PrintSetDevice, PrintSetMode, PrintSetStream and
 * PrintHomeCursor.
 *
 * Port32: the driver and device info are the driver's own data, not
 * resources; DR_PRINT_DRIVER_INFO and DR_PRINT_DEVICE_INFO return a
 * pointer in DR_dsSi (dx and si 0).  DR_PRINT_DRIVER_INFO points at the
 * PrintDriverInfo itself, not at a DriverExtendedInfoTable before it.
 */
#include "printcom.h"

Boolean PrintGetDriverInfo(DriverRegs *r)
{
    r->DR_dx = 0;
    r->DR_si = 0;
    r->DR_dsSi = (void *) &printDriverInfo;
    return FALSE;
}

Boolean PrintDeviceInfo(PState *ps, DriverRegs *r)
{
    r->DR_dx = 0;
    r->DR_si = 0;
    r->DR_dsSi = (void *) ps->PS_deviceInfo;
    return FALSE;
}

/* PrintTestDevice: no idea whether such a printer is out there */
Boolean PrintTestDevice(DriverRegs *r)
{
    r->DR_ax = DP_CANT_TELL;
    return FALSE;
}

/* PrintSetDevice: dx:si (DR_dsSi) = the device name.  As the asm's
 * EnumerateDevice: the name's characters are compared with each table
 * name in turn, so a name matches the first table name it begins. */
Boolean PrintSetDevice(PState *ps, DriverRegs *r)
{
    const char *name = r->DR_dsSi;
    word len = 0, i, k;

    if (name)
	while (name[len])
	    len++;
    for (i = 0; i < printNumDevices; i++) {
	const char *d = printDevices[i].PDE_name;
	for (k = 0; k < len && d[k] == name[k]; k++)
	    ;
	if (k == len) {
	    ps->PS_device = (PrintDevice) (i * 2);
	    ps->PS_deviceInfo = printDevices[i].PDE_info;
	    r->DR_ax = DP_PRESENT;
	    return FALSE;
	}
    }
    r->DR_ax = (word) DP_INVALID_DEVICE;
    r->DR_di = 0;
    return TRUE;
}

/* PrintSetMode: cl = the PrinterMode, ax = paper width, si = paper
 * height (points).  The text modes' translation table: the driver's
 * PDI_asciiTransChars is not a chunk in Port32; a text driver's port
 * will bring it. */
Boolean PrintSetMode(PState *ps, DriverRegs *r)
{
    word i;

    ps->PS_mode = (PrinterMode) r->DR_cx;
    ps->PS_customWidth = (word) r->DR_ax;
    ps->PS_customHeight = (word) r->DR_si;
    if (ps->PS_mode >= PM_FIRST_TEXT_MODE)
	for (i = 0; i < sizeof(ps->PS_asciiTrans); i++)
	    ps->PS_asciiTrans[i] = 0;
    return FALSE;
}

/* PrintSetStream: cx = stream token, dx = stream driver handle (0: keep
 * the strategy routine), si = PrinterPortType */
Boolean PrintSetStream(PState *ps, DriverRegs *r)
{
    ps->PS_streamToken = r->DR_cx;
    ps->PS_streamType = (word) r->DR_si;
    if (r->DR_dx) {
	DriverInfoStruct *dis = GeodeInfoDriver((GeodeHandle) r->DR_dx);
	ps->PS_streamStrategy = dis ? DriverStrategyOf(dis) : NULL;
    }
    return PrintInitStream(ps);
}

Boolean PrintHomeCursor(PState *ps, DriverRegs *r)
{
    (void) r;
    ps->PS_cursorPos.P_x = 0;
    ps->PS_cursorPos.P_y = 0;
    return FALSE;
}
