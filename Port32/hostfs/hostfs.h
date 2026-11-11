/*
 * hostfs.h -- the host side of GEOS files, on ghal_fs: the GEOS file
 * header in the file and the names of files and directories on the
 * host.  Used by the kernel's file routines (Library/Kernel/File) until
 * it becomes the HostFS file system driver (GHAL-Concept.md section 4).
 *
 * A GEOS file starts with the 256-byte GeosFileHeader of DOS16 GEOS on
 * every host (Include/Internal/fileStr.def), so files can be copied
 * between hosts (Port32/tools/geostree.py).  The names differ:
 *
 *  - DOS names (dos32): a GEOS file gets a generated 8.3 name (the first
 *    eight characters of its GEOS name made legal, extension .000, .001
 *    ...), a directory whose name is no DOS name gets one too and a
 *    @DIRNAME.000 file with its GEOS name (dosVirtual.asm, dosPath.asm);
 *  - long names (other hosts): the GEOS name is the host name.
 *
 * Looking a GEOS name up tries the host name, then the name in any case,
 * then the GEOS names in the headers of likely files, so either kind of
 * tree can be read on any host.  Only ghal_fs and the C library are
 * used.
 */
#ifndef HOSTFS_H
#define HOSTFS_H

#include "ghal.h"

#include <stddef.h>
#include <stdint.h>

/* GeosFileHeader (fileStr.def): offsets and sizes */
#define GFH_SIZE		256
#define GFH_SIGNATURE		0	/* 4: 'G'|0x80 'E' 'A'|0x80 'S' */
#define GFH_LONG_NAME		4	/* 36 */
#define GFH_LONG_NAME_SIZE	36
#define GFH_TYPE		40	/* 2: GeosFileType */
#define GFH_FLAGS		42	/* 2: GeosFileHeaderFlags */
#define GFH_RELEASE		44	/* 8: ReleaseNumber */
#define GFH_PROTOCOL		52	/* 4: ProtocolNumber */
#define GFH_TOKEN		56	/* 6: GeodeToken */
#define GFH_CREATOR		62	/* 6 */
#define GFH_USER_NOTES		68	/* 100 */
#define GFH_NOTICE		168	/* 32 */
#define GFH_CREATED		200	/* 4: FileDateAndTime */
#define GFH_PASSWORD		204	/* 8 */
#define GFH_DESKTOP		212	/* 16 */

#define HOSTFS_DIRNAME_FILE	"@DIRNAME.000"
#define HOSTFS_NAME_MAX		256	/* host names, with the NUL */

/* DOS names (default on dos32) or long names */
void        HostFSSetDosNames(int on);
int         HostFSDosNames(void);
char        HostFSSep(void);	/* the host's path separator */

int         HostFSIsHeader(const unsigned char *hdr);
/* a header for a new file: GEOS name, GeosFileType, creation time */
void        HostFSInitHeader(unsigned char *hdr, const char *geosName,
                             unsigned type, uint32_t created);
/* the header of a host file; GHAL_ERR_NOTFOUND if it is no GEOS file */
ghal_status HostFSReadHeader(const char *hostPath, unsigned char *hdr);

/* `dir'/`name' into out */
int         HostFSJoin(char *out, size_t size, const char *dir,
                       const char *name);
/* the host name of GEOS name `name' in host directory `dir' */
ghal_status HostFSLookup(const char *dir, const char *name, char *out,
                         size_t size);
/* the host name for creating `name' in `dir': kind 0 a native file, 1 a
 * GEOS file, 2 a directory; *needsDirName: a directory that needs a
 * @DIRNAME.000 */
#define HOSTFS_NATIVE   0
#define HOSTFS_GEOS     1
#define HOSTFS_DIR      2
ghal_status HostFSNewName(const char *dir, const char *name, int kind,
                          char *out, size_t size, int *needsDirName);
/* write @DIRNAME.000 with GEOS name `name' into directory `dirPath' */
ghal_status HostFSWriteDirName(const char *dirPath, const char *name,
                               uint32_t created);
/* whether a directory entry is @DIRNAME.000 (in any case) */
int         HostFSIsDirNameFile(const char *entry);
/* change the GEOS name in the header of a file */
ghal_status HostFSSetName(const char *hostPath, const char *name);
/* the GEOS name of a host directory entry */
ghal_status HostFSGeosName(const char *dir, const char *hostName, char *out,
                           size_t size);

#endif
