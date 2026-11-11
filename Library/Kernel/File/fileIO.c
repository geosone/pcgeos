/*
 * fileIO.c -- input and output on an open file (after fileIO.asm).
 *
 * Every routine sets the error for ThreadGetError.  A read or write
 * that moves fewer bytes than asked is ERROR_SHORT_READ_WRITE (the end
 * of the file, a full disk).  With noErrorFlag set, an error is fatal.
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

word FileRead(FileHandle fh, void *buf, word count, Boolean noErrorFlag)
{
    size_t done = 0;
    ghal_status s = ghal_fs_read(FileOf(fh)->KF_file, buf, count, &done);

    Done(s != GHAL_OK ? FileErrorOf(s, NULL) :
	 done < count ? ERROR_SHORT_READ_WRITE : 0, noErrorFlag);
    return (word) done;
}

word FileWrite(FileHandle fh, const void *buf, word count,
	       Boolean noErrorFlag)
{
    size_t done = 0;
    ghal_status s = ghal_fs_write(FileOf(fh)->KF_file, buf, count, &done);

    Done(s != GHAL_OK ? FileErrorOf(s, NULL) :
	 done < count ? ERROR_SHORT_READ_WRITE : 0, noErrorFlag);
    return (word) done;
}

dword FilePos(FileHandle fh, dword posOrOffset, FilePosMode mode)
{
    int whence = mode == FILE_POS_RELATIVE ? GHAL_FS_SEEK_CUR :
		 mode == FILE_POS_END ? GHAL_FS_SEEK_END : GHAL_FS_SEEK_SET;
    int64_t off = mode == FILE_POS_START ? (int64_t) posOrOffset
					 : (int64_t) (sdword) posOrOffset;
    uint64_t pos = 0;
    ghal_status s = ghal_fs_seek(FileOf(fh)->KF_file, off, whence, &pos);

    KernelSetError(FileErrorOf(s, NULL));
    return s == GHAL_OK ? (dword) pos : (dword) -1;
}

word FileTruncate(FileHandle fh, dword offset, Boolean noErrorFlag)
{
    KFile *kf = FileOf(fh);
    ghal_status s = ghal_fs_truncate(kf->KF_file, offset);

    /* the position is at the new end, as DOS left it */
    if (s == GHAL_OK)
	s = ghal_fs_seek(kf->KF_file, offset, GHAL_FS_SEEK_SET, NULL);
    return Done(FileErrorOf(s, NULL), noErrorFlag);
}

dword FileSize(FileHandle fh)
{
    ghal_fs_info info;
    ghal_status s = ghal_fs_fstat(FileOf(fh)->KF_file, &info);

    KernelSetError(FileErrorOf(s, NULL));
    return s == GHAL_OK ? (dword) info.size : 0;
}

word FileCommit(FileHandle fh, Boolean noErrorFlag)
{
    return Done(FileErrorOf(ghal_fs_sync(FileOf(fh)->KF_file), NULL),
		noErrorFlag);
}

/* ---------------------------------------------------------------------
 * Date and time: FileDateAndTime is the DOS format
 * ------------------------------------------------------------------- */

static FileDateAndTime FileDateAndTimeOf(const ghal_datetime *dt)
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
    ghal_fs_info info;
    ghal_status s = ghal_fs_fstat(FileOf(fh)->KF_file, &info);

    KernelSetError(FileErrorOf(s, NULL));
    return s == GHAL_OK ? FileDateAndTimeOf(&info.mtime) : 0;
}

word FileSetDateAndTime(FileHandle fh, FileDateAndTime dateAndTime)
{
    KFile *kf = FileOf(fh);
    ghal_datetime dt;

    dt.year = (uint16_t) FDATExtractYearAD(dateAndTime);
    dt.month = FDATExtractMonth(dateAndTime);
    dt.day = FDATExtractDay(dateAndTime);
    dt.hour = FDATExtractHour(dateAndTime);
    dt.minute = FDATExtractMinute(dateAndTime);
    dt.second = (uint8_t) FDATExtractSecond(dateAndTime);
    dt.weekday = 0;
    return Done(FileErrorOf(ghal_fs_set_fmtime(kf->KF_file, &dt), NULL),
		FALSE);
}
