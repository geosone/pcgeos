/*
 * keyboardInput.c -- the keyboard "interrupt" of the C driver (after
 * KbdInterrupt of keyboardProcess.asm, HARDWARE_TYPE PC).
 *
 * DOS16 read scan codes from port 60h.  Here the host's key events come
 * from ghal_input as HID usages (page 7); they become the scan codes of
 * set 1 the 8042 gave, with the 0xe0 extension for the keys of the
 * extended keyboard, so the keyboard maps work unchanged.  Each goes to
 * the output as MSG_IM_KBD_SCAN (cx = extension << 8 | code, 0x80 on a
 * release), queued, as from the interrupt.
 *
 * Not yet: hotkeys (KbdCheckHotkey).
 */
#include "keyboard.h"

#include <object.h>
#include <ghal.h>

/* HID keyboard usage -> set 1 scan code; 0x100 = with 0xe0 */
#define E0(c)	(0x100 | (c))
static const word hidToScan[0xe8] = {
    /* 0x00 */ 0, 0, 0, 0, 0x1e, 0x30, 0x2e, 0x20,
    /* 0x08 */ 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, 0x25, 0x26,
    /* 0x10 */ 0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1f, 0x14,
    /* 0x18 */ 0x16, 0x2f, 0x11, 0x2d, 0x15, 0x2c, 0x02, 0x03,
    /* 0x20 */ 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
    /* 0x28 */ 0x1c, 0x01, 0x0e, 0x0f, 0x39, 0x0c, 0x0d, 0x1a,
    /* 0x30 */ 0x1b, 0x2b, 0x2b, 0x27, 0x28, 0x29, 0x33, 0x34,
    /* 0x38 */ 0x35, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f, 0x40,
    /* 0x40 */ 0x41, 0x42, 0x43, 0x44, 0x57, 0x58, E0(0x37), 0x46,
    /* 0x48 */ 0, E0(0x52), E0(0x47), E0(0x49), E0(0x53), E0(0x4f),
	       E0(0x51), E0(0x4d),
    /* 0x50 */ E0(0x4b), E0(0x50), E0(0x48), 0x45, E0(0x35), 0x37, 0x4a,
	       0x4e,
    /* 0x58 */ E0(0x1c), 0x4f, 0x50, 0x51, 0x4b, 0x4c, 0x4d, 0x47,
    /* 0x60 */ 0x48, 0x49, 0x52, 0x53, 0x56, E0(0x5d), 0, 0,
    /* 0x68 */ [0xe0] = 0x1d, 0x2a, 0x38, E0(0x5b), E0(0x1d), 0x36,
	       E0(0x38), E0(0x5c)
};
#define HID_PAUSE	0x48

/* send a scan code to the output (KINT_ScanCode) */
void KbdScanEvent(word scan)
{
    IMKbdScanParams p;

    if (kbd.KV_outputHandle == 0xffff || !kbd.KV_outputHandle)
	return;				/* no recipient */
    p.IMKS_scanCode = scan;
    p.IMKS_dx = 0;
    p.IMKS_bp = 0;
    p.IMKS_driver = 0;
    CObjMessageP(MF_FORCE_QUEUE, ConstructOptr(kbd.KV_outputHandle, 0),
		 MSG_IM_KBD_SCAN, &p, sizeof(p), NULL, 0);
}

/* the GHAL_IRQ_INPUT handler: the keyboard's events, as an interrupt */
void KbdInputHandler(void *ctx)
{
    ghal_input_event e;
    word s, release;

    (void) ctx;
    while (ghal_input_read(GHAL_INPUT_KEYBOARD, &e)) {
	if (e.page != GHAL_HID_PAGE_KEYBOARD)
	    continue;
	release = e.value ? 0 : KEY_RELEASE;
	if (e.usage == HID_PAUSE) {
	    /* <Pause> gives e1 1d 45 e1 9d c5 on the press, nothing on
	     * the release: <ext-ctrl><Num Lock> */
	    if (!release) {
		KbdScanEvent(0xe11d);
		KbdScanEvent(0x0045);
		KbdScanEvent(0xe19d);
		KbdScanEvent(0x00c5);
	    }
	    continue;
	}
	if (e.usage >= sizeof(hidToScan) / sizeof(hidToScan[0]))
	    continue;
	s = hidToScan[e.usage];
	if (!s)
	    continue;
	KbdScanEvent((word) (((s & 0x100) ? 0xe000 : 0) | (s & 0xff) |
			     release));
    }
}
