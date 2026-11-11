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

#endif /* GEOSPORT_H */
