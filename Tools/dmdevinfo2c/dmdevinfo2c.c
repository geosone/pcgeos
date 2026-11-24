/***********************************************************************
 * dmdevinfo2c.c -- the device tables of a raster printer driver (the
 * dot matrix drivers of Driver/Printer/DotMatrix) from its asm sources,
 * as C.
 *
 *	dmdevinfo2c -o devices.c [-D constants.def]... [--fonts font.asm]...
 *		    [--header printcom.h] driverinfo.asm info.asm...
 *
 * driverinfo.asm lists the devices ("DefPrinter PD_X, "name", infoRes")
 * and holds the PrintDriverInfo with the ISO substitution table and the
 * ASCII translation list (chunks in the asm).  Each info.asm holds one
 * or more device info resources: a PrinterInfo, its GraphicsProperties,
 * a FontGeometry table, the pitch tables and a symbol set list (the
 * control codes by PrinterSymbolSet).  The font file (--fonts) holds
 * the printerFontInfo resource: lists of FontEntry offsets and the
 * FontEntries; the PCL drivers have none (their NLQ list is an empty
 * list in the info resource, the fonts are the printer's).  The -D
 * files give the constants (NAME = value).
 *
 * The output defines, for each info resource, a structure of its parts
 * in the order of the asm, so that the offsets the asm stored
 * (PI_lowRes, PI_fontGeometries, FG_pitchTab) are offsets from the
 * start of the PrinterInfo, as in the resource; printerFontInfo
 * likewise (PI_draft and PI_nlq are offsets into it); the table
 * "printDevices" (PrintCom/printcom.h) in the order of the PD_ enum;
 * printDriverInfo and printTransTables (the tables of
 * SpoolUpdateTranslationTable, Internal/spoolDr.h).
 *
 * Port32 differences: FE_command, the offset of a control code in the
 * asm, is an index into printFontCommands[] (1 on; 0: none), as are
 * the entries of a symbol set list.  PI_customEntry and PI_customExit,
 * the offsets of a job's custom routines in the asm, are indices into
 * printCustomRoutines[] (1 on), which the output declares.  The PCL4
 * drivers' PI_fontGeometries is a DownloadProperties (two words).  The
 * UI fields (PI_mainUI, PI_optionsUI, PI_evalRoutine) are 0, as is
 * GP_colorCorrection (a handle of a correction table): the print UI
 * and color correction are not ported.  Records are filled by
 * position, as esp does (psdevinfo2c).
 *
 * The asm is read as the standard build has it: no symbol of an
 * ifdef is defined, an "if <symbol>" is false.
 ***********************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
Fail(const char *a, const char *b, const char *c)
{
    fprintf(stderr, "dmdevinfo2c: %s%s%s%s%s\n", a, b ? " " : "",
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

static void *
Grow(void *items, int count, int *size, size_t itemSize)
{
    void *n;

    if (count < *size) {
	return items;
    }
    *size = *size ? 2 * *size : 32;
    n = Alloc(*size * itemSize);
    if (count) {
	memcpy(n, items, count * itemSize);
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

static char *
DupStr(const char *s)
{
    return Dup(s, strlen(s));
}

/*
 * The classes of characters: the files are Latin-1 text for this
 * program (blanks and letters above 0x7f too)
 */
static int
IsSpace(int ch)
{
    unsigned char c = (unsigned char) ch;

    return c == ' ' || (c >= 0x09 && c <= 0x0d) || (c >= 0x1c && c <= 0x1f) ||
	   c == 0x85 || c == 0xa0;
}

static int
IsDigit(int ch)
{
    return ch >= '0' && ch <= '9';
}

static int
IsWord(int ch)
{
    unsigned char c = (unsigned char) ch;

    if (c < 0x80) {
	return IsDigit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
	       c == '_';
    }
    return c == 0xaa || c == 0xb2 || c == 0xb3 || c == 0xb5 || c == 0xb9 ||
	   c == 0xba || (c >= 0xbc && c <= 0xbe) ||
	   (c >= 0xc0 && c != 0xd7 && c != 0xf7);
}

static char *
Trim(const char *s, size_t n)
{
    while (n > 0 && IsSpace(*s)) {
	s++, n--;
    }
    while (n > 0 && IsSpace(s[n - 1])) {
	n--;
    }
    return Dup(s, n);
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
ListAdd(List *l, char *s)		/* s: now the list's */
{
    l->item = (char **) Grow(l->item, l->count, &l->size, sizeof(char *));
    l->item[l->count++] = s;
}

static int
ListFind(const List *l, const char *s)
{
    int i;

    for (i = 0; i < l->count; i++) {
	if (strcmp(l->item[i], s) == 0) {
	    return i;
	}
    }
    return -1;
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

/* the words of a text (what its blanks separate) */
static void
SplitWords(const char *s, List *out)
{
    for (;;) {
	const char *e;

	while (IsSpace(*s)) {
	    s++;
	}
	if (*s == '\0') {
	    break;
	}
	for (e = s; *e != '\0' && !IsSpace(*e); e++) {
	    ;
	}
	ListAdd(out, Dup(s, e - s));
	s = e;
    }
}

/* "a, b, c" at its commas, each without blanks around it */
static void
SplitCommas(const char *s, List *out)
{
    for (;;) {
	const char *e = strchr(s, ',');

	ListAdd(out, Trim(s, e != NULL ? (size_t) (e - s) : strlen(s)));
	if (e == NULL) {
	    break;
	}
	s = e + 1;
    }
}

/*
 * The symbols: the enums of the printer driver's definitions, and the
 * constants of the -D files
 */
typedef struct {
    const char	*name;
    long	value;
} Sym;

static const Sym enums[] = {
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
    {"PR_DONT_RESEND", 0}, {"PR_RESEND", 1},
    {"PDT_PRINTER", 0}, {"PDT_PLOTTER", 1}, {"PDT_FACSIMILE", 2},
    {"PDT_CAMERA", 3}, {"PDT_OTHER", 4},
    {"FID_INVALID", 0}, {"FID_DTC_URW_ROMAN", 0x1000},
    {"FID_DTC_URW_SANS", 0x1200}, {"FID_DTC_URW_MONO", 0x1400},
    {"TP_PROPORTIONAL", 0}, {"TP_5_PITCH", 1}, {"TP_6_PITCH", 2},
    {"TP_10_PITCH", 3}, {"TP_10_6_PITCH", 0x6a}, {"TP_12_PITCH", 0x6b},
    {"TP_15_PITCH", 0x6c}, {"TP_16_PITCH", 0x6d}, {"TP_16_6_PITCH", 0xa6},
    {"TP_17_PITCH", 0xa7}, {"TP_19_2_PITCH", 0xc0}, {"TP_20_PITCH", 0xc1},
    {"TP_21_3_PITCH", 0xd5}, {"TP_24_PITCH", 0xd6},
    {"PSS_ASCII7", 0}, {"PSS_IBM437", 2}, {"PSS_IBM850", 4},
    {"PSS_IBM860", 6}, {"PSS_IBM863", 8}, {"PSS_IBM865", 10},
    {"PSS_ROMAN8", 12}, {"PSS_WINDOWS", 14}, {"PSS_VENTURA", 16},
    {"PSS_LATIN1", 18}, {"PSS_PCGEOS", 20},
    /* PrintTextStyle, a record: the masks of its fields */
    {"mask PTS_FUTURE", 1L << 0}, {"mask PTS_OVERLINE", 1L << 1},
    {"mask PTS_QUADHEIGHT", 1L << 2}, {"mask PTS_DBLHEIGHT", 1L << 3},
    {"mask PTS_DBLWIDTH", 1L << 4}, {"mask PTS_REVERSE", 1L << 5},
    {"mask PTS_OUTLINE", 1L << 6}, {"mask PTS_SHADOW", 1L << 7},
    {"mask PTS_STRIKETHRU", 1L << 8}, {"mask PTS_UNDERLINE", 1L << 9},
    {"mask PTS_ITALIC", 1L << 10}, {"mask PTS_BOLD", 1L << 11},
    {"mask PTS_NLQ", 1L << 12}, {"mask PTS_SUPERSCRIPT", 1L << 13},
    {"mask PTS_SUBSCRIPT", 1L << 14}, {"mask PTS_CONDENSED", 1L << 15}
};

static struct {
    char    *name;
    long    value;
}	    *consts;
static int  numConsts, sizeConsts;

static int
FindSym(const char *name, long *value)
{
    size_t i;
    int	   k;

    for (k = 0; k < numConsts; k++) {
	if (strcmp(consts[k].name, name) == 0) {
	    *value = consts[k].value;
	    return 1;
	}
    }
    for (i = 0; i < sizeof(enums) / sizeof(enums[0]); i++) {
	if (strcmp(enums[i].name, name) == 0) {
	    *value = enums[i].value;
	    return 1;
	}
    }
    return 0;
}

static void
SetConst(const char *name, long value)
{
    int k;

    for (k = 0; k < numConsts; k++) {
	if (strcmp(consts[k].name, name) == 0) {
	    consts[k].value = value;
	    return;
	}
    }
    consts = Grow(consts, numConsts, &sizeConsts, sizeof(*consts));
    consts[numConsts].name = DupStr(name);
    consts[numConsts++].value = value;
}

/***********************************************************************
 *				TryValue
 ***********************************************************************
 * SYNOPSIS:	    The value of a token: a symbol, a number (16, 0x10,
 *		    10h), "a or b", "a shl b", "a shr b".
 * RETURN:	    whether it has one
 ***********************************************************************/
static int
TryValue(const char *token, long *result)
{
    List    w = {NULL, 0, 0};
    Str	    t = {NULL, 0, 0};
    char    *tok, *p;
    size_t  n, i;
    int	    k, ok = 0;

    /* one blank between its words */
    SplitWords(token, &w);
    StrAdd(&t, "", 0);
    for (k = 0; k < w.count; k++) {
	if (k) {
	    StrAdd(&t, " ", 1);
	}
	StrAdd(&t, w.item[k], strlen(w.item[k]));
    }
    ListFree(&w);
    tok = t.text;
    n = t.len;

    if (FindSym(tok, result)) {
	ok = 1;
    } else if ((p = strstr(tok, " or ")) != NULL) {
	long v = 0, x;
	char *s = tok;

	ok = 1;
	for (;;) {
	    if (p != NULL) {
		*p = '\0';
	    }
	    if (!TryValue(s, &x)) {
		ok = 0;
		break;
	    }
	    v |= x;
	    if (p == NULL) {
		break;
	    }
	    s = p + 4;
	    p = strstr(s, " or ");
	}
	*result = v;
    } else if (n > 2 && tok[0] == '0' && tok[1] == 'x' &&
	       strspn(tok + 2, "0123456789abcdefABCDEF") == n - 2) {
	*result = strtol(tok + 2, NULL, 16);
	ok = 1;
    } else if (n > 0 && strspn(tok, "0123456789") == n) {
	/* (no number has zeros before it) */
	*result = strtol(tok, NULL, 10);
	ok = n == 1 || tok[0] != '0' || *result == 0;
    } else if (n > 1 && (tok[n - 1] == 'h' || tok[n - 1] == 'H') &&
	       IsDigit(tok[0]) &&
	       strspn(tok, "0123456789abcdefABCDEF") == n - 1) {
	*result = strtol(tok, NULL, 16);
	ok = 1;
    } else {
	/* <a> shl <b>, <a> shr <b>: at the first of them */
	for (i = 1; i + 5 < n; i++) {
	    if (tok[i] == ' ' && (strncmp(tok + i + 1, "shl ", 4) == 0 ||
				  strncmp(tok + i + 1, "shr ", 4) == 0)) {
		long a, b;
		int  left = tok[i + 3] == 'l';

		tok[i] = '\0';
		if (TryValue(tok, &a) && TryValue(tok + i + 5, &b) &&
		    b >= 0 && b < 63) {
		    *result = left ? (long) ((unsigned long) a << b) : a >> b;
		    ok = 1;
		}
		break;
	    }
	}
    }
    free(t.text);
    return ok;
}

static long
Value(const char *tok)
{
    long v;

    if (!TryValue(tok, &v)) {
	Fail("unknown value", tok, NULL);
    }
    return v;
}

/***********************************************************************
 *				ReadText
 ***********************************************************************
 * SYNOPSIS:	    The text of an asm file without its ; comments, the
 *		    conditionals taken, lines that end in "\" joined to
 *		    the next; its lines end with a line feed.
 ***********************************************************************/
static char *
ReadText(const char *path)
{
    FILE    *f = fopen(path, "rb");
    Str	    raw = {NULL, 0, 0}, out = {NULL, 0, 0};
    int	    stack[64], depth = 0, first = 1, c, k;
    size_t  pos = 0, r, w;

    if (f == NULL) {
	perror(path);
	exit(1);
    }
    StrAdd(&raw, "", 0);
    while ((c = fgetc(f)) != EOF) {
	char ch = (char) c;

	StrAdd(&raw, &ch, 1);
    }
    fclose(f);
    StrAdd(&out, "", 0);
    while (pos < raw.len) {
	Str	line = {NULL, 0, 0};
	List	words = {NULL, 0, 0};
	char	q = 0;
	int	comment = 0, keep = 1, active = 1;
	const char *w0;

	/* a line: to its end (CR LF is one) */
	StrAdd(&line, "", 0);
	for (; pos < raw.len; pos++) {
	    unsigned char ch = (unsigned char) raw.text[pos];

	    if (ch == '\n' || ch == '\r' || ch == 0x0b || ch == 0x0c ||
		(ch >= 0x1c && ch <= 0x1e) || ch == 0x85) {
		if (ch == '\r' && pos + 1 < raw.len &&
		    raw.text[pos + 1] == '\n') {
		    pos++;
		}
		pos++;
		break;
	    }
	    if (comment) {
		continue;
	    }
	    if (q) {
		if (raw.text[pos] == q) {
		    q = 0;
		}
	    } else if (ch == '"' || ch == '\'') {
		q = (char) ch;
	    } else if (ch == ';') {
		comment = 1;
		continue;
	    }
	    StrAdd(&line, raw.text + pos, 1);
	}
	SplitWords(line.text, &words);
	w0 = words.count ? words.item[0] : "";
	if (strcmp(w0, "ifdef") == 0 || strcmp(w0, "ifndef") == 0) {
	    if (depth == 64) {
		Fail("conditionals nested too deep in", path, NULL);
	    }
	    stack[depth++] = w0[2] == 'n';
	    keep = 0;
	} else if (strcmp(w0, "if") == 0 && words.count == 2 &&
		   strspn(words.item[1], "0123456789") ==
		   strlen(words.item[1])) {
	    /* a number */
	    if (depth == 64) {
		Fail("conditionals nested too deep in", path, NULL);
	    }
	    stack[depth++] = strtol(words.item[1], NULL, 10) != 0;
	    keep = 0;
	} else if (strcmp(w0, "if") == 0 && words.count == 2) {
	    /* a symbol: the non-EC, SBCS build (ERROR_CHECK,
	     * DBCS_PCGEOS ... 0) */
	    const char *s;

	    for (s = words.item[1]; IsWord(*s); s++) {
		;
	    }
	    if (*s != '\0') {
		Fail("conditional", line.text, NULL);
	    }
	    if (depth == 64) {
		Fail("conditionals nested too deep in", path, NULL);
	    }
	    stack[depth++] = 0;
	    keep = 0;
	} else if (strcmp(w0, "if") == 0 || strcmp(w0, "ife") == 0 ||
		   strcmp(w0, "elseif") == 0) {
	    Fail("conditional", line.text, NULL);
	} else if (strcmp(w0, "else") == 0) {
	    if (depth == 0) {
		Fail("else without if in", path, NULL);
	    }
	    stack[depth - 1] = !stack[depth - 1];
	    keep = 0;
	} else if (strcmp(w0, "endif") == 0) {
	    if (depth == 0) {
		Fail("endif without if in", path, NULL);
	    }
	    depth--;
	    keep = 0;
	}
	if (keep) {
	    for (k = 0; k < depth; k++) {
		active = active && stack[k];
	    }
	    if (!first) {
		StrAdd(&out, "\n", 1);
	    }
	    first = 0;
	    if (active) {
		StrAdd(&out, line.text, line.len);
	    }
	}
	ListFree(&words);
	free(line.text);
    }
    free(raw.text);
    /* the line continuations */
    for (r = w = 0; r < out.len; r++) {
	if (out.text[r] == '\\' && out.text[r + 1] == '\n') {
	    out.text[w++] = ' ';
	    r++;
	} else {
	    out.text[w++] = out.text[r];
	}
    }
    out.text[w] = '\0';
    return out.text;
}

/*
 * Matchers on a text: each moves *p on over its part and says whether
 * it was there
 */
static int
MWord(const char **p)
{
    const char *s = *p;

    while (IsWord(**p)) {
	(*p)++;
    }
    return *p > s;
}

static void
MSpace(const char **p)
{
    while (IsSpace(**p)) {
	(*p)++;
    }
}

static int
MSpace1(const char **p)
{
    if (!IsSpace(**p)) {
	return 0;
    }
    MSpace(p);
    return 1;
}

static int
MLit(const char **p, const char *lit)
{
    size_t n = strlen(lit);

    if (strncmp(*p, lit, n) != 0) {
	return 0;
    }
    *p += n;
    return 1;
}

/* blanks to the end of a line: *p is left at that end, the last one
 * of the blanks */
static int
MEol(const char **p)
{
    const char *s = *p, *end = NULL;

    for (;; s++) {
	if (*s == '\n' || *s == '\0') {
	    end = s;
	}
	if (*s == '\0' || !IsSpace(*s)) {
	    break;
	}
    }
    if (end == NULL) {
	return 0;
    }
    *p = end;
    return 1;
}

/* the first start of a line at p or after it; NULL: none */
static const char *
LineStart(const char *text, const char *p)
{
    if (p == text || p[-1] == '\n') {
	return p;
    }
    p = strchr(p, '\n');
    return p != NULL ? p + 1 : NULL;
}

static const char *
NextLine(const char *p)
{
    p = strchr(p, '\n');
    return p != NULL ? p + 1 : NULL;
}

/* the text between the first < and its >: a new string; *rest: what
 * follows */
static char *
Inner(const char *s, const char **rest)
{
    const char *a = strchr(s, '<'), *p;
    int	       depth = 0;

    if (a == NULL) {
	Fail("no < in", s, NULL);
    }
    for (p = a; *p != '\0'; p++) {
	if (*p == '<') {
	    depth++;
	} else if (*p == '>' && --depth == 0) {
	    if (rest != NULL) {
		*rest = p + 1;
	    }
	    return Dup(a + 1, p - a - 1);
	}
    }
    Fail("unbalanced <>", NULL, NULL);
    return NULL;
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
		flags = (int *) Grow(flags, items->count, &size, sizeof(int));
		flags[items->count] = nested;
		ListAdd(items, nested ? DupStr(cur.text) : DupStr(t));
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

/* the bit widths of the fields of a record, most significant first */
static const int typeWidths[] = {1, 3, 0};	/* :4, PT_TECH, PT_COLOR */
static const int connectWidths[] = {1, 1, 1, 1, 1, 1, 1, 0};
static const int inputWidths[] = {2, 3, 2, 0};	/* :1, PIO_MANUAL, _ASF,
						 * _TRACTOR */
static const int outputWidths[] = {1, 1, 2, 1, 1, 2, 0};

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

/* "offset x", "offset res:x": x; NULL or 0 (Oki9: no medium
 * resolution): NULL */
static char *
OffsetOf(const char *tok)
{
    const char *p = tok, *a;

    if (strcmp(tok, "NULL") == 0 || strcmp(tok, "0") == 0) {
	return NULL;
    }
    if (MLit(&p, "offset") && MSpace1(&p)) {
	a = p;
	if (MWord(&p)) {
	    if (*p == ':') {
		a = ++p;
		if (!MWord(&p)) {
		    a = NULL;
		}
	    }
	    if (a != NULL && *p == '\0') {
		return DupStr(a);
	    }
	}
    }
    Fail("not an offset:", tok, NULL);
    return NULL;
}

/* a name, "offset name" or NULL */
static char *
RoutineOf(const char *tok)
{
    const char *p = tok;

    if (strcmp(tok, "NULL") == 0) {
	return NULL;
    }
    if (strncmp(tok, "offset", 6) == 0) {
	return OffsetOf(tok);
    }
    if (!MWord(&p) || *p != '\0') {
	Fail("not a name:", tok, NULL);
    }
    return DupStr(tok);
}

/*
 * A device info resource and its parts
 */
enum { PART_GP, PART_GEOM, PART_WORDS, PART_PITCH };
enum { WORD_NONE, WORD_CODE, WORD_LABEL, WORD_VALUE };

typedef struct {
    int	    kind;	/* WORD_x (PART_WORDS) */
    char    *name;	/* a code, a label; the pitch table (PART_GEOM) */
    long    a, b;	/* the value; font and size (PART_GEOM) */
} Item;

typedef struct {
    size_t  pos;
    int	    kind;
    char    *name;
    Item    *items;
    int	    count, size;
} Part;

typedef struct {
    char    *name;
    long    type, connect, smarts;
    char    *custom[2], *syms, *modes[3], *draft, *nlq, *geom;
    List    marginTractor, marginAsf;
    long    input, output, width;
    char    *ui[3];
    Part    *parts;
    int	    numParts, sizeParts;
} Info;

static Info *infos;
static int  numInfos, sizeInfos;

static Part *
NewPart(Info *inf, size_t pos, int kind, const char *name, size_t nameLen)
{
    Part *p;

    inf->parts = (Part *) Grow(inf->parts, inf->numParts, &inf->sizeParts,
			       sizeof(Part));
    p = &inf->parts[inf->numParts++];
    p->pos = pos;
    p->kind = kind;
    p->name = Dup(name, nameLen);
    p->items = NULL;
    p->count = p->size = 0;
    return p;
}

static Item *
NewItem(Part *p)
{
    Item *i;

    p->items = (Item *) Grow(p->items, p->count, &p->size, sizeof(Item));
    i = &p->items[p->count++];
    i->kind = WORD_NONE;
    i->name = NULL;
    i->a = i->b = 0;
    return i;
}

static int
HasPart(const Info *inf, const char *name)
{
    int i;

    for (i = 0; i < inf->numParts; i++) {
	if (strcmp(inf->parts[i].name, name) == 0) {
	    return 1;
	}
    }
    return 0;
}

/*
 * "<name> label <type>" and blanks to the end of the line, at the
 * start of a line: its block reaches to the next line that begins with
 * "<word> label", "<word> ends" or (alsoDownload) "<word>
 * DownloadProperties".  *nameEnd, *block, *blockEnd are set.
 */
static int
LabelBlock(const char *p, const char *type, int alsoDownload,
	   const char **nameEnd, const char **block, const char **blockEnd)
{
    const char *q = p, *s;

    if (!MWord(&q)) {
	return 0;
    }
    *nameEnd = q;
    if (!MSpace1(&q) || !MLit(&q, "label") || !MSpace1(&q) ||
	!MLit(&q, type) || !MEol(&q)) {
	return 0;
    }
    *block = q;
    for (s = NextLine(q); s != NULL; s = NextLine(s)) {
	const char *x = s;

	if (MWord(&x) && MSpace1(&x) &&
	    (strncmp(x, "label", 5) == 0 || strncmp(x, "ends", 4) == 0 ||
	     (alsoDownload && strncmp(x, "DownloadProperties", 18) == 0))) {
	    break;
	}
    }
    *blockEnd = s != NULL ? s : q + strlen(q);
    return 1;
}

static void
ParseInfoBody(const char *name, const char *body)
{
    const char	*p = strstr(body, "PrinterInfo"), *q, *e;
    char	*text, *s;
    List	items = {NULL, 0, 0}, f = {NULL, 0, 0};
    int		*nested, i, k;
    Info	inf, *slot = NULL;

    if (p == NULL) {
	Fail(name, "has no PrinterInfo", NULL);
    }
    text = Inner(p, NULL);
    SubLists(text, &items, &nested);
    free(text);
    if (items.count != 20) {
	Fail(name, "has not 20 PrinterInfo fields", NULL);
    }
    memset(&inf, 0, sizeof(inf));
    inf.name = DupStr(name);
    inf.type = Record("type", typeWidths, items.item[0], nested[0]);
    inf.connect = Record("connect", connectWidths, items.item[1], nested[1]);
    inf.smarts = Value(items.item[2]);
    inf.custom[0] = RoutineOf(items.item[3]);
    inf.custom[1] = RoutineOf(items.item[4]);
    inf.syms = OffsetOf(items.item[11]);
    for (i = 0; i < 3; i++) {
	inf.modes[i] = OffsetOf(items.item[5 + i]);
    }
    inf.draft = OffsetOf(items.item[8]);
    inf.nlq = OffsetOf(items.item[9]);
    inf.geom = RoutineOf(items.item[10]);
    if (!nested[12] || !nested[13]) {
	Fail(name, "has no margins", NULL);
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
	inf.ui[i] = DupStr(items.item[17 + i]);
    }
    ListFree(&items);
    free(nested);

    /* the parts after the PrinterInfo, in their order:
     * <name> GraphicsProperties <...> */
    for (p = body; p != NULL; ) {
	q = p;
	if (MWord(&q) && (e = q, MSpace1(&q)) &&
	    MLit(&q, "GraphicsProperties") && (MSpace(&q), *q == '<') &&
	    strchr(q, '>') != NULL) {
	    const char	*end = strchr(q, '>');
	    Part	*part = NewPart(&inf, p - body, PART_GP, p, e - p);

	    s = Dup(q + 1, end - q - 1);
	    SplitCommas(s, &f);
	    free(s);
	    for (i = 0; i < f.count && i < 6; i++) {
		NewItem(part)->a = Value(f.item[i]);
	    }
	    ListFree(&f);
	    p = LineStart(body, end + 1);
	} else {
	    p = NextLine(p);
	}
    }
    if (inf.geom != NULL) {
	const char *down = NULL, *geom = NULL;

	for (p = body; p != NULL && down == NULL; p = NextLine(p)) {
	    q = p;
	    if (MLit(&q, inf.geom) && MSpace1(&q) &&
		MLit(&q, "DownloadProperties") && (MSpace(&q), *q == '<') &&
		strchr(q, '>') != NULL) {
		down = p;
		break;
	    }
	}
	if (down != NULL) {
	    /* the PCL4 drivers: the soft fonts the printer takes, the
	     * largest point size of one */
	    Part *part = NewPart(&inf, down - body, PART_WORDS, inf.geom,
				 strlen(inf.geom));

	    s = Dup(q + 1, strchr(q, '>') - q - 1);
	    SplitCommas(s, &f);
	    free(s);
	    for (i = 0; i < f.count; i++) {
		Item *it = NewItem(part);

		it->kind = WORD_VALUE;
		it->a = Value(f.item[i]);
	    }
	    ListFree(&f);
	} else {
	    /* <geom> FontGeometry <...> ... word FID_INVALID */
	    Part *part;

	    for (p = body; p != NULL; p = NextLine(p)) {
		q = p;
		if (MLit(&q, inf.geom) && MSpace1(&q) &&
		    MLit(&q, "FontGeometry")) {
		    geom = p;
		    break;
		}
	    }
	    e = NULL;
	    if (geom != NULL) {
		for (e = strstr(q, "word"); e != NULL;
		     e = strstr(e + 1, "word")) {
		    const char *x = e + 4;

		    if (MSpace1(&x) && strncmp(x, "FID_INVALID", 11) == 0) {
			break;
		    }
		}
	    }
	    if (e == NULL) {
		Fail(name, "has no FontGeometry", inf.geom);
	    }
	    part = NewPart(&inf, geom - body, PART_GEOM, inf.geom,
			   strlen(inf.geom));
	    text = Dup(q, e - q);
	    for (p = text; strchr(p, '<') != NULL; ) {
		Item *it;

		s = Inner(p, &p);
		SplitCommas(s, &f);
		free(s);
		if (f.count < 3) {
		    Fail(name, "has a FontGeometry without its fields", NULL);
		}
		it = NewItem(part);
		it->a = Value(f.item[0]);
		it->b = Value(f.item[1]);
		it->name = OffsetOf(f.item[2]);
		ListFree(&f);
	    }
	    free(text);
	}
    }
    /* <name> label byte, then lines of "byte <value>": a pitch table */
    for (p = body; p != NULL; ) {
	const char *nameEnd, *block, *blockEnd;

	if (LabelBlock(p, "byte", 0, &nameEnd, &block, &blockEnd)) {
	    Part *part = NewPart(&inf, p - body, PART_PITCH, p, nameEnd - p);

	    for (q = block; q < blockEnd; ) {
		const char *eol = memchr(q, '\n', blockEnd - q);

		if (eol == NULL) {
		    eol = blockEnd;
		}
		s = Dup(q, eol - q);
		SplitWords(s, &f);
		free(s);
		if (f.count == 2 && strcmp(f.item[0], "byte") == 0) {
		    NewItem(part)->a = Value(f.item[1]);
		}
		ListFree(&f);
		q = eol < blockEnd ? eol + 1 : blockEnd;
	    }
	    p = *blockEnd != '\0' ? blockEnd : NULL;
	} else {
	    p = NextLine(p);
	}
    }
    /* lists of words: the symbol set list (control codes by
     * PrinterSymbolSet) and the PCL drivers' font lists (empty, or
     * nptrs to labels of the resource) */
    for (p = body; p != NULL; ) {
	const char *nameEnd, *block, *blockEnd;

	if (LabelBlock(p, "word", 1, &nameEnd, &block, &blockEnd)) {
	    Part *part = NewPart(&inf, p - body, PART_WORDS, p, nameEnd - p);

	    for (q = block; q < blockEnd; ) {
		const char *eol = memchr(q, '\n', blockEnd - q), *x;
		int	   isNptr;

		if (eol == NULL) {
		    eol = blockEnd;
		}
		s = Trim(q, eol - q);
		x = s;
		isNptr = strncmp(s, "nptr", 4) == 0;
		if ((isNptr || strncmp(s, "word", 4) == 0) &&
		    (x = s + 4, MSpace1(&x)) && *x != '\0') {
		    Item *it = NewItem(part);

		    if (strcmp(x, "NULL") == 0 || strcmp(x, "0") == 0) {
			it->kind = WORD_NONE;
		    } else if (isNptr) {
			it->kind = WORD_LABEL;
			it->name = DupStr(x);
		    } else if (inf.syms != NULL &&
			       strcmp(part->name, inf.syms) == 0) {
			it->kind = WORD_CODE;
			it->name = OffsetOf(x);
		    } else {
			Fail(name, "word list", part->name);
		    }
		}
		free(s);
		q = eol < blockEnd ? eol + 1 : blockEnd;
	    }
	    p = *blockEnd != '\0' ? blockEnd : NULL;
	} else {
	    p = NextLine(p);
	}
    }
    /* in the order of the asm */
    for (i = 1; i < inf.numParts; i++) {
	Part part = inf.parts[i];

	for (k = i; k > 0 && inf.parts[k - 1].pos > part.pos; k--) {
	    inf.parts[k] = inf.parts[k - 1];
	}
	inf.parts[k] = part;
    }
    if (inf.syms != NULL && !HasPart(&inf, inf.syms)) {
	Fail(name, "no symbol set list", inf.syms);
    }
    for (i = 0; i < 3; i++) {
	if (inf.modes[i] != NULL && !HasPart(&inf, inf.modes[i])) {
	    Fail(name, "no mode", inf.modes[i]);
	}
    }
    /* (a later one of the same name takes its place) */
    for (i = 0; i < numInfos; i++) {
	if (strcmp(infos[i].name, name) == 0) {
	    slot = &infos[i];
	}
    }
    if (slot == NULL) {
	infos = (Info *) Grow(infos, numInfos, &sizeInfos, sizeof(Info));
	slot = &infos[numInfos++];
    }
    *slot = inf;
}

/*
 * "<name> segment resource" ... "<name> ends", from the start of a
 * line p (name NULL: any): the body as a new string, *next after it
 */
static char *
Segment(const char *p, const char *name, char **nameOut, const char **next)
{
    const char *q = p, *e;
    char       *n;

    if (name != NULL ? !MLit(&q, name) : !MWord(&q)) {
	return NULL;
    }
    n = Dup(p, q - p);
    if (MSpace1(&q) && MLit(&q, "segment") && MSpace1(&q) &&
	MLit(&q, "resource") && MEol(&q)) {
	for (e = NextLine(q); e != NULL; e = NextLine(e)) {
	    const char *x = e;

	    if (MLit(&x, n) && MSpace1(&x) && MLit(&x, "ends")) {
		char *body = Dup(q, e - q);

		if (nameOut != NULL) {
		    *nameOut = n;
		} else {
		    free(n);
		}
		*next = x;
		return body;
	    }
	}
    }
    free(n);
    return NULL;
}

static void
ParseInfo(const char *path)
{
    char	*text = ReadText(path), *name, *body;
    const char	*p = text, *next;

    while (p != NULL) {
	if ((body = Segment(p, NULL, &name, &next)) != NULL) {
	    ParseInfoBody(name, body);
	    free(body);
	    free(name);
	    p = LineStart(text, next);
	} else {
	    p = NextLine(p);
	}
    }
    free(text);
}

static void
ReadConstants(const char *path)
{
    char	*text = ReadText(path);
    const char	*p;

    for (p = text; p != NULL; p = NextLine(p)) {
	const char  *eol = strchr(p, '\n'), *q, *a;
	char	    *line = Dup(p, eol != NULL ? (size_t) (eol - p) : strlen(p));
	char	    *name;
	List	    w = {NULL, 0, 0};
	long	    v;

	/* <name> = <value>, <name> = <value> shl <value> (or shr) */
	q = line;
	MSpace(&q);
	a = q;
	if (MWord(&q)) {
	    name = Dup(a, q - a);
	    MSpace(&q);
	    if (*q == '=') {
		SplitWords(q + 1, &w);
		if (w.count == 1 ||
		    (w.count == 3 && (strcmp(w.item[1], "shl") == 0 ||
				      strcmp(w.item[1], "shr") == 0))) {
		    const char *x = q + 1;

		    MSpace(&x);
		    if (TryValue(x, &v)) {
			SetConst(name, v);
		    }
		}
		ListFree(&w);
	    }
	    free(name);
	}
	free(line);
    }
    free(text);
}

typedef struct {
    char *pd, *name, *info;
} Device;

static Device	*devices;
static int	numDevices, sizeDevices;
static List	pdi;			/* the PrintDriverInfo's fields */
static long	*iso;			/* the ISO substitutions */
static int	numIso, sizeIso;
static Str	trans;			/* the ASCII translation list */

static void
ParseDevices(const char *path)
{
    char	*text = ReadText(path), *s;
    const char	*p, *q, *a, *b, *c, *d;
    int		*nested, i;
    List	f = {NULL, 0, 0};

    /* DefPrinter PD_X, "name", infoResource */
    for (p = text; p != NULL; ) {
	q = p;
	MSpace(&q);
	if (MLit(&q, "DefPrinter") && MSpace1(&q) && (a = q, MWord(&q)) &&
	    (b = q, MSpace(&q), *q == ',') && (q++, MSpace(&q), *q == '"') &&
	    (c = strchr(q + 1, '"')) != NULL) {
	    const char *nm = q + 1;

	    q = c + 1;
	    MSpace(&q);
	    if (*q == ',' && (q++, MSpace(&q), d = q, MWord(&q))) {
		devices = (Device *) Grow(devices, numDevices, &sizeDevices,
					  sizeof(Device));
		devices[numDevices].pd = Dup(a, b - a);
		devices[numDevices].name = Dup(nm, c - nm);
		devices[numDevices++].info = Dup(d, q - d);
		p = LineStart(text, q);
		continue;
	    }
	}
	p = NextLine(p);
    }
    /* the PrintDriverInfo; older drivers (mercury) give only the first
     * fields: the rest are 0, as Esp leaves them */
    p = strstr(text, "PrintDriverInfo");
    if (p == NULL) {
	Fail(path, "has no PrintDriverInfo", NULL);
    }
    s = Inner(p, NULL);
    SubLists(s, &pdi, &nested);
    free(s);
    free(nested);
    while (pdi.count < 6) {
	ListAdd(&pdi, DupStr("0"));
    }
    /* <name> chunk.word <values>, over the lines after it that begin
     * with a blank: it ends at an empty line or one that begins with a
     * word */
    for (p = text; p != NULL; p = NextLine(p)) {
	q = p;
	if (MWord(&q) && MSpace1(&q) && MLit(&q, "chunk.word")) {
	    const char *e;

	    for (e = NextLine(q); e != NULL; e = NextLine(e)) {
		const char *x = e;

		if ((*e != '\0' && !IsSpace(*e)) || *e == '\n') {
		    break;
		}
		MSpace(&x);
		if (*x == '\0' || memchr(e, '\n', x - e) != NULL) {
		    break;
		}
	    }
	    if (e != NULL) {
		s = Dup(q, e - q);
		for (i = 0; s[i] != '\0'; i++) {
		    if (s[i] == '\n') {
			s[i] = ' ';
		    }
		}
		SplitCommas(s, &f);
		free(s);
		for (i = 0; i < f.count; i++) {
		    if (f.item[i][0] != '\0') {
			iso = (long *) Grow(iso, numIso, &sizeIso,
					    sizeof(long));
			iso[numIso++] = Value(f.item[i]);
		    }
		}
		ListFree(&f);
		break;
	    }
	}
    }
    /* <name> chunk.char "text", value, ... */
    StrAdd(&trans, "", 0);
    for (p = text; p != NULL; p = NextLine(p)) {
	q = p;
	if (MWord(&q) && MSpace1(&q) && MLit(&q, "chunk.char") &&
	    MSpace1(&q)) {
	    const char *eol = strchr(q, '\n');

	    s = Dup(q, eol != NULL ? (size_t) (eol - q) : strlen(q));
	    SplitCommas(s, &f);
	    free(s);
	    for (i = 0; i < f.count; i++) {
		char *t = f.item[i];

		if (t[0] == '"') {
		    size_t n = strlen(t);

		    while (n > 0 && t[n - 1] == '"') {
			n--;
		    }
		    while (n > 0 && *t == '"') {
			t++, n--;
		    }
		    StrAdd(&trans, t, n);
		} else {
		    long v = Value(t);
		    char ch = (char) v;

		    if (v < 0 || v > 255) {
			Fail("no byte:", t, NULL);
		    }
		    StrAdd(&trans, &ch, 1);
		}
	    }
	    ListFree(&f);
	    break;
	}
    }
    free(text);
}

/*
 * printerFontInfo: the lists of fonts, the labels of each list, the
 * FontEntries
 */
typedef struct {
    char    *name;
    List    fonts;
} FontList;

typedef struct {
    char    *name;
    long    v[4];	/* font, size, pitch, symbol set */
    char    *command;	/* NULL: none */
    long    style, styles;
} FontEntry;

static FontList	    *fontLists;
static int	    numFontLists, sizeFontLists;
static List	    labelNames, labelLists;	/* a label, its list */
static FontEntry    *fontEntries;
static int	    numFontEntries, sizeFontEntries;

static void
ParseFonts(const char *path)
{
    char	*text = ReadText(path), *body = NULL, *s;
    const char	*p, *q, *next;
    List	pending = {NULL, 0, 0}, w = {NULL, 0, 0};
    List	aliasNames = {NULL, 0, 0}, aliasEntries = {NULL, 0, 0};
    int		firstList = numFontLists, cur = -1, i, k;

    for (p = text; p != NULL && body == NULL; p = NextLine(p)) {
	body = Segment(p, "printerFontInfo", NULL, &next);
    }
    if (body == NULL) {
	Fail(path, "has no printerFontInfo resource", NULL);
    }
    for (p = body; p != NULL; p = NextLine(p)) {
	const char *eol = strchr(p, '\n');

	s = Dup(p, eol != NULL ? (size_t) (eol - p) : strlen(p));
	SplitWords(s, &w);
	free(s);
	if (w.count >= 3 && strcmp(w.item[1], "label") == 0) {
	    ListAdd(&pending, DupStr(w.item[0]));
	    cur = -1;
	} else if (w.count > 0 && strcmp(w.item[0], "nptr") == 0) {
	    if (cur < 0) {
		if (pending.count == 0) {
		    Fail(path, "has a font list without a label", NULL);
		}
		fontLists = (FontList *) Grow(fontLists, numFontLists,
					      &sizeFontLists,
					      sizeof(FontList));
		cur = numFontLists++;
		fontLists[cur].name = DupStr(pending.item[0]);
		memset(&fontLists[cur].fonts, 0, sizeof(List));
		for (i = 0; i < pending.count; i++) {
		    /* (a later label of the same name takes its place) */
		    if ((k = ListFind(&labelNames, pending.item[i])) >= 0) {
			free(labelLists.item[k]);
			labelLists.item[k] = DupStr(pending.item[0]);
		    } else {
			ListAdd(&labelNames, DupStr(pending.item[i]));
			ListAdd(&labelLists, DupStr(pending.item[0]));
		    }
		}
		ListFree(&pending);
	    }
	    if (w.count < 2) {
		Fail(path, "has an nptr without a name", NULL);
	    }
	    ListAdd(&fontLists[cur].fonts, DupStr(w.item[1]));
	} else if (w.count > 0 && strcmp(w.item[0], "word") == 0 &&
		   cur >= 0) {
	    cur = -1;
	} else if (w.count >= 2 && strcmp(w.item[1], "FontEntry") == 0) {
	    /* labels right before an entry name it too (fontDumbInfo's
	     * dumb_ROMAN12_PROP) */
	    for (i = 0; i < pending.count; i++) {
		if ((k = ListFind(&aliasNames, pending.item[i])) >= 0) {
		    free(aliasEntries.item[k]);
		    aliasEntries.item[k] = DupStr(w.item[0]);
		} else {
		    ListAdd(&aliasNames, DupStr(pending.item[i]));
		    ListAdd(&aliasEntries, DupStr(w.item[0]));
		}
	    }
	    ListFree(&pending);
	}
	ListFree(&w);
    }
    for (i = firstList; i < numFontLists; i++) {
	List *l = &fontLists[i].fonts;

	for (k = 0; k < l->count; k++) {
	    int a = ListFind(&aliasNames, l->item[k]);

	    if (a >= 0) {
		free(l->item[k]);
		l->item[k] = DupStr(aliasEntries.item[a]);
	    }
	}
    }
    /* <name> FontEntry <font, size, pitch, symbol set, offset command,
     *			 style, styles> */
    for (p = body; p != NULL; ) {
	const char *e;

	q = p;
	if (MWord(&q) && (e = q, MSpace1(&q)) && MLit(&q, "FontEntry") &&
	    (MSpace(&q), *q == '<')) {
	    FontEntry *fe;

	    s = Inner(q, &next);
	    SplitCommas(s, &w);
	    free(s);
	    if (w.count != 7) {
		s = Dup(p, e - p);
		Fail(s, "has not 7 FontEntry fields", NULL);
	    }
	    fontEntries = (FontEntry *) Grow(fontEntries, numFontEntries,
					     &sizeFontEntries,
					     sizeof(FontEntry));
	    fe = &fontEntries[numFontEntries++];
	    fe->name = Dup(p, e - p);
	    for (i = 0; i < 4; i++) {
		fe->v[i] = Value(w.item[i]);
	    }
	    fe->command = OffsetOf(w.item[4]);
	    fe->style = Value(w.item[5]);
	    fe->styles = Value(w.item[6]);
	    ListFree(&w);
	    p = LineStart(body, q + 1);
	} else {
	    p = NextLine(p);
	}
    }
    ListFree(&pending);
    ListFree(&aliasNames);
    ListFree(&aliasEntries);
    free(body);
    free(text);
}

static int
CompareInfos(const void *a, const void *b)
{
    return strcmp((*(Info *const *) a)->name, (*(Info *const *) b)->name);
}

static void
PrintValues(FILE *f, const List *l)
{
    int i;

    for (i = 0; i < l->count; i++) {
	fprintf(f, "%s%ld", i ? ", " : "", Value(l->item[i]));
    }
}

/* "offsetof(DMDev_<info>, <part>)", "0" for none */
static void
PrintOff(FILE *f, const Info *inf, const char *part)
{
    if (part == NULL) {
	fprintf(f, "0");
    } else {
	fprintf(f, "offsetof(DMDev_%s, %s)", inf->name, part);
    }
}

/* a list of fonts: one in the info resource (the PCL drivers' empty
 * NLQ list: the mode is there, its fonts the printer's), or one of
 * printerFontInfo */
static void
PrintFontOff(FILE *f, const Info *inf, const char *list)
{
    int k;

    if (list == NULL || HasPart(inf, list)) {
	PrintOff(f, inf, list);
    } else if ((k = ListFind(&labelNames, list)) >= 0) {
	fprintf(f, "offsetof(PrintFontInfo, %s)", labelLists.item[k]);
    } else {
	Fail(inf->name, "no list of fonts", list);
    }
}

int
main(int argc, char **argv)
{
    const char	*out = NULL, *header = "printcom.h", *driverInfo = NULL;
    List	defs = {NULL, 0, 0}, fonts = {NULL, 0, 0};
    List	files = {NULL, 0, 0}, cmds = {NULL, 0, 0};
    List	customs = {NULL, 0, 0};
    Info	**sorted;
    FILE	*f;
    int		i, k, n;

    for (i = 1; i < argc; i++) {
	if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
	    out = argv[++i];
	} else if (strcmp(argv[i], "-D") == 0 && i + 1 < argc) {
	    ListAdd(&defs, DupStr(argv[++i]));
	} else if (strcmp(argv[i], "--fonts") == 0 && i + 1 < argc) {
	    ListAdd(&fonts, DupStr(argv[++i]));
	} else if (strcmp(argv[i], "--header") == 0 && i + 1 < argc) {
	    header = argv[++i];
	} else if (argv[i][0] == '-') {
	    out = NULL;
	    break;
	} else if (driverInfo == NULL) {
	    driverInfo = argv[i];
	} else {
	    ListAdd(&files, DupStr(argv[i]));
	}
    }
    if (out == NULL || driverInfo == NULL || files.count == 0) {
	fprintf(stderr, "usage: dmdevinfo2c -o devices.c "
		"[-D constants.def]... [--fonts font.asm]... "
		"[--header printcom.h] driverinfo.asm info.asm...\n");
	return 2;
    }
    for (i = 0; i < defs.count; i++) {
	ReadConstants(defs.item[i]);
    }
    ParseDevices(driverInfo);
    for (i = 0; i < files.count; i++) {
	ParseInfo(files.item[i]);
    }
    for (i = 0; i < fonts.count; i++) {	/* several: one after the other */
	ParseFonts(fonts.item[i]);
    }
    sorted = (Info **) Alloc(numInfos * sizeof(Info *));
    for (i = 0; i < numInfos; i++) {
	sorted[i] = &infos[i];
    }
    if (numInfos > 1) {
	qsort(sorted, numInfos, sizeof(Info *), CompareInfos);
    }

    if ((f = fopen(out, "w")) == NULL) {
	perror(out);
	return 1;
    }
    fprintf(f, "/* generated by dmdevinfo2c from the driver's info files;\n"
	    " * do not edit */\n#include \"%s\"\n#include <stddef.h>\n\n",
	    header);

    /* the font commands: those of the fonts, then those of the symbol
     * set lists (a list's own as they are in it) */
    for (i = 0; i < numFontEntries; i++) {
	const char *c = fontEntries[i].command;

	if (c != NULL && ListFind(&cmds, c) < 0) {
	    ListAdd(&cmds, DupStr(c));
	}
    }
    for (i = 0; i < numInfos; i++) {
	for (k = 0; k < infos[i].numParts; k++) {
	    const Part *part = &infos[i].parts[k];
	    int	       before = cmds.count, old;

	    if (part->kind != PART_WORDS) {
		continue;
	    }
	    for (n = 0; n < part->count; n++) {
		if (part->items[n].kind != WORD_CODE) {
		    continue;
		}
		old = ListFind(&cmds, part->items[n].name);
		if (old < 0 || old >= before) {
		    ListAdd(&cmds, DupStr(part->items[n].name));
		}
	    }
	}
    }
    for (i = 0; i < cmds.count; i++) {
	fprintf(f, "extern const byte %s[];\n", cmds.item[i]);
    }
    fprintf(f, "const byte *const printFontCommands[] = {\n    NULL,\n");
    for (i = 0; i < cmds.count; i++) {
	fprintf(f, "    %s,\n", cmds.item[i]);
    }
    fprintf(f, "};\n\n");

    /* the custom entry and exit routines of the jobs (PI_customEntry,
     * PI_customExit: indices into printCustomRoutines, 1 on) */
    for (i = 0; i < numInfos; i++) {
	int before = customs.count, old;

	for (k = 0; k < 2; k++) {
	    const char *c = sorted[i]->custom[k];

	    if (c == NULL) {
		continue;
	    }
	    old = ListFind(&customs, c);
	    if (old < 0 || old >= before) {
		ListAdd(&customs, DupStr(c));
	    }
	}
    }
    for (i = 0; i < customs.count; i++) {
	fprintf(f, "extern PrintCustomRoutine %s;\n", customs.item[i]);
    }
    fprintf(f, "PrintCustomRoutine *const printCustomRoutines[] = {\n"
	    "    NULL,\n");
    for (i = 0; i < customs.count; i++) {
	fprintf(f, "    %s,\n", customs.item[i]);
    }
    fprintf(f, "};\n\n");

    /* printerFontInfo */
    fprintf(f, "typedef struct {\n    word zero;\n");
    for (i = 0; i < numFontLists; i++) {
	fprintf(f, "    word %s[%d];\n", fontLists[i].name,
		fontLists[i].fonts.count + 1);
    }
    for (i = 0; i < numFontEntries; i++) {
	fprintf(f, "    FontEntry %s;\n", fontEntries[i].name);
    }
    fprintf(f, "} PrintFontInfo;\n\n");
    fprintf(f, "static const PrintFontInfo printerFontInfo = {\n    0,\n");
    for (i = 0; i < numFontLists; i++) {
	const List *l = &fontLists[i].fonts;

	for (k = 0; k < l->count; k++) {
	    for (n = 0; n < numFontEntries; n++) {
		if (strcmp(fontEntries[n].name, l->item[k]) == 0) {
		    break;
		}
	    }
	    if (n == numFontEntries) {
		Fail("no FontEntry", l->item[k], NULL);
	    }
	}
	fprintf(f, "    { ");
	for (k = 0; k < l->count; k++) {
	    fprintf(f, "%soffsetof(PrintFontInfo, %s)", k ? ", " : "",
		    l->item[k]);
	}
	fprintf(f, ", 0 },\n");
    }
    for (i = 0; i < numFontEntries; i++) {
	const FontEntry *e = &fontEntries[i];

	fprintf(f, "    { 0x%04lx, %ld, 0x%02lx, %ld, %d, 0x%04lx, 0x%04lx },"
		"\t/* %s */\n", e->v[0], e->v[1], e->v[2], e->v[3],
		e->command != NULL ? ListFind(&cmds, e->command) + 1 : 0,
		e->style, e->styles, e->name);
    }
    fprintf(f, "};\n\n");
    fprintf(f, "const byte *const printFontInfoBase = "
	    "(const byte *) &printerFontInfo;\n\n");

    for (i = 0; i < numInfos; i++) {
	const Info *inf = sorted[i];

	fprintf(f, "typedef struct {\n    PrinterInfo pi;\n");
	for (k = 0; k < inf->numParts; k++) {
	    const Part *part = &inf->parts[k];

	    switch (part->kind) {
	    case PART_GP:
		fprintf(f, "    GraphicsProperties %s;\n", part->name);
		break;
	    case PART_GEOM:
		fprintf(f, "    FontGeometry %s[%d];\n", part->name,
			part->count + 1);
		break;
	    case PART_WORDS:
		fprintf(f, "    word %s[%d];\n", part->name,
			part->count > 1 ? part->count : 1);
		break;
	    default:
		fprintf(f, "    byte %s[%d];\n", part->name, part->count);
		break;
	    }
	}
	fprintf(f, "} DMDev_%s;\n\n", inf->name);

	fprintf(f, "static const DMDev_%s %s = {\n  {\n", inf->name,
		inf->name);
	fprintf(f, "    0x%02lx, 0x%02lx, %ld,\t/* type, connect, smarts */\n",
		inf->type, inf->connect, inf->smarts);
	fprintf(f, "    %d, %d,\t\t\t/* custom entry, exit */\n    ",
		inf->custom[0] ? ListFind(&customs, inf->custom[0]) + 1 : 0,
		inf->custom[1] ? ListFind(&customs, inf->custom[1]) + 1 : 0);
	PrintOff(f, inf, inf->modes[0]);
	fprintf(f, ", ");
	PrintOff(f, inf, inf->modes[1]);
	fprintf(f, ",\n    ");
	PrintOff(f, inf, inf->modes[2]);
	fprintf(f, ",\n    ");
	PrintFontOff(f, inf, inf->draft);
	fprintf(f, ", ");
	PrintFontOff(f, inf, inf->nlq);
	fprintf(f, ",\n    ");
	PrintOff(f, inf, inf->geom);
	fprintf(f, ", ");
	PrintOff(f, inf, inf->syms);
	fprintf(f, ",\t/* fonts, symbol sets */\n    { ");
	PrintValues(f, &inf->marginTractor);
	fprintf(f, " },\t/* tractor margins */\n    { ");
	PrintValues(f, &inf->marginAsf);
	fprintf(f, " },\t/* ASF margins */\n");
	fprintf(f, "    0x%02lx, 0x%02lx, %ld,\t/* paper in, out, width */\n",
		inf->input, inf->output, inf->width);
	fprintf(f, "    0, 0, 0\t\t/* UI: %s, %s, %s */\n  },\n", inf->ui[0],
		inf->ui[1], inf->ui[2]);
	for (k = 0; k < inf->numParts; k++) {
	    const Part *part = &inf->parts[k];

	    switch (part->kind) {
	    case PART_GP:
		fprintf(f, "  { ");
		for (n = 0; n < part->count; n++) {
		    fprintf(f, "%s%ld", n ? ", " : "", part->items[n].a);
		}
		fprintf(f, ", 0 },\t/* %s */\n", part->name);
		break;
	    case PART_GEOM:
		fprintf(f, "  {\n");
		for (n = 0; n < part->count; n++) {
		    fprintf(f, "    { 0x%04lx, %ld, ", part->items[n].a,
			    part->items[n].b);
		    PrintOff(f, inf, part->items[n].name);
		    fprintf(f, " },\n");
		}
		fprintf(f, "    { 0, 0, 0 }\n  },\n");
		break;
	    case PART_WORDS:
		fprintf(f, "  { ");
		for (n = 0; n < part->count; n++) {
		    const Item *it = &part->items[n];

		    fprintf(f, "%s", n ? ", " : "");
		    if (it->kind == WORD_CODE) {
			fprintf(f, "%d", ListFind(&cmds, it->name) + 1);
		    } else if (it->kind == WORD_LABEL) {
			PrintOff(f, inf, it->name);
		    } else {
			fprintf(f, "%ld", it->a);
		    }
		}
		fprintf(f, "%s },\t/* %s */\n", part->count ? "" : "0",
			part->name);
		break;
	    default:
		fprintf(f, "  { ");
		for (n = 0; n < part->count; n++) {
		    fprintf(f, "%s0x%02lx", n ? ", " : "", part->items[n].a);
		}
		fprintf(f, " },\t/* %s */\n", part->name);
		break;
	    }
	}
	fprintf(f, "};\n\n");
    }

    fprintf(f, "const PrintDeviceEntry printDevices[] = {\n");
    for (i = 0; i < numDevices; i++) {
	for (k = 0; k < numInfos; k++) {
	    if (strcmp(infos[k].name, devices[i].info) == 0) {
		break;
	    }
	}
	if (k == numInfos) {
	    fprintf(stderr, "dmdevinfo2c: %s: no info resource %s\n",
		    devices[i].pd, devices[i].info);
	    return 1;
	}
	fprintf(f, "    { \"%s\", &%s.pi },\t/* %s = %d */\n",
		devices[i].name, devices[i].info, devices[i].pd, 2 * i);
    }
    fprintf(f, "};\n\n");
    fprintf(f, "const word printNumDevices = %d;\n\n", numDevices);

    /* the PrintDriverInfo and the tables of its chunks */
    fprintf(f, "const PrintDriverInfo printDriverInfo = {\n"
	    "    %ld, %ld,\n"
	    "    0, 0,\t\t/* the tables: printTransTables */\n"
	    "    %ld, %ld\n};\n\n", Value(pdi.item[0]), Value(pdi.item[1]),
	    Value(pdi.item[4]), Value(pdi.item[5]));
    if (numIso > 0) {
	fprintf(f, "static const word isoSubstitutions[] = {\n");
	for (i = 0; i < numIso; i++) {
	    fprintf(f, "%s0x%04lx,%s", i % 8 == 0 ? "    " : " ", iso[i],
		    i % 8 == 7 || i + 1 == numIso ? "\n" : "");
	}
	fprintf(f, "};\n");
    }
    fprintf(f, "static const byte asciiTrans[] = { ");
    for (i = 0; i < (int) trans.len; i++) {
	fprintf(f, "%s%d", i ? ", " : "", (unsigned char) trans.text[i]);
    }
    fprintf(f, "%s };\n", trans.len ? "" : "0");
    fprintf(f, "const SpoolTransTables printTransTables = {\n"
	    "    %s, asciiTrans, %lu\n};\n",
	    numIso > 0 ? "isoSubstitutions" : "NULL",
	    (unsigned long) trans.len);
    if (fclose(f) != 0) {
	perror(out);
	return 1;
    }
    return 0;
}
