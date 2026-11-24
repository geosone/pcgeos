/***********************************************************************
 * kexports.c -- the export table of the C kernel ("geos" library).
 *
 * A geode imports kernel routines and classes by name (Port32/ABI.md
 * section 7).  The kernel exports what Library/Kernel/geos.gp exports
 * and the C kernel defines: the global symbols of the kernel's object
 * files (from nm) whose names match an export of geos.gp, ignoring case
 * (the .gp file lists the asm names and the upper-case C stubs; the C
 * kernel defines the C names).  Port32 adds the C entry points that
 * have no asm counterpart (port32Exports) and the GHAL host services
 * for drivers (ghalExports, defined in the ghal library), and the four
 * routines a C compiler may call on its own even in a freestanding
 * geode, for structure copies and zeroing (compilerExports, from the
 * host's C library).
 *
 * Writes a C file with kernelExports[], sorted by name for a binary
 * search, and kernelExportCount.  Each entry carries its entry number
 * in geos.gp (exports and "skip" counted as glue does; the Port32
 * extras get 0x8000 + n): references to kernel entries in files use it,
 * so it stays the same when the C kernel defines more of them
 * (Port32/ABI.md 13d).
 *
 * usage: kexports --syms FILE --gp geos.gp [--underscore]
 *		   [--extra NAME]... -o out.c
 *
 * FILE is what "nm -g --defined-only" prints for the kernel's object
 * files (the build runs nm: this program starts no other).
 ***********************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* C entry points of Port32 without an asm export (Port32/ABI.md
 * section 5) */
static const char *const port32Exports[] = {
    "CObjMessageP", "CObjCallSuperP", "CMessageDispatchP",
    NULL
};
/* more of them, numbered after the lists below (their entry numbers
 * stay) */
static const char *const port32LateExports[] = {
    "GrLoadGStringPtr", "ThreadPrivData", "ObjGetMessageParams",
    "ObjSetMessageParams", "QueuePeekLastMessage",
    NULL
};
/* GHAL host services the drivers on the GHAL use (Port32/GHAL-Concept.md
 * section 4, level 2): exported always, as they live in the ghal library
 * beside the kernel.  New names go at the end (their entry numbers). */
static const char *const ghalExports[] = {
    "ghal_crit_enter", "ghal_crit_leave", "ghal_log",
    "ghal_time_monotonic_ns",
    "ghal_irq_attach", "ghal_irq_detach", "ghal_irq_active",
    "ghal_input_read", "ghal_input_post", "ghal_input_dropped",
    "ghal_display_open", "ghal_display_flush", "ghal_display_close",
    "ghal_display_close_requested", "ghal_display_read_back",
    "ghal_display_backend",
    /* GAPI the drivers and libraries use (CInclude/gapi.h): the CUPS
     * port driver, the spooler's host printers */
    "gapi_print_list", "gapi_print_default", "gapi_print_job_open",
    "gapi_print_job_write",
    "gapi_print_job_close", "gapi_print_job_cancel", "gapi_last_error",
    /* the host power driver (Driver/Power/Host) */
    "gapi_power_status",
    NULL
};
/* gcc and clang require these of a freestanding environment */
static const char *const compilerExports[] = {
    "memcpy", "memmove", "memset", "memcmp",
    NULL
};

typedef struct {
    char    **item;
    int	    *value;
    int	    count;
    int	    size;
} List;

static void *
Alloc(size_t size)
{
    void *p = malloc(size ? size : 1);

    if (p == NULL) {
	fprintf(stderr, "kexports: out of memory\n");
	exit(1);
    }
    return p;
}

static int
Find(const List *list, const char *s)
{
    int i;

    for (i = 0; i < list->count; i++) {
	if (strcmp(list->item[i], s) == 0) {
	    return i;
	}
    }
    return -1;
}

static void
Add(List *list, const char *s, size_t len, int value)
{
    if (list->count == list->size) {
	char	**n;
	int	*v;

	list->size = list->size ? 2 * list->size : 256;
	n = (char **) Alloc(list->size * sizeof(char *));
	v = (int *) Alloc(list->size * sizeof(int));
	if (list->count) {
	    memcpy(n, list->item, list->count * sizeof(char *));
	    memcpy(v, list->value, list->count * sizeof(int));
	}
	free(list->item);
	free(list->value);
	list->item = n;
	list->value = v;
    }
    list->item[list->count] = (char *) Alloc(len + 1);
    memcpy(list->item[list->count], s, len);
    list->item[list->count][len] = '\0';
    list->value[list->count] = value;
    list->count++;
}

static int
InNames(const char *const *names, const char *s)
{
    for (; *names != NULL; names++) {
	if (strcmp(*names, s) == 0) {
	    return 1;
	}
    }
    return 0;
}

/* a word of the line at *p, in upper case if wanted; NULL at the end */
static char *
Word(char **p, int upper)
{
    char *s, *w;

    while (isspace((unsigned char) **p)) {
	(*p)++;
    }
    if (**p == '\0') {
	return NULL;
    }
    for (s = *p; **p != '\0' && !isspace((unsigned char) **p); (*p)++) {
	if (upper) {
	    **p = (char) toupper((unsigned char) **p);
	}
    }
    w = (char *) Alloc(*p - s + 1);
    memcpy(w, s, *p - s);
    w[*p - s] = '\0';
    return w;
}

static int
WordIs(const char *w, const char *lower)
{
    for (; *w != '\0' && *lower != '\0'; w++, lower++) {
	if (tolower((unsigned char) *w) != *lower) {
	    return 0;
	}
    }
    return *w == *lower;
}

/*
 * The exports of geos.gp: upper-case name -> entry number
 */
static void
GpNames(const char *path, List *names)
{
    FILE    *f = fopen(path, "r");
    char    line[2048];
    int	    n = 0;

    if (f == NULL) {
	perror(path);
	exit(1);
    }
    while (fgets(line, sizeof(line), f) != NULL) {
	char *p = line, *e, *kw, *a, *as, *b;

	if ((e = strchr(line, '#')) != NULL) {
	    *e = '\0';
	}
	kw = Word(&p, 0);
	if (kw == NULL) {
	    continue;
	}
	if (WordIs(kw, "export") || WordIs(kw, "publish")) {
	    if ((a = Word(&p, 1)) != NULL) {
		if (Find(names, a) < 0) {
		    Add(names, a, strlen(a), n);
		}
		as = Word(&p, 0);
		if (as != NULL && WordIs(as, "as") &&
		    (b = Word(&p, 1)) != NULL) {
		    if (Find(names, b) < 0) {
			Add(names, b, strlen(b), n);
		    }
		    free(b);
		}
		free(as);
		free(a);
		n++;
	    }
	} else if (WordIs(kw, "skip")) {
	    if ((a = Word(&p, 0)) != NULL) {
		if (isdigit((unsigned char) a[0])) {
		    n += atoi(a);
		}
		free(a);
	    }
	}
	free(kw);
    }
    fclose(f);
}

/*
 * The global symbols the object files define, from nm's output
 */
static void
DefinedSymbols(const char *path, int underscore, List *syms)
{
    FILE    *f = fopen(path, "r");
    char    line[2048];

    if (f == NULL) {
	perror(path);
	exit(1);
    }
    while (fgets(line, sizeof(line), f) != NULL) {
	char *p = line, *w[4];
	int n = 0, i;

	while (n < 4 && (w[n] = Word(&p, 0)) != NULL) {
	    n++;
	}
	if (n == 3 && w[1][0] != '\0' && strstr("TDRBCGS", w[1]) != NULL) {
	    const char *name = w[2];

	    if (!underscore || *name == '_') {
		if (underscore) {
		    name++;
		}
		if (Find(syms, name) < 0) {
		    Add(syms, name, strlen(name), 0);
		}
	    }
	}
	for (i = 0; i < n; i++) {
	    free(w[i]);
	}
    }
    fclose(f);
}

static char *
Upper(const char *s)
{
    char *d = (char *) Alloc(strlen(s) + 1), *p;

    strcpy(d, s);
    for (p = d; *p != '\0'; p++) {
	*p = (char) toupper((unsigned char) *p);
    }
    return d;
}

static int
CompareNames(const void *a, const void *b)
{
    return strcmp(*(char *const *) a, *(char *const *) b);
}

static void
AddAll(List *list, const char *const *names)
{
    for (; *names != NULL; names++) {
	if (Find(list, *names) < 0) {
	    Add(list, *names, strlen(*names), 0);
	}
    }
}

int
main(int argc, char **argv)
{
    const char	*symsFile = NULL, *gp = NULL, *out = NULL;
    int		underscore = 0, i;
    List	wanted = {NULL, NULL, 0, 0};
    List	extra = {NULL, NULL, 0, 0};	/* the order numbers them */
    List	given = {NULL, NULL, 0, 0};
    List	syms = {NULL, NULL, 0, 0};
    List	names = {NULL, NULL, 0, 0};
    FILE	*f;

    for (i = 1; i < argc; i++) {
	if (strcmp(argv[i], "--underscore") == 0) {
	    underscore = 1;
	} else if (i + 1 < argc && strcmp(argv[i], "--syms") == 0) {
	    symsFile = argv[++i];
	} else if (i + 1 < argc && strcmp(argv[i], "--gp") == 0) {
	    gp = argv[++i];
	} else if (i + 1 < argc && strcmp(argv[i], "--extra") == 0) {
	    Add(&given, argv[i + 1], strlen(argv[i + 1]), 0);
	    i++;
	} else if (i + 1 < argc && strcmp(argv[i], "-o") == 0) {
	    out = argv[++i];
	} else {
	    symsFile = NULL;
	    break;
	}
    }
    if (symsFile == NULL || gp == NULL || out == NULL) {
	fprintf(stderr, "usage: kexports --syms FILE --gp geos.gp "
		"[--underscore] [--extra NAME]... -o out.c\n");
	return 2;
    }

    GpNames(gp, &wanted);
    AddAll(&extra, port32Exports);
    for (i = 0; i < given.count; i++) {
	if (!InNames(port32Exports, given.item[i])) {
	    /* (as often as it is given: the numbers count them) */
	    Add(&extra, given.item[i], strlen(given.item[i]), 0);
	}
    }
    DefinedSymbols(symsFile, underscore, &syms);
    for (i = 0; i < syms.count; i++) {
	char *up = Upper(syms.item[i]);

	if (Find(&wanted, up) >= 0 || Find(&extra, syms.item[i]) >= 0 ||
	    InNames(port32LateExports, syms.item[i])) {
	    Add(&names, syms.item[i], strlen(syms.item[i]), 0);
	}
	free(up);
    }
    {
	/* what is not in "extra" yet, as it is now */
	int had = extra.count, j;
	const char *const *lists[3];
	const char *const *n;

	lists[0] = ghalExports;
	lists[1] = compilerExports;
	lists[2] = port32LateExports;
	for (j = 0; j < 3; j++) {
	    for (n = lists[j]; *n != NULL; n++) {
		int k, found = 0;

		for (k = 0; k < had; k++) {
		    if (strcmp(extra.item[k], *n) == 0) {
			found = 1;
		    }
		}
		if (!found) {
		    Add(&extra, *n, strlen(*n), 0);
		}
	    }
	}
    }
    AddAll(&names, ghalExports);
    AddAll(&names, compilerExports);
    if (names.count > 1) {
	qsort(names.item, names.count, sizeof(char *), CompareNames);
    }

    if ((f = fopen(out, "w")) == NULL) {
	perror(out);
	return 1;
    }
    fprintf(f, "/* generated by kexports -- do not edit */\n");
    fprintf(f, "typedef struct { const char *name; const void *addr; "
	    "unsigned short entry; } KernelExport;\n\n");
    fprintf(f, "#include <string.h>\n");
    for (i = 0; i < names.count; i++) {
	if (!InNames(compilerExports, names.item[i])) {
	    fprintf(f, "extern char %s;\n", names.item[i]);
	}
    }
    fprintf(f, "\nconst KernelExport kernelExports[] = {\n");
    for (i = 0; i < names.count; i++) {
	char	*up = Upper(names.item[i]);
	int	w = Find(&wanted, up), entry;

	free(up);
	if (w >= 0) {
	    entry = wanted.value[w];
	} else {
	    entry = 0x8000 + Find(&extra, names.item[i]);
	}
	if (InNames(compilerExports, names.item[i])) {
	    fprintf(f, "    { \"%s\", (const void *) %s, 0x%04x },\n",
		    names.item[i], names.item[i], entry);
	} else {
	    fprintf(f, "    { \"%s\", &%s, 0x%04x },\n", names.item[i],
		    names.item[i], entry);
	}
    }
    if (names.count == 0) {
	fprintf(f, "    { 0, 0, 0 }\n");
    }
    fprintf(f, "};\n");
    fprintf(f, "const unsigned kernelExportCount = %d;\n", names.count);
    if (fclose(f) != 0) {
	perror(out);
	return 1;
    }
    return 0;
}
