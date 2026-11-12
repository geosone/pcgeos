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
 * gapi_time -- the host's clock
 * ------------------------------------------------------------------- */

/* Set the host's wall clock to a local date and time (the weekday is
 * ignored).  dos32: DOS's date and time (int 21h/2Bh, 2Dh, as the asm
 * kernel's RestoreMSDOSTime); linux: the system clock (CAP_SYS_TIME). */
ghal_status gapi_time_set_local(const ghal_datetime *dt);

#ifdef __cplusplus
}
#endif

#endif /* GAPI_H */
