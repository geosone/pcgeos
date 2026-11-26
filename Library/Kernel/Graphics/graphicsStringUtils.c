/*
 * graphicsStringUtils.c -- after graphicsStringUtils.asm:
 * GrAllocGString, GrDestroyGString.
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

/* AllocGString: block is the chunk or VM block, 0 to create one; *block
 * returns it */
GStateHandle GrAllocGString(Handle han, GStringType type, word *block,
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
	free(g->GSS_buf);
	MemFree(mh);
    }
    if (!path)
	GrDestroyState(gstring);
}
