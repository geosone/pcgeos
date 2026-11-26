/*
 * initfileC.c -- after initfileC.asm: InitFileReadStringBuffer,
 * InitFileReadStringBlock, InitFileReadStringSectionBuffer,
 * InitFileReadStringSectionBlock, InitFileReadDataBuffer,
 * InitFileReadDataBlock.
 */
#include "initfileInt.h"
#include "ghal.h"
#include <string.h>

static MemHandle NewBlock(const void *data, word size)
{
    MemHandle mh = MemAlloc(size ? size : 1, HF_SWAPABLE, HAF_STANDARD);

    if (mh) {
	memcpy(MemLock(mh), data, size);
	MemUnlock(mh);
    }
    return mh;
}
Boolean InitFileReadStringBuffer(const char *category, const char *key,
				 char *buffer, InitFileReadFlags flags,
				 word *dataSize)
{
    word len, size = flags & IFRF_SIZE;
    char *s = InitFileReadStringLow(category, key, flags, &len);

    *dataSize = 0;
    if (!s)
	return TRUE;
    if (size) {
	if (len >= size)
	    len = (word) (size - 1);
	memcpy(buffer, s, len);
	buffer[len] = 0;
	*dataSize = len;
    }
    ghal_mem_free(s);
    return FALSE;
}

Boolean InitFileReadStringBlock(const char *category, const char *key,
				MemHandle *block, InitFileReadFlags flags,
				word *dataSize)
{
    word len;
    char *s = InitFileReadStringLow(category, key, flags & ~IFRF_SIZE, &len);

    *block = 0;
    *dataSize = 0;
    if (!s)
	return TRUE;
    *block = NewBlock(s, (word) (len + 1));
    ghal_mem_free(s);
    if (!*block)
	return TRUE;
    *dataSize = len;
    return FALSE;
}

/* the section of the string of cat/key: s (to free) and where in it */
static char *ReadSection(const char *cat, const char *key, word section,
			 InitFileReadFlags flags, word *start, word *len)
{
    word n;
    char *s = InitFileReadStringLow(cat, key, flags & ~IFRF_SIZE, &n);

    if (s && !IniSection(s, n, section, start, len)) {
	ghal_mem_free(s);
	s = NULL;
    }
    return s;
}

Boolean InitFileReadStringSectionBuffer(const char *category,
					const char *key, word section,
					char *buffer, InitFileReadFlags flags,
					word *dataSize)
{
    word start, len;
    char *s = ReadSection(category, key, section, flags, &start, &len);

    *dataSize = 0;
    if (!s)
	return TRUE;
    if ((flags & IFRF_SIZE) <= len) {
	ghal_mem_free(s);
	return TRUE;
    }
    memcpy(buffer, s + start, len);
    buffer[len] = 0;
    *dataSize = len;
    ghal_mem_free(s);
    return FALSE;
}

Boolean InitFileReadStringSectionBlock(const char *category, const char *key,
				       word section, MemHandle *block,
				       InitFileReadFlags flags, word *dataSize)
{
    word start, len;
    char *s = ReadSection(category, key, section, flags, &start, &len);

    *block = 0;
    *dataSize = 0;
    if (!s)
	return TRUE;
    s[start + len] = 0;
    *block = NewBlock(s + start, (word) (len + 1));
    ghal_mem_free(s);
    if (!*block)
	return TRUE;
    *dataSize = len;
    return FALSE;
}

static Boolean ReadData(const char *cat, const char *key, byte *buffer,
			word bufSize, MemHandle *block, word *dataSize)
{
    word f = 0, body, n;
    Boolean found;
    byte *data;

    *dataSize = 0;
    PInitFile();
    found = InitFileLocate(cat, key, 0, &f, &body);
    if (found && block) {
	const char *t = iniFiles[f].IT_text;
	n = IniGetData(t, body, NULL, 0, TRUE);
	*block = MemAlloc(n ? n : 1, HF_SWAPABLE, HAF_STANDARD);
	if (!*block)
	    found = FALSE;
	else {
	    data = MemLock(*block);
	    *dataSize = IniGetData(t, body, data, n, FALSE);
	    MemUnlock(*block);
	}
    } else if (found)
	*dataSize = IniGetData(iniFiles[f].IT_text, body, buffer, bufSize, FALSE);
    VInitFile();
    return !found;
}

Boolean InitFileReadDataBuffer(const char *category, const char *key,
			       void *buffer, word bufSize, word *dataSize)
{
    return ReadData(category, key, buffer, bufSize, NULL, dataSize);
}

Boolean InitFileReadDataBlock(const char *category, const char *key,
			      MemHandle *block, word *dataSize)
{
    *block = 0;
    return ReadData(category, key, NULL, 0, block, dataSize);
}
