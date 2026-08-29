#ifndef SC_DEVICE_PICKER_LAYOUT_H
#define SC_DEVICE_PICKER_LAYOUT_H

#include "common.h"

#include <stdbool.h>
#include <stddef.h>

#include "adb/adb_device.h"

#define SC_DP_WINDOW_W 560
#define SC_DP_PAD 16
#define SC_DP_TITLE_H 28
#define SC_DP_ROW_H 36
#define SC_DP_EMPTY_MSG_H 36
#define SC_DP_BTN_W 110
#define SC_DP_BTN_H 32
#define SC_DP_BTN_GAP 12

enum {
    SC_DEVICE_PICKER_HIT_NONE = -1,
    SC_DEVICE_PICKER_HIT_REFRESH = -2,
    SC_DEVICE_PICKER_HIT_CLOSE = -3,
};

bool
sc_device_picker_should_prompt(bool has_explicit_device, bool window_enabled,
                               size_t device_count);

int
sc_device_picker_window_height(size_t device_count);

int
sc_device_picker_list_y(void);

int
sc_device_picker_list_height(size_t device_count);

int
sc_device_picker_buttons_y(size_t device_count);

/**
 * Hit-test window coordinates.
 *
 * Returns a device index (>= 0), SC_DEVICE_PICKER_HIT_REFRESH,
 * SC_DEVICE_PICKER_HIT_CLOSE, or SC_DEVICE_PICKER_HIT_NONE.
 */
int
sc_device_picker_hit_test(int x, int y, size_t device_count);

size_t
sc_device_picker_format_row(char *buf, size_t size, size_t index,
                            const struct sc_adb_device *device);

/**
 * Map digit 1-9 to a 0-based device index.
 *
 * Return -1 if the digit does not correspond to a listed device.
 */
int
sc_device_picker_index_from_digit(int digit, size_t device_count);

#endif
