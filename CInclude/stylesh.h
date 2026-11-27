/***********************************************************************
 *
 *	Copyright (c) Berkeley Softworks 1991 -- All Rights Reserved
 *
 * PROJECT:	PC GEOS
 * FILE:	stylesh.h
 * AUTHOR:	Tony Requist: February 1, 1991
 *
 * DECLARER:	Kernel
 *
 * DESCRIPTION:
 *	This file defines style sheet structures
 *
 *	$Id: stylesh.h,v 1.1 97/04/04 15:58:52 newdeal Exp $
 *
 ***********************************************************************/

#ifndef	__STYLESH_H
#define __STYLESH_H

#include "chunkarr.h"

typedef struct {
    RefElementHeader	SSEH_meta;
    word		SSEH_style;
} StyleSheetElementHeader;

typedef WordFlags StyleElementFlags;
#define SEF_DISPLAY_IN_TOOLBOX	0x8000

#ifdef GEOS_PORT32
/*
 * Port32 (Port32/ABI.md section 13ac): the private data is four bytes
 * at a word boundary, as in a TextStyleElementHeader, so the attribute
 * tokens follow it at the same offset in every style element; handles
 * are 32 bits wide; the callbacks are C functions.
 */
#define SEF_PROTECTED			0x4000

#define STYLE_SHEET_MAX_ELEMENT_SIZE	500

typedef struct {
    NameArrayElement	SEH_meta;
    word		SEH_baseStyle;
    StyleElementFlags	SEH_flags;
    byte		SEH_reserved[6];
    byte		SEH_privateData[4];	/* a dword */
    word		SEH_attrTokens[1];	/* one per attribute array */
} StyleElementHeader;

#define STYLE_ELEMENT_HEADER_SIZE \
	((word) (sizeof(StyleElementHeader) - sizeof(word)))

/* the chunk is not in an object block (but a memory block) */
#define STYLE_CHUNK_NOT_IN_OBJ_BLOCK	1

typedef struct {
    VMFileHandle	SCD_vmFile;
    Handle		SCD_vmBlockOrMemHandle;
    ChunkHandle		SCD_chunk;
} StyleChunkDesc;

#define MAX_STYLE_SHEET_ATTRS		4

/*
 * The description callback: the text describing the attribute
 * structure derived (base: the structure of the base style, or NULL)
 * goes to *ptr, at most *left characters (both updated); used: the
 * characters already in the buffer.  extraUI: the extra UI of a
 * description of attributes (or 0), privateData: the style's.
 */
typedef void StyleSheetDescriptionCallback(TCHAR **ptr, word *left,
					   word used, const void *derived,
					   const void *base, optr extraUI,
					   dword privateData, word attrsFlag);
/*
 * The merge callback: target <= new + (target - old).  result holds a
 * copy of new (STYLE_SHEET_MAX_ELEMENT_SIZE bytes) and gets the
 * result; old may be NULL; privateData: the style's four bytes; size:
 * the element's size.  Returns the result's size.
 */
typedef word StyleSheetMergeCallback(void *target, void *result,
				     const void *old,
				     const void *privateData, word size);
/*
 * The substitution callback: the object's references to oldToken become
 * newToken; updateRefs: the reference counts are to follow (one
 * reference less to the old, one more to the new).  Returns non-zero
 * if the object must be calculated again.
 */
typedef word StyleSheetSubstitutionCallback(optr obj, word oldToken,
					    word newToken,
					    Boolean updateRefs);

typedef struct {
    StyleSheetDescriptionCallback *SSP_descriptionCallbacks[MAX_STYLE_SHEET_ATTRS];
    StyleSheetDescriptionCallback *SSP_specialDescriptionCallback;
    StyleSheetMergeCallback	*SSP_mergeCallbacks[MAX_STYLE_SHEET_ATTRS];
    StyleSheetSubstitutionCallback *SSP_substitutionCallbacks[MAX_STYLE_SHEET_ATTRS];
    StyleChunkDesc	SSP_styleArray;
    StyleChunkDesc	SSP_attrArrays[MAX_STYLE_SHEET_ATTRS];
    word		SSP_attrTokens[MAX_STYLE_SHEET_ATTRS];
    StyleChunkDesc	SSP_xferStyleArray;
    StyleChunkDesc	SSP_xferAttrArrays[MAX_STYLE_SHEET_ATTRS];
} StyleSheetParams;
#else
typedef struct {
    NameArrayElement	SEH_meta;
    word		SEH_baseStyle;
    StyleElementFlags	SEH_flags;
    byte		SEH_reserved[6];
    dword		SEH_privateData;
} StyleElementHeader;


typedef struct {
    VMFileHandle	SCD_vmFile;
    word		SCD_vmBlockOrMemHandle;
    ChunkHandle		SCD_chunk;
} StyleChunkDesc;

#define MAX_STYLE_SHEET_ATTRS		4

typedef struct {
    void		*SSP_descriptionCallbacks[MAX_STYLE_SHEET_ATTRS];
    void		*SSP_specialDescriptionCallback;
    void		*SSP_mergeCallbacks[MAX_STYLE_SHEET_ATTRS];
    void		*SSP_substitutionCallbacks[MAX_STYLE_SHEET_ATTRS];
    StyleChunkDesc	SSP_styleArray;
    StyleChunkDesc	SSP_attrArrays[MAX_STYLE_SHEET_ATTRS];
    word		SSP_attrTokens[MAX_STYLE_SHEET_ATTRS];
    StyleChunkDesc	SSP_xferStyleArray;
    StyleChunkDesc	SSP_xferAttrArrays[MAX_STYLE_SHEET_ATTRS];
} StyleSheetParams;
#endif

#endif
