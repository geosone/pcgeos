/*
 * geosport.h -- base definitions of GEOS in C on 32/64-bit hosts (the
 * Port32 builds: linux, win, gonware, dos32).
 *
 * geos.h includes this file instead of its compiler sections and standard
 * types when GEOS_PORT32 is defined; the 16-bit builds (pmake, Open Watcom
 * 16-bit, Esp) never see it.  See Port32/ABI.md for the decisions.
 *
 * Rules (GHAL-Concept.md 7, 8):
 *   - data types have fixed widths (byte, word, dword are 8/16/32 bit on
 *     every host, also LP64 and LLP64);
 *   - there are no segments: _far, _near, _pascal are empty, and the
 *     segment macros (PtrToSegment, SegmentOf, PtrToOffset) do not exist,
 *     so every use shows up at compile time;
 *   - handles are 32 bit, so an optr (handle:chunk) is 64 bit.
 */
#ifndef GEOSPORT_H
#define GEOSPORT_H

#include <stddef.h>
#include <stdint.h>

/* --- calling conventions and pointer kinds: gone ------------------------ */
#define _near
#define _far
#define _pascal
#define _cdecl
#define _export
#define vfar		/* data qualifier of some 16-bit sources (Levels, GeoFile) */
#define PCB(ret, name, args)	ret (*name) args
#define CCB(ret, name, args)	ret (*name) args
/* inline machine code has no meaning above the GHAL */
#define _inline_byte(val)	_inline_byte_not_available_in_port32
#define _inline_word(val)	_inline_word_not_available_in_port32
#define _pragma_set_data_segment(seg)
#define _pragma_default_data_segment

/* --- standard types ------------------------------------------------------ */
typedef uint8_t		byte;
typedef int8_t		sbyte;
typedef uint16_t	word;
typedef int16_t		sword;
typedef uint32_t	dword;
typedef int32_t		sdword;
typedef byte		ByteFlags;
typedef word		WordFlags;
typedef dword		DWordFlags;
typedef byte		ByteEnum;
typedef sword		Boolean;
typedef uint16_t	wchar;		/* GEOS DBCS character: 16 bit */

/* --- handles and object pointers ----------------------------------------- */
typedef uint32_t	Handle;
typedef word		ChunkHandle;	/* offset in an LMem block */
typedef uint64_t	optr;		/* handle << 32 | chunk */
typedef uint32_t	Message;
typedef word		VardataKey;

#define OptrToHandle(op)	((Handle) ((optr) (op) >> 32))
#define OptrToChunk(op)		((ChunkHandle) (op))
#define ConstructOptr(han, ch)	((((optr) (Handle) (han)) << 32) | \
				 (optr) (ChunkHandle) (ch))

/* --- object resources made by goc -cg (Port32/ABI.md section 4) ----------- */
/*
 * goc gives every chunk of a resource a compile-time chunk handle:
 * the handle table follows the block header (rounded up to 4 bytes) and
 * has one word per chunk; in an object block, chunk 0 holds the flags.
 */
#define GOCR_LMEM_HANDLES(hdr)	((sizeof(hdr) + 3) & ~(size_t) 3)
#define GOCR_CHUNK_HANDLE(hdr, n) ((int) (GOCR_LMEM_HANDLES(hdr) + 2 * (n)))
#define GOCR_SCHUNK(chunk)	((ChunkHandle) __goc_ch_##chunk)

/*
 * optrs in resource data are unrelocated: the handle half holds the
 * resource number (from the .gp file) with GOCR_UNRELOCATED set, and
 * object relocation replaces it with the resource's handle when the
 * block is loaded.  GOCR_PROCESS_RES stands for the geode's process.
 */
#define GOCR_UNRELOCATED	0x80000000u
#define GOCR_PROCESS_RES		0x7fff
#define GOCR_UNRELOC_OPTR(res, ch)	((((optr) (GOCR_UNRELOCATED | (dword) (res))) << 32) \
				 | (optr) (ChunkHandle) (ch))
#define GOCR_SOPTR(chunk)	GOCR_UNRELOC_OPTR(__goc_rs_##chunk, __goc_ch_##chunk)
#define GOCR_PROCESS_OPTR	GOCR_UNRELOC_OPTR(GOCR_PROCESS_RES, 0)
/*
 * The same for an object or chunk of another file of the geode
 * (@extern): goc -cg -x writes the __goc_xch_/__goc_xrs_ macros of each
 * file, and cmake/GeosGoc.cmake collects them in <geode>_objs.h.
 */
#define GOCR_XSCHUNK(chunk)	((ChunkHandle) (__goc_xch_##chunk))
#define GOCR_XSOPTR(chunk)	GOCR_UNRELOC_OPTR(__goc_xrs_##chunk, __goc_xch_##chunk)
#define GOCR_VARIANT_SUPERCLASS	((struct _ClassStruct *) 1)
/* the base structure of an object (class pointer, master offsets) has no
 * padding: Class_masterOffset is sizeof(ClassStruct *) + 2 * (level - 1) */
#define GOCR_PACKED		__attribute__((packed))

/*
 * In code, "@Obj" is GOCR_OPTR(Obj): the handle of the resource as loaded
 * for this geode (filled in by the geode loader) and the chunk handle.
 * Obj and Obj_resid are constants defined by the file that holds Obj.
 */
#if defined(__GNUC__) && defined(__ELF__)	/* COFF/PE: no visibility */
#define GOCR_HIDDEN		__attribute__((visibility("hidden")))
#else
#define GOCR_HIDDEN
#endif
extern Handle __goc_resource_handles[] GOCR_HIDDEN;
#define GOCR_OPTR(chunk)		ConstructOptr(__goc_resource_handles[chunk##_resid], chunk)
#define GOCR_CLASS_OPTR(cls)	((optr) (uintptr_t) &(cls))

/*
 * Static data in code (a table of optrs, a GenControl feature list) holds
 * unrelocated optrs as well (GOCR_SOPTR: a constant).  They are resolved
 * where they are used: in the geode that holds them with GOCR_RESOLVE
 * (goc does that for the destination of @send/@call), elsewhere with
 * GeodeResolveOptrAt(), which finds the geode from the address of the
 * data.  Entry 0 of __goc_resource_handles is the geode's core block,
 * which is also its process (GOCR_PROCESS_RES).  Port32/ABI.md section 6.
 */
#define GOCR_IS_UNRELOCATED(op)	((OptrToHandle(op) & GOCR_UNRELOCATED) != 0)
#define GOCR_RES_OF(op)		((word) OptrToHandle(op))

static inline optr
GocrResolveWith(optr op, const Handle *handles)
{
    word res;

    if (!GOCR_IS_UNRELOCATED(op)) {
	return op;
    }
    res = GOCR_RES_OF(op);
    return ConstructOptr(handles[res == GOCR_PROCESS_RES ? 0 : res],
			 OptrToChunk(op));
}
#define GOCR_RESOLVE(op)		GocrResolveWith((op), __goc_resource_handles)

/*
 * What the loader needs to build a resource: the block header and, per
 * chunk, up to two C objects that are put one after the other (header and
 * elements of a chunk array or GCN list, a moniker and its text).
 */
typedef struct {
    const void	*GC_data;
    dword	GC_size;
    const void	*GC_data2;
    dword	GC_size2;
} GocChunk;

typedef struct {
    word		GR_number;	/* GOCR_RES_<NAME> */
    word		GR_chunks;	/* entries in GR_chunk */
    const void		*GR_header;
    dword		GR_headerSize;
    const GocChunk	*GR_chunk;
} GocResource;

#endif /* GEOSPORT_H */
