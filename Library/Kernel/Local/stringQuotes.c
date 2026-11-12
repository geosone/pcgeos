/*
 * stringQuotes.c -- the quotes of the language (after stringQuotes.asm,
 * SBCS): the chunk Quotes of localStrings.asm holds front and end single,
 * front and end double quote; geos.ini [localization] quotes overrides
 * them (as data).
 */
#include "kernelC.h"
#include "localInt.h"

static const char quotesKey[] = "quotes";

void LocalGetQuotes(LocalQuotes *quotes)
{
    const byte *q;

    LocalLockStrings();
    q = (const byte *) LocalString(LS_Quotes);
    quotes->frontSingle = q[0];
    quotes->endSingle = q[1];
    quotes->frontDouble = q[2];
    quotes->endDouble = q[3];
    LocalUnlockStrings();
}

void LocalSetQuotes(const LocalQuotes *quotes)
{
    char q[5];

    q[0] = (char) quotes->frontSingle;
    q[1] = (char) quotes->endSingle;
    q[2] = (char) quotes->frontDouble;
    q[3] = (char) quotes->endDouble;
    q[4] = 0;
    LocalSetString(LS_Quotes, q);
    LocalWriteStringAsData(quotesKey, LS_Quotes);
}

/* InitQuotes */
void LocalInitQuotes(void)
{
    (void) LocalReadStringAsData(quotesKey, LS_Quotes,
				 LOCAL_QUOTES_CHUNK_SIZE);
}
