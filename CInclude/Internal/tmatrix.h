/***********************************************************************
 *
 *	Copyright (c) GeoWorks 1989 -- All Rights Reserved
 *
 * PROJECT:	PC GEOS
 * FILE:	Internal/tmatrix.h
 *
 * DESCRIPTION:
 *	C form of Include/Internal/tmatrix.def: the transformation of
 *	windows and graphics states (Port32).
 *
 ***********************************************************************/
#ifndef __INTERNAL_TMATRIX_H
#define __INTERNAL_TMATRIX_H

#include <geos.h>

typedef ByteFlags TransFlags;
#define TF_INV_VALID	0x08
#define TF_ROTATED	0x04
#define TF_SCALED	0x02
#define TF_TRANSLATED	0x01

#define TM_ROTATED	TF_ROTATED
#define TM_SCALED	TF_SCALED
#define TM_TRANSLATED	TF_TRANSLATED
#define TM_COMPLEX	(TM_ROTATED | TM_SCALED)
#define TM_WFRAC_MIN	0x10

typedef struct {
    WWFixed	TM_11;
    WWFixed	TM_12;
    WWFixed	TM_21;
    WWFixed	TM_22;
    DWFixed	TM_31;
    DWFixed	TM_32;
    DDFixed	TM_xInv;	/* inverse translation */
    DDFixed	TM_yInv;
    TransFlags	TM_flags;
} TMatrix;

#endif /* __INTERNAL_TMATRIX_H */
