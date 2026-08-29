#include "device_picker_layout.h"

#include <stdio.h>

#include "adb/adb_device.h"

bool
sc_device_picker_should_prompt(bool has_explicit_device, bool window_enabled,
                               size_t device_count) {
    return !has_explicit_device && window_enabled && device_count != 1;
}

int
sc_device_picker_list_y(void) {
    return SC_DP_PAD + SC_DP_TITLE_H + SC_DP_PAD;
}

int
sc_device_picker_list_height(size_t device_count) {
    if (device_count == 0) {
        return SC_DP_EMPTY_MSG_H;
    }
    return (int) device_count * SC_DP_ROW_H;
}

int
sc_device_picker_buttons_y(size_t device_count) {
    return sc_device_picker_list_y()
         + sc_device_picker_list_height(device_count)
         + SC_DP_PAD;
}

int
sc_device_picker_window_height(size_t device_count) {
    return sc_device_picker_buttons_y(device_count) + SC_DP_BTN_H + SC_DP_PAD;
}

int
sc_device_picker_hit_test(int x, int y, size_t device_count) {
    if (x < 0 || x >= SC_DP_WINDOW_W || y < 0) {
        return SC_DEVICE_PICKER_HIT_NONE;
    }

    int list_y = sc_device_picker_list_y();
    int list_h = sc_device_picker_list_height(device_count);
    if (device_count > 0 && y >= list_y && y < list_y + list_h) {
        int index = (y - list_y) / SC_DP_ROW_H;
        if (index >= 0 && (size_t) index < device_count) {
            return index;
        }
    }

    int btn_y = sc_device_picker_buttons_y(device_count);
    if (y >= btn_y && y < btn_y + SC_DP_BTN_H) {
        if (x >= SC_DP_PAD && x < SC_DP_PAD + SC_DP_BTN_W) {
            return SC_DEVICE_PICKER_HIT_REFRESH;
        }
        int close_x = SC_DP_PAD + SC_DP_BTN_W + SC_DP_BTN_GAP;
        if (x >= close_x && x < close_x + SC_DP_BTN_W) {
            return SC_DEVICE_PICKER_HIT_CLOSE;
        }
    }

    return SC_DEVICE_PICKER_HIT_NONE;
}

size_t
sc_device_picker_format_row(char *buf, size_t size, size_t index,
                            const struct sc_adb_device *device) {
    const char *type =
        sc_adb_device_get_type(device->serial) == SC_ADB_DEVICE_TYPE_USB
            ? "usb" : "tcpip";
    const char *model = (device->model && device->model[0]) ? device->model
                                                            : "unknown";
    const char *state = device->state ? device->state : "";
    int n = snprintf(buf, size, "%d  (%s)  %s  %s  %s",
                     (int) index + 1, type, model, device->serial, state);
    if (n < 0) {
        if (size) {
            buf[0] = '\0';
        }
        return 0;
    }
    return (size_t) n;
}

int
sc_device_picker_index_from_digit(int digit, size_t device_count) {
    if (digit < 1 || digit > 9) {
        return -1;
    }
    size_t index = (size_t) digit - 1;
    if (index >= device_count) {
        return -1;
    }
    return (int) index;
}
