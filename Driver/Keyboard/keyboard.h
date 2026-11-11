/*
 * keyboard.h -- the C keyboard driver: constants of keyboardGeode.def and
 * keyboardConstant.def, the variables of keyboardVariable.def.
 *
 * The C driver (keyboardProcess.c, keyboardInit.c) keeps the asm driver's
 * design: the "interrupt" turns key events into PC scan codes (set 1, as
 * the 8042 gave them) and sends them to the output as MSG_IM_KBD_SCAN;
 * DR_KBD_XLATE_SCAN turns a scan code into a character with the keyboard
 * map (kmap*.def, made C by Port32/tools/kmapgen.py).  The key events come
 * from ghal_input (HID usages) instead of port 60h.  SBCS.
 */
#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <geos.h>
#include <char.h>
#include <input.h>
#include <localize.h>
#include <Internal/kbdMap.h>
#include <Internal/kbdDr.h>
#include <Internal/im.h>
#include <geosdriver.h>

/* keyboardGeode.def */
#define TOGGLE_CAPSLOCK		TS_CAPSLOCK
#define TOGGLE_NUMLOCK		TS_NUMLOCK
#define TOGGLE_SCROLLLOCK	TS_SCROLLLOCK
#define TOGGLE_SHIFTSTICK	TS_SHIFTSTICK
#define TOGGLE_CTRLSTICK	TS_CTRLSTICK
#define TOGGLE_ALTSTICK		TS_ALTSTICK
#define TOGGLE_FNCTSTICK	TS_FNCTSTICK
#define TOGGLE_STICKS		(TOGGLE_SHIFTSTICK | TOGGLE_CTRLSTICK | \
				 TOGGLE_ALTSTICK | TOGGLE_FNCTSTICK)

#define LALT		SS_LALT
#define RALT		SS_RALT
#define LCTRL		SS_LCTRL
#define RCTRL		SS_RCTRL
#define LSHIFT		SS_LSHIFT
#define RSHIFT		SS_RSHIFT
#define FIREBUTTON1	SS_FIRE_BUTTON_1
#define FIREBUTTON2	SS_FIRE_BUTTON_2
#define LFUNCT		SS_FIRE_BUTTON_1
#define RFUNCT		SS_FIRE_BUTTON_2

#define ALT_KEYS	(LALT | RALT)
#define CTRL_KEYS	(LCTRL | RCTRL)
#define SHIFT_KEYS	(LSHIFT | RSHIFT)

/* keyboardConstant.def */
#define SCANCODE_LEFT_CTRL	0x1d
#define SCANCODE_CAPS_LOCK	0x3a
#define SCANCODE_RIGHT_ALT	0x76

/* KeyboardOptions */
#define KO_FORCE_XT		0x40
#define KO_FORCE_AT		0x20
#define KO_DOES_LEDS		0x10
#define KO_ALT_GR		0x04
#define KO_SHIFT_RELEASE	0x02
#define KO_SWAP_CTRL		0x01

#define EXT_LSHIFT_PRESS	0xe02a
#define EXT_RSHIFT_PRESS	0xe036
#define EXT_LCTRL_PRESS		0xe11d

#define MAX_KEYS_DOWN		12
#define KEY_RELEASE		0x80

typedef struct {
    word	KDE_charValue;		/* <CharacterSet><char> */
    byte	KDE_charFlags;		/* CharFlags */
    byte	KDE_shiftState;		/* ShiftState */
    byte	KDE_toggleState;	/* ToggleState */
    byte	KDE_scanCode;		/* 0: the element is free */
    byte	KDE_xState1;
    byte	KDE_xState2;
} KeyDownElement;

/* keyboardVariable.def */
typedef struct {
    word		KV_outputHandle;	/* gets MSG_IM_KBD_SCAN */
    byte		KV_shiftState;		/* ShiftState */
    byte		KV_toggleState;		/* ToggleState */
    byte		KV_xState1, KV_xState2;
    byte		KV_modeIndState;	/* LEDs */
    byte		KV_typematicState;	/* KbdTypematics */
    byte		KV_options;		/* KeyboardOptions */
    byte		KV_accentPending;	/* accent char, 0 if none */
    byte		KV_accentOffset;	/* its index in the table */
    KeyDownElement	*KV_curDownElement;
    KeyDownElement	KV_keysDown[MAX_KEYS_DOWN];
    /* the map's KeyDefs, writable (the options change them) */
    KeyDef		KV_keyDefs[KBD_NUM_KEYDEFS];
} KbdVars;

extern KbdVars kbd;
/* the keyboard map, generated from kmap*.def */
extern const KeyboardMap kbdKeyboardMap;

/* keyboardProcess.c */
Boolean KbdXlateScan(word scan, DriverRegs *r);
void KbdScanEvent(word scan);
/* keyboardInit.c */
Boolean KbdInit(void);
void KbdExit(void);
/* the HID key events of ghal_input as scan codes (keyboardInput.c) */
void KbdInputHandler(void *ctx);

#endif /* KEYBOARD_H */
