/*
 * fileUtils.c -- after fileUtils.asm: FileOpOnPath.
 */
#include "kernelC.h"
#include <initfile.h>
#include <string.h>

static word OpOnPlaces(DiskHandle base, const char *tail, const char *path,
		       FileOpFunc *op, void *data)
{
    return FileOpOnPlacesN(base, tail, path, op, data, 0);
}

word FileOpOnPath(const char *path, FileOpFunc *op, void *data)
{
    KPath *cur = FileCurPath();

    return OpOnPlaces(cur->KP_disk, cur->KP_tail, path, op, data);
}
