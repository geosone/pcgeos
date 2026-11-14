/*
 * fsDriver.h -- the file system driver interface of Port32 (the C form of
 * DR_FS_* in Include/Internal/fsDriver.def; Port32/ABI.md section 13f).
 *
 * The kernel keeps paths, standard paths, current paths, drives, disks
 * and file handles (Library/Kernel/File, FSD, Disk); a file system driver
 * does what touches the medium.  It is a table of C functions instead of
 * a strategy routine with registers.  Each returns 0 or a FileError.
 *
 * A path passed to a driver is absolute on its disk: "\\" and the
 * components separated by "\\", "." and ".." resolved by the kernel,
 * the names as GEOS knows them (long names).  How they map to the
 * medium is the driver's business (DOS 8.3 names, the GeosFileHeader).
 */
#ifndef __FSDRIVER_H
#define __FSDRIVER_H

#include <geos.h>
#include <file.h>
#include <disk.h>
#include <drive.h>

struct FSDriver;

/* DriveStatusEntry: a drive, by number (0 is A:) */
typedef struct FSDDrive {
    word		FDR_number;
    char		FDR_name[DRIVE_NAME_MAX_LENGTH + 1];
    byte		FDR_status;	/* DS_* and DriveType */
    word		FDR_extStatus;	/* DES_* */
    MediaType		FDR_media;	/* default media */
    const struct FSDriver *FDR_fsd;
    void		*FDR_private;	/* the driver's */
} FSDDrive;

/* DiskDesc: the disk in a drive */
#define FDK_WRITABLE	0x0001
#define FDK_NAMELESS	0x0002	/* the volume name is made up (FSDGenNameless) */
typedef struct FSDDisk {
    DiskHandle		FDK_handle;
    FSDDrive		*FDK_drive;
    dword		FDK_id;		/* DR_FS_DISK_ID */
    word		FDK_flags;	/* FDK_* */
    char		FDK_volume[VOLUME_NAME_LENGTH_ZT];
    word		FDK_inUse;	/* the kernel's: open files */
    void		*FDK_private;	/* the driver's */
} FSDDisk;

/* an open file: the kernel's file handles share it */
typedef struct FSDFile {
    FSDDisk		*FF_disk;
    void		*FF_private;	/* the driver's */
} FSDFile;

typedef struct FSDriver {
    const char	*FSD_name;

    /* disks (DR_FS_DISK_*) */
    word (*FSD_diskID)(FSDDrive *drive, dword *id);
    word (*FSD_diskInit)(FSDDisk *disk);	/* volume name, flags */
    word (*FSD_diskInfo)(FSDDisk *disk, DiskInfoStruct *info);

    /* DR_FS_CUR_PATH_SET: is `path' a directory? */
    word (*FSD_curPathSet)(FSDDisk *disk, const char *path);

    /* DR_FS_ALLOC_OP: FSAOF_OPEN, FSAOF_CREATE (FileCreate's flags and
     * attributes, the format of an existing file checked) */
    word (*FSD_open)(FSDDisk *disk, const char *path, FileAccessFlags flags,
		     FSDFile *file);
    word (*FSD_create)(FSDDisk *disk, const char *path, FileCreateFlags flags,
		       FileAttrs attrs, FSDFile *file);

    /* DR_FS_HANDLE_OP: positions and sizes are those of the data */
    word (*FSD_read)(FSDFile *file, void *buf, word count, word *done);
    word (*FSD_write)(FSDFile *file, const void *buf, word count,
		      word *done);
    word (*FSD_position)(FSDFile *file, sdword offset, FilePosMode mode,
			 dword *pos);
    word (*FSD_truncate)(FSDFile *file, dword size);
    word (*FSD_commit)(FSDFile *file);
    word (*FSD_size)(FSDFile *file, dword *size);
    word (*FSD_getDateTime)(FSDFile *file, FileDateAndTime *fdat);
    word (*FSD_setDateTime)(FSDFile *file, FileDateAndTime fdat);
    word (*FSD_close)(FSDFile *file);
    /* FSHOF_GET/SET_EXT_ATTRIBUTES (FEA_MULTIPLE: buf is
     * FileExtAttrDesc[size]) */
    word (*FSD_handleExtAttrs)(FSDFile *file, FileExtendedAttribute attr,
			       void *buf, word size, Boolean set);

    /* DR_FS_PATH_OP */
    word (*FSD_createDir)(FSDDisk *disk, const char *path);
    word (*FSD_deleteDir)(FSDDisk *disk, const char *path);
    word (*FSD_deleteFile)(FSDDisk *disk, const char *path);
    /* FSPOF_RENAME_FILE / FSPOF_MOVE_FILE: both paths on `disk' */
    word (*FSD_rename)(FSDDisk *disk, const char *path, const char *newPath);
    word (*FSD_getAttrs)(FSDDisk *disk, const char *path, FileAttrs *attrs);
    word (*FSD_setAttrs)(FSDDisk *disk, const char *path, FileAttrs attrs);
    word (*FSD_pathExtAttrs)(FSDDisk *disk, const char *path,
			     FileExtendedAttribute attr, void *buf, word size,
			     Boolean set);

    /* DR_FS_FILE_ENUM: calls cb for each file and directory in the
     * directory `path' (not what the driver keeps for itself); cb
     * returns non-zero to stop.  FSD_enumAttr gets an attribute of the
     * entry cb was given (a missing one: ERROR_ATTR_NOT_FOUND). */
    word (*FSD_fileEnum)(FSDDisk *disk, const char *path,
			 word (*cb)(void *data, void *entry), void *data);
    word (*FSD_enumAttr)(void *entry, FileExtendedAttribute attr,
			 void *buf, word size);
} FSDriver;

/* the kernel: a drive served by `fsd' (FSDInitDrive); replaces one with
 * the same number.  FALSE if the table is full. */
Boolean FSDInitDrive(word number, const char *name, byte status,
		     MediaType media, const FSDriver *fsd, void *priv);
FSDDrive *FSDDriveOf(word number);
/* DriveLocateByName: the drive named by the n characters at `name'
 * (case does not matter), -1 if there is none */
sword FSDDriveByName(const char *name, word n);
/* the disk of a DiskHandle (a StandardPath: the disk of the GEOS top
 * directory), NULL if there is none */
FSDDisk *FSDDiskOf(DiskHandle disk);
#define FSD_OF(disk)	((disk)->FDK_drive->FDR_fsd)

/* kernel services for drivers: FileDateAndTime and the host's time */
#include "ghal.h"
FileDateAndTime FilePackDate(const ghal_datetime *dt);
void FileUnpackDate(FileDateAndTime fdat, ghal_datetime *dt);
FileDateAndTime FileNow(void);

/* the HostFS driver on ghal_fs (Driver/IFS/HostFS/hostfsDriver.c), and the
 * private data of a drive for it: the host directory that is its root */
extern const FSDriver hostFSDriver;
void *HostFSDriveData(const char *hostRoot);
void HostFSFreeDriveData(void *data);

#endif
