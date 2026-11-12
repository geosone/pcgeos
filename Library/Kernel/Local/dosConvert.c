/*
 * dosConvert.c -- text between the DOS code pages and the GEOS character
 * set (after dosConvert.asm and dosVariable.asm, SBCS).  The tables of the
 * cmap*.asm files are made C at build time; characters below MIN_MAP_CHAR
 * are the same in all of them.
 *
 * Port32: the code pages are tables of the kernel, not resources, so
 * LocalSetCodePage returns no handle of one: non-zero (the DosCodePage)
 * if the page is known, zero if not.  The text routines return TRUE where
 * the asm set the carry (the default character was used).
 */
#include "localInt.h"

extern const byte localCodePageUS[128], localToUSCodePage[128];
extern const byte localCodePageMultilingual[128], localToMultiCodePage[128];
extern const byte localCodePageFrenchCanadian[128], localToFrenchCodePage[128];
extern const byte localCodePageNordic[128], localToNordicCodePage[128];
extern const byte localCodePagePortuguese[128], localToPortCodePage[128];
extern const byte localCodePageLatin1[128], localToLatin1CodePage[128];

/* codePageList and codePageHandles; the default first */
static const LocalCodePage codePages[] = {
    { CODE_PAGE_US, localCodePageUS, localToUSCodePage },
    { CODE_PAGE_MULTILINGUAL, localCodePageMultilingual, localToMultiCodePage },
    { CODE_PAGE_MULTILINGUAL_EURO, localCodePageMultilingual,
      localToMultiCodePage },
    { CODE_PAGE_CANADIAN_FRENCH, localCodePageFrenchCanadian,
      localToFrenchCodePage },
    { CODE_PAGE_NORDIC, localCodePageNordic, localToNordicCodePage },
    { CODE_PAGE_PORTUGUESE, localCodePagePortuguese, localToPortCodePage },
    { CODE_PAGE_LATIN_1, localCodePageLatin1, localToLatin1CodePage }
};
#define NUM_CODE_PAGES	(sizeof(codePages) / sizeof(codePages[0]))

/* currentCodePage, currentCodePageHandle */
static const LocalCodePage *currentCodePage = &codePages[0];

static const LocalCodePage *Lookup(DosCodePage page)
{
    unsigned i;

    for (i = 0; i < NUM_CODE_PAGES; i++)
	if (codePages[i].LCP_page == page)
	    return &codePages[i];
    return NULL;
}

const LocalCodePage *LocalFindCodePage(DosCodePage page)
{
    const LocalCodePage *cp = Lookup(page);

    return cp ? cp : &codePages[0];
}

const LocalCodePage *LocalCurrentCodePage(void)
{
    return currentCodePage;
}

Boolean LocalIsCodePageSupported(DosCodePage codePage)
{
    return Lookup(codePage) != NULL;
}

DosCodePage LocalGetCodePage(void)
{
    return currentCodePage->LCP_page;
}

/* For the primary file system driver only */
Handle LocalSetCodePage(DosCodePage dcp)
{
    const LocalCodePage *cp = Lookup(dcp);

    if (!cp)
	return 0;
    currentCodePage = cp;
    return (Handle) dcp;
}

/* ConvertBufferInt: in place, up to size characters (0: to the null) */
static Boolean ConvertBuffer(char *str, word size, const byte *table,
			     wchar defaultChar)
{
    Boolean usedDefault = FALSE;
    byte c;

    while ((c = (byte) *str) != 0) {
	if (c >= MIN_MAP_CHAR) {
	    c = table[c - MIN_MAP_CHAR];
	    if (!c) {
		c = (byte) defaultChar;
		usedDefault = TRUE;
	    }
	    *str = (char) c;
	}
	str++;
	if (!--size)
	    break;
    }
    return usedDefault;
}

Boolean LocalDosToGeos(char *str, word strSize, wchar defaultChar)
{
    return ConvertBuffer(str, strSize, currentCodePage->LCP_toGeos,
			 defaultChar);
}

Boolean LocalGeosToDos(char *str, word strSize, wchar defaultChar)
{
    return ConvertBuffer(str, strSize, currentCodePage->LCP_fromGeos,
			 defaultChar);
}

Boolean LocalCodePageToGeos(char *str, word strSize, DosCodePage codePage,
			    wchar defaultChar)
{
    return ConvertBuffer(str, strSize, LocalFindCodePage(codePage)->LCP_toGeos,
			 defaultChar);
}

Boolean LocalGeosToCodePage(char *str, word strSize, DosCodePage codePage,
			    wchar defaultChar)
{
    return ConvertBuffer(str, strSize,
			 LocalFindCodePage(codePage)->LCP_fromGeos,
			 defaultChar);
}

/* ConvertCharacterCP */
static wchar ConvertChar(wchar ch, const byte *table, wchar defaultChar)
{
    byte c;

    if ((ch & 0xff) < MIN_MAP_CHAR)
	return ch;
    c = table[(ch & 0xff) - MIN_MAP_CHAR];
    return c ? c : defaultChar;
}

wchar LocalCodePageToGeosChar(wchar ch, DosCodePage codePage,
			      wchar defaultChar)
{
    return ConvertChar(ch, LocalFindCodePage(codePage)->LCP_toGeos,
		       defaultChar);
}

wchar LocalGeosToCodePageChar(wchar ch, DosCodePage codePage,
			      wchar defaultChar)
{
    return ConvertChar(ch, LocalFindCodePage(codePage)->LCP_fromGeos,
		       defaultChar);
}

wchar LocalDosToGeosChar(wchar ch, wchar defaultChar)
{
    return ConvertChar(ch, currentCodePage->LCP_toGeos, defaultChar);
}

wchar LocalGeosToDosChar(wchar ch, wchar defaultChar)
{
    return ConvertChar(ch, currentCodePage->LCP_fromGeos, defaultChar);
}

/* LocalIsDosChar: whether the GEOS character is in the current code page */
Boolean LocalIsDosChar(wchar ch)
{
    if ((ch & 0xff) < MIN_MAP_CHAR)
	return TRUE;
    return currentCodePage->LCP_fromGeos[(ch & 0xff) - MIN_MAP_CHAR] >=
	MIN_MAP_CHAR;
}
