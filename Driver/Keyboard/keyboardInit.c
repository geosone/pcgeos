/*
 * keyboardInit.c -- starting and ending the C keyboard driver (after
 * KbdInitFar, KbdSetOptions, CheckKeyboardOption and KbdExitFar of
 * keyboardInit.asm).
 *
 * DR_INIT takes the keys of the host (ghal_input, through the interrupt
 * source GHAL_IRQ_INPUT) where DOS16 caught IRQ 1, and reads the options
 * of [keyboard] in the init file:
 *   keyboardTypematic     delay and rate (kept; the host repeats keys)
 *   keyboardDoesLEDs      the keyboard has LEDs
 *   keyboardAltGr         the right <Alt> is <Alt Gr> (<Ctrl><Alt>)
 *   keyboardShiftRelease  <Shift> ends <Caps Lock>
 *   keyboardSwapCtrl      swap <Ctrl> and <Caps Lock>
 * The toggles start off: DOS16 took them from the BIOS.
 *
 * The driver is a monitor of the input manager at ML_DRIVER
 * (KbdMonRoutine): it gets the scan codes back there and translates
 * them.  ImAddMonitor gives the input manager's thread, the output.
 */
#include "keyboard.h"

#include <initfile.h>
#include <ghal.h>
#include <string.h>

/* CheckKeyboardOption: TRUE (and the option set) if the key is TRUE */
Boolean CheckKeyboardOption(const char *key, byte option)
{
    Boolean b = FALSE;

    if (InitFileReadBoolean(category, key, &b) || !b)
	return FALSE;
    kbd.KV_options |= option;
    return TRUE;
}

void KbdSetOptions(void)
{
    KeyDef *ralt = &kbd.KV_keyDefs[SCANCODE_RIGHT_ALT - 1];
    KeyDef *lctrl = &kbd.KV_keyDefs[SCANCODE_LEFT_CTRL - 1];
    KeyDef *caps = &kbd.KV_keyDefs[SCANCODE_CAPS_LOCK - 1];
    KeyDef t;

    if (CheckKeyboardOption("keyboardAltGr", KO_ALT_GR)) {
	/* the right <Alt> acts as <Ctrl><Alt> and gives <Alt Gr> */
	ralt->KD_char = VC_ALT_GR;
	ralt->KD_shiftChar = RCTRL | RALT;
    }
    CheckKeyboardOption("keyboardShiftRelease", KO_SHIFT_RELEASE);
    if (!(kbd.KV_options & KO_SWAP_CTRL) &&
	CheckKeyboardOption("keyboardSwapCtrl", KO_SWAP_CTRL)) {
	t = *lctrl;
	*lctrl = *caps;
	*caps = t;
    }
}
