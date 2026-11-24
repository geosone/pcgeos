/***********************************************************************
 * gpscan.h -- what gpres and gpgeode both read: the "resource" lines of
 * a geode parameter file and the "@start" lines of goc files.
 ***********************************************************************/
#ifndef _GPSCAN_H_
#define _GPSCAN_H_

#include <stddef.h>

#define GP_FIRST_RESOURCE   2	/* 0: the core block, 1: the fixed data */

/* a list of strings that grows */
typedef struct {
    char    **item;
    int	    count;
    int	    size;
} GpList;

/* a resource a goc file starts */
typedef struct {
    char    *name;
    int	    isData;	/* "@start name, data" */
} GpStart;

typedef struct {
    GpStart *item;
    int	    count;
    int	    size;
} GpStarts;

extern void *GpAlloc(size_t size);
extern char *GpStrDup(const char *s);
extern char *GpStrNDup(const char *s, size_t n);
extern char *GpUpper(const char *s);
extern void GpListAdd(GpList *list, const char *s);
extern int GpListFind(const GpList *list, const char *s);
extern const char *GpBaseName(const char *path);
extern int GpEqualNoCase(const char *a, const char *b);

/* the file as one string (NULL: it cannot be read) */
extern char *GpReadFile(const char *path);
/* the same, or the program ends with a message */
extern char *GpMustRead(const char *path);

/* the names of the "resource" lines of a .gp file's text, in order */
extern void GpResources(const char *text, GpList *names);
/* the "@start" lines of a goc file and of the local files it includes */
extern void GpGocStarts(const char *path, GpStarts *starts);

#endif /* _GPSCAN_H_ */
