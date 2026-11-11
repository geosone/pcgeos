/*
 * hostfs.c -- GEOS file headers and host names (hostfs.h), after
 * dosVirtual.asm and dosPath.asm of the DOS file system driver.
 *
 * Not yet: the mapping of the GEOS character set to the DOS code page
 * (characters above 0x7f become '_' in DOS names), links in
 * @DIRNAME.000.
 */
#include "hostfs.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define GEOS_LONGNAME_LENGTH	32	/* FILE_LONGNAME_LENGTH */
#define GFT_DATA		3
#define GFT_DIRECTORY		4

#if defined(GEOS_HOST_DOS32) && defined(__DJGPP__)
#include <fcntl.h>
#endif

/* HOSTFS_NAMES_AUTO: not known yet (dos32 asks DOS for LFN on first
 * use, other hosts have long names) */
static int dosNames = HOSTFS_NAMES_AUTO;

void HostFSSetDosNames(int mode)
{
    dosNames = mode;
}

int HostFSDosNames(void)
{
    if (dosNames < 0) {
#if defined(GEOS_HOST_DOS32) && defined(__DJGPP__)
        dosNames = _use_lfn(NULL) ? HOSTFS_NAMES_DOS_LFN : HOSTFS_NAMES_DOS;
#elif defined(GEOS_HOST_DOS32)
        dosNames = HOSTFS_NAMES_DOS;    /* Open Watcom: no LFN calls */
#else
        dosNames = HOSTFS_NAMES_LONG;
#endif
    }
    return dosNames;
}

int HostFSHostLongNames(void)
{
#if defined(GEOS_HOST_DOS32) && defined(__DJGPP__)
    return _use_lfn(NULL) != 0;
#elif defined(GEOS_HOST_DOS32)
    return 0;
#else
    return 1;
#endif
}

char HostFSSep(void)
{
#ifdef GEOS_HOST_DOS32
    return '\\';
#else
    return '/';
#endif
}

static void Put16(unsigned char *p, unsigned v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
}

/* ---------------------------------------------------------------------
 * Headers
 * ------------------------------------------------------------------- */

int HostFSIsHeader(const unsigned char *hdr)
{
    return hdr[0] == ('G' | 0x80) && hdr[1] == 'E' &&
           hdr[2] == ('A' | 0x80) && hdr[3] == 'S';
}

void HostFSInitHeader(unsigned char *hdr, const char *geosName,
                      unsigned type, uint32_t created)
{
    memset(hdr, 0, GFH_SIZE);
    hdr[0] = 'G' | 0x80;
    hdr[1] = 'E';
    hdr[2] = 'A' | 0x80;
    hdr[3] = 'S';
    strncpy((char *)hdr + GFH_LONG_NAME, geosName, GFH_LONG_NAME_SIZE - 1);
    Put16(hdr + GFH_TYPE, type);
    Put16(hdr + GFH_PROTOCOL, 1);       /* the default <1,0> */
    Put16(hdr + GFH_CREATED, (unsigned)(created & 0xffff));
    Put16(hdr + GFH_CREATED + 2, (unsigned)(created >> 16));
}

ghal_status HostFSReadHeader(const char *hostPath, unsigned char *hdr)
{
    ghal_file *f;
    size_t done = 0;
    ghal_status s = ghal_fs_open(hostPath, GHAL_FS_READ, &f);

    if (s != GHAL_OK)
        return s;
    s = ghal_fs_read(f, hdr, GFH_SIZE, &done);
    ghal_fs_close(f);
    if (s != GHAL_OK)
        return s;
    return done == GFH_SIZE && HostFSIsHeader(hdr) ? GHAL_OK
                                                   : GHAL_ERR_NOTFOUND;
}

ghal_status HostFSWriteDirName(const char *dirPath, const char *name,
                               uint32_t created)
{
    char path[HOSTFS_NAME_MAX * 2];
    unsigned char hdr[GFH_SIZE];
    ghal_file *f;
    size_t done;
    ghal_status s;

    if (!HostFSJoin(path, sizeof(path), dirPath, HOSTFS_DIRNAME_FILE))
        return GHAL_ERR_INVAL;
    HostFSInitHeader(hdr, name, GFT_DIRECTORY, created);
    s = ghal_fs_open(path, GHAL_FS_WRITE | GHAL_FS_CREATE | GHAL_FS_TRUNCATE,
                     &f);
    if (s != GHAL_OK)
        return s;
    s = ghal_fs_write(f, hdr, sizeof(hdr), &done);
    ghal_fs_close(f);
    return s;
}

ghal_status HostFSSetName(const char *hostPath, const char *name)
{
    unsigned char field[GFH_LONG_NAME_SIZE];
    ghal_file *f;
    size_t done;
    ghal_status s;

    memset(field, 0, sizeof(field));
    strncpy((char *)field, name, sizeof(field) - 1);
    s = ghal_fs_open(hostPath, GHAL_FS_WRITE, &f);
    if (s != GHAL_OK)
        return s;
    s = ghal_fs_seek(f, GFH_LONG_NAME, GHAL_FS_SEEK_SET, NULL);
    if (s == GHAL_OK)
        s = ghal_fs_write(f, field, sizeof(field), &done);
    ghal_fs_close(f);
    return s;
}

/* ---------------------------------------------------------------------
 * Names
 * ------------------------------------------------------------------- */

int HostFSJoin(char *out, size_t size, const char *dir, const char *name)
{
    int n = snprintf(out, size, "%s%c%s", dir, HostFSSep(), name);

    return n > 0 && (size_t)n < size;
}

/* the characters DOS does not take (badDosFilenameChars) */
static int BadDosChar(int c)
{
    return c <= ' ' || c >= 0x7f || strchr("\"*+,./:;<=>?[\\]|", c);
}

/* the 8.3 form of `name' if it is a DOS name (upper case) */
static int DosForm(const char *name, char *out)
{
    const char *dot = strchr(name, '.');
    size_t base = dot ? (size_t)(dot - name) : strlen(name);
    size_t ext = dot ? strlen(dot + 1) : 0;
    size_t i;

    if (!base || base > 8 || ext > 3 || (dot && !ext) ||
        (dot && strchr(dot + 1, '.')))
        return 0;
    for (i = 0; name[i]; i++) {
        if (name + i == dot)
            out[i] = '.';
        else if (BadDosChar((unsigned char)name[i]))
            return 0;
        else
            out[i] = (char)toupper((unsigned char)name[i]);
    }
    out[i] = 0;
    return 1;
}

/* a long DOS name (DOSVirtMapCheckComponentIsValidDosLongName):
 * no control characters, none of the characters " * / : < > ? \\ |,
 * not ending in a dot or a space */
static int DosLongForm(const char *name)
{
    size_t n = strlen(name), i;

    if (!n || n > GEOS_LONGNAME_LENGTH || name[n - 1] == '.' ||
        name[n - 1] == ' ')
        return 0;
    for (i = 0; i < n; i++)
        if ((unsigned char)name[i] < ' ' || strchr("\"*/:<>?\\|", name[i]))
            return 0;
    return 1;
}

static int ValidGeosName(const char *name)
{
    size_t n = strlen(name);

    if (!n || n > GEOS_LONGNAME_LENGTH || !strcmp(name, ".") ||
        !strcmp(name, ".."))
        return 0;
    return !strpbrk(name, "\\:*?/");
}

static int Exists(const char *dir, const char *name)
{
    char path[HOSTFS_NAME_MAX * 2];

    return HostFSJoin(path, sizeof(path), dir, name) &&
           ghal_fs_stat(path, NULL) == GHAL_OK;
}

/* DOSVirtGenerateDosName: the first eight characters made legal, then
 * the first free extension of .000 to .999 */
static ghal_status Generate(const char *dir, const char *name, char *out,
                            size_t size)
{
    char base[9];
    size_t i;
    unsigned n;

    for (i = 0; i < 8 && name[i]; i++)
        base[i] = BadDosChar((unsigned char)name[i])
                  ? '_' : (char)toupper((unsigned char)name[i]);
    base[i] = 0;
    if (size < 13)
        return GHAL_ERR_INVAL;
    for (n = 0; n < 1000; n++) {
        sprintf(out, "%s.%03u", base, n);
        if (!Exists(dir, out))
            return GHAL_OK;
    }
    return GHAL_ERR_EXISTS;
}

ghal_status HostFSNewName(const char *dir, const char *name, int kind,
                          char *out, size_t size, int *needsDirName)
{
    char dos[13];

    int mode = HostFSDosNames();

    if (needsDirName)
        *needsDirName = 0;
    if (!ValidGeosName(name))
        return GHAL_ERR_INVAL;
    /* with LFN as MSLF: native files and directories keep a long DOS
     * name, GEOS files get generated names all the same */
    if (mode == HOSTFS_NAMES_DOS_LFN && kind != HOSTFS_GEOS &&
        DosLongForm(name)) {
        if (strlen(name) >= size)
            return GHAL_ERR_INVAL;
        strcpy(out, name);
        return GHAL_OK;
    }
    if (mode == HOSTFS_NAMES_LONG) {
        if (strlen(name) >= size)
            return GHAL_ERR_INVAL;
        strcpy(out, name);
        return GHAL_OK;
    }
    switch (kind) {
    case HOSTFS_NATIVE:
        if (!DosForm(name, dos) || strlen(dos) >= size)
            return GHAL_ERR_INVAL;
        strcpy(out, dos);
        return GHAL_OK;
    case HOSTFS_DIR:
        /* a DOS name exactly: no @DIRNAME.000 (DOSPathCheckExactMatch) */
        if (DosForm(name, dos) && !strcmp(dos, name)) {
            strcpy(out, dos);
            return GHAL_OK;
        }
        if (needsDirName)
            *needsDirName = 1;
        return Generate(dir, name, out, size);
    default:
        return Generate(dir, name, out, size);
    }
}

/* the GEOS name in the header of an entry (a file, or the @DIRNAME.000
 * of a directory) */
static int HeaderName(const char *dir, const char *entry, char *out,
                      size_t size)
{
    char path[HOSTFS_NAME_MAX * 2], dn[HOSTFS_NAME_MAX * 2];
    unsigned char hdr[GFH_SIZE];
    ghal_fs_info info;

    if (!HostFSJoin(path, sizeof(path), dir, entry) ||
        ghal_fs_stat(path, &info) != GHAL_OK)
        return 0;
    if (info.attrs & GHAL_FS_ATTR_DIR) {
        if (!HostFSJoin(dn, sizeof(dn), path, HOSTFS_DIRNAME_FILE))
            return 0;
        strcpy(path, dn);
    } else if (info.size < GFH_SIZE)
        return 0;
    if (HostFSReadHeader(path, hdr) != GHAL_OK)
        return 0;
    snprintf(out, size, "%.*s", GFH_LONG_NAME_SIZE - 1,
             (const char *)hdr + GFH_LONG_NAME);
    return 1;
}

ghal_status HostFSGeosName(const char *dir, const char *hostName, char *out,
                           size_t size)
{
    if (!HeaderName(dir, hostName, out, size)) {
        if (strlen(hostName) >= size)
            return GHAL_ERR_INVAL;
        strcpy(out, hostName);
    }
    return GHAL_OK;
}

static int SameNoCase(const char *a, const char *b)
{
    while (*a && tolower((unsigned char)*a) == tolower((unsigned char)*b)) {
        a++;
        b++;
    }
    return !*a && !*b;
}

/* a name DOS GEOS may have given a GEOS file or directory
 * (DOSVirtMapCheckGeosName) */
static int Likely(const char *entry)
{
    const char *dot = strrchr(entry, '.');
    static const char *const exts[] = {"GEO", "VM", "DB", "STA", "BIT"};
    size_t i;

    if (!dot)
        return 0;
    dot++;
    if (strlen(dot) == 3 && isdigit((unsigned char)dot[0]) &&
        isdigit((unsigned char)dot[1]) && isdigit((unsigned char)dot[2]))
        return 1;
    for (i = 0; i < sizeof(exts) / sizeof(exts[0]); i++)
        if (SameNoCase(dot, exts[i]))
            return 1;
    return 0;
}

int HostFSIsDirNameFile(const char *entry)
{
    return SameNoCase(entry, HOSTFS_DIRNAME_FILE);
}

ghal_status HostFSLookup(const char *dir, const char *name, char *out,
                         size_t size)
{
    ghal_dir *d;
    char entry[HOSTFS_NAME_MAX], geos[GFH_LONG_NAME_SIZE + 1];
    char found[HOSTFS_NAME_MAX] = "";
    ghal_status s;

    if (!*name || strlen(name) >= size)
        return GHAL_ERR_INVAL;
    /* DOS without LFN cuts long names to 8.3: only a DOS name is tried
     * as it is */
    if ((HostFSDosNames() != HOSTFS_NAMES_DOS || DosForm(name, entry)) &&
        Exists(dir, name)) {
        strcpy(out, name);
        return GHAL_OK;
    }
    s = ghal_fs_opendir(dir, &d);
    if (s != GHAL_OK)
        return s == GHAL_ERR_NOTFOUND ? GHAL_ERR_NOTDIR : s;
    while (ghal_fs_readdir(d, entry, sizeof(entry), NULL) == GHAL_OK) {
        if (SameNoCase(entry, name)) {
            strcpy(found, entry);
            break;
        }
        if (!*found && Likely(entry) && !HostFSIsDirNameFile(entry) &&
            HeaderName(dir, entry, geos, sizeof(geos)) && !strcmp(geos, name))
            strcpy(found, entry);
    }
    ghal_fs_closedir(d);
    if (!*found)
        return GHAL_ERR_NOTFOUND;
    if (strlen(found) >= size)
        return GHAL_ERR_INVAL;
    strcpy(out, found);
    return GHAL_OK;
}
