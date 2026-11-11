/***********************************************************************
 *
 *	Copyright (c) GeoWorks 1989 -- All Rights Reserved
 *
 * PROJECT:	PC GEOS
 * FILE:	kbdDr.h
 *
 * DESCRIPTION:
 *	C form of Include/Internal/kbdDr.def: the keyboard driver
 *	interface.  The registers of each function are those of
 *	DriverRegs (geosdriver.h).
 *
 ***********************************************************************/
#ifndef __KBDDR_H
#define __KBDDR_H

#include <driver.h>
#include <Internal/kbdMap.h>

#define KEYBOARD_PROTO_MAJOR	(DRIVER_PROTO_MAJOR+2)
#define KEYBOARD_PROTO_MINOR	(DRIVER_PROTO_MINOR+1)

/* KeyModifiers: index into KeyMapStruct */
#define KMS_ALT		0x04
#define KMS_CTRL	0x02
#define KMS_SHIFT	0x01

/* CharTypeFlags */
#define CTF_VIRTUAL	0x01

/* returned by DR_KBD_MAP_KEY */
typedef struct {
    byte	KMS_virtual;		/* ExtVirtualBits */
    byte	KMS_keyType;		/* KeyDataFlags */
    byte	KMS_char;
    byte	KMS_shift;
    byte	KMS_ctrl;
    byte	KMS_shiftCtrl;
    byte	KMS_alt;
    byte	KMS_shiftAlt;
    byte	KMS_ctrlAlt;
    byte	KMS_shiftCtrlAlt;
} KeyMapStruct;

/* KbdDelay, KbdRepeat */
#define KBD_DELAY_SHORT		0
#define KBD_DELAY_MEDIUM	0x20
#define KBD_DELAY_LONG		0x40
#define KBD_REPEAT_FAST		0
#define KBD_REPEAT_MEDIUM	0x8
#define KBD_REPEAT_SLOW		0x1f

/* KbdStateFunctions */
#define KSF_NEW_PROCESS		0x80
#define KSF_NEW_INDICATOR	0x40
#define KSF_NEW_TYPEMATIC	0x20

/*
 * KbdFunction (after DR_UNSUSPEND):
 *
 * DR_KBD_GET_KBD_STATE	return al = ShiftState, ah = ToggleState,
 *			bx = handle receiving MSG_IM_KBD_SCAN,
 *			cl = xState1, ch = xState2, dl = LED state,
 *			dh = KbdTypematics, esSi = current down element
 *			(NULL if none)
 * DR_KBD_SET_KBD_STATE	pass ah = KbdStateFunctions, bx = handle,
 *			cl = ToggleState, ch = KbdTypematics
 * DR_KBD_XLATE_SCAN	pass cx = scan code; return di = MSG_META_KBD_CHAR,
 *			cx, dx, bp = its data, al = flags (bit 0: data,
 *			bit 1: more to come)
 * DR_KBD_ADD_HOTKEY	pass ah = ShiftState, cx = character,
 *			^lbx:si = object, bp = message
 * DR_KBD_REMOVE_HOTKEY	pass ah = ShiftState, cx = character
 * DR_KBD_CHANGE_OUTPUT	pass bx = new handle; return bx = previous one
 * DR_KBD_MAP_KEY	pass dsSi = KeyMapStruct to fill, al = scan code
 * DR_KBD_CHECK_SHORTCUT pass cx, dx, bp as MSG_META_KBD_CHAR,
 *			dsSi = KeyboardShortcut table, ax = its length;
 *			return si = index, carry if found
 * DR_KBD_PASS_HOTKEY, DR_KBD_CANCEL_HOTKEY
 */
#define DR_KBD_GET_KBD_STATE	8
#define DR_KBD_SET_KBD_STATE	10
#define DR_KBD_XLATE_SCAN	12
#define DR_KBD_ADD_HOTKEY	14
#define DR_KBD_REMOVE_HOTKEY	16
#define MIN_NON_EXCLUSIVE	18
#define DR_KBD_CHANGE_OUTPUT	18
#define DR_KBD_MAP_KEY		20
#define DR_KBD_CHECK_SHORTCUT	22
#define DR_KBD_PASS_HOTKEY	24
#define DR_KBD_CANCEL_HOTKEY	26

#endif /* __KBDDR_H */
