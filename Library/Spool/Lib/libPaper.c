/*
 * libPaper.c -- page sizes (the C port of libPaper.asm).
 *
 * The pre-defined sizes of each PageType (paper, envelope, label,
 * postcard) are the tables of libTables.asm (libPaperTables.c, made by
 * Port32/tools/papergen.py).  A user-defined size lives in the ini file:
 * a category paper128 ... (envel128, label128, postc128) with name,
 * width, height and layout, listed in [paper] newSizes and counted in
 * [paper] count; its number is the one of its category (128-255).  The
 * order the sizes are shown in is [paper] order ("029,031,...,"), else
 * the default order of the tables.  A paper size number is an index into
 * the order, or with 0xff in the high byte a size itself.
 *
 * Port32: the asm built the order arrays once in a discardable copy of
 * the PageSizeData resource and used it until it was discarded; here
 * they are read from the ini file each time, so a new order counts at
 * once.  SpoolGetPaperSizeOrder takes the PageType, which the asm's C
 * stub did not pass.  The default page size of [spool] width, height,
 * layout and margin* is that of libTables.asm (US letter, 1/4 inch
 * margins) when the ini has none.
 */
#include "spoolLocal.h"
#include "libPaper.h"

#define USER_DEFINED_MASK	0x80
#define PAPER_SIZE_NUMERIC_FIRST 5
#define PAPER_SIZE_CATEGORY_BUFFER 10
#define ORDER_ENTRY_SIZE	4	/* "ddd," */

static const char *const pageTypeCategories[4] = {
    "paper", "envelope", "label", "postcard"
};
static const char *const defaultPaperCategories[4] = {
    "paper128", "envel128", "label128", "postc128"
};
static const char sizesKey[] = "newSizes";
static const char countKey[] = "count";
static const char orderKey[] = "order";
static const char nameKey[] = "name";
static const char widthKey[] = "width";
static const char heightKey[] = "height";
static const char layoutKey[] = "layout";
static const char *const pageSizeInfoKeys[7] = {
    "width", "height", "layout",
    "marginLeft", "marginTop", "marginRight", "marginBottom"
};

#define TABLE(pt)	(&paperSizeTables[((pt) >> 1) & 3])

/* ---------------------------------------------------------------------
 * Small helpers
 * ------------------------------------------------------------------- */

/* AsciiToHexWord: n decimal digits (or up to the null) */
static word AsciiToWord(const char *s, int n)
{
    word v = 0;

    while (n-- && *s >= '0' && *s <= '9')
	v = (word) (v * 10 + (*s++ - '0'));
    return v;
}

/* HexToThreeDigitAscii */
static void ThreeDigits(char *d, byte v)
{
    d[2] = (char) ('0' + v % 10);
    v /= 10;
    d[1] = (char) ('0' + v % 10);
    v /= 10;
    d[0] = (char) ('0' + v % 10);
}

/* CopyPaperSizeCategory, with the number of a user-defined size */
static void SizeCategory(char *buf, PageType pt, byte num)
{
    const char *s = defaultPaperCategories[(pt >> 1) & 3];
    int i;

    for (i = 0; s[i]; i++)
	buf[i] = s[i];
    buf[i] = 0;
    ThreeDigits(buf + PAPER_SIZE_NUMERIC_FIRST, num);
}

/* InitFileAlterInteger */
static void AlterInteger(const char *cat, const char *key, sword by)
{
    word v;

    if (InitFileReadInteger(cat, key, &v))
	v = 0;
    InitFileWriteInteger(cat, key, (word) (v + by));
}

/* GetUserDefinedCount */
static word UserDefinedCount(PageType pt)
{
    word v;

    return InitFileReadInteger(pageTypeCategories[(pt >> 1) & 3], countKey,
			       &v) ? 0 : v;
}

/* GetUserDefinedNumber: the number of the section'th user-defined size
 * (128-255); TRUE if there is none */
static Boolean UserDefinedNumber(PageType pt, word section, byte *num)
{
    char buf[MAX_PAPER_STRING_LENGTH];
    word n;

    if (InitFileReadStringSectionBuffer(pageTypeCategories[(pt >> 1) & 3],
					sizesKey, section, buf, sizeof(buf),
					&n))
	return TRUE;
    *num = (byte) AsciiToWord(buf + PAPER_SIZE_NUMERIC_FIRST, 3);
    return FALSE;
}

/* the order array (ConvertFromDefaultOrder): [cat] order, else the
 * default; its length */
static word ReadOrder(PageType pt, byte *order)
{
    const PaperSizeTable *t = TABLE(pt);
    char buf[MAX_PAPER_SIZES * ORDER_ENTRY_SIZE + 1];
    word n, i;

    if (!InitFileReadStringBuffer(pageTypeCategories[(pt >> 1) & 3],
				  orderKey, buf, sizeof(buf), &n)) {
	n /= ORDER_ENTRY_SIZE;
	for (i = 0; i < n; i++)
	    order[i] = (byte) AsciiToWord(buf + i * ORDER_ENTRY_SIZE, 3);
	return n;
    }
    for (i = 0; i < t->PST_numDefaultOrder; i++)
	order[i] = t->PST_defaultOrder[i];
    return t->PST_numDefaultOrder;
}

/* the size a paper size number stands for (GetPaperSizeInfo) */
static byte SizeOf(word paperSizeNum, PageType pt)
{
    byte order[MAX_PAPER_SIZES];

    if ((paperSizeNum >> 8) == 0xff)
	return (byte) paperSizeNum;
    ReadOrder(pt, order);
    return order[(byte) paperSizeNum];
}

/* GetPreDefinedSize, GetUserDefinedSize */
static void GetSize(byte size, PageType pt, PaperSizeInfo *info)
{
    char cat[PAPER_SIZE_CATEGORY_BUFFER];
    const PaperSizeTable *t = TABLE(pt);
    word v;

    if (!(size & USER_DEFINED_MASK)) {
	if (size >= t->PST_numSizes)
	    size = 0;
	info->PSI_width = t->PST_sizes[size].PSE_width;
	info->PSI_height = t->PST_sizes[size].PSE_height;
	info->PSI_defaultLayout.PL_paper = t->PST_sizes[size].PSE_layout;
	return;
    }
    SizeCategory(cat, pt, size);
    info->PSI_width = InitFileReadInteger(cat, widthKey, &v) ? 612 : v;
    info->PSI_height = InitFileReadInteger(cat, heightKey, &v) ? 792 : v;
    info->PSI_defaultLayout.PL_paper =
	InitFileReadInteger(cat, layoutKey, &v) ? (word) pt : v;
}

/* ---------------------------------------------------------------------
 * The default page size
 * ------------------------------------------------------------------- */

void SpoolGetDefaultPageSizeInfo(PageSizeReport *psr)
{
    word v[7], i;

    for (i = 0; i < 7; i++)
	if (InitFileReadInteger("spool", pageSizeInfoKeys[i], &v[i]))
	    v[i] = defaultPageSizeInfo[i];
    psr->PSR_width = v[0];
    psr->PSR_height = v[1];
    psr->PSR_layout.PL_paper = v[2];
    psr->PSR_margins.PCMP_left = v[3];
    psr->PSR_margins.PCMP_top = v[4];
    psr->PSR_margins.PCMP_right = v[5];
    psr->PSR_margins.PCMP_bottom = v[6];
}

void SpoolSetDefaultPageSizeInfo(PageSizeReport *psr)
{
    word v[7], i;

    v[0] = (word) psr->PSR_width;
    v[1] = (word) psr->PSR_height;
    v[2] = psr->PSR_layout.PL_paper;
    v[3] = psr->PSR_margins.PCMP_left;
    v[4] = psr->PSR_margins.PCMP_top;
    v[5] = psr->PSR_margins.PCMP_right;
    v[6] = psr->PSR_margins.PCMP_bottom;
    for (i = 0; i < 7; i++)
	InitFileWriteInteger("spool", pageSizeInfoKeys[i], v[i]);
}

/* ---------------------------------------------------------------------
 * The paper sizes
 * ------------------------------------------------------------------- */

void SpoolGetNumPaperSizes(NumPaperSizesInfo *sizesInfo, PageType pageType)
{
    byte order[MAX_PAPER_SIZES];

    sizesInfo->NPSI_numSizes = ReadOrder(pageType, order);
    sizesInfo->NPSI_defaultSize = 0;
}

/* SpoolGetPaperString: the name into stringBuf (MAX_PAPER_STRING_LENGTH);
 * its length */
word SpoolGetPaperString(char *stringBuf, word paperSizeNum,
			 PageType pageType)
{
    char cat[PAPER_SIZE_CATEGORY_BUFFER];
    byte size = SizeOf(paperSizeNum, pageType);
    const PaperSizeTable *t = TABLE(pageType);
    const char *s;
    word n, i;

    if (size & USER_DEFINED_MASK) {
	SizeCategory(cat, pageType, size);
	if (InitFileReadStringBuffer(cat, nameKey, stringBuf,
				     MAX_PAPER_STRING_LENGTH, &n)) {
	    stringBuf[0] = 0;
	    n = 0;
	}
	return n;
    }
    s = t->PST_sizes[size < t->PST_numSizes ? size : 0].PSE_name;
    for (i = 0; s[i] && i < MAX_PAPER_STRING_LENGTH - 1; i++)
	stringBuf[i] = s[i];
    stringBuf[i] = 0;
    return i;
}

void SpoolGetPaperSize(PaperSizeInfo *sizeInfo, word paperSizeNum,
		       PageType pageType)
{
    GetSize(SizeOf(paperSizeNum, pageType), pageType, sizeInfo);
}

/* SpoolConvertPaperSize: the paper size number (an index into the order)
 * of the size width x height: the first that is it upright, else the
 * last that is it on its side, else the custom size; -1 if none */
word SpoolConvertPaperSize(word width, word height, PageType pageType)
{
    byte order[MAX_PAPER_SIZES];
    PaperSizeInfo info;
    word n, i;
    int landscape = -1, custom = -1;

    n = ReadOrder(pageType, order);
    for (i = 0; i < n; i++) {
	if (!order[i]) {
	    custom = (int) i;
	    continue;
	}
	GetSize(order[i], pageType, &info);
	if (info.PSI_width == width) {
	    if (info.PSI_height == height)
		return i;
	} else if (info.PSI_width == height && info.PSI_height == width)
	    landscape = (int) i;
    }
    if (landscape >= 0)
	return (word) landscape;
    if (custom >= 0)
	return (word) custom;
    return 0xffff;
}

/* SpoolGetPaperSizeOrder: the order into orderBuf, and in userDefBuf
 * (MAX_PAPER_SIZES) 1 for each size that is not in the order */
void SpoolGetPaperSizeOrder(byte *orderBuf, byte *userDefBuf,
			    PaperSizeOrderInfo *numBuf, PageType pageType)
{
    const PaperSizeTable *t = TABLE(pageType);
    word n, users, i;
    byte num;

    n = ReadOrder(pageType, orderBuf);
    for (i = 0; i < MAX_PAPER_SIZES; i++)
	userDefBuf[i] = 0;
    for (i = 0; i < t->PST_numSizes; i++)
	userDefBuf[i] = 1;
    users = UserDefinedCount(pageType);
    for (i = 0; i < users; i++)
	if (!UserDefinedNumber(pageType, i, &num))
	    userDefBuf[num] = 1;
    for (i = 0; i < n; i++)
	userDefBuf[orderBuf[i]] = 0;
    numBuf->PSOI_numOrdered = n;
    numBuf->PSOI_numUnused = (word) (t->PST_numSizes + users - n);
}

/* SpoolSetPaperSizeOrder (ConvertToDefaultOrder): [cat] order */
void SpoolSetPaperSizeOrder(byte *orderArray, word numEntries,
			    PageType pageType)
{
    char buf[MAX_PAPER_SIZES * ORDER_ENTRY_SIZE + 1];
    word i;

    if (numEntries > MAX_PAPER_SIZES)
	numEntries = MAX_PAPER_SIZES;
    for (i = 0; i < numEntries; i++) {
	ThreeDigits(buf + i * ORDER_ENTRY_SIZE, orderArray[i]);
	buf[i * ORDER_ENTRY_SIZE + 3] = ',';
    }
    buf[numEntries * ORDER_ENTRY_SIZE] = 0;
    InitFileWriteString(pageTypeCategories[(pageType >> 1) & 3], orderKey,
			buf);
}

/* SpoolCreatePaperSize: a user-defined size in the ini file; its number
 * (128-255), 0 if there is no room */
word SpoolCreatePaperSize(char *paperSizeString, word width, word height,
			  PageLayout layout, PageType pageType)
{
    char cat[PAPER_SIZE_CATEGORY_BUFFER];
    const char *pcat = pageTypeCategories[(pageType >> 1) & 3];
    word v, count;
    int d;
    byte num;

    count = UserDefinedCount(pageType);
    if (count >= MAX_USER_DEFINED_PAPER_SIZES + 1)
	return 0;
    SizeCategory(cat, pageType, 128);
    /* the first category not in use: paper128, paper129 ... */
    while (!InitFileReadInteger(cat, widthKey, &v)) {
	for (d = PAPER_SIZE_NUMERIC_FIRST + 2;
	     d >= PAPER_SIZE_NUMERIC_FIRST; d--) {
	    if (++cat[d] <= '9')
		break;
	    cat[d] = '0';
	}
	if (d < PAPER_SIZE_NUMERIC_FIRST)
	    return 0;
    }
    InitFileWriteInteger(cat, widthKey, width);
    InitFileWriteInteger(cat, heightKey, height);
    InitFileWriteInteger(cat, layoutKey, layout.PL_paper);
    InitFileWriteString(cat, nameKey, paperSizeString);
    InitFileWriteStringSection(pcat, sizesKey, cat);
    AlterInteger(pcat, countKey, 1);
    if (UserDefinedNumber(pageType, count, &num))
	return 0;
    return num;
}

typedef struct {
    byte	num;
    word	section;
    Boolean	found;
} DeleteData;

/* DeletePaperSizeCallBack: the section of the size; its category goes */
static Boolean DeleteCB(const char *section, word num, void *data)
{
    DeleteData *d = data;
    char cat[PAPER_SIZE_CATEGORY_BUFFER];
    word i;

    if ((byte) AsciiToWord(section + PAPER_SIZE_NUMERIC_FIRST, 3) != d->num)
	return FALSE;
    for (i = 0; i < PAPER_SIZE_CATEGORY_BUFFER - 2 && section[i]; i++)
	cat[i] = section[i];
    cat[i] = 0;
    InitFileDeleteCategory(cat);
    d->section = num;
    d->found = TRUE;
    return TRUE;
}

/* SpoolDeletePaperSize: a user-defined size (0xff in the high byte of
 * its number); FALSE on success */
Boolean SpoolDeletePaperSize(word paperSizeNum, PageType pageType)
{
    const char *pcat = pageTypeCategories[(pageType >> 1) & 3];
    DeleteData d;

    d.num = (byte) paperSizeNum;
    d.found = FALSE;
    InitFileEnumStringSection(pcat, sizesKey, 0, DeleteCB, &d);
    if (!d.found)
	return TRUE;
    InitFileDeleteStringSection(pcat, sizesKey, d.section);
    AlterInteger(pcat, countKey, -1);
    return FALSE;
}

/* the size a width and height are (portrait), for the host's default:
 * the index of the pre-defined paper size, 0 (custom) if none */
word SpoolFindPaperSize(word width, word height)
{
    const PaperSizeTable *t = TABLE(PT_PAPER);
    word i;

    for (i = 1; i < t->PST_numSizes; i++)
	if (t->PST_sizes[i].PSE_width == width &&
	    t->PST_sizes[i].PSE_height == height)
	    return i;
    return 0;
}
