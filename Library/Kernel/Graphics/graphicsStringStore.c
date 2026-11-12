/*
 * graphicsStringStore.c -- writing graphics strings (after
 * graphicsStringStore.asm: GSStore, GSStoreBytes, WriteMemGString,
 * WriteStreamGString, WriteVMemGString, GrEndGString;
 * graphicsStringUtils.asm: AllocGString, InitMemGString,
 * InitStreamGString, InitVMemGString, GrDestroyGString;
 * graphicsString.asm: GrCreateGString, GrLoadGString, GrEditGString).
 *
 * A graphics string is a GState (GrCreateState without a window) whose
 * GS_gstring is the handle of a block with a GString.  The Gr* routines
 * see GS_gstring and store an element instead of drawing (GrGSRecord);
 * the attribute and transformation routines store and set the GState.
 * The elements are kept in the DOS16 form of gstring.def, byte packed
 * and little endian, as goc writes static graphics strings and as
 * documents hold them:
 *   GST_CHUNK   a chunk of an object block; the elements one after the
 *               other, appended at the end
 *   GST_STREAM  a file; creating one writes the metafile header (a
 *               GR_COMMENT "GEOS METAFILE 2.0") first
 *   GST_VMEM    a huge array of variable size, one element in each;
 *               written at GSS_curPos (GrEditGString: the end)
 *   GST_PTR     read only, in memory (GrLoadGStringPtr)
 * The asm buffered stream elements in a chunk of the GString block
 * (GS_WRITE_THRESHOLD) and VM elements until GSSC_FLUSH; each element
 * is written whole here.  A failed write sets GSF_ERROR: further writes
 * are dropped and GrEndGString returns GSET_DISK_FULL.
 *
 * Port32 (ABI.md 13p): GST_PTR takes a pointer, which no longer fits
 * the segment and offset of GrLoadGString; GrLoadGStringPtr loads one,
 * GrLoadGString(..., GST_PTR, ...) fails.
 *
 * Not yet: GST_PATH (paths), the stream truncation after a full disk.
 */
#include "graphicsInt.h"
#include <gstring.h>
#include <file.h>
#include <hugearr.h>
#include <stdarg.h>
#include <stdlib.h>

#define CGSC_WRITING	1

static const byte metafileHeader[] = {
    GR_COMMENT, 17, 0,
    'G', 'E', 'O', 'S', ' ', 'M', 'E', 'T', 'A', 'F', 'I', 'L', 'E', ' ',
    '2', '.', '0'
};

Handle GrGStringOf(GStateHandle gstate)
{
    GState *gs;
    Handle h;

    if (!gstate)
	return 0;
    gs = MemLock(gstate);
    h = gs->GS_gstring;
    MemUnlock(gstate);
    return h;
}

/* GSStore and the Write*GString routines */
Boolean GrGSStore(Handle gstring, const void *data, word size)
{
    GStringBlock *g = MemLock(gstring);
    Boolean ok = FALSE;

    if (g->GSS_flags & GSF_ERROR) {
	MemUnlock(gstring);
	return FALSE;
    }
    switch (g->GSS_flags & GSF_HANDLE_TYPE) {
    case GST_CHUNK: {
	MemHandle mh = (MemHandle) g->GSS_hString;
	ChunkHandle ch = (ChunkHandle) g->GSS_firstBlock;
	word old;

	MemLock(mh);
	old = LMemGetChunkSizeHandles(mh, ch);
	if (!LMemReAllocHandles(mh, ch, (word) (old + size))) {
	    memcpy((byte *) LMemDerefHandles(mh, ch) + old, data, size);
	    ok = TRUE;
	}
	MemUnlock(mh);
	break;
    }
    case GST_STREAM:
	ok = (Boolean) (FileWrite((FileHandle) g->GSS_hString, data, size,
				  TRUE) == size);
	break;
    case GST_VMEM:
	HugeArrayInsert((VMFileHandle) g->GSS_hString,
			(VMBlockHandle) g->GSS_firstBlock, size, g->GSS_curPos,
			data);
	g->GSS_curPos++;
	ok = TRUE;
	break;
    default:					/* GST_PTR: read only */
	break;
    }
    if (!ok)
	g->GSS_flags |= GSF_ERROR;
    MemUnlock(gstring);
    return ok;
}

/* put v (n bytes) little endian */
static byte *Put(byte *p, dword v, int n)
{
    while (n--) {
	*p++ = (byte) v;
	v >>= 8;
    }
    return p;
}

Boolean GrGSRecord(GStateHandle gstate, byte opcode, const char *fmt, ...)
{
    Handle gstring = GrGStringOf(gstate);
    byte local[64], *buf = local, *p;
    const char *f;
    size_t size = 1;
    va_list ap;
    Boolean ok;

    if (!gstring)
	return FALSE;
    va_start(ap, fmt);				/* the size */
    for (f = fmt; *f; f++)
	switch (*f) {
	case 'b': (void) va_arg(ap, int); size += 1; break;
	case 'w': (void) va_arg(ap, int); size += 2; break;
	case 'f': (void) va_arg(ap, dword); size += 3; break;
	case 'd': (void) va_arg(ap, dword); size += 4; break;
	case 'D': (void) va_arg(ap, const DWFixed *); size += 6; break;
	case 'r':
	    (void) va_arg(ap, const void *);
	    size += (word) va_arg(ap, int);
	    break;
	}
    va_end(ap);
    if (size > 0xffff)
	return TRUE;
    if (size > sizeof(local) && !(buf = malloc(size)))
	return TRUE;
    p = buf;
    *p++ = opcode;
    va_start(ap, fmt);
    for (f = fmt; *f; f++)
	switch (*f) {
	case 'b': *p++ = (byte) va_arg(ap, int); break;
	case 'w': p = Put(p, (word) va_arg(ap, int), 2); break;
	case 'f': {				/* WWFixed to WBFixed */
	    dword v = va_arg(ap, dword);
	    *p++ = (byte) (v >> 8);
	    p = Put(p, v >> 16, 2);
	    break;
	}
	case 'd': p = Put(p, va_arg(ap, dword), 4); break;
	case 'D': {
	    const DWFixed *d = va_arg(ap, const DWFixed *);
	    p = Put(p, d->DWF_frac, 2);
	    p = Put(p, (dword) d->DWF_int, 4);
	    break;
	}
	case 'r': {
	    const void *src = va_arg(ap, const void *);
	    word n = (word) va_arg(ap, int);
	    if (n)
		memcpy(p, src, n);
	    p += n;
	    break;
	}
	}
    va_end(ap);
    ok = GrGSStore(gstring, buf, (word) size);
    (void) ok;
    if (buf != local)
	free(buf);
    return TRUE;
}

GStringErrorType GrEndGString(GStateHandle gstate)
{
    GState *gs = MemLock(gstate);
    Handle gstring = gs->GS_gstring;
    byte end = GR_END_GSTRING;
    GStringErrorType r = GSET_NO_ERROR;

    if (gstring && !(gs->GS_pathFlags & PF_DEFINING_PATH) &&
	!GrGSStore(gstring, &end, 1))
	r = GSET_DISK_FULL;
    MemUnlock(gstate);
    return r;
}

/* AllocGString: block is the chunk or VM block, 0 to create one; *block
 * returns it */
static GStateHandle AllocGString(Handle han, GStringType type, word *block,
				 word flags)
{
    MemHandle mh = MemAlloc(sizeof(GStringBlock), HF_SWAPABLE | HF_SHARABLE,
			    HAF_ZERO_INIT);
    GStringBlock *g;
    GStateHandle gstate;
    GState *gs;

    if (!mh)
	return 0;
    g = MemLock(mh);
    g->GSS_flags = (word) (type & GSF_HANDLE_TYPE);
    g->GSS_hString = han;
    g->GSS_firstBlock = *block;
    switch (type) {
    case GST_CHUNK:				/* InitMemGString */
	MemLock((MemHandle) han);
	if (!g->GSS_firstBlock)
	    g->GSS_firstBlock = LMemAlloc((MemHandle) han, 0);
	if (flags & CGSC_WRITING)
	    g->GSS_curPos = LMemGetChunkSizeHandles((MemHandle) han,
						    g->GSS_firstBlock);
	MemUnlock((MemHandle) han);
	break;
    case GST_STREAM:				/* InitStreamGString */
	g->GSS_filePos = FilePos((FileHandle) han, 0, FILE_POS_RELATIVE);
	g->GSS_curPos = g->GSS_filePos;
	if ((flags & CGSC_WRITING) &&
	    FileWrite((FileHandle) han, metafileHeader,
		      sizeof(metafileHeader), TRUE) != sizeof(metafileHeader))
	    g->GSS_flags |= GSF_ERROR;
	break;
    case GST_VMEM:				/* InitVMemGString */
	if (!g->GSS_firstBlock)
	    g->GSS_firstBlock = HugeArrayCreate((VMFileHandle) han, 0, 0);
	else if (flags & CGSC_WRITING)
	    g->GSS_curPos = HugeArrayGetCount((VMFileHandle) han,
					      g->GSS_firstBlock);
	break;
    default:
	break;
    }
    *block = g->GSS_firstBlock;
    MemUnlock(mh);
    gstate = GrCreateState(0);
    gs = MemLock(gstate);
    gs->GS_gstring = mh;
    MemUnlock(gstate);
    return gstate;
}

GStateHandle GrCreateGString(Handle han, GStringType hanType, word *vmBlock)
{
    word block = 0;
    GStateHandle gstate;

    if (hanType == GST_PTR || hanType > GST_VMEM)
	return 0;
    gstate = AllocGString(han, hanType, &block, CGSC_WRITING);
    if (vmBlock)
	*vmBlock = block;
    return gstate;
}

GStateHandle GrLoadGString(Handle han, GStringType hanType, word vmBlock)
{
    if (hanType == GST_PTR || hanType > GST_VMEM)
	return 0;			/* GST_PTR: GrLoadGStringPtr */
    return AllocGString(han, hanType, &vmBlock, 0);
}

GStateHandle GrLoadGStringPtr(const void *gstring)
{
    word block = 0;
    GStateHandle gstate = AllocGString(0, GST_PTR, &block, 0);
    GState *gs;
    GStringBlock *g;

    if (!gstate)
	return 0;
    gs = MemLock(gstate);
    g = MemLock(gs->GS_gstring);
    g->GSS_ptr = gstring;
    MemUnlock(gs->GS_gstring);
    MemUnlock(gstate);
    return gstate;
}

GStateHandle GrEditGString(Handle vmFile, word vmBlock)
{
    return AllocGString(vmFile, GST_VMEM, &vmBlock, CGSC_WRITING);
}

void GrDestroyGString(GStateHandle gstring, GStateHandle gstate,
		      GStringKillType type)
{
    GState *gs = MemLock(gstring);
    MemHandle mh = gs->GS_gstring;
    Boolean path = (Boolean) ((gs->GS_pathFlags & PF_DEFINING_PATH) != 0);
    GStringBlock *g;

    gs->GS_gstring = 0;
    MemUnlock(gstring);
    if (mh) {
	g = MemLock(mh);
	if (gstate && (g->GSS_flags & GSF_CONTINUING))
	    GrRestoreTransform(gstate);
	if (type == GSKT_KILL_DATA)
	    switch (g->GSS_flags & GSF_HANDLE_TYPE) {
	    case GST_CHUNK:
		MemLock((MemHandle) g->GSS_hString);
		LMemFreeHandles((MemHandle) g->GSS_hString,
				(ChunkHandle) g->GSS_firstBlock);
		MemUnlock((MemHandle) g->GSS_hString);
		break;
	    case GST_VMEM:
		HugeArrayDestroy((VMFileHandle) g->GSS_hString,
				 g->GSS_firstBlock);
		break;
	    default:
		break;
	    }
	free(g->GSS_elem);
	MemFree(mh);
    }
    if (!path)
	GrDestroyState(gstring);
}
