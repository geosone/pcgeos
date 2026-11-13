/*
 * libMisc.c -- the spool library's miscellany (the C port of
 * SpoolCreateSpoolFile of libMisc.asm).
 */
#include "spoolLocal.h"

/* SpoolCreateSpoolFile: a new file in the spool directory, named
 * spoolNNN.dat; the name goes into fileNameBuf (13 bytes).  0 if no
 * file could be made.  As the asm, a digit runs up to 8 only before it
 * rolls over (the 'jl' of the asm: "9" is never tried). */
FileHandle SpoolCreateSpoolFile(char *fileNameBuf)
{
    static const char base[13] = "spool000.dat";
    SpoolFileName *n = (SpoolFileName *) fileNameBuf;
    FileHandle fh;
    int i;

    for (i = 0; i < 13; i++)
	fileNameBuf[i] = base[i];
    FilePushDir();
    FileSetStandardPath(SP_SPOOL);
    for (;;) {
	fh = FileCreate(fileNameBuf, FILE_CREATE_ONLY | FILE_DENY_W |
			FILE_ACCESS_RW, FILE_ATTR_NORMAL);
	if (fh || ThreadGetError() == ERROR_SHORT_READ_WRITE)
	    break;
	for (i = 2; i >= 0; i--) {
	    if (++n->SFN_num[i] < '9')
		break;
	    n->SFN_num[i] = '0';
	}
	if (i < 0)
	    break;
    }
    FilePopDir();
    return fh;
}
