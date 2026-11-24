/***********************************************************************
 * asmchars2c.c -- the char data of asm files as C.
 *
 * The PostScript translation library
 * (Library/Trans/Graphics/Vector/EPS) keeps its PostScript text as char
 * data in asm resources:
 *
 *	emitDL		char	" DL", NL	; comment
 *	beginPSProlog	label	byte
 *		char	"%%BeginProlog", NL
 *	endPSProlog	label	byte
 *
 * Every name with char data becomes a C array, e.g.
 *
 *	const char emitDL[] = " DL\r\n";
 *
 * and the bytes between "<name> label byte" and the next label become
 * the array <name> (the prolog: beginPSProlog up to endPSProlog), with
 * <name>Size, its length; a label named end* only ends a block.  A
 * line ending in a backslash goes on in the next.  Lengths are
 * sizeof - 1, as "length <name>" was in the asm.
 *
 * Values: "strings" (with "" for a quote), NL (CR LF), C_CR, C_LF,
 * C_CTRL_D, C_NULL, C_TAB, numbers (decimal, 0x.., ..h).
 * COMMENT @ ... @ blocks and ; comments are skipped.
 *
 * With -H, a header declares the arrays with their sizes, so that
 * sizeof works on them in other files:
 *
 *	extern const char emitDL[6];
 *
 * usage: asmchars2c -o out.c [-H out.h] file.asm...
 ***********************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct {
    char    *data;
    size_t  len;
    size_t  size;
} Bytes;

typedef struct {
    char    *name;
    Bytes   data;
    int	    isLabel;	/* a label block: its size is written too */
} Array;

static Array	*arrays;
static int	numArrays, sizeArrays;

static void
Fail(const char *where, const char *what, const char *arg)
{
    fprintf(stderr, "%s%s%s%s%s\n", where ? where : "", where ? ": " : "",
	    what, arg ? " " : "", arg ? arg : "");
    exit(1);
}

static void *
Alloc(size_t size)
{
    void *p = malloc(size ? size : 1);

    if (p == NULL) {
	Fail(NULL, "out of memory", NULL);
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

static void
BytesAdd(Bytes *b, const char *data, size_t len)
{
    if (b->len + len + 1 > b->size) {
	char *n;

	b->size = 2 * (b->len + len) + 64;
	n = (char *) Alloc(b->size);
	if (b->len) {
	    memcpy(n, b->data, b->len);
	}
	free(b->data);
	b->data = n;
    }
    if (len) {
	memcpy(b->data + b->len, data, len);
    }
    b->len += len;
}

static int
AddArray(const char *name, const Bytes *data, int isLabel)
{
    if (numArrays == sizeArrays) {
	Array *n;

	sizeArrays = sizeArrays ? 2 * sizeArrays : 64;
	n = (Array *) Alloc(sizeArrays * sizeof(Array));
	if (numArrays) {
	    memcpy(n, arrays, numArrays * sizeof(Array));
	}
	free(arrays);
	arrays = n;
    }
    arrays[numArrays].name = Dup(name, strlen(name));
    arrays[numArrays].data.data = NULL;
    arrays[numArrays].data.len = arrays[numArrays].data.size = 0;
    BytesAdd(&arrays[numArrays].data, data->data, data->len);
    arrays[numArrays].isLabel = isLabel;
    return numArrays++;
}

/*
 * The line without its ; comment (not inside a string) and without
 * blanks around it: a new string
 */
static char *
StripComment(const char *s)
{
    const char	*p, *end;
    int		q = 0;

    for (p = s; *p != '\0'; p++) {
	if (*p == '"') {
	    q = !q;
	} else if (*p == ';' && !q) {
	    break;
	}
    }
    end = p;
    while (isspace((unsigned char) *s) && s < end) {
	s++;
    }
    while (end > s && isspace((unsigned char) end[-1])) {
	end--;
    }
    return Dup(s, end - s);
}

static int
IsWordChar(int c)
{
    return isalnum(c) || c == '_';
}

static int
AllHex(const char *s, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
	if (!isxdigit((unsigned char) s[i])) {
	    return 0;
	}
    }
    return n > 0;
}

/*
 * The bytes of a comma separated value list
 */
static void
Values(const char *s, const char *where, Bytes *data)
{
    static const struct {
	const char  *name;
	const char  *bytes;
	size_t	    len;
    } consts[] = {
	{"NL", "\r\n", 2}, {"C_CR", "\r", 1}, {"C_LF", "\n", 1},
	{"C_CTRL_D", "\004", 1}, {"C_NULL", "\0", 1}, {"C_TAB", "\t", 1}
    };
    const char *p = s;

    while (*p != '\0') {
	if (*p == ' ' || *p == '\t' || *p == ',') {
	    p++;
	} else if (*p == '"') {
	    p++;
	    for (;;) {
		if (*p == '\0') {
		    Fail(where, "open string", NULL);
		}
		if (*p == '"') {
		    if (p[1] == '"') {
			BytesAdd(data, "\"", 1);
			p += 2;
			continue;
		    }
		    break;
		}
		BytesAdd(data, p, 1);
		p++;
	    }
	    p++;
	} else {
	    const char	*t = p;
	    char	*tok, c;
	    size_t	n, i;
	    int		found = 0;
	    long	v = -1;

	    while (IsWordChar((unsigned char) *p)) {
		p++;
	    }
	    if (p == t) {
		Fail(where, "cannot read", t);
	    }
	    n = p - t;
	    tok = Dup(t, n);
	    for (i = 0; i < sizeof(consts) / sizeof(consts[0]); i++) {
		if (strcmp(tok, consts[i].name) == 0) {
		    BytesAdd(data, consts[i].bytes, consts[i].len);
		    found = 1;
		}
	    }
	    if (!found) {
		if (n > 2 && tok[0] == '0' && tok[1] == 'x' &&
		    AllHex(tok + 2, n - 2)) {
		    v = strtol(tok + 2, NULL, 16);
		} else if (n > 1 && tok[n - 1] == 'h' &&
			   isdigit((unsigned char) tok[0]) &&
			   AllHex(tok, n - 1)) {
		    v = strtol(tok, NULL, 16);
		} else {
		    for (i = 0; i < n && isdigit((unsigned char) tok[i]);
			 i++) {
			;
		    }
		    if (i == n) {
			v = strtol(tok, NULL, 10);
		    }
		}
		if (v < 0 || v > 255) {
		    Fail(where, "unknown value", tok);
		}
		c = (char) v;
		BytesAdd(data, &c, 1);
	    }
	    free(tok);
	}
    }
}

/* <name> then blanks then the word: where the rest begins, or NULL */
static const char *
NameThen(const char *s, const char *word, int noCase, size_t *nameLen)
{
    const char	*p = s;
    size_t	n = strlen(word), i;

    if (!(isalpha((unsigned char) *p) || *p == '_')) {
	return NULL;
    }
    while (IsWordChar((unsigned char) *p)) {
	p++;
    }
    *nameLen = p - s;
    if (!isspace((unsigned char) *p)) {
	return NULL;
    }
    while (isspace((unsigned char) *p)) {
	p++;
    }
    for (i = 0; i < n; i++) {
	if (noCase ? tolower((unsigned char) p[i]) != word[i] :
		     p[i] != word[i]) {
	    return NULL;
	}
    }
    return p + n;
}

static char *
ReadFile(const char *path)
{
    FILE    *f = fopen(path, "rb");
    char    *text;
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
    return text;
}

static void
Parse(const char *path)
{
    char    *text = ReadFile(path), *p;
    char    **lines;
    int	    numLines = 1, n, inComment = 0;
    int	    cur = -1;		/* the array collecting data */
    char    *label = NULL;	/* the name of an open label block */
    Bytes   labelData = {NULL, 0, 0};
    char    where[1100];

    /* the lines, without the carriage returns at their ends */
    for (p = text; *p != '\0'; p++) {
	if (*p == '\n') {
	    numLines++;
	}
    }
    lines = (char **) Alloc(numLines * sizeof(char *));
    for (p = text, n = 0; n < numLines; n++) {
	char	*eol = strchr(p, '\n');
	size_t	len = eol != NULL ? (size_t) (eol - p) : strlen(p);

	while (len > 0 && p[len - 1] == '\r') {
	    len--;
	}
	lines[n] = Dup(p, len);
	p = eol != NULL ? eol + 1 : p + strlen(p);
    }
    free(text);
    /* a line ending in a backslash goes on in the next (kept as one
     * line, so that the line numbers stay right for the first) */
    for (n = 0; n < numLines - 1; n++) {
	for (;;) {
	    char    *s = StripComment(lines[n]), *next, *joined;
	    size_t  len = strlen(s);

	    if (len == 0 || s[len - 1] != '\\') {
		free(s);
		break;
	    }
	    s[len - 1] = '\0';
	    next = StripComment(lines[n + 1]);
	    {
		/* (the next line whole, only without blanks around) */
		char *e;

		free(next);
		next = lines[n + 1];
		while (isspace((unsigned char) *next)) {
		    next++;
		}
		e = next + strlen(next);
		while (e > next && isspace((unsigned char) e[-1])) {
		    e--;
		}
		next = Dup(next, e - next);
	    }
	    joined = (char *) Alloc(strlen(s) + strlen(next) + 2);
	    sprintf(joined, "%s %s", s, next);
	    free(s);
	    free(next);
	    free(lines[n]);
	    lines[n] = joined;
	    lines[n + 1][0] = '\0';
	}
    }

    for (n = 0; n < numLines; n++) {
	const char  *line = lines[n], *rest, *q;
	char	    *s;
	size_t	    nameLen;

	sprintf(where, "%.1000s:%d", path, n + 1);
	if (inComment) {
	    if (strchr(line, '@') != NULL) {
		inComment = 0;
	    }
	    continue;
	}
	for (q = line; isspace((unsigned char) *q); q++) {
	    ;
	}
	if (strncmp(q, "COMMENT", 7) == 0) {
	    for (q += 7; isspace((unsigned char) *q); q++) {
		;
	    }
	    if (*q == '@') {
		inComment = strchr(strchr(line, '@') + 1, '@') == NULL;
		continue;
	    }
	}
	s = StripComment(line);
	if (*s == '\0') {
	    free(s);
	    continue;
	}
	/* <name> label byte */
	rest = NameThen(s, "label", 1, &nameLen);
	if (rest != NULL && isspace((unsigned char) *rest)) {
	    for (q = rest; isspace((unsigned char) *q); q++) {
		;
	    }
	    if (tolower((unsigned char) q[0]) == 'b' &&
		tolower((unsigned char) q[1]) == 'y' &&
		tolower((unsigned char) q[2]) == 't' &&
		tolower((unsigned char) q[3]) == 'e' &&
		!IsWordChar((unsigned char) q[4])) {
		if (label != NULL) {
		    AddArray(label, &labelData, 1);
		    free(label);
		}
		/* an end label only ends the block before it */
		label = strncmp(s, "end", 3) == 0 ? NULL : Dup(s, nameLen);
		labelData.len = 0;
		cur = -1;
		free(s);
		continue;
	    }
	}
	/* <name> char <values> */
	rest = NameThen(s, "char", 0, &nameLen);
	if (rest != NULL && isspace((unsigned char) *rest)) {
	    Bytes   data = {NULL, 0, 0};
	    char    *name = Dup(s, nameLen);

	    while (isspace((unsigned char) *rest)) {
		rest++;
	    }
	    Values(rest, where, &data);
	    if (label != NULL) {
		BytesAdd(&labelData, data.data, data.len);
	    }
	    cur = AddArray(name, &data, 0);
	    free(name);
	    free(data.data);
	    free(s);
	    continue;
	}
	/* more of it: a line that begins with blanks and "char" */
	if ((line[0] == ' ' || line[0] == '\t') &&
	    strncmp(s, "char", 4) == 0) {
	    Bytes data = {NULL, 0, 0};

	    for (q = s; *q != '\0' && !isspace((unsigned char) *q); q++) {
		;
	    }
	    while (isspace((unsigned char) *q)) {
		q++;
	    }
	    if (*q == '\0') {
		Fail(where, "char without values", NULL);
	    }
	    Values(q, where, &data);
	    if (label != NULL) {
		BytesAdd(&labelData, data.data, data.len);
	    } else if (cur >= 0) {
		BytesAdd(&arrays[cur].data, data.data, data.len);
	    }
	    free(data.data);
	    free(s);
	    continue;
	}
	cur = -1;		/* anything else ends a definition */
	free(s);
    }
    if (label != NULL && labelData.len > 0) {
	fprintf(stderr, "%s: label %s has no end label\n", path, label);
	exit(1);
    }
    for (n = 0; n < numLines; n++) {
	free(lines[n]);
    }
    free(lines);
}

/* the bytes as a C string, a new line after each line feed */
static void
CString(FILE *f, const Bytes *data)
{
    size_t i;

    fputc('"', f);
    for (i = 0; i < data->len; i++) {
	unsigned char b = (unsigned char) data->data[i];

	if (b == 0x22) {
	    fputs("\\\"", f);
	} else if (b == 0x5c) {
	    fputs("\\\\", f);
	} else if (b == 0x0d) {
	    fputs("\\r", f);
	} else if (b == 0x0a) {
	    fputs("\\n", f);
	} else if (b >= 0x20 && b < 0x7f) {
	    fputc(b, f);
	} else {
	    fprintf(f, "\\%03o", b);
	}
	if (b == 0x0a && i + 1 < data->len) {
	    fputs("\"\n    \"", f);
	}
    }
    fputc('"', f);
}

static const char *
BaseName(const char *path)
{
    const char *p, *base = path;

    for (p = path; *p != '\0'; p++) {
	if (*p == '/') {
	    base = p + 1;
	}
    }
    return base;
}

int
main(int argc, char **argv)
{
    const char	*out = NULL, *header = NULL;
    int		ac = 1, first, i, j;
    FILE	*f;

    while (ac + 1 < argc && argv[ac][0] == '-') {
	if (strcmp(argv[ac], "-o") == 0) {
	    out = argv[ac + 1];
	} else if (strcmp(argv[ac], "-H") == 0) {
	    header = argv[ac + 1];
	} else {
	    break;
	}
	ac += 2;
    }
    if (out == NULL || ac >= argc || argv[ac][0] == '-') {
	fprintf(stderr, "usage: asmchars2c -o out.c [-H out.h] "
		"file.asm...\n");
	return 2;
    }
    first = ac;
    for (i = first; i < argc; i++) {
	Parse(argv[i]);
    }
    for (i = 0; i < numArrays; i++) {
	for (j = 0; j < i; j++) {
	    if (strcmp(arrays[i].name, arrays[j].name) == 0) {
		Fail(NULL, arrays[i].name, "defined twice");
	    }
	}
    }
    if ((f = fopen(out, "w")) == NULL) {
	perror(out);
	return 1;
    }
    fprintf(f, "/* generated by asmchars2c from ");
    for (i = first; i < argc; i++) {
	fprintf(f, "%s%s", i > first ? ", " : "", BaseName(argv[i]));
    }
    fprintf(f, " */\n\n");
    for (i = 0; i < numArrays; i++) {
	fprintf(f, "const char %s[] =\n    ", arrays[i].name);
	CString(f, &arrays[i].data);
	fprintf(f, ";\n");
	if (arrays[i].isLabel) {
	    fprintf(f, "const unsigned short %sSize = sizeof(%s) - 1;\n",
		    arrays[i].name, arrays[i].name);
	}
    }
    if (fclose(f) != 0) {
	perror(out);
	return 1;
    }
    if (header != NULL) {
	const char *b = BaseName(header);

	if ((f = fopen(header, "w")) == NULL) {
	    perror(header);
	    return 1;
	}
	fprintf(f, "/* generated by asmchars2c */\n#ifndef ");
	for (j = 0; j < 2; j++) {
	    for (i = 0; b[i] != '\0'; i++) {
		fputc(IsWordChar((unsigned char) b[i]) ?
		      toupper((unsigned char) b[i]) : '_', f);
	    }
	    fprintf(f, j == 0 ? "\n#define " : "\n\n");
	}
	for (i = 0; i < numArrays; i++) {
	    fprintf(f, "extern const char %s[%lu];\n", arrays[i].name,
		    (unsigned long) arrays[i].data.len + 1);
	    if (arrays[i].isLabel) {
		fprintf(f, "extern const unsigned short %sSize;\n",
			arrays[i].name);
	    }
	}
	fprintf(f, "\n#endif\n");
	if (fclose(f) != 0) {
	    perror(header);
	    return 1;
	}
    }
    return 0;
}
