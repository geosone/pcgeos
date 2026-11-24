/***********************************************************************
 * epsfonts2c.c -- the font tables of the PostScript translation library
 * in C, from its asm.
 *
 *	epsfonts2c -o epsFonts.c --fontid Include/fontID.def
 *	    --order exportFontTables.asm exportFontTables.asm
 *	    exportMoreFonts.asm
 *
 * The Adobe font entries (AdobeFontEntry: a FontID, the style, the
 * encoding, the PostScript name), the sets of them, the font lists of
 * each PSFontList (fontMapOffsets of exportFontTables.asm, in its
 * order), and the face tables of CalcFontDiff (serifTable, sansTable,
 * scriptTable: the style bytes of the DefineFace lines of fontID.def,
 * as FID_MAPPING_CODE builds them).
 ***********************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

static void
Fail(const char *what, const char *a, const char *b)
{
    fprintf(stderr, "epsfonts2c: %s%s%s%s%s\n", what, a ? " " : "",
	    a ? a : "", b ? " " : "", b ? b : "");
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

/* a list of strings (NULL is an item too: the 0 that ends a list) */
typedef struct {
    char    **item;
    int	    count;
    int	    size;
} List;

static void
ListAdd(List *l, const char *s)
{
    if (l->count == l->size) {
	char **n;

	l->size = l->size ? 2 * l->size : 32;
	n = (char **) Alloc(l->size * sizeof(char *));
	if (l->count) {
	    memcpy(n, l->item, l->count * sizeof(char *));
	}
	free(l->item);
	l->item = n;
    }
    l->item[l->count++] = s != NULL ? Dup(s, strlen(s)) : NULL;
}

static int
ListFind(const List *l, const char *s)
{
    int i;

    for (i = 0; i < l->count; i++) {
	if (l->item[i] != NULL && strcmp(l->item[i], s) == 0) {
	    return i;
	}
    }
    return -1;
}

static void
ListClear(List *l)
{
    int i;

    for (i = 0; i < l->count; i++) {
	free(l->item[i]);
    }
    l->count = 0;
}

/***********************************************************************
 *				LogicalLines
 ***********************************************************************
 * SYNOPSIS:	    The lines of an asm file, comments gone (a ";"
 *		    outside of a string), continuations joined.
 ***********************************************************************/
static void
LogicalLines(const char *path, List *out)
{
    FILE    *f = fopen(path, "rb");
    char    *cur = Dup("", 0);
    char    *buf;
    size_t  size = 4096, len;
    int	    c;

    if (f == NULL) {
	perror(path);
	exit(1);
    }
    buf = (char *) Alloc(size);
    do {
	char	*s, *full, q = 0;
	size_t	i, n = 0;

	/* a line */
	len = 0;
	while ((c = fgetc(f)) != EOF && c != '\n') {
	    if (len + 2 >= size) {
		char *t = (char *) Alloc(size * 2);

		memcpy(t, buf, len);
		free(buf);
		buf = t;
		size *= 2;
	    }
	    buf[len++] = (char) c;
	}
	if (c == EOF && len == 0) {
	    break;
	}
	while (len > 0 && (buf[len - 1] == '\r' || buf[len - 1] == '\n')) {
	    len--;
	}
	buf[len] = '\0';
	s = (char *) Alloc(len + 1);
	for (i = 0; i < len; i++) {
	    char ch = buf[i];

	    if (q) {
		s[n++] = ch;
		if (ch == q) {
		    q = 0;
		}
	    } else if (ch == '"' || ch == '\'') {
		q = ch;
		s[n++] = ch;
	    } else if (ch == ';') {
		break;
	    } else {
		s[n++] = ch;
	    }
	}
	while (n > 0 && isspace((unsigned char) s[n - 1])) {
	    n--;
	}
	s[n] = '\0';
	full = (char *) Alloc(strlen(cur) + n + 2);
	strcpy(full, cur);
	strcat(full, s);
	free(s);
	free(cur);
	if (n > 0 && full[strlen(full) - 1] == '\\') {
	    full[strlen(full) - 1] = ' ';
	    cur = full;
	} else {
	    ListAdd(out, full);
	    free(full);
	    cur = Dup("", 0);
	}
    } while (c != EOF);
    if (*cur != '\0') {
	ListAdd(out, cur);
    }
    free(cur);
    free(buf);
    fclose(f);
}

/*
 * Matchers on a line: each moves *p on and says whether its part is
 * there.
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
Blanks1(const char **p)
{
    if (!isspace((unsigned char) **p)) {
	return 0;
    }
    Blanks(p);
    return 1;
}

static char *
Word(const char **p)
{
    const char *s = *p;

    while (IsWordChar((unsigned char) **p)) {
	(*p)++;
    }
    return *p > s ? Dup(s, *p - s) : NULL;
}

/* the keyword, and no more of a word after it if "whole" */
static int
Keyword(const char **p, const char *kw, int whole)
{
    size_t n = strlen(kw);

    if (strncmp(*p, kw, n) != 0 ||
	(whole && IsWordChar((unsigned char) (*p)[n]))) {
	return 0;
    }
    *p += n;
    return 1;
}

static int
AtEnd(const char *p)
{
    Blanks(&p);
    return *p == '\0';
}

static long
Number(const char *s, const char *what)
{
    char    *end;
    long    v = strtol(s, &end, 0);

    if (end == s || *end != '\0') {
	Fail(what, s, NULL);
    }
    return v;
}

/*
 * "mask TS_A or mask TS_B" as C: "TS_A | TS_B"
 */
static char *
StyleC(const char *expr)
{
    const char	*p = expr, *end;
    char	*out, *o;

    Blanks(&p);
    end = p + strlen(p);
    while (end > p && isspace((unsigned char) end[-1])) {
	end--;
    }
    if (end == p || (end - p == 1 && *p == '0')) {
	return Dup("0", 1);
    }
    out = o = (char *) Alloc(2 * (end - p) + 8);
    while (p < end) {
	const char *part = p, *e, *q;
	char	   *name;

	/* up to an "or" that is a word */
	for (e = p; e < end; e++) {
	    if (e[0] == 'o' && e + 1 < end && e[1] == 'r' &&
		(e == expr || !IsWordChar((unsigned char) e[-1])) &&
		(e + 2 >= end || !IsWordChar((unsigned char) e[2]))) {
		break;
	    }
	}
	p = e < end ? e + 2 : end;
	while (part < e && isspace((unsigned char) *part)) {
	    part++;
	}
	while (e > part && isspace((unsigned char) e[-1])) {
	    e--;
	}
	q = part;
	if (!(Keyword(&q, "mask", 0) && Blanks1(&q) &&
	      strncmp(q, "TS_", 3) == 0 && (name = Word(&q)) != NULL &&
	      q == e)) {
	    Fail("style", expr, NULL);
	}
	if (o > out) {
	    strcpy(o, " | ");
	    o += 3;
	}
	strcpy(o, name);
	o += strlen(name);
	free(name);
    }
    return out;
}

/*
 * The tables: the entries (in the order they are met first), the names,
 * the lists.
 */
typedef struct {
    char    *label;
    char    *fid, *style, *encode, *nameLabel;	/* NULL: not there */
} Entry;

typedef struct {
    char    *label;
    List    items;	/* labels, NULL for the 0 at the end */
    char    *head;	/* the first label stacked with it */
} FontList;

static Entry	*entries;
static int	numEntries;
static List	nameLabels, nameStrings;
static FontList	*lists;
static int	numLists;
static List	order;		/* labels in file order, for output */

static int
FindEntry(const char *label)
{
    int i;

    for (i = 0; i < numEntries; i++) {
	if (strcmp(entries[i].label, label) == 0) {
	    return i;
	}
    }
    return -1;
}

static int
FindList(const char *label)
{
    int i;

    for (i = 0; i < numLists; i++) {
	if (strcmp(lists[i].label, label) == 0) {
	    return i;
	}
    }
    return -1;
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

static void
ParseTables(char **paths, int numPaths)
{
    List    pending = {NULL, 0, 0};	/* labels that start the next list */
    List    curList = {NULL, 0, 0};	/* the labels of the list read */
    int	    curEntry = -1, inList = 0, i, k;

    for (k = 0; k < numPaths; k++) {
	List lines = {NULL, 0, 0};

	LogicalLines(paths[k], &lines);
	for (i = 0; i < lines.count; i++) {
	    const char	*line = lines.item[i], *p;
	    char	*label, *w;

	    if (AtEnd(line)) {
		continue;
	    }
	    /* <label> label nptr.AdobeFontEntry */
	    p = line;
	    if ((label = Word(&p)) != NULL) {
		const char *q = p;

		if (Blanks1(&q) && Keyword(&q, "label", 0) && Blanks1(&q) &&
		    Keyword(&q, "nptr", 0)) {
		    const char	*e = q;
		    char	*run;

		    while (*e != '\0' && !isspace((unsigned char) *e)) {
			e++;
		    }
		    run = Dup(q, e - q);
		    if (strstr(run, "AdobeFontEntry") != NULL) {
			ListAdd(&pending, label);
			curEntry = -1;
			inList = 0;
			free(run);
			free(label);
			continue;
		    }
		    free(run);
		}
		/* <label> word FID_x */
		q = p;
		if (Blanks1(&q) && Keyword(&q, "word", 0) && Blanks1(&q) &&
		    strncmp(q, "FID_", 4) == 0 && (w = Word(&q)) != NULL) {
		    if (AtEnd(q)) {
			int e = FindEntry(label);

			if (e < 0) {
			    entries = (Entry *) Grow(entries, numEntries,
						     sizeof(Entry));
			    e = numEntries++;
			    entries[e].label = Dup(label, strlen(label));
			}
			entries[e].fid = w;
			entries[e].style = entries[e].encode =
			    entries[e].nameLabel = NULL;
			curEntry = e;
			inList = 0;
			ListClear(&pending);
			free(label);
			continue;
		    }
		    free(w);
		}
		/* <label> char "name" */
		q = p;
		if (Blanks1(&q) && Keyword(&q, "char", 0) && Blanks1(&q) &&
		    *q == '"' && strchr(q + 1, '"') != NULL) {
		    char    *s = Dup(q + 1, strchr(q + 1, '"') - q - 1);
		    int	    n = ListFind(&nameLabels, label);

		    if (n < 0) {
			ListAdd(&nameLabels, label);
			ListAdd(&nameStrings, s);
		    } else {
			free(nameStrings.item[n]);
			nameStrings.item[n] = Dup(s, strlen(s));
		    }
		    free(s);
		    free(label);
		    continue;
		}
		free(label);
	    }
	    /* (blanks) byte length <name>, <style>, AFE_x */
	    p = line;
	    if (curEntry >= 0 && Blanks1(&p) && Keyword(&p, "byte", 0) &&
		Blanks1(&p) && Keyword(&p, "length", 0) && Blanks1(&p) &&
		(w = Word(&p)) != NULL) {
		const char *comma;

		Blanks(&p);
		comma = strrchr(p, ',');
		if (*p == ',' && comma != NULL && comma > p) {
		    const char	*q = comma + 1;
		    char	*enc;

		    Blanks(&q);
		    if (strncmp(q, "AFE_", 4) == 0 &&
			(enc = Word(&q)) != NULL) {
			if (AtEnd(q)) {
			    char *style = Dup(p + 1, comma - p - 1);

			    entries[curEntry].style = StyleC(style);
			    entries[curEntry].encode = enc;
			    entries[curEntry].nameLabel = w;
			    free(style);
			    curEntry = -1;
			    continue;
			}
			free(enc);
		    }
		}
		free(w);
	    }
	    /* (blanks) nptr|word [offset] <name> | 0 */
	    p = line;
	    if (Blanks1(&p) && (Keyword(&p, "nptr", 0) ||
				Keyword(&p, "word", 0)) && Blanks1(&p)) {
		char	*name = NULL;
		int	ok = 0;

		if (isalpha((unsigned char) *p) || *p == '_') {
		    name = Word(&p);
		    if (strcmp(name, "offset") == 0 &&
			isspace((unsigned char) *p)) {
			const char *q = p;

			Blanks(&q);
			if (isalpha((unsigned char) *q) || *q == '_') {
			    char *n2 = Word(&q);

			    if (AtEnd(q)) {
				free(name);
				name = n2;
				p = q;
			    } else {
				free(n2);
			    }
			}
		    }
		    ok = AtEnd(p);
		} else if (*p == '0') {
		    p++;
		    ok = AtEnd(p);
		}
		if (ok) {
		    int j;

		    if (pending.count > 0) {
			ListClear(&curList);
			for (j = 0; j < pending.count; j++) {
			    int l = FindList(pending.item[j]);

			    if (l < 0) {
				lists = (FontList *) Grow(lists, numLists,
							  sizeof(FontList));
				l = numLists++;
				lists[l].label = Dup(pending.item[j],
						     strlen(pending.item[j]));
				lists[l].items.item = NULL;
				lists[l].items.count = 0;
				lists[l].items.size = 0;
				lists[l].head = NULL;
			    }
			    ListClear(&lists[l].items);
			    free(lists[l].head);
			    lists[l].head = Dup(pending.item[0],
						strlen(pending.item[0]));
			    ListAdd(&curList, pending.item[j]);
			}
			ListAdd(&order, pending.item[0]);
			ListClear(&pending);
			inList = 1;
			curEntry = -1;
		    }
		    if (inList) {
			for (j = 0; j < curList.count; j++) {
			    ListAdd(&lists[FindList(curList.item[j])].items,
				    name);
			}
			if (name == NULL) {
			    inList = 0;
			}
		    }
		}
		free(name);
	    }
	}
	ListClear(&lines);
	free(lines.item);
    }
}

/* the labels after "fontMapOffsets label", up to the first other line */
static void
ParseMapOrder(const char *path, List *maps)
{
    List    lines = {NULL, 0, 0};
    int	    i, on = 0;

    LogicalLines(path, &lines);
    for (i = 0; i < lines.count; i++) {
	const char *p = lines.item[i];

	if (!on) {
	    if (Keyword(&p, "fontMapOffsets", 0) && Blanks1(&p) &&
		Keyword(&p, "label", 0)) {
		on = 1;
	    }
	} else {
	    char *w = NULL;

	    if (Blanks1(&p) && Keyword(&p, "nptr", 0) && Blanks1(&p) &&
		(w = Word(&p)) != NULL && AtEnd(p)) {
		ListAdd(maps, w);
		free(w);
	    } else {
		free(w);
		break;
	    }
	}
    }
}

/*
 * The style bytes of all faces, as DefineFace puts them in one table,
 * and where the faces of each family start
 */
static unsigned char *faces;
static int	numFaces;
static List	startNames;
static int	*startValues;

static void
ParseFaces(const char *path)
{
    List    lines = {NULL, 0, 0};
    List    enumNames = {NULL, 0, 0};
    long    *enumValues = NULL;
    long    val = 0;
    int	    i;

    LogicalLines(path, &lines);
    for (i = 0; i < lines.count; i++) {
	const char  *line = lines.item[i], *p = line, *q;
	char	    *name, *w;

	if ((name = Word(&p)) != NULL) {
	    /* <name> etype byte[, <first>] */
	    q = p;
	    if (Blanks1(&q) && Keyword(&q, "etype", 0) && Blanks1(&q) &&
		Keyword(&q, "byte", 0)) {
		val = 0;
		Blanks(&q);
		if (*q == ',') {
		    q++;
		    Blanks(&q);
		    if ((w = Word(&q)) != NULL) {
			val = Number(w, "not a number:");
			free(w);
		    }
		}
		free(name);
		continue;
	    }
	    /* SF_<name> enum <type>[, <value>] */
	    q = p;
	    if (strncmp(name, "SF_", 3) == 0 && Blanks1(&q) &&
		Keyword(&q, "enum", 0) && Blanks1(&q) &&
		(w = Word(&q)) != NULL) {
		int n;

		free(w);
		Blanks(&q);
		if (*q == ',') {
		    q++;
		    Blanks(&q);
		    if ((w = Word(&q)) != NULL) {
			val = Number(w, "not a number:");
			free(w);
		    }
		}
		n = ListFind(&enumNames, name);
		if (n < 0) {
		    enumValues = (long *) Grow(enumValues, enumNames.count,
					       sizeof(long));
		    n = enumNames.count;
		    ListAdd(&enumNames, name);
		}
		enumValues[n] = val++;
		free(name);
		continue;
	    }
	    free(name);
	}
	/* DefineFace <name>, <style>[, <family>] */
	p = line;
	if (Keyword(&p, "DefineFace", 0) && Blanks1(&p) &&
	    !Keyword(&p, "macro", 1)) {
	    char    *args[3];
	    int	    n = 0;

	    args[0] = args[1] = args[2] = NULL;
	    while (n < 3) {
		const char *e = strchr(p, ','), *a = p, *ae;

		ae = e != NULL ? e : p + strlen(p);
		while (a < ae && isspace((unsigned char) *a)) {
		    a++;
		}
		while (ae > a && isspace((unsigned char) ae[-1])) {
		    ae--;
		}
		args[n++] = Dup(a, ae - a);
		if (e == NULL) {
		    break;
		}
		p = e + 1;
	    }
	    if (n >= 3 && args[2][0] != '\0' &&
		ListFind(&startNames, args[2]) < 0) {
		startValues = (int *) Grow(startValues, startNames.count,
					   sizeof(int));
		startValues[startNames.count] = numFaces;
		ListAdd(&startNames, args[2]);
	    }
	    if (n >= 2 && args[1][0] != '\0') {
		long	    v = 0;
		const char  *t = args[1];

		while (*t != '\0') {
		    const char	*e = strchr(t, '+'), *a = t, *ae;
		    char	*term;
		    int		en;

		    ae = e != NULL ? e : t + strlen(t);
		    while (a < ae && isspace((unsigned char) *a)) {
			a++;
		    }
		    while (ae > a && isspace((unsigned char) ae[-1])) {
			ae--;
		    }
		    term = Dup(a, ae - a);
		    en = ListFind(&enumNames, term);
		    v += en >= 0 ? enumValues[en] :
				   Number(term, "not a number:");
		    free(term);
		    if (e == NULL) {
			break;
		    }
		    t = e + 1;
		}
		faces = (unsigned char *) Grow(faces, numFaces, 1);
		faces[numFaces++] = (unsigned char) (v & 0xff);
	    }
	    free(args[0]);
	    free(args[1]);
	    free(args[2]);
	}
    }
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

/* a list of entries only (a set), not one of sets */
static int
IsEntryList(int l)
{
    int i;

    for (i = 0; i < lists[l].items.count; i++) {
	if (lists[l].items.item[i] != NULL &&
	    FindEntry(lists[l].items.item[i]) < 0) {
	    return 0;
	}
    }
    return 1;
}

static int
StartOf(const char *family)
{
    int n = ListFind(&startNames, family);

    if (n < 0) {
	Fail("no faces of", family, NULL);
    }
    return startValues[n];
}

int
main(int argc, char **argv)
{
    const char	*out = NULL, *fontid = NULL, *orderFile = NULL;
    List	maps = {NULL, 0, 0};
    int		ac = 1, i, j, numAsm, *done, *used;
    char	**asmFiles;
    FILE	*f;

    while (ac + 1 < argc && argv[ac][0] == '-') {
	if (strcmp(argv[ac], "-o") == 0) {
	    out = argv[ac + 1];
	} else if (strcmp(argv[ac], "--fontid") == 0) {
	    fontid = argv[ac + 1];
	} else if (strcmp(argv[ac], "--order") == 0) {
	    orderFile = argv[ac + 1];
	} else {
	    break;
	}
	ac += 2;
    }
    if (out == NULL || fontid == NULL || orderFile == NULL || ac >= argc ||
	argv[ac][0] == '-') {
	fprintf(stderr, "usage: epsfonts2c -o epsFonts.c --fontid fontID.def "
		"--order tables.asm file.asm...\n");
	return 2;
    }
    asmFiles = argv + ac;
    numAsm = argc - ac;

    ParseTables(asmFiles, numAsm);
    ParseMapOrder(orderFile, &maps);
    ParseFaces(fontid);

    if ((f = fopen(out, "w")) == NULL) {
	perror(out);
	return 1;
    }
    fprintf(f, "/* epsFonts.c -- generated by epsfonts2c from\n *");
    for (i = 0; i < numAsm; i++) {
	fprintf(f, " %s", BaseName(asmFiles[i]));
    }
    fprintf(f, " and %s; do not edit */\n", BaseName(fontid));
    fprintf(f, "#include \"exportInt.h\"\n#include <fontID.h>\n\n");

    /* the entries a list has, in their order */
    used = (int *) Alloc((numEntries + 1) * sizeof(int));
    for (i = 0; i < numEntries; i++) {
	used[i] = 0;
    }
    for (i = 0; i < numLists; i++) {
	for (j = 0; j < lists[i].items.count; j++) {
	    int e = lists[i].items.item[j] != NULL ?
		    FindEntry(lists[i].items.item[j]) : -1;

	    if (e >= 0) {
		used[e] = 1;
	    }
	}
    }
    for (i = 0; i < numEntries; i++) {
	int n;

	if (!used[i]) {
	    continue;
	}
	if (entries[i].nameLabel == NULL) {
	    Fail("entry", entries[i].label, "incomplete");
	}
	n = ListFind(&nameLabels, entries[i].nameLabel);
	if (n < 0) {
	    Fail("no name", entries[i].nameLabel, NULL);
	}
	fprintf(f, "static const EPSAdobeFont f_%s = { %s, %s, %s, %d, "
		"\"%s\" };\n", entries[i].label, entries[i].fid,
		entries[i].style, entries[i].encode,
		(int) strlen(nameStrings.item[n]), nameStrings.item[n]);
    }
    fprintf(f, "\n");

    /* the sets (lists of entries), then the font lists (lists of sets) */
    done = (int *) Alloc((numLists + 1) * sizeof(int));
    for (i = 0; i < numLists; i++) {
	done[i] = 0;
    }
    for (i = 0; i < order.count; i++) {
	int l = FindList(order.item[i]);

	if (done[l] || !IsEntryList(l)) {
	    continue;
	}
	done[l] = 1;
	fprintf(f, "static const EPSAdobeFont *const s_%s[] = {\n    ",
		lists[l].label);
	for (j = 0; j < lists[l].items.count; j++) {
	    if (lists[l].items.item[j] != NULL) {
		fprintf(f, "&f_%s,\n    ", lists[l].items.item[j]);
	    }
	}
	fprintf(f, "NULL\n};\n");
    }
    fprintf(f, "\n");
    for (i = 0; i < order.count; i++) {
	int l = FindList(order.item[i]);

	if (done[l]) {
	    continue;
	}
	done[l] = 1;
	/* (checked before anything of the list is written) */
	for (j = 0; j < lists[l].items.count; j++) {
	    const char *x = lists[l].items.item[j];

	    if (x != NULL && (FindList(x) < 0 || !IsEntryList(FindList(x)))) {
		fprintf(stderr, "epsfonts2c: %s in %s is no set\n", x,
			lists[l].label);
		return 1;
	    }
	}
	fprintf(f, "static const EPSAdobeFont *const *const t_%s[] = {\n    ",
		lists[l].label);
	for (j = 0; j < lists[l].items.count; j++) {
	    if (lists[l].items.item[j] != NULL) {
		fprintf(f, "s_%s,\n    ", lists[l].items.item[j]);
	    }
	}
	fprintf(f, "NULL\n};\n");
    }
    fprintf(f, "\n/* per PSFontList / 2 (fontMapOffsets) */\n");
    fprintf(f, "const EPSAdobeFont *const *const *const epsFontMaps[%d] = "
	    "{\n", maps.count);
    for (i = 0; i < maps.count; i++) {
	if (FindList(maps.item[i]) < 0) {
	    Fail("font list", maps.item[i], "unknown");
	}
    }
    /* labels stacked on one list (Wang14 and Standard13) share it */
    fprintf(f, "    ");
    for (i = 0; i < maps.count; i++) {
	fprintf(f, "%st_%s", i ? ",\n    " : "",
		lists[FindList(maps.item[i])].head);
    }
    fprintf(f, "\n};\n\n");
    fprintf(f, "/* the face table of CalcFontDiff (fontID.def): the style "
	    "of\n * each face that has one, in order; the serif faces from "
	    "0, the\n * sans serif and the script ones from epsFaceStart[1], "
	    "[2], up to\n * [3] (the asm compared the index with ja: one "
	    "past the end may\n * be read, the next table's first byte) */\n");
    fprintf(f, "const byte epsFaces[%d] = {\n", numFaces + 1);
    for (i = 0; i <= numFaces; i++) {
	fprintf(f, "%s0x%02x%s", i % 12 == 0 ? "    " : " ",
		i < numFaces ? faces[i] : 0,
		i == numFaces ? "\n" : i % 12 == 11 ? ",\n" : ",");
    }
    fprintf(f, "};\n");
    fprintf(f, "const word epsFaceStart[4] = { %d, %d, %d, %d };\n",
	    StartOf("FG_SERIF"), StartOf("FG_SANS_SERIF"),
	    StartOf("FG_SCRIPT"), StartOf("FG_ORNAMENT"));
    if (fclose(f) != 0) {
	perror(out);
	return 1;
    }
    return 0;
}
