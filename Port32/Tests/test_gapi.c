/*
 * test_gapi.c -- GAPI, the GEOS Host API (CInclude/gapi.h, Gapi/).
 *
 * The clock is set to the time it has (read again just before), so the
 * test never moves the host's clock: on linux a user without
 * CAP_SYS_TIME gets GHAL_ERR_ACCESS, root or DOS get GHAL_OK.  Bad
 * values are refused before the host is asked.
 *
 * Power: only gapi_power_can, which changes nothing.  This test never
 * calls gapi_power_do.
 */
#include "gapi.h"

#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) \
    do { if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } \
    } while (0)

int main(void)
{
    ghal_datetime dt, bad;
    ghal_status st;

    CHECK(ghal_time_get_local(&dt) == GHAL_OK);
    st = gapi_time_set_local(&dt);
    CHECK(st == GHAL_OK || st == GHAL_ERR_ACCESS);
    printf("%s: setting the clock: %s (%d) %s\n", ghal_host_name(),
	   st == GHAL_OK ? "allowed" : "not allowed", (int) st,
	   gapi_last_error());
    bad = dt;
    bad.month = 13;
    CHECK(gapi_time_set_local(&bad) == GHAL_ERR_INVAL);
    bad = dt;
    bad.hour = 24;
    CHECK(gapi_time_set_local(&bad) == GHAL_ERR_INVAL);
    CHECK(gapi_time_set_local(NULL) == GHAL_ERR_INVAL);

    {
	static const char *const names[] = {
	    "off", "reboot", "suspend", "hibernate"
	};
	int a;
	for (a = GAPI_POWER_OFF; a <= GAPI_POWER_HIBERNATE; a++) {
	    st = gapi_power_can((gapi_power_action) a);
	    CHECK(st == GHAL_OK || st == GHAL_ERR_ACCESS ||
		  st == GHAL_ERR_UNSUPPORTED);
	    printf("%s: power %s: %s %s\n", ghal_host_name(), names[a],
		   st == GHAL_OK ? "yes" : st == GHAL_ERR_ACCESS ?
		   "not allowed" : "not there", gapi_last_error());
	}
	CHECK(gapi_power_can((gapi_power_action) 9) == GHAL_ERR_INVAL);
	CHECK(gapi_host_is_geos() == !strcmp(ghal_host_name(), "dos32"));
    }

    printf("%s: %s: %d failure(s)\n", ghal_host_name(),
	   fails ? "FAIL" : "ok", fails);
    return fails != 0;
}
