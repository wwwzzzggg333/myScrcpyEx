#include "common.h"

#include <assert.h>
#include <string.h>

#include "adb/adb_device.h"
#include "device_picker_layout.h"

static void
test_should_prompt(void) {
    assert(!sc_device_picker_should_prompt(true, true, 0));
    assert(!sc_device_picker_should_prompt(true, true, 2));
    assert(!sc_device_picker_should_prompt(false, false, 0));
    assert(!sc_device_picker_should_prompt(false, false, 2));
    assert(!sc_device_picker_should_prompt(false, true, 1));
    assert(sc_device_picker_should_prompt(false, true, 0));
    assert(sc_device_picker_should_prompt(false, true, 2));
}

static void
test_window_height(void) {
    int empty = SC_DP_PAD + SC_DP_TITLE_H + SC_DP_PAD + SC_DP_EMPTY_MSG_H
                + SC_DP_PAD + SC_DP_BTN_H + SC_DP_PAD;
    int two = SC_DP_PAD + SC_DP_TITLE_H + SC_DP_PAD + 2 * SC_DP_ROW_H
              + SC_DP_PAD + SC_DP_BTN_H + SC_DP_PAD;
    assert(sc_device_picker_window_height(0) == empty);
    assert(sc_device_picker_window_height(2) == two);
    assert(sc_device_picker_buttons_y(0)
           == sc_device_picker_list_y() + SC_DP_EMPTY_MSG_H + SC_DP_PAD);
    assert(sc_device_picker_buttons_y(2)
           == sc_device_picker_list_y() + 2 * SC_DP_ROW_H + SC_DP_PAD);
}

static void
test_format_row(void) {
    struct sc_adb_device usb = {
        .serial = "0123456789abcdef",
        .state = "device",
        .model = "Pixel_8",
        .selected = false,
    };
    char buf[128];
    size_t n = sc_device_picker_format_row(buf, sizeof(buf), 0, &usb);
    assert(n == strlen(buf));
    assert(!strcmp(buf, "1  (usb)  Pixel_8  0123456789abcdef  device"));

    struct sc_adb_device tcpip = {
        .serial = "192.168.1.5:5555",
        .state = "device",
        .model = "SM_G991B",
        .selected = false,
    };
    n = sc_device_picker_format_row(buf, sizeof(buf), 1, &tcpip);
    assert(n == strlen(buf));
    assert(!strcmp(buf, "2  (tcpip)  SM_G991B  192.168.1.5:5555  device"));

    struct sc_adb_device no_model = {
        .serial = "emulator-5554",
        .state = "offline",
        .model = NULL,
        .selected = false,
    };
    n = sc_device_picker_format_row(buf, sizeof(buf), 0, &no_model);
    assert(n == strlen(buf));
    assert(!strcmp(buf, "1  (tcpip)  unknown  emulator-5554  offline"));
}

static void
test_hit_test(void) {
    size_t count = 2;
    int list_y = sc_device_picker_list_y();
    int btn_y = sc_device_picker_buttons_y(count);

    assert(sc_device_picker_hit_test(20, list_y + 1, count) == 0);
    assert(sc_device_picker_hit_test(20, list_y + SC_DP_ROW_H + 1, count) == 1);
    assert(sc_device_picker_hit_test(20, list_y - 1, count)
           == SC_DEVICE_PICKER_HIT_NONE);
    assert(sc_device_picker_hit_test(-1, list_y + 1, count)
           == SC_DEVICE_PICKER_HIT_NONE);
    assert(sc_device_picker_hit_test(SC_DP_WINDOW_W, list_y + 1, count)
           == SC_DEVICE_PICKER_HIT_NONE);

    int refresh_x = SC_DP_PAD + 1;
    int close_x = SC_DP_PAD + SC_DP_BTN_W + SC_DP_BTN_GAP + 1;
    assert(sc_device_picker_hit_test(refresh_x, btn_y + 1, count)
           == SC_DEVICE_PICKER_HIT_REFRESH);
    assert(sc_device_picker_hit_test(close_x, btn_y + 1, count)
           == SC_DEVICE_PICKER_HIT_CLOSE);
    assert(sc_device_picker_hit_test(SC_DP_PAD + SC_DP_BTN_W + 1, btn_y + 1,
                                    count)
           == SC_DEVICE_PICKER_HIT_NONE);

    int empty_btn_y = sc_device_picker_buttons_y(0);
    assert(sc_device_picker_hit_test(20, list_y + 1, 0)
           == SC_DEVICE_PICKER_HIT_NONE);
    assert(sc_device_picker_hit_test(refresh_x, empty_btn_y + 1, 0)
           == SC_DEVICE_PICKER_HIT_REFRESH);
    assert(sc_device_picker_hit_test(close_x, empty_btn_y + 1, 0)
           == SC_DEVICE_PICKER_HIT_CLOSE);
}

static void
test_index_from_digit(void) {
    assert(sc_device_picker_index_from_digit(1, 2) == 0);
    assert(sc_device_picker_index_from_digit(2, 2) == 1);
    assert(sc_device_picker_index_from_digit(3, 2) == -1);
    assert(sc_device_picker_index_from_digit(0, 2) == -1);
    assert(sc_device_picker_index_from_digit(9, 9) == 8);
    assert(sc_device_picker_index_from_digit(1, 0) == -1);
}

int
main(int argc, char *argv[]) {
    (void) argc;
    (void) argv;

    test_should_prompt();
    test_window_height();
    test_format_row();
    test_hit_test();
    test_index_from_digit();
    return 0;
}
