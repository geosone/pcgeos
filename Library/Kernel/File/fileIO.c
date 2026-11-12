/*
 * fileIO.c -- input and output on an open file (after fileIO.asm), over
 * DR_FS_HANDLE_OP of the file's driver.
 *
 * Every routine sets the error for ThreadGetError.  A read or write
 * that moves fewer bytes than asked is ERROR_SHORT_READ_WRITE (the end
 * of the file, a full disk).  With noErrorFlag set, an error is fatal.
 * Positions and sizes are those of the data (in a GEOS file: after the
 * GeosFileHeader), as on DOS16.
 *
 * Not yet: record locks.
 */
#include "kernelC.h"
#include "ghal.h"

static word Done(word e, Boolean noErrorFlag)
{
    if (e && e != ERROR_SHORT_READ_WRITE && noErrorFlag)
	KernelFatal(FILE_ERROR_NOT_EXPECTED);
    KernelSetError(e);
    return e;
}

#define DRV(kf)	FSD_OF((kf)->KF_file.FF_disk)

word FileRead(FileHandle fh, void *buf, word count, Boolean noErrorFlag)
{
    KFile *kf = FileOf(fh);
    word done = 0, e = DRV(kf)->FSD_read(&kf->KF_file, buf, count, &done);

    Done(e ? e : done < count ? ERROR_SHORT_READ_WRITE : 0, noErrorFlag);
    return done;
}

word FileWrite(FileHandle fh, const void *buf, word count,
	       Boolean noErrorFlag)
{
    KFile *kf = FileOf(fh);
    word done = 0, e = DRV(kf)->FSD_write(&kf->KF_file, buf, count, &done);

    Done(e ? e : done < count ? ERROR_SHORT_READ_WRITE : 0, noErrorFlag);
    return done;
}

dword FilePos(FileHandle fh, dword posOrOffset, FilePosMode mode)
{
    KFile *kf = FileOf(fh);
    dword pos = 0;
    word e = DRV(kf)->FSD_position(&kf->KF_file, (sdword) posOrOffset, mode,
				   &pos);

    KernelSetError(e);
    return e ? (dword) -1 : pos;
}

word FileTruncate(FileHandle fh, dword offset, Boolean noErrorFlag)
{
    KFile *kf = FileOf(fh);

    return Done(DRV(kf)->FSD_truncate(&kf->KF_file, offset), noErrorFlag);
}

dword FileSize(FileHandle fh)
{
    KFile *kf = FileOf(fh);
    dword size = 0;

    KernelSetError(DRV(kf)->FSD_size(&kf->KF_file, &size));
    return size;
}

word FileCommit(FileHandle fh, Boolean noErrorFlag)
{
    KFile *kf = FileOf(fh);

    return Done(DRV(kf)->FSD_commit(&kf->KF_file), noErrorFlag);
}

/* ---------------------------------------------------------------------
 * Date and time: FileDateAndTime is the DOS format
 * ------------------------------------------------------------------- */

FileDateAndTime FilePackDate(const ghal_datetime *dt)
{
    if (dt->year < FDAT_BASE_YEAR)
	return 0;
    return ((dword) dt->hour << FDAT_HOUR_OFFSET) |
	   ((dword) dt->minute << FDAT_MINUTE_OFFSET) |
	   ((dword) (dt->second / 2) << FDAT_2SECOND_OFFSET) |
	   ((dword) (dt->year - FDAT_BASE_YEAR) << FDAT_YEAR_OFFSET) |
	   ((dword) dt->month << FDAT_MONTH_OFFSET) |
	   ((dword) dt->day << FDAT_DAY_OFFSET);
}

FileDateAndTime FileGetDateAndTime(FileHandle fh)
{
    KFile *kf = FileOf(fh);
    FileDateAndTime fdat = 0;

    KernelSetError(DRV(kf)->FSD_getDateTime(&kf->KF_file, &fdat));
    return fdat;
}

void FileUnpackDate(FileDateAndTime dateAndTime, ghal_datetime *dt)
{
    dt->year = (uint16_t) FDATExtractYearAD(dateAndTime);
    dt->month = FDATExtractMonth(dateAndTime);
    dt->day = FDATExtractDay(dateAndTime);
    dt->hour = FDATExtractHour(dateAndTime);
    dt->minute = FDATExtractMinute(dateAndTime);
    dt->second = (uint8_t) FDATExtractSecond(dateAndTime);
    dt->weekday = 0;
}

FileDateAndTime FileNow(void)
{
    ghal_datetime dt;

    KernelGetDateTime(&dt);		/* GEOS's clock (timerMisc.c) */
    return FilePackDate(&dt);
}

word FileSetDateAndTime(FileHandle fh, FileDateAndTime dateAndTime)
{
    KFile *kf = FileOf(fh);

    return Done(DRV(kf)->FSD_setDateTime(&kf->KF_file, dateAndTime), FALSE);
}
