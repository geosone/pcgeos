/*
 * processCustom.c -- a custom port (the C port of processCustom.asm):
 * a stream driver named by the printer's category (e.g. cups.geo), which
 * loads its options from that category and gets the port's CPP_info
 * when it opens.  With the build options of the default spooler the
 * data of a canceled job is still sent (no
 * _KILL_CUSTOM_PORT_DATA_ON_CANCEL).
 */
#include "spoolLocal.h"
#include <string.h>

#define CUSTOM_PARAMS(j)	((j)->SJI_info.JP_portInfo.PPI_params.PP_custom)

/* InitCustomPort: TRUE and a PortErrors on an error.  The asm asked the
 * user to retry; SpoolErrorBox says IC_DISMISS without the UI. */
Boolean InitCustomPort(SpoolJobInfo *j, word *portError)
{
    DriverRegs r;
    MemHandle info;
    Boolean err;

    memset(&r, 0, sizeof(r));
    r.DR_dsSi = j->SJI_info.JP_printerName;
    CallPort(j, STREAM_ESC_LOAD_OPTIONS, &r);

    for (;;) {
	info = MemAlloc(sizeof(CUSTOM_PARAMS(j).CPP_info),
			HF_DYNAMIC | HF_SHARABLE, HAF_NO_ERR | HAF_LOCK);
	memcpy(MemDeref(info), CUSTOM_PARAMS(j).CPP_info,
	       sizeof(CUSTOM_PARAMS(j).CPP_info));
	MemUnlock(info);
	CUSTOM_PARAMS(j).CPP_unit = 0xffff;
	memset(&r, 0, sizeof(r));
	r.DR_bx = info;
	r.DR_dx = SPOOL_PARALLEL_BUFFER_SIZE;
	r.DR_ax = STREAM_OPEN_NO_BLOCK;
	err = CallPort(j, DR_STREAM_OPEN, &r);
	MemFree(info);
	if (!err)
	    break;
	if (SpoolErrorBox(PERROR_PARALLEL_ERR, j->SJI_qHan) != SPOOL_IC_OK) {
	    *portError = PE_PORT_NOT_OPENED;
	    return TRUE;
	}
    }
    CUSTOM_PARAMS(j).CPP_unit = (word) r.DR_bx;

    memset(&r, 0, sizeof(r));
    r.DR_cx = CUSTOM_PARAMS(j).CPP_unit;
    r.DR_si = PPT_CUSTOM;
    r.DR_dx = j->SJI_sHan;
    CallPrinter(j, DR_PRINT_SET_STREAM, &r);
    return FALSE;
}

void ExitCustomPort(SpoolJobInfo *j)
{
    DriverRegs r;

    if (CUSTOM_PARAMS(j).CPP_unit == 0xffff)
	return;
    memset(&r, 0, sizeof(r));
    r.DR_ax = (word) STREAM_LINGER;
    r.DR_bx = CUSTOM_PARAMS(j).CPP_unit;
    CallPort(j, DR_STREAM_CLOSE, &r);
    CUSTOM_PARAMS(j).CPP_unit = 0xffff;
}
