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

/* ---------------------------------------------------------------------
 * gapi_print -- the host's printers
 *
 * GEOS renders a job itself (PostScript, later PDF) and hands it to a
 * queue of the host, which does the rest (Port32/GAPI-Concept.md §5).
 * linux: CUPS over IPP on its socket ($CUPS_SERVER if it is a path,
 * else /run/cups/cups.sock).  dos32: none (GEOS's printer drivers write
 * to LPT, COM or a file themselves).
 * ------------------------------------------------------------------- */

#define GAPI_PRINT_NAME_SIZE 128

typedef struct gapi_printer {
    char name[GAPI_PRINT_NAME_SIZE];        /* the host's queue */
    char info[GAPI_PRINT_NAME_SIZE];        /* the name the user sees */
    char location[GAPI_PRINT_NAME_SIZE];
    char makeModel[GAPI_PRINT_NAME_SIZE];
    int  isDefault;
    int  accepting;         /* takes jobs */
    int  state;             /* 3 idle, 4 printing, 5 stopped */
    int  color;
    int  duplex;            /* can print on both sides */
} gapi_printer;

/* The host's printers, at most max of them; *count is how many there
 * are (may be more than max). */
ghal_status gapi_print_list(gapi_printer *list, int max, int *count);
/* The name of the default printer (LPDEST/PRINTER, else the host's);
 * GHAL_ERR_NOTFOUND if there is none. */
ghal_status gapi_print_default(char *name, size_t size);

typedef enum gapi_doc_format {
    GAPI_DOC_POSTSCRIPT,    /* application/postscript */
    GAPI_DOC_PDF            /* application/pdf */
} gapi_doc_format;

typedef struct gapi_print_job gapi_print_job;

/* A job to a printer: open it, write the document as it is made, close
 * it to print (with the host's job id), or cancel it. */
ghal_status gapi_print_job_open(const char *printer, const char *title,
                                gapi_doc_format format, int copies,
                                int duplex, gapi_print_job **job);
ghal_status gapi_print_job_write(gapi_print_job *job, const void *data,
                                 size_t size);
ghal_status gapi_print_job_close(gapi_print_job *job, uint32_t *jobId);
void        gapi_print_job_cancel(gapi_print_job *job);

#ifdef __cplusplus
}
#endif

#endif /* GAPI_H */
