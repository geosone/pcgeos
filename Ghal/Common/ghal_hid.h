/*
 * ghal_hid.h -- host key codes to HID usages, for the backends of
 * ghal_input.
 */
#ifndef GHAL_HID_H
#define GHAL_HID_H

#include <stdint.h>

/* HID keyboard usage of a Linux key code (KEY_*), 0 if none. */
uint16_t ghal_hid_from_linux_key(unsigned code);

#endif /* GHAL_HID_H */
