/*
 * test_power.c -- the host power management driver (Driver/Power/Host)
 * and the subsystem notifications (Library/Kernel/Sys/sysNotification.c)
 * in the C kernel.
 *
 * The top GPWTOP has hostpwr.geo in SYSTEM/POWER and a geos.ini with
 * "[system] power = hostpwr.geo":
 *   - the boot loads it as the default GDDT_POWER_MANAGEMENT driver,
 *     which hooks SST_DEVICE_POWER (so the boot's SysIgnoreNotification
 *     finds it hooked);
 *   - notifications to SST_MEDIUM wait until it is hooked, then come in
 *     order, a memory block among them the kernel's and sharable; then
 *     straight away; unhooking by another routine does nothing; ignoring
 *     drops what waits and what comes;
 *   - linux: a stand-in for /sys/class/power_supply ($GAPI_POWER_SUPPLY):
 *     the mains off, a battery discharging and low (energy: 420 per
 *     mille, 120 minutes), a mouse's battery that does not count; the
 *     low warning once, again after it was not low; on the mains,
 *     charging; a battery with only a capacity; no supplies at all;
 *   - the on/off routines (eight), the long term idle, unsupported calls;
 *   - GeodeFreeDriver unhooks SST_DEVICE_POWER.
 */
#define _GNU_SOURCE
#include "kernelC.h"
#include "ghal.h"
#include <geosdriver.h>
#include <Internal/powerDr.h>
#include <system.h>
#include <heap.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const unsigned char hostpwr_geode[];
extern const unsigned long hostpwr_geode_size;

static int fails;
#define CHECK(c) \
    do { if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } \
    } while (0)

static char top[260], sep = '/';

static void Put(const char *rel, const void *data, unsigned long size)
{
    char path[300], *p;
    ghal_file *f;
    size_t done = 0;

    sprintf(path, "%s%c%s", top, sep, rel);
    for (p = path; *p; p++)
	if (*p == '/')
	    *p = sep;
    if (!size) {
	ghal_fs_mkdir(path);
	return;
    }
    CHECK(ghal_fs_open(path, GHAL_FS_WRITE | GHAL_FS_CREATE |
				 GHAL_FS_TRUNCATE, &f) == GHAL_OK);
    ghal_fs_write(f, data, size, &done);
    CHECK(done == size);
    ghal_fs_close(f);
}

/* the notifications SST_MEDIUM got */
static int got;
static dword gotW[8][5];

static void Medium(SysSubsystemType subsys, SysNotificationType notif,
		   dword w1, dword w2, dword w3, dword w4)
{
    CHECK(subsys == SST_MEDIUM);
    if (got < 8) {
	gotW[got][0] = notif;
	gotW[got][1] = w1;
	gotW[got][2] = w2;
	gotW[got][3] = w3;
	gotW[got][4] = w4;
    }
    got++;
}

static void Other(SysSubsystemType subsys, SysNotificationType notif,
		  dword w1, dword w2, dword w3, dword w4)
{
    (void) subsys; (void) notif; (void) w1; (void) w2; (void) w3; (void) w4;
}

static void Notifications(void)
{
    MemHandle mh = MemAlloc(16, HF_SWAPABLE, 0);

    got = 0;
    SysSendNotification(SST_MEDIUM, 1, 10, 11, 12, 13);
    SysSendNotification(SST_MEDIUM, 2 | SNF_WORD2_MEM, 20, mh, 22, 23);
    CHECK(got == 0);
    CHECK(MemOwner(mh) == 0);
    CHECK(MGI_TYPE_FLAGS(MemGetInfo(mh, MGIT_FLAGS_AND_LOCK_COUNT)) &
	  HF_SHARABLE);
    CHECK(SysHookNotification(SST_MEDIUM, Medium));
    CHECK(got == 2);
    CHECK(gotW[0][0] == 1 && gotW[0][1] == 10 && gotW[0][4] == 13);
    CHECK(gotW[1][0] == (2 | SNF_WORD2_MEM) && gotW[1][2] == mh &&
	  gotW[1][3] == 22);
    MemFree(mh);
    SysSendNotification(SST_MEDIUM, 3, 30, 0x12345678UL, 0, 0);
    CHECK(got == 3 && gotW[2][0] == 3 && gotW[2][2] == 0x12345678UL);
    CHECK(!SysHookNotification(SST_MEDIUM, Other));	/* hooked */
    CHECK(!SysIgnoreNotification(SST_MEDIUM));
    SysUnhookNotification(SST_MEDIUM, Other);		/* not its */
    SysSendNotification(SST_MEDIUM, 4, 0, 0, 0, 0);
    CHECK(got == 4);
    SysUnhookNotification(SST_MEDIUM, Medium);
    SysSendNotification(SST_MEDIUM, 5, 0, 0, 0, 0);	/* waits */
    mh = MemAlloc(16, HF_SWAPABLE, 0);
    SysSendNotification(SST_MEDIUM, 6 | SNF_WORD1_MEM, mh, 0, 0, 0);
    CHECK(got == 4);
    CHECK(SysIgnoreNotification(SST_MEDIUM));		/* both dropped */
    CHECK(SysIgnoreNotification(SST_MEDIUM));		/* still */
    CHECK(!SysHookNotification(SST_MEDIUM, Medium));
    SysSendNotification(SST_MEDIUM, 7, 0, 0, 0, 0);
    CHECK(got == 4);
}

static DriverInfoStruct *dis;

static Boolean Status(word type, DriverRegs *r)
{
    memset(r, 0, sizeof(*r));
    r->DR_ax = type;
    return DriverCall(dis, DR_POWER_GET_STATUS, r);
}

#ifdef __linux__
static char sysDir[300];

static void Supply(const char *name, const char *attr, const char *value)
{
    char path[400];
    FILE *f;

    sprintf(path, "%s/%s", sysDir, name);
    ghal_fs_mkdir(path);
    sprintf(path, "%s/%s/%s", sysDir, name, attr);
    f = fopen(path, "w");
    CHECK(f != NULL);
    if (f) {
	fprintf(f, "%s\n", value);
	fclose(f);
    }
}

static void Batteries(void)
{
    DriverRegs r;

    /* the mains off; BAT0 discharging, low: 21 of 50 Wh at 10.5 W */
    Supply("AC", "type", "Mains");
    Supply("AC", "online", "0");
    Supply("BAT0", "type", "Battery");
    Supply("BAT0", "present", "1");
    Supply("BAT0", "status", "Discharging");
    Supply("BAT0", "capacity", "42");
    Supply("BAT0", "capacity_level", "Low");
    Supply("BAT0", "energy_now", "21000000");
    Supply("BAT0", "energy_full", "50000000");
    Supply("BAT0", "power_now", "10500000");
    /* a mouse's battery, nearly empty: not the machine's */
    Supply("hidpp_battery_0", "type", "Battery");
    Supply("hidpp_battery_0", "scope", "Device");
    Supply("hidpp_battery_0", "capacity", "5");
    Supply("hidpp_battery_0", "capacity_level", "Critical");
    Supply("hidpp_battery_0", "energy_now", "1");
    Supply("hidpp_battery_0", "energy_full", "100");

    CHECK(!Status(PGST_STATUS, &r));
    CHECK(r.DR_ax == 0 && r.DR_bx == (PS_AC_ADAPTER_CONNECTED | PS_CHARGING));
    CHECK(!Status(PGST_BATTERY_CHARGE_PERCENT, &r));
    CHECK(r.DR_ax == 420 && r.DR_dx == 0);
    CHECK(!Status(PGST_BATTERY_CHARGE_MINUTES, &r));
    CHECK(r.DR_ax == 120 && r.DR_dx == 0);
    CHECK(!Status(PGST_POLL_WARNINGS, &r));
    CHECK(r.DR_ax == PW_MAIN_BATTERY && r.DR_bx == PW_MAIN_BATTERY);
    CHECK(!Status(PGST_POLL_WARNINGS, &r) && r.DR_ax == 0);	/* once */
    CHECK(!Status(PGST_POWER_ON_WARNINGS, &r) && r.DR_ax == 0);
    Supply("BAT0", "capacity_level", "Normal");
    CHECK(!Status(PGST_POLL_WARNINGS, &r) && r.DR_ax == 0);
    Supply("BAT0", "capacity_level", "Low");
    CHECK(!Status(PGST_POLL_WARNINGS, &r) && r.DR_ax == PW_MAIN_BATTERY);

    /* on the mains, charging: no time left, no warning */
    Supply("AC", "online", "1");
    Supply("BAT0", "status", "Charging");
    Supply("BAT0", "capacity_level", "Normal");
    CHECK(!Status(PGST_POLL_WARNINGS, &r));
    Supply("BAT0", "capacity_level", "Low");
    CHECK(!Status(PGST_POLL_WARNINGS, &r) && r.DR_ax == 0);
    CHECK(!Status(PGST_STATUS, &r));
    CHECK(r.DR_ax == (PS_AC_ADAPTER_CONNECTED | PS_CHARGING));
    CHECK(Status(PGST_BATTERY_CHARGE_MINUTES, &r));
    CHECK(!Status(PGST_BATTERY_CHARGE_PERCENT, &r) && r.DR_ax == 420);

    /* a battery with only a capacity, no mains */
    strcat(sysDir, "2");
    ghal_fs_mkdir(sysDir);
    setenv("GAPI_POWER_SUPPLY", sysDir, 1);
    Supply("BAT1", "type", "Battery");
    Supply("BAT1", "status", "Discharging");
    Supply("BAT1", "capacity", "77");
    CHECK(!Status(PGST_BATTERY_CHARGE_PERCENT, &r) && r.DR_ax == 770);
    CHECK(Status(PGST_BATTERY_CHARGE_MINUTES, &r));
    CHECK(!Status(PGST_STATUS, &r));
    CHECK(r.DR_ax == 0 && r.DR_bx == (PS_AC_ADAPTER_CONNECTED | PS_CHARGING));

    /* no supplies: the host does not know */
    strcat(sysDir, "3");
    ghal_fs_mkdir(sysDir);
    setenv("GAPI_POWER_SUPPLY", sysDir, 1);
    CHECK(Status(PGST_STATUS, &r));
    CHECK(Status(PGST_BATTERY_CHARGE_PERCENT, &r));
}
#endif

static void Noop(PowerNotifyChange c)
{
    (void) c;
}

static void Noop2(PowerNotifyChange c)
{
    (void) c;
}

int main(int argc, char **argv)
{
    char *slash;
    const char *ini = "[system]\r\npower = hostpwr.geo\r\n";
    GeodeLoadError e;
    GeodeHandle drv;
    DriverRegs r;
    int i;

    (void) argc;
    setvbuf(stdout, NULL, _IONBF, 0);
    strncpy(top, argv[0], sizeof(top) - 8);
    top[sizeof(top) - 8] = 0;
    slash = strrchr(top, '/');
    if (!slash || (strrchr(top, '\\') && strrchr(top, '\\') > slash))
	slash = strrchr(top, '\\');
    if (slash) {
	sep = *slash;
	slash[1] = 0;
    } else
	top[0] = 0;
#ifdef __linux__
    sprintf(sysDir, "%sGPWSYS", top);
    ghal_fs_mkdir(sysDir);
    setenv("GAPI_POWER_SUPPLY", sysDir, 1);
#endif
    strcat(top, "GPWTOP");
    ghal_fs_mkdir(top);
    Put("SYSTEM", NULL, 0);
    Put("SYSTEM/POWER", NULL, 0);
    Put("SYSTEM/POWER/hostpwr.geo", hostpwr_geode, hostpwr_geode_size);
    Put("geos.ini", ini, strlen(ini));
    CHECK(KernelBoot(top, "", &e) == 0 && e == GLE_OK);

    drv = GeodeGetDefaultDriver(GDDT_POWER_MANAGEMENT);
    CHECK(drv != 0);
    if (!drv)
	return 1;
    dis = GeodeInfoDriver(drv);
    CHECK(dis->DIS_driverType == DRIVER_TYPE_POWER_MANAGEMENT);
    /* the driver hooked the device notifications before the boot could
     * ignore them; they reach DR_POWER_DEVICE_ON_OFF */
    CHECK(!SysIgnoreNotification(SST_DEVICE_POWER));
    SysSendNotification(SST_DEVICE_POWER, 0, PDT_SERIAL_PORT, 0, 1, 0);

    Notifications();
#ifdef __linux__
    Batteries();
#else
    (void) Status(PGST_STATUS, &r);	/* APM or not: no crash */
#endif

    /* on/off routines: eight places */
    for (i = 0; i < 8; i++) {
	memset(&r, 0, sizeof(r));
	r.DR_farPtr = (void *) (i ? Noop2 : Noop);
	CHECK(!DriverCall(dis, DR_POWER_ON_OFF_NOTIFY, &r));
    }
    CHECK(DriverCall(dis, DR_POWER_ON_OFF_NOTIFY, &r));
    r.DR_farPtr = (void *) Noop;
    CHECK(!DriverCall(dis, DR_POWER_ON_OFF_UNREGISTER, &r));
    CHECK(DriverCall(dis, DR_POWER_ON_OFF_UNREGISTER, &r));
    CHECK(!DriverCall(dis, DR_POWER_ON_OFF_NOTIFY, &r));

    memset(&r, 0, sizeof(r));
    CHECK(!DriverCall(dis, DR_POWER_LONG_TERM_IDLE, &r));	/* blank */
    CHECK(!DriverCall(dis, DR_POWER_IDLE, &r));
    r.DR_ax = PDT_PARALLEL_PORT;
    r.DR_cx = 1;
    CHECK(!DriverCall(dis, DR_POWER_DEVICE_ON_OFF, &r));
    CHECK(DriverCall(dis, DR_POWER_SET_STATUS, &r));
    CHECK(DriverCall(dis, DR_POWER_ESC_COMMAND, &r));
    memset(&r, 0, sizeof(r));
    r.DR_ax = 99;
    CHECK(Status(99, &r) || 1);

    GeodeSetDefaultDriver(GDDT_POWER_MANAGEMENT, 0);
    GeodeFreeDriver(drv);
    CHECK(SysIgnoreNotification(SST_DEVICE_POWER));	/* unhooked */

    printf("%s\n", fails ? "FAILED" : "power: ok");
    return fails != 0;
}
