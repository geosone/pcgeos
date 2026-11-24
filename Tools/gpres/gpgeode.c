/***********************************************************************
 * gpgeode.c -- the GEOS data of a geode module from its .gp file.
 *
 * Usage:  gpgeode [-o OUT.c] [--rev FILE.rev] [--protocol M.m]
 *		   [-D NAME]... [--res RES.h] [--objs OBJS.h]
 *		   GEODE.gp [FILE.goc...]
 *
 * Writes the C definition of __geos_module (CInclude/geosmod.h): what
 * glue put into the geode header from the geode parameter file.
 * Resource numbers are those of gpres (the .gp "resource" lines, then
 * the @start resources of the .goc files); resources made by goc -cg
 * point to their GocResource descriptors.  --res and --objs name the
 * headers of gpres and of cmake/GeosGocObjs.cmake to include (for
 * GOCR_RES_* and the chunk constants of "appobj").  -D defines a name
 * for ifdef/ifndef.
 *
 * Handled like glue (Tools/glue/parse.y): name, longname, tokenchars,
 * tokenid, type (attributes; without "single" the geode is
 * multi-launchable), class, appobj, entry, heapspace, stack,
 * library [noload [fixed]], resource <name> <flags> (RESF_* as glue
 * computes them), export [x as y], publish, skip <n>, skip until <n>,
 * ifdef/ifndef/else/endif.  Ignored: usernotes, platform, ship, exempt,
 * nosort, incminor, load, rev.
 *
 * The protocol comes from --protocol major.minor, else from the first
 * "P major.minor" line of the .rev file (0.0 without one).  The
 * protocol a library needs is 0.0 ("any") for now: glue took it from
 * the library's .ldf file.
 ***********************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "gpscan.h"

#define GA_PROCESS		0x8000
#define GA_LIBRARY		0x4000
#define GA_DRIVER		0x2000
#define GA_SYSTEM		0x0800
#define GA_MULTI_LAUNCHABLE	0x0400
#define GA_APPL			0x0200
#define GA_USES_COPROC		0x0020
#define GA_NEEDS_COPROC		0x0010
#define GA_HAS_GCM		0x0008
#define GA_C_API		0x0004

#define RESF_DISCARDED		0x0002
#define RESF_LMEM		0x0008
#define RESF_SWAPABLE		0x0010
#define RESF_DISCARDABLE	0x0020
#define RESF_SHARED		0x0040
#define RESF_FIXED		0x0080
#define RESF_CONFORMING		0x0100
#define RESF_CODE		0x0200
#define RESF_OBJECT		0x0400
#define RESF_READ_ONLY		0x0800
#define RESF_UI			0x1000
#define RESF_STANDARD		0x0032

typedef struct {
    char    *sym;	/* NULL: a skipped entry */
    char    *name;
    int	    published;
} Export;

typedef struct {
    char    *name;
    int	    flags;
} Library;

typedef struct {
    char    *name;	/* upper case */
    unsigned flags;
} Resource;

static char	gpName[9], gpExt[5];
static char	*longname = NULL;
static char	tokenchars[4];
static long	tokenid = 0;
static unsigned	attrs = 0;
static int	typed = 0, single = 0;
static char	*processClass = NULL, *appobj = NULL, *entry = NULL;
static long	heapspace = 0, stackSize = 0;

static Library	*libraries;
static int	numLibraries;
static Export	*exports;
static int	numExports;
static Resource	*resources;
static int	numResources;

static const char *curLine;	/* for messages */
static int	curLineNo;

static void
Usage(void)
{
    fprintf(stderr,
	    "Usage: gpgeode [-o OUT.c] [--rev FILE.rev] [--protocol M.m] "
	    "[-D NAME]...\n"
	    "               [--res RES.h] [--objs OBJS.h] GEODE.gp "
	    "[FILE.goc...]\n");
    exit(1);
}

static void
Fail(const char *what, const char *arg)
{
    const char	*p = curLine, *e;

    while (isspace((unsigned char) *p)) {
	p++;
    }
    for (e = p; *e != '\0' && *e != '\n' && *e != '\r'; e++) {
	;
    }
    while (e > p && isspace((unsigned char) e[-1])) {
	e--;
    }
    fprintf(stderr, "line %d: %s", curLineNo, what);
    if (arg != NULL) {
	fprintf(stderr, " \"%s\"", arg);
    }
    fprintf(stderr, ": %.*s\n", (int) (e - p), p);
    exit(1);
}

static void *
Grow(void *items, int count, size_t size)
{
    /* (room for one more: the lists are short) */
    void *n = GpAlloc((count + 1) * size);

    if (count) {
	memcpy(n, items, count * size);
    }
    free(items);
    return n;
}

static char *
Lower(const char *s)
{
    char *d = GpStrDup(s), *p;

    for (p = d; *p != '\0'; p++) {
	*p = (char) tolower((unsigned char) *p);
    }
    return d;
}

/* without the quotes around it */
static char *
Unquote(const char *s)
{
    size_t n = strlen(s);

    while (*s == '"') {
	s++, n--;
    }
    while (n > 0 && s[n - 1] == '"') {
	n--;
    }
    return GpStrNDup(s, n);
}

static long
Number(const char *s)
{
    char    *end;
    long    v = strtol(s, &end, 0);

    if (end == s || *end != '\0') {
	Fail("not a number", s);
    }
    return v;
}

/***********************************************************************
 *				ResFlags
 ***********************************************************************
 * SYNOPSIS:	    RESF_* of a resource line, as glue's resArg rules
 *		    compute them.
 ***********************************************************************/
static unsigned
ResFlags(char **args, int count)
{
    unsigned	f = RESF_STANDARD & ~RESF_DISCARDABLE;
    int		i;

    for (i = 0; i < count; i++) {
	const char *a = args[i];

	if (strcmp(a, "read-only") == 0) {
	    f |= RESF_READ_ONLY;
	    if (!(f & RESF_FIXED)) {
		f |= RESF_DISCARDABLE;
	    }
	} else if (strcmp(a, "discardable") == 0) {
	    if (!(f & RESF_FIXED)) {
		f |= RESF_DISCARDABLE;
	    }
	} else if (strcmp(a, "discard-only") == 0) {
	    f = (f & ~RESF_SWAPABLE) | RESF_DISCARDABLE;
	} else if (strcmp(a, "swap-only") == 0) {
	    f = (f & ~RESF_DISCARDABLE) | RESF_SWAPABLE;
	} else if (strcmp(a, "swapable") == 0) {
	    f |= RESF_SWAPABLE;
	} else if (strcmp(a, "no-swap") == 0) {
	    f &= ~RESF_SWAPABLE;
	} else if (strcmp(a, "no-discard") == 0) {
	    f &= ~RESF_DISCARDABLE;
	} else if (strcmp(a, "preload") == 0) {
	    f &= ~RESF_DISCARDED;
	} else if (strcmp(a, "fixed") == 0) {
	    f = (f | RESF_FIXED) & ~(RESF_SWAPABLE | RESF_DISCARDABLE |
				     RESF_DISCARDED | RESF_LMEM);
	} else if (strcmp(a, "conforming") == 0) {
	    f |= RESF_CONFORMING;
	} else if (strcmp(a, "shared") == 0) {
	    f |= RESF_SHARED;
	} else if (strcmp(a, "code") == 0) {
	    f |= RESF_CODE;
	} else if (strcmp(a, "lmem") == 0) {
	    f = (f | RESF_LMEM) & ~RESF_CODE;
	} else if (strcmp(a, "data") == 0 || strcmp(a, "ui-object") == 0 ||
		   strcmp(a, "object") == 0) {
	    if (a[0] == 'd') {
		f &= ~(RESF_CODE | RESF_DISCARDABLE);
	    } else if (a[0] == 'u') {
		f = (f | RESF_OBJECT | RESF_UI | RESF_LMEM | RESF_SHARED) &
		    ~(RESF_CODE | RESF_DISCARDABLE);
	    } else {
		f = (f | RESF_OBJECT | RESF_LMEM) &
		    ~(RESF_CODE | RESF_UI | RESF_DISCARDABLE);
	    }
	    if ((f & (RESF_READ_ONLY | RESF_FIXED)) == RESF_READ_ONLY) {
		f |= RESF_DISCARDABLE;
	    }
	} else {
	    Fail("unknown resource flag", a);
	}
    }
    return f;
}

/*
 * The words of a .gp line; a quoted string is one word (with its
 * quotes); "#" starts a comment.
 */
static void
Tokens(const char *line, const char *end, GpList *words)
{
    const char *p = line;

    while (p < end) {
	const char *q;
	char	   *w;

	if (*p == '#') {
	    break;
	}
	if (isspace((unsigned char) *p) || *p == ',') {
	    p++;
	    continue;
	}
	if (*p == '"') {
	    for (q = p + 1; q < end && *q != '"'; q++) {
		;
	    }
	    if (q == end) {
		Fail("syntax error", NULL);
	    }
	    q++;
	} else {
	    for (q = p; q < end && !isspace((unsigned char) *q) &&
		 *q != ',' && *q != '#'; q++) {
		;
	    }
	}
	w = GpStrNDup(p, q - p);
	GpListAdd(words, w);
	free(w);
	p = q;
    }
}

static void
AddExport(const char *sym, const char *name, int published)
{
    exports = (Export *) Grow(exports, numExports, sizeof(Export));
    exports[numExports].sym = sym ? GpStrDup(sym) : NULL;
    exports[numExports].name = name ? GpStrDup(name) : NULL;
    exports[numExports].published = published;
    numExports++;
}

static void
Need(int n, int count)
{
    if (n < count) {
	Fail("syntax error", NULL);
    }
}

static void
Line(const char *kw, char **a, int n)
{
    int i;

    if (strcmp(kw, "name") == 0) {
	const char *dot;

	Need(n, 1);
	dot = strchr(a[0], '.');
	sprintf(gpName, "%.8s", a[0]);
	if (dot != NULL && dot - a[0] < 8) {
	    gpName[dot - a[0]] = '\0';
	}
	sprintf(gpExt, "%.4s", dot != NULL ? dot + 1 : "");
    } else if (strcmp(kw, "longname") == 0) {
	Need(n, 1);
	longname = Unquote(a[0]);
    } else if (strcmp(kw, "tokenchars") == 0) {
	char *t;

	Need(n, 1);
	t = Unquote(a[0]);
	memset(tokenchars, 0, 4);
	memcpy(tokenchars, t, strlen(t) < 4 ? strlen(t) : 4);
	free(t);
    } else if (strcmp(kw, "tokenid") == 0) {
	Need(n, 1);
	tokenid = Number(a[0]);
    } else if (strcmp(kw, "type") == 0) {
	static const struct {
	    const char	*name;
	    unsigned	bit;
	} types[] = {
	    {"process", GA_PROCESS}, {"library", GA_LIBRARY},
	    {"driver", GA_DRIVER}, {"system", GA_SYSTEM},
	    {"appl", GA_APPL}, {"uses-coproc", GA_USES_COPROC},
	    {"needs-coproc", GA_NEEDS_COPROC}, {"has-gcm", GA_HAS_GCM},
	    {"c-api", GA_C_API}, {"single", 0},
	    {"discardable-dgroup", 0}
	};

	typed = 1;
	for (i = 0; i < n; i++) {
	    char    *t = Lower(a[i]);
	    int	    j, found = 0;

	    for (j = 0; j < (int) (sizeof(types) / sizeof(types[0])); j++) {
		if (strcmp(t, types[j].name) == 0) {
		    attrs |= types[j].bit;
		    found = 1;
		}
	    }
	    if (!found) {
		Fail("unknown type", t);
	    }
	    if (strcmp(t, "single") == 0) {
		single = 1;
	    }
	    free(t);
	}
    } else if (strcmp(kw, "class") == 0) {
	Need(n, 1);
	processClass = GpStrDup(a[0]);
    } else if (strcmp(kw, "appobj") == 0) {
	Need(n, 1);
	appobj = GpStrDup(a[0]);
    } else if (strcmp(kw, "entry") == 0) {
	Need(n, 1);
	entry = GpStrDup(a[0]);
    } else if (strcmp(kw, "heapspace") == 0) {
	char	*v;
	size_t	len;
	int	k;

	Need(n, 1);
	v = GpStrDup(a[0]);
	len = strlen(v);
	k = len > 0 && (v[len - 1] == 'k' || v[len - 1] == 'K');
	while (len > 0 && (v[len - 1] == 'k' || v[len - 1] == 'K')) {
	    v[--len] = '\0';
	}
	heapspace = Number(v) * (k ? 1024 / 16 : 1);
	free(v);
    } else if (strcmp(kw, "stack") == 0) {
	Need(n, 1);
	stackSize = Number(a[0]);
    } else if (strcmp(kw, "library") == 0) {
	int flags = 0;

	Need(n, 1);
	if (n > 1 && GpEqualNoCase(a[1], "noload")) {
	    flags = (n > 2 && GpEqualNoCase(a[2], "fixed")) ? 2 : 1;
	}
	libraries = (Library *) Grow(libraries, numLibraries,
				     sizeof(Library));
	libraries[numLibraries].name = GpStrDup(a[0]);
	libraries[numLibraries].flags = flags;
	numLibraries++;
    } else if (strcmp(kw, "resource") == 0) {
	char	 *up, **low;
	unsigned flags;

	Need(n, 1);
	if (GpEqualNoCase(a[0], "ifdef")) {
	    return;
	}
	low = (char **) GpAlloc(n * sizeof(char *));
	for (i = 1; i < n; i++) {
	    low[i - 1] = Lower(a[i]);
	}
	flags = ResFlags(low, n - 1);
	for (i = 1; i < n; i++) {
	    free(low[i - 1]);
	}
	free(low);
	up = GpUpper(a[0]);
	for (i = 0; i < numResources; i++) {
	    if (strcmp(resources[i].name, up) == 0) {
		break;
	    }
	}
	if (i == numResources) {
	    resources = (Resource *) Grow(resources, numResources,
					  sizeof(Resource));
	    resources[numResources++].name = up;
	} else {
	    free(up);
	}
	resources[i].flags = flags;
    } else if (strcmp(kw, "export") == 0) {
	for (i = 0; i < n; ) {
	    if (i + 2 < n && GpEqualNoCase(a[i + 1], "as")) {
		AddExport(a[i], a[i + 2], 0);
		i += 3;
	    } else {
		AddExport(a[i], a[i], 0);
		i += 1;
	    }
	}
    } else if (strcmp(kw, "publish") == 0) {
	Need(n, 1);
	AddExport(a[0], a[0], 1);
    } else if (strcmp(kw, "skip") == 0) {
	long count;

	Need(n, 1);
	if (GpEqualNoCase(a[0], "until")) {
	    const char *d;

	    Need(n, 2);
	    for (d = a[1]; isdigit((unsigned char) *d); d++) {
		;
	    }
	    if (d == a[1] || *d != '\0') {
		Fail("\"skip until <constant>\" not supported", NULL);
	    }
	    count = strtol(a[1], NULL, 10) - numExports;
	} else {
	    count = Number(a[0]);
	}
	while (count-- > 0) {
	    AddExport(NULL, NULL, 0);
	}
    } else if (strcmp(kw, "usernotes") == 0 ||
	       strcmp(kw, "platform") == 0 || strcmp(kw, "ship") == 0 ||
	       strcmp(kw, "exempt") == 0 || strcmp(kw, "nosort") == 0 ||
	       strcmp(kw, "incminor") == 0 || strcmp(kw, "load") == 0 ||
	       strcmp(kw, "rev") == 0) {
	/* nothing */
    } else {
	Fail("unknown keyword", kw);
    }
}

static void
ParseGp(const char *text, const GpList *defines)
{
    const char	*p = text;
    int		stack[64], depth = 0, active = 1;

    curLineNo = 0;
    while (*p != '\0') {
	const char  *eol = p;
	GpList	    w = {NULL, 0, 0};
	int	    i;

	while (*eol != '\0' && *eol != '\n') {
	    eol++;
	}
	curLine = p;
	curLineNo++;
	Tokens(p, eol, &w);
	if (w.count > 0) {
	    char *kw = Lower(w.item[0]);

	    if (strcmp(kw, "ifdef") == 0 || strcmp(kw, "ifndef") == 0) {
		int defined = w.count > 1 &&
			      GpListFind(defines, w.item[1]) >= 0;

		if (depth == 64) {
		    Fail("ifdef nested too deep", NULL);
		}
		stack[depth++] = active;
		active = active && (kw[2] == 'd' ? defined : !defined);
	    } else if (strcmp(kw, "else") == 0) {
		active = depth > 0 ? (stack[depth - 1] && !active) : !active;
	    } else if (strcmp(kw, "endif") == 0) {
		active = depth > 0 ? stack[--depth] : 1;
	    } else if (active) {
		Line(kw, w.item + 1, w.count - 1);
	    }
	    free(kw);
	}
	for (i = 0; i < w.count; i++) {
	    free(w.item[i]);
	}
	free(w.item);
	p = *eol != '\0' ? eol + 1 : eol;
    }
    if (typed && !single) {
	attrs |= GA_MULTI_LAUNCHABLE;
    }
}

/* the first "P major.minor" line of a .rev file */
static void
RevProtocol(const char *rev, long *major, long *minor)
{
    char    *text, *p;

    *major = *minor = 0;
    if (rev == NULL || (text = GpReadFile(rev)) == NULL) {
	return;
    }
    for (p = text; *p != '\0'; ) {
	if (p[0] == 'P' && isspace((unsigned char) p[1])) {
	    char *q = p + 1, *e;
	    long a, b;

	    while (*q == ' ' || *q == '\t') {
		q++;
	    }
	    if (isdigit((unsigned char) *q)) {
		a = strtol(q, &e, 10);
		if (*e == '.' && isdigit((unsigned char) e[1])) {
		    b = strtol(e + 1, NULL, 10);
		    *major = a;
		    *minor = b;
		    break;
		}
	    }
	}
	while (*p != '\0' && *p != '\n') {
	    p++;
	}
	if (*p == '\n') {
	    p++;
	}
    }
    free(text);
}

/* "text" as a C string */
static void
CString(FILE *f, const char *s)
{
    fputc('"', f);
    for (; *s != '\0'; s++) {
	unsigned char c = (unsigned char) *s;

	if (c >= 0x20 && c < 0x7f && c != '"' && c != '\\') {
	    fputc(c, f);
	} else {
	    fprintf(f, "\\%03o", c);
	}
    }
    fputc('"', f);
}

/* a char array without NUL, padded with spaces to "width" */
static void
CChars(FILE *f, const char *s, int width)
{
    int i, len = (int) strlen(s);

    fputc('{', f);
    for (i = 0; i < width; i++) {
	unsigned char c = (unsigned char) (i < len ? s[i] : ' ');

	if (i > 0) {
	    fprintf(f, ", ");
	}
	if ((c < 0x80 && isalnum(c)) || c == ' ' || c == '.' || c == '_' ||
	    c == '-') {
	    fprintf(f, "'%c'", c);
	} else {
	    fprintf(f, "%d", c);
	}
    }
    fputc('}', f);
}

static int
CompareNames(const void *a, const void *b)
{
    return strcmp(*(char *const *) a, *(char *const *) b);
}

int
main(int argc, char **argv)
{
    const char	*out = NULL, *rev = NULL, *resH = NULL, *objsH = NULL;
    const char	*proto = NULL;
    GpList	defines = {NULL, 0, 0};
    GpList	names = {NULL, 0, 0};	/* upper case, in number order */
    GpList	nameCase = {NULL, 0, 0};/* as first written */
    GpList	found = {NULL, 0, 0};
    GpList	syms = {NULL, 0, 0};
    char	**gocName;		/* per name: as the goc file has it */
    int		*gocData;
    char	*gpText;
    FILE	*f = stdout;
    int		ac = 1, i, j, count, driver;
    long	major, minor;
    size_t	n;

    while (ac < argc && argv[ac][0] == '-') {
	const char *opt = argv[ac++];

	if (strncmp(opt, "-D", 2) == 0) {
	    if (opt[2] != '\0') {
		GpListAdd(&defines, opt + 2);
	    } else if (ac < argc) {
		GpListAdd(&defines, argv[ac++]);
	    } else {
		Usage();
	    }
	    continue;
	}
	if (ac >= argc) {
	    Usage();
	}
	if (strcmp(opt, "-o") == 0) {
	    out = argv[ac++];
	} else if (strcmp(opt, "--rev") == 0) {
	    rev = argv[ac++];
	} else if (strcmp(opt, "--protocol") == 0) {
	    proto = argv[ac++];
	} else if (strcmp(opt, "--res") == 0) {
	    resH = argv[ac++];
	} else if (strcmp(opt, "--objs") == 0) {
	    objsH = argv[ac++];
	} else {
	    Usage();
	}
    }
    if (ac >= argc) {
	Usage();
    }
    n = strlen(argv[ac]);
    if (n < 3 || !GpEqualNoCase(argv[ac] + n - 3, ".gp")) {
	Usage();
    }
    memset(tokenchars, 0, 4);
    gpName[0] = gpExt[0] = '\0';
    gpText = GpMustRead(argv[ac]);
    ParseGp(gpText, &defines);

    /* resource numbers exactly as gpres makes them */
    GpResources(gpText, &found);
    for (i = 0; i < found.count; i++) {
	char *up = GpUpper(found.item[i]);

	if (GpListFind(&names, up) < 0) {
	    GpListAdd(&names, up);
	    GpListAdd(&nameCase, found.item[i]);
	}
	free(up);
    }
    gocName = NULL;
    gocData = NULL;
    {
	GpStarts starts = {NULL, 0, 0};
	int	 size = 0;

	for (i = ac + 1; i < argc; i++) {
	    GpGocStarts(argv[i], &starts);
	}
	/* (the lists per name, as long as "names" can get) */
	size = names.count + starts.count + 1;
	gocName = (char **) GpAlloc(size * sizeof(char *));
	gocData = (int *) GpAlloc(size * sizeof(int));
	for (i = 0; i < size; i++) {
	    gocName[i] = NULL;
	    gocData[i] = 0;
	}
	for (i = 0; i < starts.count; i++) {
	    char *up = GpUpper(starts.item[i].name);

	    j = GpListFind(&names, up);
	    if (j < 0) {
		j = names.count;
		GpListAdd(&names, up);
		GpListAdd(&nameCase, starts.item[i].name);
	    }
	    /* (the last "@start" of a resource says what it is) */
	    gocName[j] = starts.item[i].name;
	    gocData[j] = starts.item[i].isData;
	    free(up);
	}
    }

    if (out != NULL && (f = fopen(out, "w")) == NULL) {
	perror(out);
	return 1;
    }
    fprintf(f, "/* generated by gpgeode from %s */\n", GpBaseName(argv[ac]));
    fprintf(f, "#include <geos.h>\n#include <object.h>\n"
	    "#include <geosmod.h>\n");
    if (resH != NULL) {
	fprintf(f, "#include \"%s\"\n", resH);
    }
    if (objsH != NULL) {
	fprintf(f, "#include \"%s\"\n", objsH);
    }
    fprintf(f, "\n");
    if (processClass != NULL) {
	fprintf(f, "extern ClassStruct %s;\n", processClass);
    }
    for (i = 0; i < names.count; i++) {
	if (gocName[i] != NULL) {
	    fprintf(f, "extern const GocResource __goc_resource_%s;\n",
		    gocName[i]);
	}
    }
    /* the symbols exported and the entry point, each once, sorted */
    for (i = 0; i < numExports; i++) {
	if (exports[i].sym != NULL && GpListFind(&syms, exports[i].sym) < 0) {
	    GpListAdd(&syms, exports[i].sym);
	}
    }
    if (entry != NULL && GpListFind(&syms, entry) < 0) {
	GpListAdd(&syms, entry);
    }
    if (syms.count > 1) {
	qsort(syms.item, syms.count, sizeof(char *), CompareNames);
    }
    for (i = 0; i < syms.count; i++) {
	if (processClass == NULL || strcmp(syms.item[i], processClass) != 0) {
	    fprintf(f, "extern char %s[];\n", syms.item[i]);
	}
    }
    driver = (attrs & GA_DRIVER) != 0;
    if (driver && GpListFind(&syms, "DriverTable") < 0) {
	fprintf(f, "extern char DriverTable[];\n");
    }
    fprintf(f, "\n");

    count = GP_FIRST_RESOURCE + names.count;
    fprintf(f, "Handle __goc_resource_handles[%d];\n", count);
    fprintf(f, "static const GeosModuleLibrary libraries[] = {\n");
    for (i = 0; i < numLibraries; i++) {
	fprintf(f, "    {");
	CString(f, libraries[i].name);
	fprintf(f, ", 0, 0, %d},\n", libraries[i].flags);
    }
    fprintf(f, "    {0, 0, 0, 0}\n};\n");
    fprintf(f, "static const GeosModuleExport exports[] = {\n");
    for (i = 0; i < numExports; i++) {
	if (exports[i].sym == NULL) {
	    fprintf(f, "    {0, 0, 0},\n");
	} else {
	    fprintf(f, "    {(const void *) &%s, ", exports[i].sym);
	    CString(f, exports[i].name);
	    fprintf(f, ", %d},\n", exports[i].published ? 1 : 0);
	}
    }
    fprintf(f, "    {0, 0, 0}\n};\n");
    fprintf(f, "static const GeosModuleResource resources[] = {\n");
    fprintf(f, "    {0, \"(core block)\", 0},\n");
    fprintf(f, "    {0, \"(fixed data)\", 0},\n");
    for (i = 0; i < names.count; i++) {
	unsigned flags;

	for (j = 0; j < numResources; j++) {
	    if (strcmp(resources[j].name, names.item[i]) == 0) {
		break;
	    }
	}
	if (j < numResources) {
	    flags = resources[j].flags;
	} else if (gocName[i] != NULL) {
	    /* only in a .goc file: an LMem block (glue: an object block
	     * unless "@start x, data") */
	    char *arg = gocData[i] ? "lmem" : "object";

	    flags = ResFlags(&arg, 1);
	} else {
	    flags = ResFlags(NULL, 0);
	}
	fprintf(f, "    {");
	if (gocName[i] != NULL) {
	    fprintf(f, "&__goc_resource_%s", gocName[i]);
	} else {
	    fprintf(f, "0");
	}
	fprintf(f, ", ");
	CString(f, nameCase.item[i]);
	fprintf(f, ", 0x%04x},\n", flags);
    }
    fprintf(f, "};\n\n");

    if (proto != NULL) {
	char *e;

	major = strtol(proto, &e, 10);
	if (e == proto || *e != '.') {
	    Usage();
	}
	minor = strtol(e + 1, NULL, 10);
    } else {
	RevProtocol(rev, &major, &minor);
    }
    fprintf(f, "const GeosModuleInfo __geos_module = {\n");
    fprintf(f, "    GEOS_MODULE_MAGIC, GEOS_MODULE_VERSION,\n    ");
    CChars(f, gpName, 8);
    fprintf(f, ", ");
    CChars(f, gpExt, 4);
    fprintf(f, ",\n    ");
    if (longname != NULL) {
	CString(f, longname);
    } else {
	fprintf(f, "0");
    }
    fprintf(f, ",\n    {{%d, %d, %d, %d}, %ld},\n",
	    (unsigned char) tokenchars[0], (unsigned char) tokenchars[1],
	    (unsigned char) tokenchars[2], (unsigned char) tokenchars[3],
	    tokenid);
    fprintf(f, "    0x%04x, %ld, %ld, %ld, %ld,\n", attrs, major, minor,
	    heapspace, stackSize);
    if (processClass != NULL) {
	fprintf(f, "    &%s,\n", processClass);
    } else {
	fprintf(f, "    0,\n");
    }
    if (appobj != NULL) {
	fprintf(f, "    GOCR_XSOPTR(%s),\n", appobj);
    } else {
	fprintf(f, "    0,\n");
    }
    if (entry != NULL) {
	fprintf(f, "    (const void *) &%s,\n", entry);
    } else {
	fprintf(f, "    0,\n");
    }
    fprintf(f, "    %d, libraries,\n", numLibraries);
    fprintf(f, "    %d, exports,\n", numExports);
    fprintf(f, "    %d, resources,\n", count);
    fprintf(f, "    __goc_resource_handles,\n");
    fprintf(f, "    %s\n};\n", driver ? "(const void *) &DriverTable" : "0");
    if (out != NULL && fclose(f) != 0) {
	perror(out);
	return 1;
    }
    return 0;
}
