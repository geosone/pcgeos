/*
 * kgdrv.c -- a driver for the kernel.geode test: a C strategy routine
 * (CInclude/geosdriver.h) that counts DR_INIT and DR_EXIT, refuses
 * DR_INIT with cx = 0xdead, and adds ax and bx into cx for function
 * KGDRV_ADD.
 */
#include <geos.h>
#include <geode.h>
#include <geosdriver.h>

#define KGDRV_ADD	100

typedef struct {
    word	KGDC_init, KGDC_exit;
    GeodeHandle	KGDC_handle;	/* bx of DR_INIT */
    word	KGDC_value1, KGDC_value2;
} KGDrvCounts;

static KGDrvCounts counts;

static Boolean KGDrvStrategy(word function, DriverRegs *r)
{
    switch (function) {
    case DR_INIT:
	if (r->DR_cx == 0xdead)
	    return TRUE;
	counts.KGDC_init++;
	counts.KGDC_handle = (GeodeHandle) r->DR_bx;
	counts.KGDC_value1 = (word) r->DR_cx;
	counts.KGDC_value2 = (word) r->DR_dx;
	return FALSE;
    case DR_EXIT:
	counts.KGDC_exit++;
	return FALSE;
    case KGDRV_ADD:
	r->DR_cx = r->DR_ax + r->DR_bx;
	return FALSE;
    default:
	return TRUE;
    }
}

DriverInfoStruct DriverTable = {
    DRIVER_STRATEGY(KGDrvStrategy), DA_CHARACTER, DRIVER_TYPE_OTHER_PROCESSOR
};

KGDrvCounts *KGDrvGetCounts(void)
{
    return &counts;
}
