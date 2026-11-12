/***********************************************************************
 *
 *	Copyright (c) GeoWorks 1991 -- All Rights Reserved
 *
 * PROJECT:	PC GEOS
 * FILE:	grWinInt.h
 *
 * DESCRIPTION:
 *	Graphics and window routines for the kernel and the system
 *	libraries, not for applications (after grWinInt.def).
 *
 *	A region here has no bounds: lines of the bottom y of a band,
 *	its on/off x pairs (inclusive), EOREGREC; one more EOREGREC
 *	ends it.  The first band reaches up to the top of the
 *	coordinate space; below the last one the region is empty.  A
 *	null region is a single EOREGREC.
 *
 ***********************************************************************/
#ifndef __GRWININT_H
#define __GRWININT_H

#include <graphics.h>

/* RegionOpFlags */
typedef WordFlags RegionOpFlags;
#define ROF_ES_DS_SAME	0x0010	/* not used by the C kernel */
#define ROF_OVERFLOW	0x0008
#define ROF_NOT_OP	0x0004
#define ROF_OR_OP	0x0002
#define ROF_AND_OP	0x0001

/*
 * reg3 = reg1 op reg2 (ROF_AND_OP, ROF_OR_OP) or NOT reg1 within the
 * coordinate space (ROF_NOT_OP, reg2 unused).  *size is the room at
 * reg3 in bytes; on return the size of the region, or, when it did not
 * fit (TRUE), how much to try again with.
 */
extern Boolean GrPtrRegOp(RegionOpFlags op, const Region *reg1,
			  const Region *reg2, Region *reg3, word *size);

/*
 * The same on chunks; reg3's chunk is resized to the result.  reg1,
 * reg2 and reg3 may share a block (reg3 not being reg1 or reg2).
 */
extern void GrChunkRegOp(RegionOpFlags op, optr reg1, optr reg2,
			 optr reg3);

#endif
