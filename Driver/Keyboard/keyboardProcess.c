/*
 * keyboardProcess.c -- the keyboard driver's strategy routine and the
 * translation of scan codes into characters (after keyboardProcess.asm:
 * KbdStrategy, KbdSetState, KbdGetState, KbdChangeOutput, KbdXlateScan,
 * ProcessKeyElement, ConvertExtCodes, FindScanCode, ProcessScanCode,
 * HandleExtendedDef, HandleNormalDef, ProcessStateRelease,
 * AccentTranslation).  SBCS; the PC hardware type.
 *
 * As in the asm driver, the "interrupt" (keyboardInput.c) only sends scan
 * codes to the output (MSG_IM_KBD_SCAN), and the input manager calls
 * DR_KBD_XLATE_SCAN with each to get the character.  The tables are those
 * of the keyboard map (kbdKeyboardMap, from kmap*.def); the driver keeps
 * the state: modifiers, toggles, the keys that are down, the accent.
 *
 * Not in the C driver (nothing on the host to talk to): the BIOS shift
 * and toggle bytes (KeyboardTrackBios*), the LEDs and the typematic rate
 * of the 8042 (SetIndicatorState, SetTypematicState: the states are kept,
 * the host repeats keys itself).  Not yet: hotkeys (DR_KBD_ADD_HOTKEY,
 * ...), DR_KBD_MAP_KEY, DR_KBD_CHECK_SHORTCUT.
 */
#include "keyboard.h"

#include <string.h>

KbdVars kbd;

static const byte bitTable[8] = { 0x01, 0x02, 0x04, 0x08,
				  0x10, 0x20, 0x40, 0x80 };

/* the LEDs: the host has none to set (SetIndicatorState) */
static void SetIndicatorState(void)
{
}

static void ClearSticks(void)
{
    kbd.KV_toggleState &= (byte) ~TOGGLE_STICKS;
    kbd.KV_modeIndState &= (byte) ~TOGGLE_STICKS;
}

/* 16-bit scan code to 8 bits; FALSE for an extended shift (ignored) */
static Boolean ConvertExtCodes(word *ax)
{
    byte release = (byte) (*ax & KEY_RELEASE);
    word code = (word) (*ax & ~KEY_RELEASE);
    int i;

    if (code == EXT_LSHIFT_PRESS || code == EXT_RSHIFT_PRESS)
	return FALSE;
    if (code == EXT_LCTRL_PRESS && (kbd.KV_options & KO_SWAP_CTRL))
	code = SCANCODE_CAPS_LOCK;
    for (i = 0; i < KBD_NUM_EXTSCANMAPS; i++)
	if (kbdKeyboardMap.KBM_extScanTab[i].EMD_extScanCode == code) {
	    code = (word) ((code & 0xff00) |
			   kbdKeyboardMap.KBM_extScanTab[i].EMD_mappedScanCode);
	    break;
	}
    *ax = (word) (code | release);
    return TRUE;
}

/* the element of a key that is down, or a free one (*empty), or none */
static KeyDownElement *FindScanCode(byte scan, KeyDownElement **empty)
{
    int i;

    *empty = NULL;
    for (i = 0; i < MAX_KEYS_DOWN; i++) {
	KeyDownElement *e = &kbd.KV_keysDown[i];
	if (e->KDE_scanCode == scan)
	    return e;
	if (!e->KDE_scanCode && !*empty)
	    *empty = e;
    }
    return NULL;
}

/* an accent of the map's accent row; -1 if none */
static int AccentIndex(byte c)
{
    int i;

    for (i = 0; i < KBD_NUM_ACCENTS; i++)
	if (kbdKeyboardMap.KBM_accentTab[0].AD_accentedValue[i] == c)
	    return i;
    return -1;
}

/*
 * HandleExtendedDef: the <ctrl>, <alt> and <ctrl><alt> cases of a key with
 * an extended entry.  TRUE: translated, *ax the character, *ch the
 * modifiers used.  FALSE: *ah and *bh the character sets of the base and
 * shifted case.
 */
static Boolean HandleExtendedDef(const KeyDef *kd, KeyDownElement *elem,
				 word *ax, byte *ah, byte *bh, byte *ch)
{
    const ExtendedDef *ext = &kbdKeyboardMap.KBM_extDefTab[kd->KD_extEntry];
    const byte *bytes = (const byte *) ext;
    word state = (word) (kbd.KV_shiftState | (kbd.KV_toggleState << 8));
    byte bl = 0, cl, c;

    if (state & (SHIFT_KEYS | (TOGGLE_SHIFTSTICK << 8)))
	bl |= EXT_SHIFT_MASK;
    if (state & (CTRL_KEYS | (TOGGLE_CTRLSTICK << 8)))
	bl |= EXT_CTRL_MASK;
    if (state & (ALT_KEYS | (TOGGLE_ALTSTICK << 8)))
	bl |= EXT_ALT_MASK;
    cl = bitTable[bl];
    c = bytes[bl];		/* ctrl .. shiftCtrlAlt from byte 2 on */
    if (bl > EXT_SHIFT_MASK && c) {
	byte set = VC_ISANSI;
	if (ext->EDD_charAccents & cl) {
	    int i = AccentIndex(c);
	    if (i >= 0) {
		kbd.KV_accentPending = c;
		kbd.KV_accentOffset = (byte) i;
		elem->KDE_charFlags |= CF_TEMP_ACCENT;
	    }
	}
	if (ext->EDD_charSysFlags & cl)
	    set = VC_ISCTRL;
	*ch |= kbd.KV_shiftState;
	*ax = (word) ((set << 8) | c);
	return TRUE;
    }
    *ah = (ext->EDD_charSysFlags & EV_KEY) ? 0xff : 0;
    *bh = (ext->EDD_charSysFlags & EV_SHIFT) ? 0xff : 0;
    return FALSE;
}

/* HandleNormalDef: the <alt> value of a key without an extended entry */
static Boolean HandleNormalDef(const KeyDef *kd, byte type, word *ax,
			       byte *ah, byte *bh, byte *ch)
{
    *ah = *bh = (byte) (type >= MIN_VIRTUAL_KEY_TYPE ? 0xff : 0);
    if (kd->KD_keyType & KD_ACCENT)
	*ah = 0xff;
    if (!(kd->KD_keyType & KD_EXTENDED))
	return FALSE;
    if ((kbd.KV_shiftState & CTRL_KEYS) ||
	(kbd.KV_toggleState & TOGGLE_CTRLSTICK))
	return FALSE;
    if (!(kbd.KV_shiftState & ALT_KEYS))
	return FALSE;
    *ch |= ALT_KEYS;
    *ax = (word) ((*ah << 8) | kd->KD_extEntry);
    return TRUE;
}

/* a toggle changed: the LED with it, if the key says so */
static void HandleToggle(const KeyDef *kd, byte bits)
{
    if (kd->KD_keyType & KD_SET_LED) {
	kbd.KV_modeIndState ^= bits;
	SetIndicatorState();
    }
}

/* a modifier key is down (PSC_CaseShift) */
static void CaseShift(byte bits)
{
    kbd.KV_shiftState |= bits;
    if ((kbd.KV_options & KO_SHIFT_RELEASE) && (bits & SHIFT_KEYS)) {
	/* the European way: <Shift> ends <Caps Lock> */
	kbd.KV_toggleState &= (byte) ~TOGGLE_CAPSLOCK;
	kbd.KV_modeIndState &= (byte) ~TOGGLE_CAPSLOCK;
	SetIndicatorState();
    }
}

/*
 * ProcessScanCode: the character of a new press.  Returns it
 * (<CharacterSet><char>); *ch gets the modifiers used to make it.
 */
static word ProcessScanCode(Boolean extended, KeyDownElement *elem,
			    const KeyDef *kd, byte *ch)
{
    byte keyType = kd->KD_keyType;
    byte type = (byte) (keyType & KD_TYPE);
    byte ah = 0, bh = 0, cl = SHIFT_KEYS;
    word ax, bx, t;

    if (extended)
	elem->KDE_charFlags |= CF_EXTENDED;
    *ch = 0;
    if (keyType & KD_STATE_KEY)
	elem->KDE_charFlags |= CF_STATE_KEY;
    if (keyType & KD_EXTENDED) {
	if (HandleExtendedDef(kd, elem, &ax, &ah, &bh, ch)) {
	    ClearSticks();
	    return ax;
	}
    } else if (HandleNormalDef(kd, type, &ax, &ah, &bh, ch))
	return ax;

    ax = (word) ((ah << 8) | kd->KD_char);
    bx = (word) ((bh << 8) | kd->KD_shiftChar);
    switch (type) {
    case KEY_ALPHA:
	if (kbd.KV_toggleState & TOGGLE_CAPSLOCK) {
	    t = ax; ax = bx; bx = t;
	}
	goto shift;
    case KEY_PAD:
	if (!(elem->KDE_charFlags & CF_EXTENDED) &&
	    (kbd.KV_toggleState & TOGGLE_NUMLOCK)) {
	    t = ax; ax = bx; bx = t;
	}
	goto shift;
    case KEY_NONALPHA:
    case KEY_MISC:
    shift:
	if ((kbd.KV_toggleState & TOGGLE_SHIFTSTICK) ||
	    (kbd.KV_shiftState & cl)) {
	    t = ax; ax = bx; bx = t;
	}
	ClearSticks();
	/* the same with and without <Shift>: <Shift> stays a modifier
	 * (<Shift>-space as a shortcut) */
	if (ax != bx)
	    *ch |= cl;
	return ax;
    case KEY_SOLO:
	ClearSticks();
	return (word) ((kd->KD_shiftChar << 8) | kd->KD_char);
    case KEY_SHIFT_STICK:
	if (kd->KD_char == VC_LSHIFT) {
	    kbd.KV_toggleState ^= TOGGLE_SHIFTSTICK;
	    kbd.KV_modeIndState ^= TOGGLE_SHIFTSTICK;
	} else if (kd->KD_char == VC_LCTRL) {
	    kbd.KV_toggleState ^= TOGGLE_CTRLSTICK;
	    kbd.KV_modeIndState ^= TOGGLE_CTRLSTICK;
	} else {
	    kbd.KV_toggleState ^= TOGGLE_ALTSTICK;
	    kbd.KV_modeIndState ^= TOGGLE_ALTSTICK;
	}
	/* FALLTHROUGH */
    case KEY_SHIFT:
	CaseShift(kd->KD_shiftChar);
	return ax;
    case KEY_TOGGLE:
	kbd.KV_toggleState ^= kd->KD_shiftChar;
	HandleToggle(kd, kd->KD_shiftChar);
	return ax;
    case KEY_XSHIFT:
	kbd.KV_xState1 |= kd->KD_shiftChar;
	return ax;
    case KEY_XTOGGLE:
	kbd.KV_xState1 ^= kd->KD_shiftChar;
	HandleToggle(kd, kd->KD_shiftChar);
	return ax;
    default:
	return (word) ((0xff << 8) | VC_INVALID_KEY);
    }
}

/* ProcessStateRelease: a modifier or toggle key goes up */
static void ProcessStateRelease(const KeyDef *kd)
{
    byte type = (byte) (kd->KD_keyType & KD_TYPE);
    byte bits = kd->KD_shiftChar;

    if (kbdKeyboardMap.KBM_flags & KCF_KBD_HANDLES_TOGGLES) {
	if (type == KEY_TOGGLE) {
	    kbd.KV_toggleState ^= bits;
	    HandleToggle(kd, bits);
	    return;
	}
	if (type == KEY_XTOGGLE) {
	    kbd.KV_xState1 ^= bits;
	    HandleToggle(kd, bits);
	    return;
	}
    }
    if (type == KEY_SHIFT || type == KEY_SHIFT_STICK)
	kbd.KV_shiftState &= (byte) ~bits;
    else if (type == KEY_XSHIFT)
	kbd.KV_xState1 &= (byte) ~bits;
}

/* AccentTranslation: an accent pending makes the next key accented; an
 * accent key makes one pending */
static void AccentTranslation(KeyDownElement *elem, const KeyDef *kd)
{
    byte pending = kbd.KV_accentPending;
    byte c = (byte) elem->KDE_charValue;
    int i;

    if (pending && (kd->KD_keyType & (KD_ACCENTABLE | KD_ACCENT))) {
	if (c == pending) {		/* the accent twice: the accent */
	    elem->KDE_charValue = (word) ((elem->KDE_charValue & 0xff00) |
					  pending);
	    kbd.KV_accentPending = 0;
	    return;
	}
	for (i = 0; i < KBD_NUM_ACCENTABLES; i++)
	    if (kbdKeyboardMap.KBM_accentables[i] == c)
		break;
	if (i < KBD_NUM_ACCENTABLES) {
	    byte v = kbdKeyboardMap.KBM_accentTab[i].
			AD_accentedValue[kbd.KV_accentOffset];
	    if (v) {
		elem->KDE_charValue = (word) ((elem->KDE_charValue & 0xff00) |
					      v);
		kbd.KV_accentPending = 0;
		return;
	    }
	}
    }
    if (elem->KDE_charFlags & CF_STATE_KEY)
	return;
    /* <Shift> may be down for an accent, nothing else */
    if ((elem->KDE_shiftState & (byte) ~SHIFT_KEYS) ||
	!(kd->KD_keyType & KD_ACCENT)) {
	kbd.KV_accentPending = 0;
	return;
    }
    i = AccentIndex(c);
    if (i >= 0) {
	kbd.KV_accentPending = c;
	kbd.KV_accentOffset = (byte) i;
	elem->KDE_charFlags |= CF_TEMP_ACCENT;
    }
}

/*
 * ProcessKeyElement: put a scan code into the list of keys that are down,
 * making the character on a new press.  NULL for a scan code that is not
 * a key event (an extended shift, a release of a key not down, the list
 * full).
 */
static KeyDownElement *ProcessKeyElement(word ax, const KeyDef **kdp)
{
    KeyDownElement *elem, *empty;
    const KeyDef *kd;
    Boolean extended;
    byte scan, ch;

    if ((ax >> 8) && !ConvertExtCodes(&ax))
	goto error;
    extended = (ax >> 8) != 0;
    scan = (byte) (ax & ~KEY_RELEASE & 0xff);
    if (!scan || scan > kbdKeyboardMap.KBM_maxScan)
	goto error;
    kd = &kbd.KV_keyDefs[scan - 1];
    *kdp = kd;
    elem = FindScanCode(scan, &empty);
    if (elem) {
	if (ax & KEY_RELEASE) {
	    elem->KDE_charFlags = (byte) ((elem->KDE_charFlags &
					   ~(CF_FIRST_PRESS | CF_REPEAT_PRESS))
					  | CF_RELEASE);
	    ProcessStateRelease(kd);
	} else
	    elem->KDE_charFlags = (byte) ((elem->KDE_charFlags &
					   ~CF_FIRST_PRESS) | CF_REPEAT_PRESS);
    } else {
	if ((ax & KEY_RELEASE) || !empty)
	    goto error;
	elem = empty;
	elem->KDE_scanCode = scan;
	elem->KDE_charFlags = CF_FIRST_PRESS;
	elem->KDE_charValue = ProcessScanCode(extended, elem, kd, &ch);
	elem->KDE_shiftState = (byte) (kbd.KV_shiftState & ~ch);
	elem->KDE_toggleState = kbd.KV_toggleState;
	elem->KDE_xState1 = kbd.KV_xState1;
	elem->KDE_xState2 = kbd.KV_xState2;
    }
    kbd.KV_curDownElement = elem;
    return elem;
error:
    kbd.KV_curDownElement = NULL;
    return NULL;
}

/*
 * KbdXlateScan (DR_KBD_XLATE_SCAN): returns di = MSG_META_KBD_CHAR with
 * cx = character, dl = CharFlags, dh = ShiftState, bp low = ToggleState,
 * bp high = scan code; al = 1 if there is a character.  A state key held
 * down (repeat) gives none.
 */
Boolean KbdXlateScan(word scan, DriverRegs *r)
{
    const KeyDef *kd = NULL;
    KeyDownElement *elem = ProcessKeyElement(scan, &kd);
    byte flags;

    r->DR_ax &= ~0xffu;			/* al = 0: no data */
    if (!elem)
	return FALSE;
    if ((elem->KDE_charFlags & CF_FIRST_PRESS) &&
	!(elem->KDE_charFlags & CF_TEMP_ACCENT))
	AccentTranslation(elem, kd);
    flags = elem->KDE_charFlags;
    r->DR_cx = elem->KDE_charValue;
    r->DR_dx = (word) ((elem->KDE_shiftState << 8) | flags);
    r->DR_bp = (word) ((elem->KDE_scanCode << 8) | elem->KDE_toggleState);
    r->DR_si = 0;
    if (flags & CF_RELEASE)
	elem->KDE_scanCode = 0;		/* the element is free */
    if ((flags & CF_STATE_KEY) && (flags & CF_REPEAT_PRESS))
	return FALSE;
    r->DR_di = IM_META_KBD_CHAR;
    r->DR_ax |= 1;			/* MF_DATA */
    return FALSE;
}

/* KbdMonRoutine: the scan codes in the input manager's chain */
byte KbdMonRoutine(Monitor *mon, ImEvent *ev)
{
    DriverRegs r;

    (void) mon;
    if (ev->IE_message != MSG_IM_KBD_SCAN)
	return MF_DATA;
    memset(&r, 0, sizeof(r));
    KbdXlateScan(ev->IE_cx, &r);
    if (!(r.DR_ax & MF_DATA))
	return 0;			/* used up */
    ev->IE_message = (Message) r.DR_di;
    ev->IE_cx = (word) r.DR_cx;
    ev->IE_dx = (word) r.DR_dx;
    ev->IE_bp = (word) r.DR_bp;
    ev->IE_si = (Handle) r.DR_si;
    return (byte) (r.DR_ax & (MF_DATA | MF_MORE_TO_DO));
}

/* KbdSetState (DR_KBD_SET_KBD_STATE) */
static void KbdSetState(DriverRegs *r)
{
    byte ah = (byte) (r->DR_ax >> 8);
    byte cl = (byte) r->DR_cx, ch = (byte) (r->DR_cx >> 8);

    if (ah & KSF_NEW_PROCESS)
	kbd.KV_outputHandle = (Handle) r->DR_bx;
    if (ah & KSF_NEW_INDICATOR) {
	byte diff = (byte) (cl ^ kbd.KV_modeIndState);
	kbd.KV_modeIndState = cl;
	kbd.KV_toggleState ^= diff;
	SetIndicatorState();
    }
    if (ah & KSF_NEW_TYPEMATIC)
	kbd.KV_typematicState = ch;	/* the host repeats keys itself */
}

/* KbdGetState (DR_KBD_GET_KBD_STATE) */
static void KbdGetState(DriverRegs *r)
{
    r->DR_ax = (word) ((kbd.KV_toggleState << 8) | kbd.KV_shiftState);
    r->DR_bx = kbd.KV_outputHandle;
    r->DR_cx = (word) ((kbd.KV_xState2 << 8) | kbd.KV_xState1);
    r->DR_dx = (word) ((kbd.KV_typematicState << 8) | kbd.KV_modeIndState);
    r->DR_esSi = kbd.KV_curDownElement;
}

static Boolean KbdStrategy(word function, DriverRegs *r)
{
    Handle old;

    switch (function) {
    case DR_INIT:
	return KbdInit();
    case DR_EXIT:
	KbdExit();
	return FALSE;
    case DR_SUSPEND:
    case DR_UNSUSPEND:
	return FALSE;
    case DR_KBD_GET_KBD_STATE:
	KbdGetState(r);
	return FALSE;
    case DR_KBD_SET_KBD_STATE:
	KbdSetState(r);
	return FALSE;
    case DR_KBD_XLATE_SCAN:
	return KbdXlateScan((word) r->DR_cx, r);
    case DR_KBD_CHANGE_OUTPUT:
	old = kbd.KV_outputHandle;
	kbd.KV_outputHandle = (Handle) r->DR_bx;
	r->DR_bx = old;
	return FALSE;
    case DR_KBD_PASS_HOTKEY:
    case DR_KBD_CANCEL_HOTKEY:
	return FALSE;
    default:
	/* hotkeys, DR_KBD_MAP_KEY, DR_KBD_CHECK_SHORTCUT: not yet */
	return TRUE;
    }
}

DriverInfoStruct DriverTable = {
    DRIVER_STRATEGY(KbdStrategy), 0, DRIVER_TYPE_INPUT
};
