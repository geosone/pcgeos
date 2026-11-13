/*
 * xlatPS.h -- the PostScript translation library (eps.geo), Port32.
 *
 * The C form of Include/xlatPS.def: the export options, the stream a
 * caller hands the library, and its export routines.  Port32 ABI: the
 * routines take their arguments as C parameters instead of registers
 * (dx = options block, di = stream block, ...), handles are 32 bits wide
 * (Port32/ABI.md §2), and the stream block holds a C strategy pointer.
 *
 * The caller (the PostScript printer driver, or TransExport) allocates
 * a PSExportBlock, fills in PSEB_public and zeroes the rest, and passes
 * its handle to every routine:
 *
 *   EPSExportHeader      the header comments and the prolog
 *   EPSExportBeginPage   the document setup (first page), the page setup
 *   EPSExportLow         a graphics string: its PostScript
 *   EPSExportRaw         bytes as they are
 *   EPSExportEndPage     the end of the page
 *   EPSExportTrailer     the trailer
 */
#ifndef XLATPS_H
#define XLATPS_H

#include <geos.h>
#include <graphics.h>
#include <gstring.h>
#include <file.h>
#include <geosdriver.h>

/* TransError (Include/Internal/xlatLib.def) */
typedef word TransError;
#define TE_NO_ERROR             0
#define TE_ERROR                1
#define TE_INVALID_FORMAT       2
#define TE_IMPORT_NOT_SUPPORTED 3
#define TE_EXPORT_NOT_SUPPORTED 4
#define TE_IMPORT_ERROR         5
#define TE_EXPORT_ERROR         6

/* GExportFlags (Include/Internal/xlatGrfx.def; the first field is the
 * high bit) */
typedef WordFlags GExportFlags;
#define GEF_NO_COMMENTS         0x8000
#define GEF_NO_HEADER           0x4000
#define GEF_NO_TRAILER          0x2000
#define GEF_COLLATE             0x1000
#define GEF_PAGES_DESCENDING    0x0800

typedef struct {
    GExportFlags GEO_flags;
    GSControl    GEO_drawFlags;         /* for GrDrawGString */
    word         GEO_pages;             /* 0 if not known */
    word         GEO_copies;
    char         GEO_appName[FILE_LONGNAME_LENGTH + 1];
    char         GEO_docName[FILE_LONGNAME_LENGTH + 1];
    char         GEO_fileName[PATH_BUFFER_SIZE];
    dword        GEO_docH;              /* points */
    dword        GEO_docW;
    FileHandle   GEO_hFile;
} GExportOpts;

typedef WordFlags PSExportFlags;
#define PSEF_EPS_FILE           0x8000
#define PSEF_INCLUDE_PREVIEW    0x4000

typedef word PSFontList;                /* PSFL_* of xlatPS.def */
#define PSFL_STANDARD_13        0
#define PSFL_STANDARD_35N       2

/* PSLevel: the extensions in the high byte, the level in the low byte */
typedef WordFlags PSLevel;
#define PSL_CMYK                0x8000
#define PSL_DPS                 0x4000
#define PSL_COMPOSITE           0x2000
#define PSL_FILE                0x1000
#define PSL_LEVEL               0x00ff

typedef struct {
    GExportOpts   PSEO_common;
    PSExportFlags PSEO_flags;
    PSFontList    PSEO_fonts;
    PSLevel       PSEO_level;
} PSExportOpts;

/* the library's own state after the options (epsConstant.def) */
typedef WordFlags PSExportStatus;
#define PSES_OPEN_PROLOG        0x8000
#define PSES_OPEN_PAGE_SETUP    0x4000
#define PSES_EXPORTING_BITMAP   0x2000

typedef struct {
    PSExportOpts   PSEB_public;
    PSExportStatus PSEB_status;
    word           PSEB_curPage;
} PSExportBlock;

/* where the PostScript goes: a stream driver's unit (DR_STREAM_WRITE) */
typedef struct {
    dword           ESPELSS_token;
    DriverStrategy *ESPELSS_strategy;
} EPSExportLowStreamStruct;

/* options: the handle of a PSExportBlock; stream: the handle of an
 * EPSExportLowStreamStruct */
extern TransError EPSExportHeader(MemHandle options, MemHandle stream);
extern TransError EPSExportBeginPage(MemHandle options, MemHandle stream);
/* *ret: what GrDrawGString returned (GSRT_FAULT on an error) */
extern TransError EPSExportLow(MemHandle options, GStateHandle gstring,
                               MemHandle stream, GSControl flags,
                               GSRetType *ret);
extern TransError EPSExportRaw(MemHandle options, const void *data,
                               word size, MemHandle stream);
extern TransError EPSExportEndPage(MemHandle options, MemHandle stream);
extern TransError EPSExportTrailer(MemHandle options, MemHandle stream);
extern TransError EPSNormalizeFilename(MemHandle options);
/* a PSExportBlock of the defaults (ui 0), unlocked; 0 for a ui block */
extern MemHandle TransGetExportOptions(MemHandle ui);

#endif /* XLATPS_H */
