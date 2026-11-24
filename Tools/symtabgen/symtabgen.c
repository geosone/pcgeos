/***********************************************************************
 * symtabgen.c -- the symbol set tables of the spooler (the upper 128
 * characters of the GEOS character set in a printer's symbol set, one
 * asm resource each: Library/Spool/Lib/lib*Tab.asm) as C.
 *
 *	symtabgen -o out.c TABLE=file.asm ...
 *
 * Each TABLE=file pair gives "const byte TABLE[128]" from the 128
 * "byte" lines of the file (decimal, 0x hex, hex with an "h",
 * C_SPACE).
 ***********************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define TABLE_SIZE  128

static void
Fail(const char *path, const char *what, const char *arg)
{
    fprintf(stderr, "symtabgen: %s: %s%s%s\n", path, what,
	    arg ? " " : "", arg ? arg : "");
    exit(1);
}

static int
AllOf(const char *s, size_t n, int hex)
{
    size_t i;

    if (n == 0) {
	return 0;
    }
    for (i = 0; i < n; i++) {
	if (hex ? !isxdigit((unsigned char) s[i]) :
		  !isdigit((unsigned char) s[i])) {
	    return 0;
	}
    }
    return 1;
}

static int
Value(const char *path, const char *tok)
{
    size_t n = strlen(tok);

    if (strcmp(tok, "C_SPACE") == 0) {
	return 0x20;
    }
    if (n > 2 && tok[0] == '0' && tok[1] == 'x' && AllOf(tok + 2, n - 2, 1)) {
	return (int) strtol(tok + 2, NULL, 16);
    }
    if (n > 1 && (tok[n - 1] == 'h' || tok[n - 1] == 'H') &&
	AllOf(tok, n - 1, 1)) {
	return (int) strtol(tok, NULL, 16);
    }
    if (AllOf(tok, n, 0)) {
	return (int) strtol(tok, NULL, 10);
    }
    Fail(path, "unknown value", tok);
    return 0;
}

static void
Table(const char *path, int *out)
{
    FILE    *f = fopen(path, "r");
    char    line[1024];
    int	    count = 0;

    if (f == NULL) {
	perror(path);
	exit(1);
    }
    while (fgets(line, sizeof(line), f) != NULL) {
	char *p = line, *e, *tok;

	/* without the comment, without blanks around it */
	if ((e = strchr(p, ';')) != NULL) {
	    *e = '\0';
	}
	while (isspace((unsigned char) *p)) {
	    p++;
	}
	e = p + strlen(p);
	while (e > p && isspace((unsigned char) e[-1])) {
	    *--e = '\0';
	}
	/* "byte <value>" */
	if (!(tolower((unsigned char) p[0]) == 'b' &&
	      tolower((unsigned char) p[1]) == 'y' &&
	      tolower((unsigned char) p[2]) == 't' &&
	      tolower((unsigned char) p[3]) == 'e' &&
	      isspace((unsigned char) p[4]))) {
	    continue;
	}
	tok = p + 4;
	while (isspace((unsigned char) *tok)) {
	    tok++;
	}
	for (e = tok; *e != '\0' && !isspace((unsigned char) *e); e++) {
	    ;
	}
	if (*tok == '\0' || *e != '\0') {
	    continue;
	}
	if (count < TABLE_SIZE) {
	    out[count] = Value(path, tok);
	}
	count++;
    }
    fclose(f);
    if (count != TABLE_SIZE) {
	char msg[64];

	sprintf(msg, "%d entries, not %d", count, TABLE_SIZE);
	Fail(path, msg, NULL);
    }
}

int
main(int argc, char **argv)
{
    const char	*out = NULL;
    FILE	*f;
    int		i, first = 1;

    if (argc >= 3 && strcmp(argv[1], "-o") == 0) {
	out = argv[2];
	first = 3;
    }
    if (out == NULL || first >= argc) {
	fprintf(stderr, "usage: symtabgen -o out.c TABLE=file.asm ...\n");
	return 2;
    }
    if ((f = fopen(out, "w")) == NULL) {
	perror(out);
	return 1;
    }
    fprintf(f, "/* made by symtabgen -- do not edit */\n"
	    "#include <geos.h>\n\n");
    for (i = first; i < argc; i++) {
	char	*eq = strchr(argv[i], '=');
	int	v[TABLE_SIZE], j;

	if (eq == NULL) {
	    fprintf(stderr, "symtabgen: %s: not TABLE=file\n", argv[i]);
	    return 2;
	}
	Table(eq + 1, v);
	fprintf(f, "const byte %.*s[%d] = {\n", (int) (eq - argv[i]),
		argv[i], TABLE_SIZE);
	for (j = 0; j < TABLE_SIZE; j++) {
	    fprintf(f, "%s0x%02x%s", j % 12 == 0 ? "    " : " ", v[j],
		    (j % 12 == 11 || j == TABLE_SIZE - 1) ? ",\n" : ",");
	}
	fprintf(f, "};\n");
	if (i + 1 < argc) {
	    fprintf(f, "\n");
	}
    }
    if (fclose(f) != 0) {
	perror(out);
	return 1;
    }
    return 0;
}
