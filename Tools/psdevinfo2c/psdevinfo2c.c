/***********************************************************************
 * psdevinfo2c.c -- the device tables of the PostScript printer driver
 * (Driver/Printer/PScript) from its asm sources, as C.
 *
 *	psdevinfo2c -o pscriptDevices.c pscriptDriverInfo.asm *Info.asm
 *
 * pscriptDriverInfo.asm lists the devices ("DefPrinter PD_X, "name",
 * infoResource"); the branch for GPC_ONLY is left out, as the DOS16
 * build does.  Each *Info.asm holds one or more device info resources:
 * a PrinterInfo, a GraphicsProperties, a PSInfoStruct and the prolog.
 *
 * The output defines, for each resource, a structure of these four in
 * a row, so that the offsets the asm stored (PI_hiRes, PSIS_prolog) are
 * offsets from the start of the PrinterInfo, as in the resource; and
 * the table "printDevices" of {name, info} (PrintCom/printcom.h) in the
 * order of the PD_ enum.
 *
 * Records are filled by position, as esp does: the asm's paper input
 * initializers are written "< MF_x, TF_x, ASF_x >" but the record's
 * fields are PIO_MANUAL, PIO_ASF, PIO_TRACTOR, so the TF value lands in
 * PIO_ASF and the ASF value in PIO_TRACTOR.  The tables keep that.
 *
 * Routine fields (PI_customEntry, PI_customExit) become the PSC_
 * numbers of pscript.h; the UI fields (PI_mainUI, PI_optionsUI,
 * PI_evalRoutine) are 0: the print UI is not ported.
 ***********************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static const struct {
    const char	*name;
    long	value;
} enums[] = {
    {"NULL", 0}, {"FALSE", 0}, {"TRUE", 0xffff},
    {"PT_RASTER", 0}, {"PT_VECTOR", 1},
    {"BMF_MONO", 0}, {"BMF_4BIT", 1}, {"BMF_8BIT", 2}, {"BMF_24BIT", 3},
    {"BMF_4CMYK", 4}, {"BMF_3CMY", 5},
    {"IC_NO_IEEE488", 0}, {"IC_IEEE488", 1},
    {"CC_NO_CUSTOM", 0}, {"CC_CUSTOM", 1},
    {"SC_NO_SCSI", 0}, {"SC_SCSI", 1},
    {"RC_NO_RS232C", 0}, {"RC_RS232C", 1},
    {"CC_NO_CENTRONICS", 0}, {"CC_CENTRONICS", 1},
    {"FC_NO_FILE", 0}, {"FC_FILE", 1},
    {"AC_NO_APPLETALK", 0}, {"AC_APPLETALK", 1},
    {"PS_DUMB_RASTER", 0}, {"PS_DOES_FONTS", 1}, {"PS_PDL", 2},
    {"MF_NO_MANUAL", 0}, {"MF_MANUAL1", 1}, {"MF_MANUAL2", 2},
    {"MF_MANUAL3", 3},
    {"TF_NO_TRACTOR", 0}, {"TF_TRACTOR1", 1}, {"TF_TRACTOR2", 2},
    {"TF_TRACTOR3", 3},
    {"ASF_NO_TRAY", 0}, {"ASF_TRAY1", 1}, {"ASF_TRAY2", 2},
    {"ASF_TRAY3", 3},
    {"OC_NO_COPIES", 0}, {"OC_COPIES", 1},
    {"PS_NORMAL", 0}, {"PS_REVERSE", 1},
    {"OD_SIMPLEX", 0}, {"OD_DUPLEXLO", 1}, {"OD_DUPLEXSO", 2},
    {"SO_NO_STAPLER", 0}, {"SO_STAPLER", 1},
    {"OS_NO_SORTER", 0}, {"OS_SORTER", 1},
    {"OB_NO_OUTPUTBIN", 0}, {"OB_OUTPUTBIN1", 1}, {"OB_OUTPUTBIN2", 2},
    {"OB_OUTPUTBIN3", 3},
    /* pscriptConstant.def */
    {"PR_MARGIN_LEFT", 18}, {"PR_MARGIN_RIGHT", 18},
    {"PR_MARGIN_TRACTOR", 18}, {"PR_MARGIN_TOP", 18},
    {"PR_MARGIN_BOTTOM", 18}
};

static const char *const psfl[] = {
    "PSFL_STANDARD_13", "PSFL_STANDARD_35N", "PSFL_STANDARD_35C",
    "PSFL_AGFA_74", "PSFL_DEC_29", "PSFL_IBM_17", "PSFL_TI_17",
    "PSFL_IBM_39", "PSFL_IBM_43", "PSFL_IBM_47", "PSFL_MONOTYPE_8",
    "PSFL_MONOTYPE_70", "PSFL_JAPANESE_2", "PSFL_JAPANESE_5",
    "PSFL_WANG_14", "PSFL_WANG_32", "PSFL_ADOBE_TC1", "PSFL_ADOBE_TC2",
    "PSFL_ADOBE_FULL_SET", "PSFL_QMS_43"
};

/* the bit widths of the fields of a record, most significant first */
static const int typeWidths[] = {1, 3, 0};	/* :4, PT_TECH, PT_COLOR */
static const int connectWidths[] = {1, 1, 1, 1, 1, 1, 1, 0};
static const int inputWidths[] = {2, 3, 2, 0};	/* :1, PIO_MANUAL, _ASF,
						 * _TRACTOR */
static const int outputWidths[] = {1, 1, 2, 1, 1, 2, 0};

static void
Fail(const char *a, const char *b, const char *c)
{
    fprintf(stderr, "psdevinfo2c: %s%s%s%s%s\n", a, b ? " " : "",
	    b ? b : "", c ? " " : "", c ? c : "");
    exit(1);
}

static void *
Alloc(size_t size)
{
    void *p = malloc(size ? size : 1);

    if (p == NULL) {
	Fail("out of memory", NULL, NULL);
    }
    return p;
}

static char *
Dup(const char *s, size_t n)
{
    char *d = (char *) Alloc(n + 1);

    memcpy(d, s, n);
    d[n] = '\0';
    return d;
}

static char *
Trim(const char *s, size_t n)
{
    while (n > 0 && isspace((unsigned char) *s)) {
	s++, n--;
    }
    while (n > 0 && isspace((unsigned char) s[n - 1])) {
	n--;
    }
    return Dup(s, n);
}

static int
IsWordChar(int c)
{
    return isalnum(c) || c == '_';
}

typedef struct {
    char    *text;
    size_t  len;
    size_t  size;
} Str;

static void
StrAdd(Str *s, const char *t, size_t n)
{
    if (s->len + n + 1 > s->size) {
	char *d;

	s->size = 2 * (s->len + n) + 64;
	d = (char *) Alloc(s->size);
	if (s->len) {
	    memcpy(d, s->text, s->len);
	}
	free(s->text);
	s->text = d;
    }
    memcpy(s->text + s->len, t, n);
    s->len += n;
    s->text[s->len] = '\0';
}

typedef struct {
    char    **item;
    int	    count;
    int	    size;
} List;

static void
ListAddN(List *l, const char *s, size_t n)
{
    if (l->count == l->size) {
	char **d;

	l->size = l->size ? 2 * l->size : 32;
	d = (char **) Alloc(l->size * sizeof(char *));
	if (l->count) {
	    memcpy(d, l->item, l->count * sizeof(char *));
	}
	free(l->item);
	l->item = d;
    }
    l->item[l->count++] = Dup(s, n);
}

static void
ListFree(List *l)
{
    int i;

    for (i = 0; i < l->count; i++) {
	free(l->item[i]);
    }
    free(l->item);
    l->item = NULL;
    l->count = l->size = 0;
}

/* the file's text without the ; comments (not in quotes), its lines
 * ended by a line feed alone */
static char *
ReadStripped(const char *path)
{
    FILE    *f = fopen(path, "rb");
    Str	    s = {NULL, 0, 0};
    int	    c, comment = 0;
    char    q = 0;

    if (f == NULL) {
	perror(path);
	exit(1);
    }
    StrAdd(&s, "", 0);
    while ((c = fgetc(f)) != EOF) {
	char ch = (char) c;

	if (c == '\r') {
	    continue;
	}
	if (c == '\n') {
	    comment = 0;
	    q = 0;
	    StrAdd(&s, &ch, 1);
	} else if (comment) {
	    /* nothing */
	} else if (q) {
	    StrAdd(&s, &ch, 1);
	    if (ch == q) {
		q = 0;
	    }
	} else if (ch == '"' || ch == '\'') {
	    q = ch;
	    StrAdd(&s, &ch, 1);
	} else if (ch == ';') {
	    comment = 1;
	} else {
	    StrAdd(&s, &ch, 1);
	}
    }
    fclose(f);
    return s.text;
}

static long
Value(const char *tok)
{
    char    *t = Trim(tok, strlen(tok));
    size_t  n = strlen(t), i;
    long    v;

    for (i = 0; i < sizeof(enums) / sizeof(enums[0]); i++) {
	if (strcmp(t, enums[i].name) == 0) {
	    free(t);
	    return enums[i].value;
	}
    }
    if (n > 2 && t[0] == '0' && t[1] == 'x' &&
	strspn(t + 2, "0123456789abcdefABCDEF") == n - 2) {
	v = strtol(t + 2, NULL, 16);
    } else if (n > 0 && strspn(t, "0123456789") == n) {
	v = strtol(t, NULL, 10);
    } else if (n > 1 && t[n - 1] == 'h' && isdigit((unsigned char) t[0]) &&
	       strspn(t, "0123456789abcdefABCDEF") == n - 1) {
	v = strtol(t, NULL, 16);
    } else {
	Fail("unknown value", t, NULL);
	v = 0;
    }
    free(t);
    return v;
}

/* "a, b, c" at its commas, each without blanks around it */
static void
SplitCommas(const char *s, List *out)
{
    for (;;) {
	const char  *e = strchr(s, ',');
	char	    *t = Trim(s, e != NULL ? (size_t) (e - s) : strlen(s));

	ListAddN(out, t, strlen(t));
	free(t);
	if (e == NULL) {
	    break;
	}
	s = e + 1;
    }
}

/* the value of a record from the values of its fields */
static long
Record(const char *kind, const int *widths, const char *item, int nested)
{
    List    toks = {NULL, 0, 0};
    long    v = 0;
    int	    n = 0, i;

    if (!nested) {
	Fail(kind, "is no record:", item);
    }
    SplitCommas(item, &toks);
    while (widths[n] != 0) {
	n++;
    }
    if (toks.count != n) {
	Fail(kind, "has not the values of its fields:", item);
    }
    for (i = 0; i < n; i++) {
	long x = Value(toks.item[i]);

	if (x >= (1L << widths[i])) {
	    Fail(kind, "has a value too big for its field:", toks.item[i]);
	}
	v = (v << widths[i]) | x;
    }
    ListFree(&toks);
    return v;
}

/*
 * The top-level items of a structure's initializer; of one in <> the
 * text between them (nested[i] set)
 */
static void
SubLists(const char *s, List *items, int **nestedOut)
{
    Str	    cur = {NULL, 0, 0};
    int	    depth = 0, nested = 0, *flags = NULL, size = 0;
    const char *p;
    char    *t;

    StrAdd(&cur, "", 0);
    for (p = s; ; p++) {
	if (*p == '<') {
	    depth++;
	    if (depth == 1) {
		nested = 1;
		continue;
	    }
	} else if (*p == '>') {
	    depth--;
	    if (depth == 0) {
		continue;
	    }
	}
	if (*p == '\0' || (*p == ',' && depth == 0)) {
	    t = Trim(cur.text, cur.len);
	    if (*p != '\0' || *t != '\0') {
		if (items->count >= size) {
		    int *n;

		    size = size ? 2 * size : 32;
		    n = (int *) Alloc(size * sizeof(int));
		    if (items->count) {
			memcpy(n, flags, items->count * sizeof(int));
		    }
		    free(flags);
		    flags = n;
		}
		flags[items->count] = nested;
		ListAddN(items, nested ? cur.text : t,
			 nested ? cur.len : strlen(t));
	    }
	    free(t);
	    cur.len = 0;
	    cur.text[0] = '\0';
	    nested = 0;
	    if (*p == '\0') {
		break;
	    }
	} else {
	    StrAdd(&cur, p, 1);
	}
    }
    free(cur.text);
    *nestedOut = flags;
}

typedef struct {
    char    *name;
    long    type, connect, smarts;
    char    *entry, *exit;	/* NULL: none */
    List    marginTractor, marginAsf;
    long    input, output, width;
    char    *ui[3];
    long    gp[7];
    char    *fonts;
    long    level;
    Str	    prolog;
} Info;

static Info *infos;
static int  numInfos;

/* blanks (line ends too) at p: what follows, NULL if there are none */
static const char *
Blanks1(const char *p)
{
    if (!isspace((unsigned char) *p)) {
	return NULL;
    }
    while (isspace((unsigned char) *p)) {
	p++;
    }
    return p;
}

/* p after the words given, with blanks between them and before the
 * first, or NULL */
static const char *
Words(const char *p, const char *w1, const char *w2)
{
    if ((p = Blanks1(p)) == NULL || strncmp(p, w1, strlen(w1)) != 0) {
	return NULL;
    }
    p += strlen(w1);
    if (w2 != NULL) {
	if ((p = Blanks1(p)) == NULL || strncmp(p, w2, strlen(w2)) != 0) {
	    return NULL;
	}
	p += strlen(w2);
    }
    return p;
}

/* "<name> label byte" at the start of a line: where, or NULL */
static const char *
FindLabelByte(const char *text, const char *name, const char **after)
{
    const char *p = text;

    for (;;) {
	const char *q = p;

	if (name != NULL) {
	    if (strncmp(q, name, strlen(name)) != 0) {
		q = NULL;
	    } else {
		q += strlen(name);
	    }
	} else {
	    while (IsWordChar((unsigned char) *q)) {
		q++;
	    }
	    if (q == p) {
		q = NULL;
	    }
	}
	if (q != NULL && (q = Words(q, "label", "byte")) != NULL) {
	    if (name == NULL) {
		*after = q;
		return p;
	    }
	    /* (nothing more in its line) */
	    while (*q == ' ' || *q == '\t') {
		q++;
	    }
	    if (*q == '\n' || *q == '\0') {
		*after = q;
		return p;
	    }
	}
	p = strchr(p, '\n');
	if (p == NULL) {
	    return NULL;
	}
	p++;
    }
}

/* "offset <name>": the name, or NULL */
static char *
OffsetOf(const char *s)
{
    const char *p = s, *e;

    if (strncmp(p, "offset", 6) != 0 || (p = Blanks1(p + 6)) == NULL) {
	return NULL;
    }
    for (e = p; IsWordChar((unsigned char) *e); e++) {
	;
    }
    return e > p && *e == '\0' ? Dup(p, e - p) : NULL;
}

/* the bytes of the "char" lines of a prolog */
static void
PrologBytes(const char *lines, const char *end, Str *data)
{
    const char *p = lines;

    StrAdd(data, "", 0);
    while (p < end) {
	const char  *eol = memchr(p, '\n', end - p);
	char	    *line, *rest;
	size_t	    n = eol != NULL ? (size_t) (eol - p) : (size_t) (end - p);

	line = Trim(p, n);
	p += n + (eol != NULL ? 1 : 0);
	if (*line == '\0') {
	    free(line);
	    continue;
	}
	if (strncmp(line, "char", 4) != 0 ||
	    !isspace((unsigned char) line[4])) {
	    Fail("prolog line", line, NULL);
	}
	rest = line + 4;
	for (;;) {
	    while (isspace((unsigned char) *rest)) {
		rest++;
	    }
	    if (*rest == '\0') {
		break;
	    }
	    if (*rest == '"') {
		char *e = strchr(rest + 1, '"');

		if (e == NULL) {
		    Fail("open string in prolog line", line, NULL);
		}
		StrAdd(data, rest + 1, e - rest - 1);
		rest = e + 1;
	    } else if (rest[0] == 'N' && rest[1] == 'L') {
		StrAdd(data, "\r\n", 2);
		rest += 2;
	    } else {
		char	*e = rest, *tok, c;

		while (*e != '\0' && *e != ',') {
		    e++;
		}
		tok = Dup(rest, e - rest);
		c = (char) Value(tok);
		StrAdd(data, &c, 1);
		free(tok);
		rest = e;
	    }
	    while (isspace((unsigned char) *rest)) {
		rest++;
	    }
	    if (*rest == ',') {
		rest++;
	    }
	}
	free(line);
    }
}

/* the text between the "<" that follows "name" + blanks and the next
 * ">", NULL if "name ... <" is not there ("before": a name the word
 * comes after, with blanks between) */
static char *
StructText(const char *body, const char *before, const char *name)
{
    const char	*key = before != NULL ? before : name, *p = body;

    while ((p = strstr(p, key)) != NULL) {
	const char *q = p + strlen(key), *e;

	if (before != NULL) {
	    q = Blanks1(q);
	    if (q != NULL && strncmp(q, name, strlen(name)) == 0) {
		q += strlen(name);
	    } else {
		q = NULL;
	    }
	}
	if (q != NULL) {
	    while (isspace((unsigned char) *q)) {
		q++;
	    }
	    if (*q == '<' && (e = strchr(q, '>')) != NULL) {
		return Dup(q + 1, e - q - 1);
	    }
	}
	p++;
    }
    return NULL;
}

static void
ParseInfoBody(const char *name, const char *body)
{
    const char	*p = strstr(body, "PrinterInfo"), *start, *after, *lab, *end;
    char	*text, *hi, *label, *s;
    List	items = {NULL, 0, 0}, parts = {NULL, 0, 0};
    int		*nested, depth = 0, i;
    Info	inf, *slot = NULL;

    if (p == NULL || (start = strchr(p, '<')) == NULL) {
	Fail(name, "has no PrinterInfo", NULL);
    }
    /* the text between the outermost < and its > */
    for (p = start; *p != '\0'; p++) {
	if (*p == '<') {
	    depth++;
	} else if (*p == '>' && --depth == 0) {
	    break;
	}
    }
    if (*p == '\0') {
	Fail(name, "unbalanced <>", NULL);
    }
    text = Dup(start + 1, p - start - 1);
    SubLists(text, &items, &nested);
    free(text);
    if (items.count != 20) {
	Fail(name, "has not 20 PrinterInfo fields", NULL);
    }
    memset(&inf, 0, sizeof(inf));
    inf.name = Dup(name, strlen(name));
    inf.type = Record("type", typeWidths, items.item[0], nested[0]);
    inf.connect = Record("connect", connectWidths, items.item[1], nested[1]);
    inf.smarts = Value(items.item[2]);
    inf.entry = strcmp(items.item[3], "NULL") == 0 ? NULL :
		Dup(items.item[3], strlen(items.item[3]));
    inf.exit = strcmp(items.item[4], "NULL") == 0 ? NULL :
	       Dup(items.item[4], strlen(items.item[4]));
    for (i = 5; i <= 11; i++) {
	if (i != 7 && strcmp(items.item[i], "NULL") != 0) {
	    Fail(name, "unexpected mode/font table", NULL);
	}
    }
    hi = OffsetOf(items.item[7]);
    if (hi == NULL) {
	Fail(name, "PI_hiRes", items.item[7]);
    }
    SplitCommas(items.item[12], &inf.marginTractor);
    SplitCommas(items.item[13], &inf.marginAsf);
    for (i = 0; i < inf.marginTractor.count; i++) {
	(void) Value(inf.marginTractor.item[i]);
    }
    for (i = 0; i < inf.marginAsf.count; i++) {
	(void) Value(inf.marginAsf.item[i]);
    }
    inf.input = Record("input", inputWidths, items.item[14], nested[14]);
    inf.output = Record("output", outputWidths, items.item[15], nested[15]);
    inf.width = Value(items.item[16]);
    for (i = 0; i < 3; i++) {
	inf.ui[i] = Dup(items.item[17 + i], strlen(items.item[17 + i]));
    }
    /* <hiRes> GraphicsProperties <...> */
    s = StructText(body, hi, "GraphicsProperties");
    if (s == NULL) {
	Fail(name, "has no GraphicsProperties", hi);
    }
    SplitCommas(s, &parts);
    free(s);
    if (parts.count < 7) {
	Fail(name, "has not 7 GraphicsProperties fields", NULL);
    }
    for (i = 0; i < parts.count; i++) {
	long v = Value(parts.item[i]);

	if (i < 7) {
	    inf.gp[i] = v;
	}
    }
    ListFree(&parts);
    /* PSInfoStruct <fonts, level, size, offset prolog> */
    s = StructText(body, NULL, "PSInfoStruct");
    if (s == NULL) {
	Fail(name, "has no PSInfoStruct", NULL);
    }
    SplitCommas(s, &parts);
    free(s);
    if (parts.count < 4) {
	Fail(name, "has not 4 PSInfoStruct fields", NULL);
    }
    for (i = 0; i < (int) (sizeof(psfl) / sizeof(psfl[0])); i++) {
	if (strcmp(psfl[i], parts.item[0]) == 0) {
	    break;
	}
    }
    if (i == (int) (sizeof(psfl) / sizeof(psfl[0]))) {
	Fail(name, "font list", parts.item[0]);
    }
    inf.fonts = Dup(parts.item[0], strlen(parts.item[0]));
    inf.level = Value(parts.item[1]);
    label = OffsetOf(parts.item[3]);
    if (label == NULL) {
	Fail(name, "PSIS_prolog", parts.item[3]);
    }
    ListFree(&parts);
    /* the prolog: from its label to the next one */
    lab = FindLabelByte(body, label, &after);
    if (lab == NULL || (after = strchr(after, '\n')) == NULL ||
	(end = FindLabelByte(after + 1, NULL, &p)) == NULL) {
	Fail(name, "has no prolog between labels:", label);
    }
    PrologBytes(after + 1, end, &inf.prolog);
    free(label);
    free(hi);
    ListFree(&items);
    free(nested);

    /* (a later one of the same name takes its place) */
    for (i = 0; i < numInfos; i++) {
	if (strcmp(infos[i].name, name) == 0) {
	    slot = &infos[i];
	}
    }
    if (slot == NULL) {
	Info *n = (Info *) Alloc((numInfos + 1) * sizeof(Info));

	if (numInfos) {
	    memcpy(n, infos, numInfos * sizeof(Info));
	}
	free(infos);
	infos = n;
	slot = &infos[numInfos++];
    }
    *slot = inf;
}

/*
 * The device info resources of a file:
 * "<name> segment resource" ... "<name> ends"
 */
static void
ParseInfo(const char *path)
{
    char	*text = ReadStripped(path);
    const char	*p = text;

    while (p != NULL && *p != '\0') {
	const char  *q = p, *after;
	char	    *name = NULL;

	while (IsWordChar((unsigned char) *q)) {
	    q++;
	}
	if (q > p && (after = Words(q, "segment", "resource")) != NULL) {
	    while (*after == ' ' || *after == '\t') {
		after++;
	    }
	    if (*after == '\n' || *after == '\0') {
		name = Dup(p, q - p);
	    }
	}
	if (name != NULL) {
	    const char *e = after;

	    /* the line that begins with "<name> ends" */
	    for (;;) {
		const char *x;

		e = strchr(e, '\n');
		if (e == NULL) {
		    break;
		}
		e++;
		if (strncmp(e, name, strlen(name)) == 0 &&
		    (x = Blanks1(e + strlen(name))) != NULL &&
		    strncmp(x, "ends", 4) == 0) {
		    break;
		}
	    }
	    if (e != NULL) {
		char *body = Dup(after, e - after);

		ParseInfoBody(name, body);
		free(body);
		free(name);
		p = e;
		continue;
	    }
	    free(name);
	}
	p = strchr(p, '\n');
	if (p != NULL) {
	    p++;
	}
    }
    free(text);
}

typedef struct {
    char *pd, *name, *info;
} Device;

static Device	*devices;
static int	numDevices;

static void
ParseDevices(const char *path)
{
    char    *text = ReadStripped(path), *p = text;
    int	    stack[64], depth = 0, skip = 0;

    while (*p != '\0') {
	char	    *eol = strchr(p, '\n'), *line, *w0, *w1;
	const char  *q, *e;

	line = Dup(p, eol != NULL ? (size_t) (eol - p) : strlen(p));
	p = eol != NULL ? eol + 1 : p + strlen(p);
	for (q = line; isspace((unsigned char) *q); q++) {
	    ;
	}
	for (e = q; *e != '\0' && !isspace((unsigned char) *e); e++) {
	    ;
	}
	w0 = Dup(q, e - q);
	for (q = e; isspace((unsigned char) *q); q++) {
	    ;
	}
	for (e = q; *e != '\0' && !isspace((unsigned char) *e); e++) {
	    ;
	}
	w1 = Dup(q, e - q);
	if (strcmp(w0, "ifdef") == 0) {
	    if (depth == 64) {
		Fail("ifdef nested too deep in", path, NULL);
	    }
	    stack[depth++] = skip;
	    skip = skip || strcmp(w1, "GPC_ONLY") == 0;
	} else if (strcmp(w0, "else") == 0) {
	    if (depth == 0) {
		Fail("else without ifdef in", path, NULL);
	    }
	    skip = stack[depth - 1] || !skip;
	} else if (strcmp(w0, "endif") == 0) {
	    if (depth == 0) {
		Fail("endif without ifdef in", path, NULL);
	    }
	    skip = stack[--depth];
	} else if (!skip && strcmp(w0, "DefPrinter") == 0) {
	    /* DefPrinter PD_X, "name", infoResource */
	    Device	d;
	    const char	*a, *b;
	    Device	*n;

	    q = strstr(line, "DefPrinter") + 10;
	    while (isspace((unsigned char) *q)) {
		q++;
	    }
	    for (a = q; IsWordChar((unsigned char) *q); q++) {
		;
	    }
	    d.pd = Dup(a, q - a);
	    while (isspace((unsigned char) *q)) {
		q++;
	    }
	    if (*q != ',' || d.pd[0] == '\0') {
		Fail("DefPrinter line", line, NULL);
	    }
	    for (q++; isspace((unsigned char) *q); q++) {
		;
	    }
	    if (*q != '"' || (b = strchr(q + 1, '"')) == NULL) {
		Fail("DefPrinter line", line, NULL);
	    }
	    d.name = Dup(q + 1, b - q - 1);
	    for (q = b + 1; isspace((unsigned char) *q); q++) {
		;
	    }
	    if (*q != ',') {
		Fail("DefPrinter line", line, NULL);
	    }
	    for (q++; isspace((unsigned char) *q); q++) {
		;
	    }
	    for (a = q; IsWordChar((unsigned char) *q); q++) {
		;
	    }
	    if (q == a) {
		Fail("DefPrinter line", line, NULL);
	    }
	    d.info = Dup(a, q - a);
	    n = (Device *) Alloc((numDevices + 1) * sizeof(Device));
	    if (numDevices) {
		memcpy(n, devices, numDevices * sizeof(Device));
	    }
	    free(devices);
	    devices = n;
	    devices[numDevices++] = d;
	}
	free(w0);
	free(w1);
	free(line);
    }
    free(text);
}

/* the prolog as a C string, a new line after each line feed */
static void
CString(FILE *f, const Str *data)
{
    size_t i;

    fputc('"', f);
    for (i = 0; i < data->len; i++) {
	unsigned char b = (unsigned char) data->text[i];

	if (b == 0x0d) {
	    fputs("\\r", f);
	} else if (b == 0x0a) {
	    fputs("\\n", f);
	    if (i + 1 < data->len) {
		fputs("\"\n\t\"", f);
	    }
	} else if (b == 0x22 || b == 0x5c) {
	    fprintf(f, "\\%c", b);
	} else if (b >= 0x20 && b < 0x7f) {
	    fputc(b, f);
	} else {
	    fprintf(f, "\\%03o", b);
	}
    }
    fputc('"', f);
}

static int
CompareInfos(const void *a, const void *b)
{
    return strcmp(((const Info *) a)->name, ((const Info *) b)->name);
}

static void
PrintJoined(FILE *f, const List *l)
{
    int i;

    for (i = 0; i < l->count; i++) {
	fprintf(f, "%s%ld", i ? ", " : "", Value(l->item[i]));
    }
}

int
main(int argc, char **argv)
{
    FILE    *f;
    int	    i, k;

    if (argc < 5 || strcmp(argv[1], "-o") != 0) {
	fprintf(stderr, "usage: psdevinfo2c -o pscriptDevices.c "
		"pscriptDriverInfo.asm xInfo.asm...\n");
	return 2;
    }
    ParseDevices(argv[3]);
    for (i = 4; i < argc; i++) {
	ParseInfo(argv[i]);
    }
    if (numInfos > 1) {
	qsort(infos, numInfos, sizeof(Info), CompareInfos);
    }
    if ((f = fopen(argv[2], "w")) == NULL) {
	perror(argv[2]);
	return 1;
    }
    fprintf(f, "/* generated by psdevinfo2c from pscriptDriverInfo.asm\n"
	    " * and the *Info.asm files; do not edit */\n"
	    "#include \"pscript.h\"\n#include <stddef.h>\n\n");
    for (i = 0; i < numInfos; i++) {
	const Info  *inf = &infos[i];
	const char  *name = inf->name;
	unsigned long n = (unsigned long) inf->prolog.len;

	fprintf(f, "typedef struct {\n    PrinterInfo pi;\n"
		"    GraphicsProperties hiRes;\n    PSInfoStruct ps;\n"
		"    char prolog[%lu];\n} PSDev_%s;\n\n", n + 1, name);
	/* PS_INFO_STRUCT: the PSInfoStruct right after the properties */
	fprintf(f, "typedef char PSDev_%s_check[offsetof(PSDev_%s, ps) == "
		"offsetof(PSDev_%s, hiRes) + sizeof(GraphicsProperties) "
		"? 1 : -1];\n\n", name, name, name);
	fprintf(f, "static const PSDev_%s %s = {\n  {\n", name, name);
	fprintf(f, "    0x%02lx, 0x%02lx, %ld,\t/* type, connect, smarts */\n",
		inf->type, inf->connect, inf->smarts);
	fprintf(f, "    %s%s, %s%s,\n",
		inf->entry ? "PSC_" : "PSC_NONE", inf->entry ? inf->entry : "",
		inf->exit ? "PSC_" : "PSC_NONE", inf->exit ? inf->exit : "");
	fprintf(f, "    0, 0, offsetof(PSDev_%s, hiRes), 0, 0,"
		"\t/* the modes */\n", name);
	fprintf(f, "    0, 0,\t\t\t/* fonts, symbol sets */\n");
	fprintf(f, "    { ");
	PrintJoined(f, &inf->marginTractor);
	fprintf(f, " },\t/* tractor margins */\n    { ");
	PrintJoined(f, &inf->marginAsf);
	fprintf(f, " },\t/* ASF margins */\n");
	fprintf(f, "    0x%02lx, 0x%02lx, %ld,\t/* paper in, out, width */\n",
		inf->input, inf->output, inf->width);
	fprintf(f, "    0, 0, 0\t\t/* UI: %s, %s, %s */\n  },\n", inf->ui[0],
		inf->ui[1], inf->ui[2]);
	fprintf(f, "  { %ld, %ld, %ld, %ld, %ld, %ld, %ld },\n", inf->gp[0],
		inf->gp[1], inf->gp[2], inf->gp[3], inf->gp[4], inf->gp[5],
		inf->gp[6]);
	fprintf(f, "  { %s, 0x%04lx, %lu, offsetof(PSDev_%s, prolog) },\n  ",
		inf->fonts, inf->level, n, name);
	CString(f, &inf->prolog);
	fprintf(f, "\n};\n\n");
    }
    fprintf(f, "const PrintDeviceEntry printDevices[] = {\n");
    for (i = 0; i < numDevices; i++) {
	for (k = 0; k < numInfos; k++) {
	    if (strcmp(infos[k].name, devices[i].info) == 0) {
		break;
	    }
	}
	if (k == numInfos) {
	    fprintf(stderr, "psdevinfo2c: %s: no info resource %s\n",
		    devices[i].pd, devices[i].info);
	    return 1;
	}
	fprintf(f, "    { \"%s\", &%s.pi },\t/* %s = %d */\n",
		devices[i].name, devices[i].info, devices[i].pd, 2 * i);
    }
    fprintf(f, "};\n\n");
    fprintf(f, "const word printNumDevices = %d;\n", numDevices);
    if (fclose(f) != 0) {
	perror(argv[2]);
	return 1;
    }
    return 0;
}
