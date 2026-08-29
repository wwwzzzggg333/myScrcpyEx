#ifndef SC_DEVICE_PICKER_H
#define SC_DEVICE_PICKER_H

#include "common.h"

#include <stdbool.h>

/**
 * List ADB devices and show a picker when there are zero or multiple devices.
 *
 * Return true if mirroring should continue:
 *  - *out_serial is NULL: keep auto-selection (exactly one device, no picker)
 *  - *out_serial is a heap string: use it as -s (caller must free)
 *
 * Return false if the user cancelled or listing/display failed.
 */
bool
sc_device_picker_choose(char **out_serial);

#endif
