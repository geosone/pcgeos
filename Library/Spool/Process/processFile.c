/*
 * processFile.c -- the file "port" (the C port of processFile.asm): the
 * output file is made in the directory of the FilePortParams and written
 * through the file stream driver (filestr.geo).
 */
#include "spoolLocal.h"
#include <string.h>

#define FILE_PARAMS(j)	((j)->SJI_info.JP_portInfo.PPI_params.PP_file)

/* InitFilePort: TRUE and a PortErrors on an error */
Boolean InitFilePort(SpoolJobInfo *j, word *portError)
{
    FileHandle fh;
    DriverRegs r;
    PState *ps;

    FilePushDir();
    FileSetCurrentPath((DiskHandle) FILE_PARAMS(j).FPP_diskHandle,
		       FILE_PARAMS(j).FPP_path);
    fh = FileCreate(FILE_PARAMS(j).FPP_fileName,
		    FCF_NATIVE | FILE_CREATE_TRUNCATE | FILE_ACCESS_W |
		    FILE_DENY_W, 0);
    FilePopDir();
    FILE_PARAMS(j).FPP_file = fh;
    ps = MemLock(j->SJI_pstate);
    PS_JOB_PARAMS(ps)->JP_portInfo.PPI_params.PP_file.FPP_file = fh;
    MemUnlock(j->SJI_pstate);
    if (fh) {
	memset(&r, 0, sizeof(r));
	r.DR_bx = fh;
	r.DR_dx = SPOOL_FILE_BUFFER_SIZE;
	r.DR_ax = STREAM_OPEN_NO_BLOCK;
	if (!CallPort(j, DR_STREAM_OPEN, &r)) {
	    FILE_PARAMS(j).FPP_unit = (word) r.DR_bx;
	    /* the connection for the printer driver */
	    memset(&r, 0, sizeof(r));
	    r.DR_cx = FILE_PARAMS(j).FPP_unit;
	    r.DR_si = PPT_FILE;
	    r.DR_dx = j->SJI_sHan;
	    CallPrinter(j, DR_PRINT_SET_STREAM, &r);
	    return FALSE;
	}
	/* Port32: the asm left the file open */
	FileClose(fh, TRUE);
	FILE_PARAMS(j).FPP_file = 0;
    }
    SpoolErrorBox(SERROR_CANNOT_OPEN_FILE, j->SJI_qHan);
    *portError = PE_PORT_NOT_OPENED;
    return TRUE;
}

/* ExitFilePort: the stream writes the rest; then the file is closed */
void ExitFilePort(SpoolJobInfo *j)
{
    DriverRegs r;

    memset(&r, 0, sizeof(r));
    r.DR_ax = (word) STREAM_LINGER;
    r.DR_bx = FILE_PARAMS(j).FPP_unit;
    CallPort(j, DR_STREAM_CLOSE, &r);
    if (FILE_PARAMS(j).FPP_file)
	FileClose(FILE_PARAMS(j).FPP_file, TRUE);
    FILE_PARAMS(j).FPP_file = 0;
}
