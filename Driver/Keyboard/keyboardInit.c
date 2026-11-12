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

static const char category[] = "keyboard";
static ghal_irq_source *irq;
static Monitor kbdMonitor;

/* CheckKeyboardOption: TRUE (and the option set) if the key is TRUE */
static Boolean CheckKeyboardOption(const char *key, byte option)
{
    Boolean b = FALSE;

    if (InitFileReadBoolean(category, key, &b) || !b)
	return FALSE;
    kbd.KV_options |= option;
    return TRUE;
}

static void KbdSetOptions(void)
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

Boolean KbdInit(void)
{
    word w;
    word n = kbdKeyboardMap.KBM_maxScan;

    if (irq)
	return TRUE;			/* KBD_NESTED_INIT */
    memset(&kbd, 0, sizeof(kbd));
    kbd.KV_outputHandle = 0xffff;	/* no process yet */
    if (n > KBD_NUM_KEYDEFS)
	n = KBD_NUM_KEYDEFS;
    memcpy(kbd.KV_keyDefs, kbdKeyboardMap.KBM_keyDefTab, n * sizeof(KeyDef));
    CheckKeyboardOption("keyboardDoesLEDs", KO_DOES_LEDS);
    if (!InitFileReadInteger(category, "keyboardTypematic", &w))
	kbd.KV_typematicState = (byte) (w & 0x7f);
    KbdSetOptions();
    kbd.KV_outputHandle = ImAddMonitor(&kbdMonitor, ML_DRIVER,
				       KbdMonRoutine);
    if (ghal_irq_attach(GHAL_IRQ_INPUT, KbdInputHandler, NULL, &irq) !=
	GHAL_OK) {
	irq = NULL;
	ImRemoveMonitor(&kbdMonitor, MF_REMOVE_IMMEDIATE);
	return TRUE;
    }
    return FALSE;
}

void KbdExit(void)
{
    kbd.KV_shiftState = 0;		/* no modifiers down */
    if (irq)
	ghal_irq_detach(irq);
    irq = NULL;
    ImRemoveMonitor(&kbdMonitor, MF_REMOVE_IMMEDIATE);
}
