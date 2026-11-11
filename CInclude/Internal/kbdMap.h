/***********************************************************************
 *
 *	Copyright (c) GeoWorks 1990 -- All Rights Reserved
 *
 * PROJECT:	PC GEOS
 * FILE:	kbdMap.h
 *
 * DESCRIPTION:
 *	C form of Include/Internal/kbdMap.def: what a keyboard map looks
 *	like (SBCS).  The maps themselves (Driver/Keyboard/kmap*.def) are
 *	turned into C by Port32/tools/kmapgen.py.
 *
 ***********************************************************************/
#ifndef __KBDMAP_H
#define __KBDMAP_H

#include <geos.h>

/* KeyTypeFlag */
#define KEY_INVALID	0x00	/* results in VC_INVALID */
#define KEY_SOLO	0x01	/* charValue = <shiftChar><char> */
#define KEY_NONALPHA	0x02	/* <0><char>, <0><shiftChar>; no CAPSLOCK */
#define KEY_ALPHA	0x03	/* as NONALPHA; CAPSLOCK xors shift */
#define KEY_PAD		0x08	/* <ff><char>; NUMLOCK xors shift */
#define KEY_SHIFT	0x09	/* <ff><char>; shiftChar: ShiftState bits */
#define KEY_TOGGLE	0x0a	/* <ff><char>; shiftChar: ToggleState bits */
#define KEY_XSHIFT	0x0b	/* <ff><char>; shiftChar: xState1 bits */
#define KEY_XTOGGLE	0x0c	/* <ff><char>; shiftChar: xState1 bits */
#define KEY_MISC	0x0d	/* as NONALPHA, for virtual characters */
#define KEY_SHIFT_STICK	0x0e	/* <ff><char>; shiftChar: shift to stick */

#define MIN_VIRTUAL_KEY_TYPE	KEY_PAD	/* less than is ASCII */

/* ExtVirtualBits: which extended entries are virtual or accents */
#define EV_KEY			0x01
#define EV_SHIFT		0x02
#define EV_CTRL			0x04
#define EV_SHIFT_CTRL		0x08
#define EV_ALT			0x10
#define EV_SHIFT_ALT		0x20
#define EV_CTRL_ALT		0x40
#define EV_SHIFT_CTRL_ALT	0x80

/* KeyDataFlags */
#define KD_STATE_KEY	0x80	/* no repeats, keeps the temp accent */
#define KD_EXTENDED	0x40	/* KD_extEntry is an extended entry */
#define KD_ACCENT	0x20	/* the key is an accent */
#define KD_ACCENTABLE	0x10	/* the key can be accented */
#define KD_TYPE		0x0f	/* KeyTypeFlag */
#define KD_SET_LED	KD_ACCENTABLE	/* TOGGLE and XTOGGLE: set the LED */

/* ExtOffsets: index into an ExtendedDef */
#define EXT_SHIFT_MASK	0x01
#define EXT_CTRL_MASK	0x02
#define EXT_ALT_MASK	0x04

#define KBD_NUM_RESERVED_BYTES	11
#define KBD_NUM_EXTSCANCODES	4
#define KBD_NUM_EXTSCANMAPS	16
#define KBD_NUM_ACCENTS		8
#define KBD_NUM_ACCENTABLES	16
#define KBD_NUM_KEYDEFS		0x79
#define KBD_NUM_EXTDEFS		56
#define NUM_EXTENDED_CHARS	6

typedef struct {
    byte	KD_keyType;	/* KeyDataFlags */
    byte	KD_char;	/* unshifted key */
    byte	KD_shiftChar;	/* shifted key */
    byte	KD_extEntry;	/* # of extended entry, if any */
} KeyDef;

typedef struct {
    byte	EDD_charSysFlags;	/* ExtVirtualBits: virtual */
    byte	EDD_charAccents;	/* ExtVirtualBits: accents */
    byte	EDD_ctrlChar;
    byte	EDD_shiftCtrlChar;
    byte	EDD_altChar;
    byte	EDD_shiftAltChar;
    byte	EDD_ctrlAltChar;
    byte	EDD_shiftCtrlAltChar;
} ExtendedDef;

typedef struct {
    byte	AD_accentedValue[KBD_NUM_ACCENTS];
} AccentDef;

/* KbdConvFlags */
#define KCF_KBD_HANDLES_TOGGLES	0x0001

typedef struct {
    word	EMD_extScanCode;	/* extended scan code */
    byte	EMD_mappedScanCode;	/* remapped scan code */
} ExtendedScanDef;

/*
 * A keyboard map: the tables of KeyboardTable, as separate arrays.
 * KBM_maxScan is the number of KeyDefs (KBD_MAX_SCAN).
 */
typedef struct {
    word			KBM_tableID;
    word			KBM_keyboardType;
    word			KBM_flags;		/* KbdConvFlags */
    const byte			*KBM_extScanCodes;	/* [4] */
    const ExtendedScanDef	*KBM_extScanTab;	/* [16] */
    const KeyDef		*KBM_keyDefTab;		/* [KBM_maxScan] */
    word			KBM_maxScan;
    const ExtendedDef		*KBM_extDefTab;
    const byte			*KBM_accentables;	/* [16] */
    const AccentDef		*KBM_accentTab;		/* [16] */
} KeyboardMap;

#endif /* __KBDMAP_H */
