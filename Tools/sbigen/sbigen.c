/***********************************************************************
 * sbigen.c -- the Sound Blaster driver's instruments in C from the asm.
 *
 * Reads the InstrumentTable of Driver/Sound/SoundBlaster/
 * soundblasterVoice.asm (SBIEnvelopeFormat entries of 11 bytes: the
 * modulator's and carrier's timbre, scaling, attack/decay,
 * sustain/release, wave, and the feedback/connection byte) and writes
 * them as a C array, for the host sound driver (Driver/Sound/Host),
 * which plays the same instruments with its own FM synthesis.
 *
 *	sbigen -o hostInstruments.c soundblasterVoice.asm
 ***********************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define SBI_SIZE	11

static void
Fail(const char *what, const char *arg)
{
    fprintf(stderr, "sbigen: %s%s%s\n", what, arg ? " " : "",
	    arg ? arg : "");
    exit(1);
}

/* the asm's numbers: 0F1H, 12, 0x12 */
static int
Number(const char *s, long *v)
{
    char buf[32];
    size_t n = 0;
    char *end;

    while (isalnum((unsigned char)s[n]) && n < sizeof(buf) - 1) {
	buf[n] = s[n];
	n++;
    }
    buf[n] = 0;
    if (!n)
	return 0;
    if (toupper((unsigned char)buf[n - 1]) == 'H') {
	buf[n - 1] = 0;
	*v = strtol(buf, &end, 16);
    } else {
	*v = strtol(buf, &end, 0);
    }
    if (*end)
	Fail("bad number", buf);
    return (int)n;
}

int
main(int argc, char **argv)
{
    const char *out = NULL, *in = NULL;
    FILE *f, *o;
    int inTable = 0, inEntry = 0, count = 0;
    long bytes[SBI_SIZE], v;
    int nb = 0, i;
    static unsigned char table[512][SBI_SIZE];

    for (i = 1; i < argc; i++) {
	if (!strcmp(argv[i], "-o") && i + 1 < argc)
	    out = argv[++i];
	else
	    in = argv[i];
    }
    if (!out || !in)
	Fail("usage: sbigen -o out.c soundblasterVoice.asm", NULL);
    f = fopen(in, "r");
    if (!f)
	Fail("cannot read", in);
    /* find "InstrumentTable", then read <...> groups up to the first
     * line that is neither a group nor blank nor a comment */
    {
	char line[1024];

	while (fgets(line, sizeof line, f)) {
	    char *p = line;

	    if (!inTable) {
		char *t = strstr(line, "InstrumentTable");
		if (!t || strncmp(t + 15, " SBIEnvelopeFormat", 18))
		    continue;
		inTable = 1;
		p = t + 15 + 18;
	    }
	    {
		char *sc = strchr(p, ';');

		if (sc)
		    *sc = 0;
		for (; *p; p++) {
		    if (*p == '<') {
			if (inEntry)
			    Fail("nested <", NULL);
			inEntry = 1;
			nb = 0;
		    } else if (*p == '>') {
			if (!inEntry || nb != SBI_SIZE)
			    Fail("entry not of 11 bytes", NULL);
			if (count >= 512)
			    Fail("too many entries", NULL);
			for (i = 0; i < SBI_SIZE; i++)
			    table[count][i] = (unsigned char)bytes[i];
			count++;
			inEntry = 0;
		    } else if (inEntry && isalnum((unsigned char)*p)) {
			int n = Number(p, &v);
			if (nb >= SBI_SIZE || v < 0 || v > 255)
			    Fail("bad entry", NULL);
			bytes[nb++] = v;
			p += n - 1;
		    } else if (!isspace((unsigned char)*p) && *p != ',') {
			if (!inEntry && count)
			    goto done;
			if (!inEntry)
			    Fail("unexpected text in the table", NULL);
		    }
		}
	    }
	}
    }
done:
    fclose(f);
    if (!count)
	Fail("no InstrumentTable in", in);
    o = fopen(out, "w");
    if (!o)
	Fail("cannot write", out);
    fprintf(o, "/* made by sbigen from soundblasterVoice.asm: do not edit */\n"
	    "#include \"hostInstruments.h\"\n\n"
	    "const HostSBI hostInstruments[%d] = {\n", count);
    for (i = 0; i < count; i++) {
	int k;

	fprintf(o, "    {");
	for (k = 0; k < SBI_SIZE; k++)
	    fprintf(o, "0x%02x%s", table[i][k], k < SBI_SIZE - 1 ? ", " : "");
	fprintf(o, "}%s\n", i < count - 1 ? "," : "");
    }
    fprintf(o, "};\nconst unsigned hostInstrumentCount = %d;\n", count);
    fclose(o);
    return 0;
}
