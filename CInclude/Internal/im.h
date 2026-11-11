/***********************************************************************
 *
 *	Copyright (c) GeoWorks 1989 -- All Rights Reserved
 *
 * PROJECT:	PC GEOS
 * FILE:	im.h
 *
 * DESCRIPTION:
 *	C form of the part of Include/Internal/im.def that the input
 *	drivers use: the messages they send to the input manager.
 *
 *	IMClass is a subclass of ProcessClass (first message 8192), so
 *	its messages start at 8704 (goc and Esp give a subclass 512).
 *	Port32: the parameters of these register messages are the
 *	structures below (Port32/ABI.md section 5).
 *
 ***********************************************************************/
#ifndef __IM_H
#define __IM_H

#include <geos.h>

#define IM_FIRST_MESSAGE	8704
#define MSG_IM_BUTTON_CHANGE	(IM_FIRST_MESSAGE + 0)
#define MSG_IM_PTR_CHANGE	(IM_FIRST_MESSAGE + 1)
#define MSG_IM_PRESSURE_CHANGE	(IM_FIRST_MESSAGE + 2)
#define MSG_IM_DIRECTION_CHANGE	(IM_FIRST_MESSAGE + 3)
#define MSG_IM_KBD_SCAN		(IM_FIRST_MESSAGE + 4)

/* MSG_META_KBD_CHAR (MetaInputMessages, inputC.goh), which the
 * keyboard driver returns from DR_KBD_XLATE_SCAN; for C code that is
 * not goc (ctest kernel.obj checks it against goc) */
#define IM_META_KBD_CHAR	119

/*
 * MSG_IM_KBD_SCAN: cx = scan code (high byte the extension, 0xe0 or
 * 0xe1, low byte the code with 0x80 on a release), dx, bp = 0,
 * si = driver handle (0)
 */
typedef struct {
    word	IMKS_scanCode;
    word	IMKS_dx;
    word	IMKS_bp;
    word	IMKS_driver;
} IMKbdScanParams;

#endif /* __IM_H */
