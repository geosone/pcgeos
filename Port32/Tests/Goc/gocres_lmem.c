/*
 * gocres_lmem.c -- builds the LMem blocks of the gocres resources with
 * the C kernel (LMemCreateResourceBlock) and checks that every chunk
 * handle of goc -cg finds its data there.
 */
#include "kernelC.h"

#include <object.h>
#include <chunkarr.h>
#include <stdio.h>
#include <string.h>

extern const GocResource __goc_resource_AppResource;
extern const GocResource __goc_resource_Interface;
extern const GocResource __goc_resource_Extra;
extern const ChunkHandle GocTestText, GocTestExtraRef, GocTestPrimary,
    GocTestNumbers;
extern const word GocTestText_resid;

static int fails;

static void Check(int ok, const char *what)
{
    if (!ok) {
	fails++;
	printf("FAIL %s\n", what);
    }
}

static MemHandle Build(const GocResource *r)
{
    MemHandle mh = LMemCreateResourceBlock(r, 0, HF_SWAPABLE, 0);
    word i, first = ((const LMemBlockHeader *) r->GR_header)->LMBH_offset;

    LMemValidate(mh);
    for (i = 0; i < r->GR_chunks; i++) {
	const GocChunk *c = &r->GR_chunk[i];
	optr o = ConstructOptr(mh, first + 2 * i);
	byte *p = LMemDeref(o);
	char what[64];
	snprintf(what, sizeof(what), "resource %u chunk %u", r->GR_number, i);
	if (c->GC_size + c->GC_size2 == 0) {
	    Check(p == NULL, what);
	    continue;
	}
	Check(p != NULL && LMemGetChunkSize(o) == c->GC_size + c->GC_size2 &&
	      memcmp(p, c->GC_data, c->GC_size) == 0 &&
	      (!c->GC_size2 ||
	       memcmp(p + c->GC_size, c->GC_data2, c->GC_size2) == 0), what);
    }
    return mh;
}

int GocResLMemCheck(void)
{
    MemHandle app = Build(&__goc_resource_AppResource);
    MemHandle ui = Build(&__goc_resource_Interface);
    MemHandle extra = Build(&__goc_resource_Extra);
    optr ref;

    Check(strcmp(LMemDeref(ConstructOptr(ui, GocTestText)), "some text") == 0,
	  "GocTestText in its block");
    Check(GocTestText_resid == __goc_resource_Interface.GR_number,
	  "GocTestText resource");
    /* an optr chunk of another file refers to it, unrelocated */
    ref = *(optr *) LMemDeref(ConstructOptr(extra, GocTestExtraRef));
    Check(GOCR_IS_UNRELOCATED(ref) &&
	  GOCR_RES_OF(ref) == __goc_resource_Interface.GR_number &&
	  OptrToChunk(ref) == GocTestText, "GocTestExtraRef");
    Check(LMemGetChunkSize(ConstructOptr(ui, GocTestPrimary)) > 0,
	  "GocTestPrimary is an object");
    /* @chunkArray: header and elements are one chunk array */
    {
	optr numbers = ConstructOptr(extra, GocTestNumbers);
	word *p;
	Check(ChunkArrayGetCount(numbers) == 3, "@chunkArray count");
	p = ChunkArrayElementToPtr(numbers, 2, NULL);
	Check(p && *p == 30 && *(word *) ChunkArrayElementToPtr(numbers, 0,
							       NULL) == 10,
	      "@chunkArray elements");
	*(word *) ChunkArrayAppend(numbers, 0) = 40;
	Check(ChunkArrayGetCount(numbers) == 4 &&
	      *(word *) ChunkArrayElementToPtr(numbers, 3, NULL) == 40,
	      "@chunkArray append");
	LMemValidate(extra);
    }
    MemFree(app);
    MemFree(ui);
    MemFree(extra);
    return fails;
}
