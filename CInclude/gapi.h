/*
 * gapi.h -- GEOS Host API, C interface (draft, API 1).
 *
 * Where ghal.h gives GEOS what it needs to run on a host (clocks to read,
 * memory, contexts, display, input, files), GAPI is the system
 * management GEOS offers its user as an operating system of its own:
 * setting the clock, printers, power, network, locale, drives.  Under DOS
 * GEOS does these itself (DOS, BIOS, its drivers); on a hosted system
 * they belong to the host, and GAPI hands them to the host's own
 * services.  Design: Port32/GAPI-Concept.md.
 *
 * Conventions (as ghal.h, whose ghal_status and ghal_datetime it uses):
 *   - GHAL_ERR_UNSUPPORTED: this host has no such function;
 *     GHAL_ERR_ACCESS: the host refuses it (no privilege).  The caller
 *     then falls back to GEOS's own way or reports "managed by the host".
 *   - Only the kernel and system libraries call GAPI, never applications.
 */
#ifndef GAPI_H
#define GAPI_H

#include "ghal.h"

#ifdef __cplusplus
extern "C" {
#endif

#define GAPI_API_VERSION 1

/* ---------------------------------------------------------------------
 * gapi_session -- how GEOS stands to the host
 * ------------------------------------------------------------------- */

/* May the host ask the user (for a password: polkit on linux) when a
 * change needs more rights?  Off by default; on when GEOS is the user's
 * session ([system] hostRole = session).  A call that asks blocks until
 * the user answered. */
void gapi_set_interactive(int allow);

/* Send ghal_log to the host's system log (linux: the journal, with
 * SYSLOG_IDENTIFIER=geos).  GHAL_ERR_UNSUPPORTED where there is none
 * (dos32); the log then stays where it was. */
ghal_status gapi_log_to_system(void);

/* Is GEOS the system on this host (dos32: yes, there is nothing else),
 * or one program of it (linux)?  The default of [system] hostRole. */
int gapi_host_is_geos(void);

/* What the host said about the last refused or failed call (a D-Bus
 * error name and message, ...), for the log; "" if nothing. */
const char *gapi_last_error(void);

/* ---------------------------------------------------------------------
 * gapi_time -- the host's clock
 * ------------------------------------------------------------------- */

/* Set the host's wall clock to a local date and time (the weekday is
 * ignored).  dos32: DOS's date and time (int 21h/2Bh, 2Dh, as the asm
 * kernel's RestoreMSDOSTime).  linux: systemd-timedated's SetTime over
 * D-Bus (polkit decides, and asks when interactive); without timedated,
 * clock_settime (CAP_SYS_TIME).  GHAL_ERR_ACCESS also when the host
 * synchronizes its clock itself (NTP on). */
ghal_status gapi_time_set_local(const ghal_datetime *dt);

/* ---------------------------------------------------------------------
 * gapi_power -- switching the machine off, rebooting, suspending
 * ------------------------------------------------------------------- */

typedef enum gapi_power_action {
    GAPI_POWER_OFF,
    GAPI_POWER_REBOOT,
    GAPI_POWER_SUSPEND,
    GAPI_POWER_HIBERNATE
} gapi_power_action;

/* Could the action be done now?  GHAL_OK: yes (when interactive, the
 * host may still ask for a password); GHAL_ERR_ACCESS: not without
 * rights; GHAL_ERR_UNSUPPORTED: the machine cannot.  Changes nothing. */
ghal_status gapi_power_can(gapi_power_action a);

/* Do it.  dos32: reboot through the keyboard controller (as EndGeos of
 * bootBoot.asm), off through APM; neither returns when it works.  linux:
 * org.freedesktop.login1 (PowerOff, Reboot, Suspend, Hibernate), which
 * returns: the host then ends the session and GEOS with it. */
ghal_status gapi_power_do(gapi_power_action a);

#ifdef __cplusplus
}
#endif

#endif /* GAPI_H */
