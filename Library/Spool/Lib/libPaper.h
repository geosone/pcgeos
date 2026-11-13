/*
 * libPaper.h -- the pre-defined page sizes of the spooler (libTables.asm,
 * made into C by Port32/tools/papergen.py) for libPaper.c.
 */
#ifndef LIBPAPER_H
#define LIBPAPER_H

#include <geos.h>

typedef struct {
    word	PSE_width;		/* points */
    word	PSE_height;
    word	PSE_layout;		/* the default PageLayout */
    const char	*PSE_name;
} PaperSizeEntry;

typedef struct {
    const PaperSizeEntry *PST_sizes;	/* by PaperSizes, EnvelopeSizes ... */
    word	PST_numSizes;
    const byte	*PST_defaultOrder;
    word	PST_numDefaultOrder;
} PaperSizeTable;

/* by PageType / 2 */
extern const PaperSizeTable paperSizeTables[4];
/* width, height, layout, margins left, top, right, bottom */
extern const word defaultPageSizeInfo[7];

#endif /* LIBPAPER_H */
