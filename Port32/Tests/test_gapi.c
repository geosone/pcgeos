/*
 * test_gapi.c -- GAPI, the GEOS Host API (CInclude/gapi.h, Gapi/).
 *
 * The clock is set to the time it has (read again just before), so the
 * test never moves the host's clock: on linux a user without
 * CAP_SYS_TIME gets GHAL_ERR_ACCESS, root or DOS get GHAL_OK.  Bad
 * values are refused before the host is asked.
 */
#include "gapi.h"

#include <stdio.h>

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

    printf("%s: %s: %d failure(s)\n", ghal_host_name(),
	   fails ? "FAIL" : "ok", fails);
    return fails != 0;
}
