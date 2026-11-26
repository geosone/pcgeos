/*
 * keyboardUtils.c -- after keyboardUtils.asm: Contains keyboard driver
 * utility routines
 */
#include "keyboard.h"
#include <string.h>
#include <initfile.h>
#include <ghal.h>

/*
 * IsCharOnKey (keyboardUtils.asm): can the key of the scan code make
 * the character (a KeyboardShortcut: the set's low nibble above it)?
 * *mods: the modifiers it takes (KMS_* << 4); as in the asm they are
 * left as the search ended if it is not on the key.  *type: the key's
 * KeyDataFlags.
 */
static Boolean IsCharOnKey(word ax, byte scan, byte *mods, byte *type)
{
    byte al = (byte) ax, ah = (byte) ((ax >> 8) & 0x0f);
    byte dl = 0, bh, t;
    const KeyDef *kd;
    const byte *ext;
    int i;

    *mods = 0;
    *type = 0;
    if (!scan || scan > KBD_NUM_KEYDEFS)
	return FALSE;
    kd = &kbd.KV_keyDefs[scan - 1];
    bh = kd->KD_keyType;
    *type = bh;
    if (kd->KD_char != al) {
	if (kd->KD_shiftChar != al) {
	    if (!(bh & KD_EXTENDED))
		return FALSE;
	    ext = (const byte *) &kbdKeyboardMap.KBM_extDefTab[kd->KD_extEntry];
	    dl = KMS_CTRL | (KMS_CTRL << 4);
	    for (i = 0; i < NUM_EXTENDED_CHARS; i++) {
		if (ext[2 + i] == al)
		    break;
		dl = (byte) (dl + 0x11);
	    }
	    *mods = dl;
	    if (i == NUM_EXTENDED_CHARS)
		return FALSE;
	    /* a virtual character is in another set than a printable */
	    if (ext[0] & bitTable[dl & 0x07] ? ah == (CS_BSW & 0x0f)
					     : ah != (CS_BSW & 0x0f))
		return FALSE;
	    *mods = (byte) (dl & 0xf0);
	    return TRUE;
	}
	dl = KMS_SHIFT | (KMS_SHIFT << 4);
    }
    *mods = dl;
    t = (byte) (bh & KD_TYPE);
    if (ah == (CS_BSW & 0x0f)) {
	if (t != KEY_ALPHA && t != KEY_NONALPHA)
	    return FALSE;
    } else if (t != KEY_MISC && (t == KEY_ALPHA || t == KEY_NONALPHA))
	return FALSE;
    *mods = (byte) (dl & 0xf0);
    return TRUE;
}
#define KCS_NUMLOCK_FLAG	0x40	/* in the CharFlags, unused there */

/*
 * KbdCheckShortcut (DR_KBD_CHECK_SHORTCUT): is the key press (cx, dx,
 * bp as MSG_META_KBD_CHAR) one of the ax KeyboardShortcuts at dsSi?
 * TRUE and si the offset of the entry if so.  A shortcut matches by
 * character and modifiers (those not used up to make the character),
 * or with KS_PHYSICAL by key: the character is on the key pressed and
 * the modifiers are the ones listed plus those the character takes.
 */
Boolean KbdCheckShortcut(DriverRegs *r)
{
    word count = (word) r->DR_ax, ax = (word) r->DR_cx;
    byte shift = (byte) ((word) r->DR_dx >> 8);
    byte toggle = (byte) r->DR_bp, scan = (byte) ((word) r->DR_bp >> 8);
    word bp = (word) (((word) r->DR_dx & 0xff) | (shift << 8));
    const word *tab = r->DR_dsSi;
    byte ah, mods, type, bl;
    word di, i;

    if ((ax >> 8) == CS_UI_FUNCS)
	return FALSE;
    /* the modifiers that are down */
    ah = (byte) (((ax >> 8) & 0x0f) | ((KMS_SHIFT | KMS_CTRL | KMS_ALT) << 4));
    if (!(shift & SHIFT_KEYS) && !(toggle & TOGGLE_SHIFTSTICK))
	ah &= (byte) ~(KMS_SHIFT << 4);
    if (!(shift & CTRL_KEYS) && !(toggle & TOGGLE_CTRLSTICK))
	ah &= (byte) ~(KMS_CTRL << 4);
    if (!(shift & ALT_KEYS) && !(toggle & TOGGLE_ALTSTICK))
	ah &= (byte) ~(KMS_ALT << 4);
    /* and those the character itself takes on this key */
    (void) IsCharOnKey((word) ((ah << 8) | (ax & 0xff)), scan, &mods, &type);
    if ((toggle & TOGGLE_CAPSLOCK) && (type & KD_TYPE) == KEY_ALPHA)
	mods ^= (byte) (KMS_SHIFT << 4);
    if (toggle & TOGGLE_NUMLOCK)
	bp |= KCS_NUMLOCK_FLAG;
    ah |= mods;
    di = (word) ((ah << 8) | (ax & 0xff));
    bl = ah;

    for (i = 0; i < count; i++) {
	word e = tab[i];

	if (!(e & KS_PHYSICAL)) {
	    if (e != di)
		continue;
	} else {
	    if (!IsCharOnKey(e, scan, &mods, &type))
		continue;
	    mods |= (byte) ((e >> 8) & 0x7f);
	    if (mods != bl)
		continue;
	    /* a key of the numeric keypad, unmodified, with <NumLock>
	     * means the digit, not what the shortcut names */
	    if ((type & KD_TYPE) == KEY_PAD && !(bp & CF_EXTENDED) &&
		!(bp & ((CTRL_KEYS | ALT_KEYS | SHIFT_KEYS) << 8)) &&
		(bp & KCS_NUMLOCK_FLAG))
		continue;
	}
	r->DR_si = (word) (i * sizeof(word));
	return TRUE;
    }
    return FALSE;
}
