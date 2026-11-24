/***********************************************************************
 * gpscan.c -- what gpres and gpgeode both read: the "resource" lines of
 * a geode parameter file and the "@start" lines of goc files.
 *
 * Plain C, no host services but stdio: the tools are to run wherever
 * GEOS is built, DOS included.
 ***********************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "gpscan.h"

#if defined(__unix__) || defined(__APPLE__) || defined(__DJGPP__)
#include <unistd.h>
#else
#include <direct.h>
#endif

void *
GpAlloc(size_t size)
{
    void *p = malloc(size ? size : 1);

    if (p == NULL) {
	fprintf(stderr, "out of memory\n");
	exit(1);
    }
    return p;
}

char *
GpStrNDup(const char *s, size_t n)
{
    char *d = (char *) GpAlloc(n + 1);

    memcpy(d, s, n);
    d[n] = '\0';
    return d;
}

char *
GpStrDup(const char *s)
{
    return GpStrNDup(s, strlen(s));
}

char *
GpUpper(const char *s)
{
    char *d = GpStrDup(s), *p;

    for (p = d; *p != '\0'; p++) {
	*p = (char) toupper((unsigned char) *p);
    }
    return d;
}

int
GpEqualNoCase(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
	if (tolower((unsigned char) *a) != tolower((unsigned char) *b)) {
	    return 0;
	}
	a++, b++;
    }
    return *a == *b;
}

void
GpListAdd(GpList *list, const char *s)
{
    if (list->count == list->size) {
	char **n;

	list->size = list->size ? 2 * list->size : 16;
	n = (char **) GpAlloc(list->size * sizeof(char *));
	if (list->count) {
	    memcpy(n, list->item, list->count * sizeof(char *));
	}
	free(list->item);
	list->item = n;
    }
    list->item[list->count++] = GpStrDup(s);
}

int
GpListFind(const GpList *list, const char *s)
{
    int i;

    for (i = 0; i < list->count; i++) {
	if (strcmp(list->item[i], s) == 0) {
	    return i;
	}
    }
    return -1;
}

static int
IsSep(int c)
{
#if defined(__unix__) || defined(__APPLE__)
    return c == '/';
#else
    return c == '/' || c == '\\';
#endif
}

const char *
GpBaseName(const char *path)
{
    const char *p, *base = path;

    for (p = path; *p != '\0'; p++) {
	if (*p == '/') {
	    base = p + 1;
	}
    }
    return base;
}

char *
GpReadFile(const char *path)
{
    FILE    *f = fopen(path, "rb");
    char    *text;
    size_t  len = 0, size = 4096, n;

    if (f == NULL) {
	return NULL;
    }
    text = (char *) GpAlloc(size);
    while ((n = fread(text + len, 1, size - len - 1, f)) > 0) {
	len += n;
	if (len + 1 >= size) {
	    char *t = (char *) GpAlloc(size * 2);

	    memcpy(t, text, len);
	    free(text);
	    text = t;
	    size *= 2;
	}
    }
    fclose(f);
    text[len] = '\0';
    return text;
}

char *
GpMustRead(const char *path)
{
    char *text = GpReadFile(path);

    if (text == NULL) {
	perror(path);
	exit(1);
    }
    return text;
}

/***********************************************************************
 *				GpResources
 ***********************************************************************
 * SYNOPSIS:	    The names of the "resource" lines, in their order
 *		    (as they are written: the caller folds the case).
 *		    A "#" starts a comment.  Lines in an "ifdef" part
 *		    count as well: the numbers do not depend on it.
 ***********************************************************************/
void
GpResources(const char *text, GpList *names)
{
    const char *p = text;

    while (*p != '\0') {
	const char *eol = p, *end, *q;

	while (*eol != '\0' && *eol != '\n' && *eol != '\r') {
	    eol++;
	}
	for (end = p; end < eol && *end != '#'; end++) {
	    ;
	}
	while (p < end && isspace((unsigned char) *p)) {
	    p++;
	}
	while (end > p && isspace((unsigned char) end[-1])) {
	    end--;
	}
	if (end - p > 8 && isspace((unsigned char) p[8])) {
	    char *kw = GpStrNDup(p, 8);

	    if (GpEqualNoCase(kw, "resource")) {
		q = p + 8;
		while (q < end && isspace((unsigned char) *q)) {
		    q++;
		}
		p = q;
		while (q < end && !isspace((unsigned char) *q)) {
		    q++;
		}
		if (q > p) {
		    char *name = GpStrNDup(p, q - p);

		    GpListAdd(names, name);
		    free(name);
		}
	    }
	    free(kw);
	}
	p = eol;
	while (*p == '\n' || *p == '\r') {
	    p++;
	}
    }
}

/*
 * The directory of a file, as a full path without "." and ".." (the
 * local includes of a goc file are looked for there and above it)
 */
static char *
FullDir(const char *path)
{
    char    *full, *out, *p, *start;
    size_t  n;

    if (IsSep(path[0]) || (isalpha((unsigned char) path[0]) &&
			   path[1] == ':')) {
	full = GpStrDup(path);
    } else {
	char cwd[1024];

	if (getcwd(cwd, sizeof(cwd)) == NULL) {
	    cwd[0] = '\0';
	}
	full = (char *) GpAlloc(strlen(cwd) + strlen(path) + 2);
	sprintf(full, "%s/%s", cwd, path);
    }
    /* the file's name off */
    n = strlen(full);
    while (n > 0 && !IsSep(full[n - 1])) {
	n--;
    }
    full[n] = '\0';
    /* "a/./b", "a/../b", "a//b" */
    start = full;
    if (isalpha((unsigned char) start[0]) && start[1] == ':') {
	start += 2;
    }
    out = p = start;
    while (*p != '\0') {
	char *e = p;

	while (*e != '\0' && !IsSep(*e)) {
	    e++;
	}
	if (e == p || (e - p == 1 && p[0] == '.')) {
	    /* nothing */
	} else if (e - p == 2 && p[0] == '.' && p[1] == '.') {
	    while (out > start && !IsSep(out[-1])) {
		out--;
	    }
	    if (out > start + 1) {
		out--;
	    }
	    while (out > start && !IsSep(out[-1])) {
		out--;
	    }
	} else {
	    memmove(out, p, e - p);
	    out += e - p;
	    *out++ = '/';
	}
	if (out == start && IsSep(*p)) {
	    *out++ = '/';
	}
	p = *e != '\0' ? e + 1 : e;
    }
    while (out > start + 1 && IsSep(out[-1])) {
	out--;
    }
    *out = '\0';
    return full;
}

static int
FileExists(const char *path)
{
    FILE *f = fopen(path, "rb");

    if (f == NULL) {
	return 0;
    }
    fclose(f);
    return 1;
}

/*
 * A file a goc file includes with @include "...", if it is beside the
 * goc file or in one of the two directories above (the art .goh files
 * that start resources of their own): its path, or NULL.
 */
static char *
LocalInclude(const char *path, const char *name)
{
    char    *dir = FullDir(path);
    int	    level;

    for (level = 0; level < 3; level++) {
	char	*p = (char *) GpAlloc(strlen(dir) + strlen(name) + 2);
	size_t	n = strlen(dir);

	sprintf(p, "%s%s%s", dir, n > 0 && IsSep(dir[n - 1]) ? "" : "/",
		name);
	if (FileExists(p)) {
	    free(dir);
	    return p;
	}
	free(p);
	/* one up */
	while (n > 0 && !IsSep(dir[n - 1])) {
	    n--;
	}
	if (n > 1) {
	    n--;
	}
	dir[n] = '\0';
    }
    free(dir);
    return NULL;
}

static void
AddStart(GpStarts *starts, const char *name, size_t len, int isData)
{
    if (starts->count == starts->size) {
	GpStart *n;

	starts->size = starts->size ? 2 * starts->size : 16;
	n = (GpStart *) GpAlloc(starts->size * sizeof(GpStart));
	if (starts->count) {
	    memcpy(n, starts->item, starts->count * sizeof(GpStart));
	}
	free(starts->item);
	starts->item = n;
    }
    starts->item[starts->count].name = GpStrNDup(name, len);
    starts->item[starts->count].isData = isData;
    starts->count++;
}

static int
IsWord(int c)
{
    return isalnum(c) || c == '_';
}

static void
GocStarts(const char *path, GpStarts *starts, GpList *seen)
{
    char    *text, *p, *out;

    if (GpListFind(seen, path) >= 0) {
	return;
    }
    GpListAdd(seen, path);
    text = GpMustRead(path);
    /* the comments out (good enough for goc files) */
    for (p = out = text; *p != '\0'; ) {
	if (p[0] == '/' && p[1] == '*') {
	    char *e = strstr(p + 2, "*/");

	    if (e != NULL) {
		p = e + 2;
		continue;
	    }
	}
	*out++ = *p++;
    }
    *out = '\0';
    /* the lines that begin with @include "file" or @start name */
    for (p = text; *p != '\0'; ) {
	char *q = p;

	while (isspace((unsigned char) *q)) {
	    q++;
	}
	if (strncmp(q, "@include", 8) == 0 &&
	    isspace((unsigned char) q[8])) {
	    q += 8;
	    while (isspace((unsigned char) *q)) {
		q++;
	    }
	    if (*q == '"') {
		char *e = strchr(q + 1, '"');

		if (e != NULL && e > q + 1) {
		    char *name = GpStrNDup(q + 1, e - q - 1);
		    char *inc = LocalInclude(path, name);

		    if (inc != NULL) {
			GocStarts(inc, starts, seen);
			free(inc);
		    }
		    free(name);
		    q = e + 1;
		}
	    }
	} else if (strncmp(q, "@start", 6) == 0 &&
		   isspace((unsigned char) q[6])) {
	    char *name, *e;
	    int isData = 0;

	    q += 6;
	    while (isspace((unsigned char) *q)) {
		q++;
	    }
	    name = q;
	    while (IsWord((unsigned char) *q)) {
		q++;
	    }
	    if (q > name) {
		/* "data" in what follows, up to the ";" */
		for (e = q; *e != '\0' && *e != ';' && *e != '\n'; e++) {
		    if (tolower((unsigned char) e[0]) == 'd' &&
			tolower((unsigned char) e[1]) == 'a' &&
			tolower((unsigned char) e[2]) == 't' &&
			tolower((unsigned char) e[3]) == 'a') {
			isData = 1;
		    }
		}
		AddStart(starts, name, q - name, isData);
	    }
	}
	/* the next line */
	p = q;
	while (*p != '\0' && *p != '\n') {
	    p++;
	}
	if (*p == '\n') {
	    p++;
	}
    }
    free(text);
}

/***********************************************************************
 *				GpGocStarts
 ***********************************************************************
 * SYNOPSIS:	    The "@start name[, flags];" lines of a goc file, in
 *		    order, with those of the local files it includes
 *		    where it includes them (each file once).
 ***********************************************************************/
void
GpGocStarts(const char *path, GpStarts *starts)
{
    GpList seen = {NULL, 0, 0};

    GocStarts(path, starts, &seen);
}
