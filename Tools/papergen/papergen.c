/***********************************************************************
 * papergen.c -- the spooler's page size tables in C from the asm.
 *
 * Reads Library/Spool/Lib/libTables.asm (with libConstant.def's order
 * of the AddPaperSize entries) and writes a C file with, per PageType
 * (paper, envelope, label, postcard), the pre-defined sizes (width,
 * height, default PageLayout, name) and the default order, and the
 * default page size information.  Conditional parts (if _GPC,
 * _NIKE_EUROPE, PZ_PCGEOS ...) are taken as the standard PC/GEOS build
 * has them: false, so the else branches count.
 *
 *	papergen -o libPaperTables.c libTables.asm
 ***********************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define NUM_TYPES   4
static const char *const typeNames[NUM_TYPES] = {
    "paper", "envelope", "label", "postcard"
};
static const char *const tableNames[NUM_TYPES] = {
    "PaperSizes", "EnvelopeSizes", "LabelSizes", "PostcardSizes"
};
static const char *const orderChunks[NUM_TYPES] = {
    "DefaultPaperOrder", "DefaultEnvelopeOrder", "DefaultLabelOrder",
    "DefaultPostcardOrder"
};
#define PT_LABEL_VALUE	4

#define NUM_DEFAULTS	7
static const char *const defaultNames[NUM_DEFAULTS] = {
    "defaultWidthChunk", "defaultHeightChunk", "defaultLayoutChunk",
    "defaultMarginLeftChunk", "defaultMarginTopChunk",
    "defaultMarginRightChunk", "defaultMarginBottomChunk"
};

typedef struct {
    long    width, height;
    unsigned layout;
    char    *name;
} Size;

typedef struct {
    char    *name;
    long    value;
} Named;

static Size	*sizes[NUM_TYPES];
static int	numSizes[NUM_TYPES], haveTable[NUM_TYPES];
static int	*orders[NUM_TYPES];
static int	numOrders[NUM_TYPES], haveOrder[NUM_TYPES];
static Named	*equs, *enums;
static int	numEqus, numEnums;
static long	defaults[NUM_DEFAULTS];
static int	haveDefault[NUM_DEFAULTS];

static void
Fail(const char *what, const char *arg)
{
    fprintf(stderr, "papergen: %s%s%s\n", what, arg ? " " : "",
	    arg ? arg : "");
    exit(1);
}

static void *
Alloc(size_t size)
{
    void *p = malloc(size ? size : 1);

    if (p == NULL) {
	Fail("out of memory", NULL);
    }
    return p;
}

static void *
Grow(void *items, int count, size_t size)
{
    void *n = Alloc((count + 1) * size);

    if (count) {
	memcpy(n, items, count * size);
    }
    free(items);
    return n;
}

static char *
Dup(const char *s, size_t n)
{
    char *d = (char *) Alloc(n + 1);

    memcpy(d, s, n);
    d[n] = '\0';
    return d;
}

static void
SetNamed(Named **list, int *count, char *name, long value)
{
    int i;

    for (i = 0; i < *count; i++) {
	if (strcmp((*list)[i].name, name) == 0) {
	    (*list)[i].value = value;
	    free(name);
	    return;
	}
    }
    *list = (Named *) Grow(*list, *count, sizeof(Named));
    (*list)[*count].name = name;
    (*list)[*count].value = value;
    (*count)++;
}

static long
GetNamed(const Named *list, int count, const char *name, const char *what)
{
    int i;

    for (i = 0; i < count; i++) {
	if (strcmp(list[i].name, name) == 0) {
	    return list[i].value;
	}
    }
    Fail(what, name);
    return 0;
}

static int
IndexOf(const char *const *names, int count, const char *name)
{
    int i;

    for (i = 0; i < count; i++) {
	if (strcmp(names[i], name) == 0) {
	    return i;
	}
    }
    return -1;
}

/*
 * Small matchers on a line: each moves *p on and returns whether it
 * found its part.
 */
static int
IsWordChar(int c)
{
    return isalnum(c) || c == '_';
}

static void
Blanks(const char **p)
{
    while (isspace((unsigned char) **p)) {
	(*p)++;
    }
}

static int
Blanks1(const char **p)		/* at least one */
{
    if (!isspace((unsigned char) **p)) {
	return 0;
    }
    Blanks(p);
    return 1;
}

static char *
Word(const char **p)		/* NULL: none */
{
    const char *s = *p;

    while (IsWordChar((unsigned char) **p)) {
	(*p)++;
    }
    return *p > s ? Dup(s, *p - s) : NULL;
}

static int
Digits(const char **p, long *value)
{
    const char *s = *p;

    while (isdigit((unsigned char) **p)) {
	(*p)++;
    }
    if (*p == s) {
	return 0;
    }
    *value = strtol(s, NULL, 10);
    return 1;
}

static int
Keyword(const char **p, const char *kw)
{
    size_t n = strlen(kw);

    if (strncmp(*p, kw, n) != 0) {
	return 0;
    }
    *p += n;
    return 1;
}

static int
Comma(const char **p)		/* blanks, a comma, blanks */
{
    Blanks(p);
    if (**p != ',') {
	return 0;
    }
    (*p)++;
    Blanks(p);
    return 1;
}

/* PO_PORTRAIT ...: the orientation's value, -1 if it is none */
static int
Orientation(const char *name)
{
    if (strcmp(name, "PO_PORTRAIT") == 0 || strcmp(name, "EO_PORTRAIT") == 0 ||
	strcmp(name, "PCO_PORTRAIT") == 0) {
	return 0;
    }
    if (strcmp(name, "PO_LANDSCAPE") == 0 ||
	strcmp(name, "EO_LANDSCAPE") == 0 ||
	strcmp(name, "PCO_LANDSCAPE") == 0) {
	return 1;
    }
    return -1;
}

static int
TypeValue(const char *name)
{
    static const char *const names[NUM_TYPES] = {
	"PT_PAPER", "PT_ENVELOPE", "PT_LABEL", "PT_POSTCARD"
    };
    int i = IndexOf(names, NUM_TYPES, name);

    return i < 0 ? -1 : 2 * i;
}

/* NAME equ Record<a, b>: a layout */
static void
Equate(const char *line)
{
    const char	*p = line;
    char	*name, *rec, *a = NULL, *b = NULL;

    if ((name = Word(&p)) == NULL) {
	return;
    }
    if (Blanks1(&p) && Keyword(&p, "equ") && Blanks1(&p) &&
	(rec = Word(&p)) != NULL) {
	free(rec);
	if (*p == '<') {
	    p++;
	    Blanks(&p);
	    if ((a = Word(&p)) != NULL && Comma(&p) &&
		(b = Word(&p)) != NULL) {
		Blanks(&p);
		if (*p == '>') {
		    int o = Orientation(a), t = TypeValue(b);

		    if (o < 0) {
			Fail("unknown orientation", a);
		    }
		    if (t < 0) {
			Fail("unknown page type", b);
		    }
		    SetNamed(&equs, &numEqus, name, (o << 3) | t);
		    name = NULL;
		}
	    }
	}
    }
    free(name);
    free(a);
    free(b);
}

static int cur = -1;		/* the table being read */
static int order = -1;		/* the order chunk being read */

static void
Line(const char *line)
{
    const char	*p = line;
    char	*w;
    long	width, height;

    /* AddPaperTable <name> */
    if (Keyword(&p, "AddPaperTable") && Blanks1(&p) &&
	(w = Word(&p)) != NULL) {
	cur = IndexOf(tableNames, NUM_TYPES, w);
	if (cur < 0) {
	    Fail("unknown paper table", w);
	}
	free(w);
	numSizes[cur] = 0;
	haveTable[cur] = 1;
	return;
    }
    /* AddPaperSize w, h, <label>|layout, enum, "name"[, across, down] */
    p = line;
    if (Keyword(&p, "AddPaperSize") && Blanks1(&p) && Digits(&p, &width) &&
	Comma(&p) && Digits(&p, &height) && Comma(&p)) {
	char	*lay = NULL, *en = NULL;
	int	label = 0;

	if (strncmp(p, "<label>", 7) == 0) {
	    label = 1;
	    p += 7;
	} else {
	    lay = Word(&p);
	}
	if ((label || lay != NULL) && Comma(&p) && (en = Word(&p)) != NULL &&
	    Comma(&p) && *p == '"' && strchr(p + 1, '"') != NULL) {
	    const char	*q = strchr(p + 1, '"');
	    char	*name = Dup(p + 1, q - p - 1);
	    long	across = 0, down = 0;
	    int		counts = 0;
	    unsigned	layout;

	    p = q + 1;
	    if (Comma(&p) && Digits(&p, &across) && Comma(&p) &&
		Digits(&p, &down)) {
		counts = 1;
	    }
	    if (label) {
		if (!counts) {
		    Fail("a label without its counts:", name);
		}
		layout = (unsigned) ((down << 9) | (across << 3) |
				     PT_LABEL_VALUE);
	    } else {
		layout = (unsigned) GetNamed(equs, numEqus, lay,
					     "unknown layout");
	    }
	    if (cur < 0) {
		Fail("a size before any table:", name);
	    }
	    SetNamed(&enums, &numEnums, en, numSizes[cur]);
	    sizes[cur] = (Size *) Grow(sizes[cur], numSizes[cur],
				       sizeof(Size));
	    sizes[cur][numSizes[cur]].width = width;
	    sizes[cur][numSizes[cur]].height = height;
	    sizes[cur][numSizes[cur]].layout = layout;
	    sizes[cur][numSizes[cur]].name = name;
	    numSizes[cur]++;
	    free(lay);
	    return;
	}
	free(lay);
	free(en);
    }
    /* <name> chunk byte: an order */
    p = line;
    if ((w = Word(&p)) != NULL) {
	int i = IndexOf(orderChunks, NUM_TYPES, w);

	free(w);
	if (i >= 0 && Blanks1(&p) && Keyword(&p, "chunk") && Blanks1(&p) &&
	    Keyword(&p, "byte")) {
	    order = i;
	    numOrders[i] = 0;
	    haveOrder[i] = 1;
	    return;
	}
    }
    /* MakeDefaultOrderEntry %<enum> */
    p = line;
    if (order >= 0 && Keyword(&p, "MakeDefaultOrderEntry") && Blanks1(&p) &&
	*p == '%') {
	p++;
	if ((w = Word(&p)) != NULL) {
	    orders[order] = (int *) Grow(orders[order], numOrders[order],
					 sizeof(int));
	    orders[order][numOrders[order]++] =
		(int) GetNamed(enums, numEnums, w, "unknown paper size");
	    free(w);
	    return;
	}
    }
    /* <name> endc */
    p = line;
    if ((w = Word(&p)) != NULL) {
	free(w);
	if (Blanks1(&p) && Keyword(&p, "endc")) {
	    order = -1;
	    return;
	}
    }
    /* LocalDefString <name> <"digits" */
    p = line;
    if (Keyword(&p, "LocalDefString") && Blanks1(&p) &&
	(w = Word(&p)) != NULL) {
	long v;

	Blanks(&p);
	if (*p == '<') {
	    p++;
	    Blanks(&p);
	    if (*p == '"') {
		p++;
		if (Digits(&p, &v) && *p == '"') {
		    int i = IndexOf(defaultNames, NUM_DEFAULTS, w);

		    if (i >= 0) {
			defaults[i] = v;
			haveDefault[i] = 1;
		    }
		}
	    }
	}
	free(w);
    }
}

/*
 * The file's lines without comments, continuations joined,
 * conditionals resolved (every condition false): each goes to Equate
 * (first pass) or Line (second pass).
 */
static void
Lines(char *text, void (*handle)(const char *line))
{
    /* per level of "if": taking now, a branch was taken */
    int	    taking[64], taken[64], depth = 0;
    char    *p = text, *cont = Dup("", 0);

    while (*p != '\0') {
	char	*eol = strchr(p, '\n'), *line, *e, *full, *s;
	size_t	n;
	int	i, all;

	n = eol != NULL ? (size_t) (eol - p) : strlen(p);
	line = Dup(p, n);
	p += n + (eol != NULL ? 1 : 0);
	/* the comment off: the first ";" after the last quote */
	e = strrchr(line, '"');
	e = strchr(e != NULL ? e : line, ';');
	if (e != NULL) {
	    *e = '\0';
	}
	n = strlen(line);
	while (n > 0 && isspace((unsigned char) line[n - 1])) {
	    line[--n] = '\0';
	}
	full = (char *) Alloc(strlen(cont) + n + 2);
	strcpy(full, cont);
	strcat(full, line);
	free(line);
	free(cont);
	if (n > 0 && full[strlen(full) - 1] == '\\') {
	    /* goes on in the next line */
	    full[strlen(full) - 1] = ' ';
	    cont = full;
	    continue;
	}
	cont = Dup("", 0);

	for (s = full; isspace((unsigned char) *s); s++) {
	    ;
	}
	if (*s != '\0') {
	    char kw[8];

	    for (i = 0; i < 7 && s[i] != '\0' &&
		 !isspace((unsigned char) s[i]); i++) {
		kw[i] = (char) tolower((unsigned char) s[i]);
	    }
	    kw[i] = '\0';
	    if (s[i] != '\0' && !isspace((unsigned char) s[i])) {
		kw[0] = '\0';		/* a longer word */
	    }
	    if (strcmp(kw, "if") == 0) {
		if (depth == 64) {
		    Fail("if nested too deep", NULL);
		}
		taking[depth] = taken[depth] = 0;
		depth++;
	    } else if (strcmp(kw, "elseif") == 0) {
		if (depth > 0) {
		    taking[depth - 1] = 0;
		}
	    } else if (strcmp(kw, "else") == 0) {
		if (depth > 0) {
		    taking[depth - 1] = !taken[depth - 1];
		    taken[depth - 1] = 1;
		}
	    } else if (strcmp(kw, "endif") == 0) {
		if (depth > 0) {
		    depth--;
		}
	    } else {
		for (all = 1, i = 0; i < depth; i++) {
		    all = all && taking[i];
		}
		if (all) {
		    handle(s);
		}
	    }
	}
	free(full);
    }
    free(cont);
}

static char *
ReadText(const char *path)
{
    FILE    *f = fopen(path, "rb");
    char    *text, *p, *out;
    size_t  len = 0, size = 65536, n;

    if (f == NULL) {
	perror(path);
	exit(1);
    }
    text = (char *) Alloc(size);
    while ((n = fread(text + len, 1, size - len - 1, f)) > 0) {
	len += n;
	if (len + 1 >= size) {
	    char *t = (char *) Alloc(size * 2);

	    memcpy(t, text, len);
	    free(text);
	    text = t;
	    size *= 2;
	}
    }
    fclose(f);
    text[len] = '\0';
    /* no carriage returns, no "COMMENT @ ... @" blocks */
    for (p = out = text; *p != '\0'; ) {
	if (*p == '\r') {
	    p++;
	} else if (strncmp(p, "COMMENT", 7) == 0) {
	    char *q = p + 7, *e;

	    while (isspace((unsigned char) *q) || *q == '\r') {
		q++;
	    }
	    if (*q == '@' && (e = strchr(q + 1, '@')) != NULL) {
		p = e + 1;
	    } else {
		*out++ = *p++;
	    }
	} else {
	    *out++ = *p++;
	}
    }
    *out = '\0';
    return text;
}

int
main(int argc, char **argv)
{
    char    *text;
    FILE    *f;
    int	    k, i;

    if (argc != 4 || strcmp(argv[1], "-o") != 0) {
	fprintf(stderr, "usage: papergen -o libPaperTables.c "
		"libTables.asm\n");
	return 2;
    }
    text = ReadText(argv[3]);
    Lines(text, Equate);
    Lines(text, Line);
    for (k = 0; k < NUM_TYPES; k++) {
	if (!haveTable[k] || !haveOrder[k]) {
	    fprintf(stderr, "papergen: no %s table or order\n",
		    typeNames[k]);
	    return 1;
	}
    }
    for (i = 0; i < NUM_DEFAULTS; i++) {
	if (!haveDefault[i]) {
	    Fail("no", defaultNames[i]);
	}
    }
    if ((f = fopen(argv[2], "w")) == NULL) {
	perror(argv[2]);
	return 1;
    }
    fprintf(f, "/* generated by papergen from libTables.asm;\n"
	    " * do not edit */\n#include \"libPaper.h\"\n\n");
    for (k = 0; k < NUM_TYPES; k++) {
	fprintf(f, "const PaperSizeEntry %sSizes[] = {\n", typeNames[k]);
	for (i = 0; i < numSizes[k]; i++) {
	    fprintf(f, "    { %4ld, %4ld, 0x%04x, \"%s\" },\t/* %d */\n",
		    sizes[k][i].width, sizes[k][i].height,
		    sizes[k][i].layout, sizes[k][i].name, i);
	}
	fprintf(f, "};\nconst byte %sDefaultOrder[] = { ", typeNames[k]);
	for (i = 0; i < numOrders[k]; i++) {
	    fprintf(f, "%s%d", i ? ", " : "", orders[k][i]);
	}
	fprintf(f, " };\n\n");
    }
    fprintf(f, "const PaperSizeTable paperSizeTables[4] = {\n");
    for (k = 0; k < NUM_TYPES; k++) {
	fprintf(f, "    { %sSizes, %d, %sDefaultOrder, %d },\n",
		typeNames[k], numSizes[k], typeNames[k], numOrders[k]);
    }
    fprintf(f, "};\n\n/* width, height, layout, margins left, top, right, "
	    "bottom */\nconst word defaultPageSizeInfo[7] = { ");
    for (i = 0; i < NUM_DEFAULTS; i++) {
	fprintf(f, "%s%ld", i ? ", " : "", defaults[i]);
    }
    fprintf(f, " };\n");
    if (fclose(f) != 0) {
	perror(argv[2]);
	return 1;
    }
    return 0;
}
