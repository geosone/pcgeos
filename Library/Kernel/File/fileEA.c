/*
 * fileEA.c -- extended attributes of files and directories (the
 * FileGet/Set...ExtAttributes of fileFile.asm and fileOpenClose.asm), over
 * FSHOF_GET/SET_EXT_ATTRIBUTES and FSPOF_GET/SET_EXT_ATTRIBUTES of the
 * disk's driver: what they are and where they are kept is the driver's
 * (HostFS: the GeosFileHeader, Port32/hostfs/hostfsDriver.c).
 *
 * Not yet: FileGetHandleAllExtAttributes, FileCopyPathExtAttributes.
 */
#include "kernelC.h"

static word Done(word e)
{
    KernelSetError(e);
    return e;
}

static word ByPath(const TCHAR *path, FileExtendedAttribute attr,
		   void *buffer, word bufSize, Boolean set)
{
    char dp[FILE_DISK_PATH_SIZE];
    FSDDisk *d = FileLocate(0, path, dp, sizeof(dp));

    if (!d)
	return ThreadGetError();
    return Done(FSD_OF(d)->FSD_pathExtAttrs(d, dp, attr, buffer, bufSize,
					    set));
}

static word ByHandle(FileHandle fh, FileExtendedAttribute attr, void *buffer,
		     word bufSize, Boolean set)
{
    KFile *kf = FileOf(fh);

    return Done(FSD_OF(kf->KF_file.FF_disk)->FSD_handleExtAttrs(
		    &kf->KF_file, attr, buffer, bufSize, set));
}

word FileGetPathExtAttributes(const TCHAR *path, FileExtendedAttribute attr,
			      void *buffer, word bufSize)
{
    return ByPath(path, attr, buffer, bufSize, FALSE);
}

word FileSetPathExtAttributes(const TCHAR *path, FileExtendedAttribute attr,
			      const void *buffer, word bufSize)
{
    return ByPath(path, attr, (void *) buffer, bufSize, TRUE);
}

word FileGetHandleExtAttributes(FileHandle fh, FileExtendedAttribute attr,
				void *buffer, word bufSize)
{
    return ByHandle(fh, attr, buffer, bufSize, FALSE);
}

word FileSetHandleExtAttributes(FileHandle fh, FileExtendedAttribute attr,
				const void *buffer, word bufSize)
{
    return ByHandle(fh, attr, (void *) buffer, bufSize, TRUE);
}
