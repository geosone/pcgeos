/*
 * lmemNameArray.c -- name arrays of the C kernel (CInclude/chunkarr.h;
 * after lmemNameArray.asm).
 *
 * A name array is a variable-sized element array (NameArrayHeader) whose
 * elements are a RefElementHeader, NAH_dataSize bytes of data and the
 * name (TCHARs, no terminator).  Adding a name that is there adds a
 * reference to it (and replaces its data with NAAF_SET_DATA_ON_REPLACE).
 * Names compare exactly.
 */
#include "kernelC.h"

#include <chunkarr.h>
#include <string.h>

static word NameLength(const TCHAR *name, word nameLength)
{
    word n = 0;

    if (nameLength)
	return nameLength;
    while (name[n])
	n++;
    return n;
}

static word DataSize(optr arr)
{
    return ((NameArrayHeader *) LMemDeref(arr))->NAH_dataSize;
}

/* the token of a name, or CA_NULL_ELEMENT */
static word Lookup(optr arr, const TCHAR *name, word len)
{
    word n = ChunkArrayGetCount(arr), i, size, ds = DataSize(arr);
    word bytes = (word) (len * sizeof(TCHAR));

    for (i = 0; i < n; i++) {
	byte *e = ChunkArrayElementToPtr(arr, i, &size);
	if (((RefElementHeader *) e)->REH_refCount.WAAH_high == (sbyte) 0xff)
	    continue;				/* free */
	if (size == sizeof(RefElementHeader) + ds + bytes &&
	    memcmp(e + sizeof(RefElementHeader) + ds, name, bytes) == 0)
	    return i;
    }
    return CA_NULL_ELEMENT;
}

ChunkHandle NameArrayCreateAt(optr arr, word dataSize, word headerSize,
			      ObjChunkFlags ocf)
{
    ChunkHandle ch;

    if (headerSize == 0)
	headerSize = sizeof(NameArrayHeader);
    if (headerSize < sizeof(NameArrayHeader))
	KernelFatal(CHUNK_ARRAY_HEADER_SIZE_TOO_SMALL);
    ch = ElementArrayCreateAt(arr, 0, headerSize, ocf);
    if (ch)
	((NameArrayHeader *) LMemDeref(ConstructOptr(OptrToHandle(arr), ch)))
	    ->NAH_dataSize = dataSize;
    return ch;
}

static Boolean NeverEqual(void *a, void *b, dword v)
{
    (void) a; (void) b; (void) v;
    return FALSE;
}

word NameArrayAdd(optr arr, const TCHAR *nameToAdd, word nameLength,
		  NameArrayAddFlags flags, const void *data)
{
    word len = NameLength(nameToAdd, nameLength), ds = DataSize(arr);
    word token = Lookup(arr, nameToAdd, len), size;
    NameArrayMaxElement el;

    if (len > NAME_ARRAY_MAX_NAME_LENGTH || ds > NAME_ARRAY_MAX_DATA_SIZE)
	KernelFatal(CHUNK_ARRAY_BAD_ELEMENT);
    if (token != CA_NULL_ELEMENT) {
	ElementArrayAddReference(arr, token);
	if ((flags & NAAF_SET_DATA_ON_REPLACE) && data && ds)
	    memcpy((byte *) ChunkArrayElementToPtr(arr, token, NULL) +
		   sizeof(RefElementHeader), data, ds);
	return token;
    }
    size = (word) (sizeof(RefElementHeader) + ds + len * sizeof(TCHAR));
    memset(&el, 0, sizeof(el));
    if (data && ds)
	memcpy((byte *) &el + sizeof(RefElementHeader), data, ds);
    memcpy((byte *) &el + sizeof(RefElementHeader) + ds, nameToAdd,
	   len * sizeof(TCHAR));
    return ElementArrayAddElementSized(arr, &el, size, 0, NeverEqual);
}

word NameArrayFind(optr arr, const TCHAR *nameToFind, word nameLength,
		   void *returnData)
{
    word token = Lookup(arr, nameToFind, NameLength(nameToFind, nameLength));
    word ds = DataSize(arr);

    if (token != CA_NULL_ELEMENT && returnData && ds)
	memcpy(returnData, (byte *) ChunkArrayElementToPtr(arr, token, NULL) +
	       sizeof(RefElementHeader), ds);
    return token;
}

void NameArrayChangeName(optr arr, word nameToken, const TCHAR *newName,
			 word nameLength)
{
    word len = NameLength(newName, nameLength), ds = DataSize(arr);
    byte *e;

    ChunkArrayElementResize(arr, nameToken,
			    (word) (sizeof(RefElementHeader) + ds +
				    len * sizeof(TCHAR)));
    e = ChunkArrayElementToPtr(arr, nameToken, NULL);
    memcpy(e + sizeof(RefElementHeader) + ds, newName, len * sizeof(TCHAR));
}
