/*
 * hostPower.c -- the host power management driver.  No asm original:
 * DOS16 had a driver per machine (APM BIOSes, palmtops, NoPower as the
 * template, Driver/Power); on a host the machine's power is the host's,
 * and GAPI (gapi_power_status, CInclude/gapi.h) says what it is: the
 * mains, the batteries, how much is left.  dos32 asks APM through GAPI,
 * as the APM drivers asked the BIOS.
 *
 * Functions (Internal/powerDr.h):
 *   DR_POWER_GET_STATUS   the status from GAPI, read on each call
 *     PGST_POWER_ON_WARNINGS, PGST_POLL_WARNINGS  PW_MAIN_BATTERY once
 *                         when the battery became low (the host says
 *                         so), again only after it was not low
 *     PGST_STATUS         PS_AC_ADAPTER_CONNECTED, PS_CHARGING; bx has
 *                         the bits the host knows
 *     PGST_BATTERY_CHARGE_MINUTES, _PERCENT  dx:ax; carry if unknown
 *   DR_POWER_DEVICE_ON_OFF  every device may be on (the host powers
 *                         them); also through SysSendNotification
 *                         (SST_DEVICE_POWER), which DR_INIT hooks, as
 *                         PowerInit did
 *   DR_POWER_ON_OFF_NOTIFY, DR_POWER_ON_OFF_UNREGISTER  the routines
 *                         are kept (eight, as most DOS16 drivers)
 *   DR_POWER_LONG_TERM_IDLE  carry clear: the screen saver blanks the
 *                         screen; the host decides when to sleep
 *   the idle calls, DR_POWER_LONG_TERM_IDLE_END  nothing to do
 *   DR_POWER_SET_STATUS, the passwords, DR_POWER_ESC_COMMAND  carry:
 *                         not supported
 *
 * Not yet: the warning to the user (DOS16 polled the battery from a timer
 * and showed a dialog in the UI thread), which comes with the UI
 * library; calling the on/off routines when the host suspends.
 */
#include <geos.h>
#include <driver.h>
#include <geosdriver.h>
#include <system.h>
#include <Internal/powerDr.h>
#include <gapi.h>
#include <string.h>

static Boolean HostPowerStrategy(word function, DriverRegs *r);

DriverInfoStruct DriverTable = {
    DRIVER_STRATEGY(HostPowerStrategy), 0, DRIVER_TYPE_POWER_MANAGEMENT
};

#define MAX_NOTIFY	8

static PowerNotifyRoutine *notify[MAX_NOTIFY];
static Boolean warned;

/* SST_DEVICE_POWER: DR_POWER_DEVICE_ON_OFF with the four words */
static void DeviceNotification(SysSubsystemType subsys,
			       SysNotificationType notif, dword ax, dword bx,
			       dword cx, dword dx)
{
    DriverRegs r;

    (void) subsys;
    (void) notif;
    memset(&r, 0, sizeof(r));
    r.DR_ax = ax;
    r.DR_bx = bx;
    r.DR_cx = cx;
    r.DR_dx = dx;
    (void) HostPowerStrategy(DR_POWER_DEVICE_ON_OFF, &r);
}

static Boolean GetStatus(DriverRegs *r)
{
    gapi_power_info s;
    dword v;

    if (gapi_power_status(&s) != GHAL_OK)
	return TRUE;
    switch ((word) r->DR_ax) {
    case PGST_POWER_ON_WARNINGS:
    case PGST_POLL_WARNINGS:
	r->DR_ax = 0;
	r->DR_bx = s.battery ? PW_MAIN_BATTERY : 0;
	if (s.battery && s.low && s.ac != 1) {
	    if (!warned)
		r->DR_ax = PW_MAIN_BATTERY;
	    warned = TRUE;
	} else
	    warned = FALSE;
	return FALSE;
    case PGST_STATUS:
	r->DR_ax = r->DR_bx = 0;
	if (s.ac >= 0)
	    r->DR_bx |= PS_AC_ADAPTER_CONNECTED;
	if (s.ac == 1)
	    r->DR_ax |= PS_AC_ADAPTER_CONNECTED;
	if (s.charging >= 0)
	    r->DR_bx |= PS_CHARGING;
	if (s.charging == 1)
	    r->DR_ax |= PS_CHARGING;
	return FALSE;
    case PGST_BATTERY_CHARGE_MINUTES:
	if (!s.battery || s.minutes < 0)
	    return TRUE;
	v = (dword) s.minutes;
	break;
    case PGST_BATTERY_CHARGE_PERCENT:
	if (!s.battery || s.permille < 0)
	    return TRUE;
	v = (dword) s.permille;
	break;
    default:
	return TRUE;
    }
    r->DR_ax = v & 0xffff;
    r->DR_dx = v >> 16;
    return FALSE;
}

static Boolean Register(PowerNotifyRoutine *fn)
{
    int i;

    for (i = 0; i < MAX_NOTIFY; i++)
	if (!notify[i]) {
	    notify[i] = fn;
	    return FALSE;
	}
    return TRUE;
}

static Boolean Unregister(PowerNotifyRoutine *fn)
{
    int i;

    for (i = 0; i < MAX_NOTIFY; i++)
	if (notify[i] == fn) {
	    notify[i] = NULL;
	    return FALSE;
	}
    return TRUE;
}

static Boolean HostPowerStrategy(word function, DriverRegs *r)
{
    switch (function) {
    case DR_INIT:
	(void) SysHookNotification(SST_DEVICE_POWER, DeviceNotification);
	return FALSE;
    case DR_EXIT:
	SysUnhookNotification(SST_DEVICE_POWER, DeviceNotification);
	return FALSE;
    case DR_SUSPEND:
    case DR_UNSUSPEND:
    case DR_POWER_IDLE:
    case DR_POWER_NOT_IDLE:
    case DR_POWER_NOT_IDLE_ON_INTERRUPT_COMPLETION:
    case DR_POWER_LONG_TERM_IDLE:
    case DR_POWER_LONG_TERM_IDLE_END:
	return FALSE;
    case DR_POWER_GET_STATUS:
	return GetStatus(r);
    case DR_POWER_DEVICE_ON_OFF:
	return FALSE;
    case DR_POWER_ON_OFF_NOTIFY:
	return Register((PowerNotifyRoutine *) r->DR_farPtr);
    case DR_POWER_ON_OFF_UNREGISTER:
	return Unregister((PowerNotifyRoutine *) r->DR_farPtr);
    default:
	return TRUE;
    }
}
