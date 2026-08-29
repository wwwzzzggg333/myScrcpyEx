#include "device_picker.h"

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <SDL2/SDL.h>

enum sc_device_picker_action {
    SC_DEVICE_PICKER_SELECT,
    SC_DEVICE_PICKER_REFRESH,
    SC_DEVICE_PICKER_CANCEL,
};

#include "adb/adb.h"
#include "adb/adb_device.h"
#include "device_picker_layout.h"
#include "icon.h"
#include "util/log.h"

#define SC_DP_SCALE 2
#define SC_DP_CHAR_W (8 * SC_DP_SCALE)
#define SC_DP_CHAR_H (8 * SC_DP_SCALE)

/* 8x8 ASCII glyphs (U+0020..U+007E), public domain VGA font via font8x8. */
static const uint8_t sc_dp_font[95][8] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00 },
    { 0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x36, 0x36, 0x7F, 0x36, 0x7F, 0x36, 0x36, 0x00 },
    { 0x0C, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x0C, 0x00 },
    { 0x00, 0x63, 0x33, 0x18, 0x0C, 0x66, 0x63, 0x00 },
    { 0x1C, 0x36, 0x1C, 0x6E, 0x3B, 0x33, 0x6E, 0x00 },
    { 0x06, 0x06, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x18, 0x0C, 0x06, 0x06, 0x06, 0x0C, 0x18, 0x00 },
    { 0x06, 0x0C, 0x18, 0x18, 0x18, 0x0C, 0x06, 0x00 },
    { 0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00 },
    { 0x00, 0x0C, 0x0C, 0x3F, 0x0C, 0x0C, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x06 },
    { 0x00, 0x00, 0x00, 0x3F, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C, 0x00 },
    { 0x60, 0x30, 0x18, 0x0C, 0x06, 0x03, 0x01, 0x00 },
    { 0x3E, 0x63, 0x73, 0x7B, 0x6F, 0x67, 0x3E, 0x00 },
    { 0x0C, 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x3F, 0x00 },
    { 0x1E, 0x33, 0x30, 0x1C, 0x06, 0x33, 0x3F, 0x00 },
    { 0x1E, 0x33, 0x30, 0x1C, 0x30, 0x33, 0x1E, 0x00 },
    { 0x38, 0x3C, 0x36, 0x33, 0x7F, 0x30, 0x78, 0x00 },
    { 0x3F, 0x03, 0x1F, 0x30, 0x30, 0x33, 0x1E, 0x00 },
    { 0x1C, 0x06, 0x03, 0x1F, 0x33, 0x33, 0x1E, 0x00 },
    { 0x3F, 0x33, 0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x00 },
    { 0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E, 0x00 },
    { 0x1E, 0x33, 0x33, 0x3E, 0x30, 0x18, 0x0E, 0x00 },
    { 0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x00 },
    { 0x00, 0x0C, 0x0C, 0x00, 0x00, 0x0C, 0x0C, 0x06 },
    { 0x18, 0x0C, 0x06, 0x03, 0x06, 0x0C, 0x18, 0x00 },
    { 0x00, 0x00, 0x3F, 0x00, 0x00, 0x3F, 0x00, 0x00 },
    { 0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00 },
    { 0x1E, 0x33, 0x30, 0x18, 0x0C, 0x00, 0x0C, 0x00 },
    { 0x3E, 0x63, 0x7B, 0x7B, 0x7B, 0x03, 0x1E, 0x00 },
    { 0x0C, 0x1E, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x00 },
    { 0x3F, 0x66, 0x66, 0x3E, 0x66, 0x66, 0x3F, 0x00 },
    { 0x3C, 0x66, 0x03, 0x03, 0x03, 0x66, 0x3C, 0x00 },
    { 0x1F, 0x36, 0x66, 0x66, 0x66, 0x36, 0x1F, 0x00 },
    { 0x7F, 0x46, 0x16, 0x1E, 0x16, 0x46, 0x7F, 0x00 },
    { 0x7F, 0x46, 0x16, 0x1E, 0x16, 0x06, 0x0F, 0x00 },
    { 0x3C, 0x66, 0x03, 0x03, 0x73, 0x66, 0x7C, 0x00 },
    { 0x33, 0x33, 0x33, 0x3F, 0x33, 0x33, 0x33, 0x00 },
    { 0x1E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00 },
    { 0x78, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E, 0x00 },
    { 0x67, 0x66, 0x36, 0x1E, 0x36, 0x66, 0x67, 0x00 },
    { 0x0F, 0x06, 0x06, 0x06, 0x46, 0x66, 0x7F, 0x00 },
    { 0x63, 0x77, 0x7F, 0x7F, 0x6B, 0x63, 0x63, 0x00 },
    { 0x63, 0x67, 0x6F, 0x7B, 0x73, 0x63, 0x63, 0x00 },
    { 0x1C, 0x36, 0x63, 0x63, 0x63, 0x36, 0x1C, 0x00 },
    { 0x3F, 0x66, 0x66, 0x3E, 0x06, 0x06, 0x0F, 0x00 },
    { 0x1E, 0x33, 0x33, 0x33, 0x3B, 0x1E, 0x38, 0x00 },
    { 0x3F, 0x66, 0x66, 0x3E, 0x36, 0x66, 0x67, 0x00 },
    { 0x1E, 0x33, 0x07, 0x0E, 0x38, 0x33, 0x1E, 0x00 },
    { 0x3F, 0x2D, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00 },
    { 0x33, 0x33, 0x33, 0x33, 0x33, 0x33, 0x3F, 0x00 },
    { 0x33, 0x33, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00 },
    { 0x63, 0x63, 0x63, 0x6B, 0x7F, 0x77, 0x63, 0x00 },
    { 0x63, 0x63, 0x36, 0x1C, 0x1C, 0x36, 0x63, 0x00 },
    { 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x0C, 0x1E, 0x00 },
    { 0x7F, 0x63, 0x31, 0x18, 0x4C, 0x66, 0x7F, 0x00 },
    { 0x1E, 0x06, 0x06, 0x06, 0x06, 0x06, 0x1E, 0x00 },
    { 0x03, 0x06, 0x0C, 0x18, 0x30, 0x60, 0x40, 0x00 },
    { 0x1E, 0x18, 0x18, 0x18, 0x18, 0x18, 0x1E, 0x00 },
    { 0x08, 0x1C, 0x36, 0x63, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFF },
    { 0x0C, 0x0C, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00 },
    { 0x00, 0x00, 0x1E, 0x30, 0x3E, 0x33, 0x6E, 0x00 },
    { 0x07, 0x06, 0x06, 0x3E, 0x66, 0x66, 0x3B, 0x00 },
    { 0x00, 0x00, 0x1E, 0x33, 0x03, 0x33, 0x1E, 0x00 },
    { 0x38, 0x30, 0x30, 0x3e, 0x33, 0x33, 0x6E, 0x00 },
    { 0x00, 0x00, 0x1E, 0x33, 0x3f, 0x03, 0x1E, 0x00 },
    { 0x1C, 0x36, 0x06, 0x0f, 0x06, 0x06, 0x0F, 0x00 },
    { 0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x1F },
    { 0x07, 0x06, 0x36, 0x6E, 0x66, 0x66, 0x67, 0x00 },
    { 0x0C, 0x00, 0x0E, 0x0C, 0x0C, 0x0C, 0x1E, 0x00 },
    { 0x30, 0x00, 0x30, 0x30, 0x30, 0x33, 0x33, 0x1E },
    { 0x07, 0x06, 0x66, 0x36, 0x1E, 0x36, 0x67, 0x00 },
    { 0x0E, 0x0C, 0x0C, 0x0C, 0x0C, 0x0C, 0x1E, 0x00 },
    { 0x00, 0x00, 0x33, 0x7F, 0x7F, 0x6B, 0x63, 0x00 },
    { 0x00, 0x00, 0x1F, 0x33, 0x33, 0x33, 0x33, 0x00 },
    { 0x00, 0x00, 0x1E, 0x33, 0x33, 0x33, 0x1E, 0x00 },
    { 0x00, 0x00, 0x3B, 0x66, 0x66, 0x3E, 0x06, 0x0F },
    { 0x00, 0x00, 0x6E, 0x33, 0x33, 0x3E, 0x30, 0x78 },
    { 0x00, 0x00, 0x3B, 0x6E, 0x66, 0x06, 0x0F, 0x00 },
    { 0x00, 0x00, 0x3E, 0x03, 0x1E, 0x30, 0x1F, 0x00 },
    { 0x08, 0x0C, 0x3E, 0x0C, 0x0C, 0x2C, 0x18, 0x00 },
    { 0x00, 0x00, 0x33, 0x33, 0x33, 0x33, 0x6E, 0x00 },
    { 0x00, 0x00, 0x33, 0x33, 0x33, 0x1E, 0x0C, 0x00 },
    { 0x00, 0x00, 0x63, 0x6B, 0x7F, 0x7F, 0x36, 0x00 },
    { 0x00, 0x00, 0x63, 0x36, 0x1C, 0x36, 0x63, 0x00 },
    { 0x00, 0x00, 0x33, 0x33, 0x33, 0x3E, 0x30, 0x1F },
    { 0x00, 0x00, 0x3F, 0x19, 0x0C, 0x26, 0x3F, 0x00 },
    { 0x38, 0x0C, 0x0C, 0x07, 0x0C, 0x0C, 0x38, 0x00 },
    { 0x18, 0x18, 0x18, 0x00, 0x18, 0x18, 0x18, 0x00 },
    { 0x07, 0x0C, 0x0C, 0x38, 0x0C, 0x0C, 0x07, 0x00 },
    { 0x6E, 0x3B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },
};

static void
sc_dp_set_color(SDL_Renderer *renderer, uint32_t rgb) {
    SDL_SetRenderDrawColor(renderer,
                           (rgb >> 16) & 0xff,
                           (rgb >> 8) & 0xff,
                           rgb & 0xff,
                           0xff);
}

static void
sc_dp_draw_char(SDL_Renderer *renderer, int x, int y, char c, uint32_t rgb) {
    unsigned char uc = (unsigned char) c;
    if (uc < 32 || uc > 126) {
        uc = '?';
    }
    const uint8_t *glyph = sc_dp_font[uc - 32];
    sc_dp_set_color(renderer, rgb);
    for (int row = 0; row < 8; ++row) {
        uint8_t bits = glyph[row];
        for (int col = 0; col < 8; ++col) {
            if (bits & (1u << col)) {
                SDL_Rect pixel = {
                    .x = x + col * SC_DP_SCALE,
                    .y = y + row * SC_DP_SCALE,
                    .w = SC_DP_SCALE,
                    .h = SC_DP_SCALE,
                };
                SDL_RenderFillRect(renderer, &pixel);
            }
        }
    }
}

static void
sc_dp_draw_text(SDL_Renderer *renderer, int x, int y, const char *text,
                uint32_t rgb) {
    for (; *text; ++text) {
        sc_dp_draw_char(renderer, x, y, *text, rgb);
        x += SC_DP_CHAR_W;
    }
}

static void
sc_dp_draw_button(SDL_Renderer *renderer, int x, int y, const char *label,
                  bool hover) {
    SDL_Rect rect = { .x = x, .y = y, .w = SC_DP_BTN_W, .h = SC_DP_BTN_H };
    sc_dp_set_color(renderer, hover ? 0x4d6ab0 : 0x3d5a99);
    SDL_RenderFillRect(renderer, &rect);

    int text_w = (int) strlen(label) * SC_DP_CHAR_W;
    int tx = x + (SC_DP_BTN_W - text_w) / 2;
    int ty = y + (SC_DP_BTN_H - SC_DP_CHAR_H) / 2;
    sc_dp_draw_text(renderer, tx, ty, label, 0xffffff);
}

static void
sc_dp_log_devices(const struct sc_adb_device *devices, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        char row[256];
        sc_device_picker_format_row(row, sizeof(row), i, &devices[i]);
        LOGE("    %s", row);
    }
}

static enum sc_device_picker_action
sc_device_picker_run(const struct sc_adb_device *devices, size_t count,
                     size_t *selected_index) {
    if (!(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO)) {
        if (SDL_Init(SDL_INIT_VIDEO)) {
            LOGE("Could not initialize SDL video: %s", SDL_GetError());
            if (count == 0) {
                LOGE("Could not find any ADB device");
            } else {
                LOGE("Multiple (%" SC_PRIsizet ") ADB devices:", count);
                sc_dp_log_devices(devices, count);
                LOGE("Select a device via -s (--serial), -d (--select-usb) or "
                     "-e (--select-tcpip)");
            }
            return SC_DEVICE_PICKER_CANCEL;
        }
    }

    int height = sc_device_picker_window_height(count);
    SDL_Window *window = SDL_CreateWindow("scrcpy",
                                          SDL_WINDOWPOS_CENTERED,
                                          SDL_WINDOWPOS_CENTERED,
                                          SC_DP_WINDOW_W, height, 0);
    if (!window) {
        LOGE("Could not create device picker window: %s", SDL_GetError());
        return SC_DEVICE_PICKER_CANCEL;
    }

    SDL_Surface *icon = scrcpy_icon_load();
    if (icon) {
        SDL_SetWindowIcon(window, icon);
        scrcpy_icon_destroy(icon);
    }

    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, 0);
    if (!renderer) {
        LOGE("Could not create device picker renderer: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        return SC_DEVICE_PICKER_CANCEL;
    }

    int hover = SC_DEVICE_PICKER_HIT_NONE;
    enum sc_device_picker_action result = SC_DEVICE_PICKER_CANCEL;
    bool running = true;
    while (running) {
        sc_dp_set_color(renderer, 0x1e1e1e);
        SDL_RenderClear(renderer);

        const char *title = count == 0 ? "No ADB device found"
                                       : "Select a device";
        sc_dp_draw_text(renderer, SC_DP_PAD, SC_DP_PAD, title, 0xffffff);

        int list_y = sc_device_picker_list_y();
        if (count == 0) {
            sc_dp_draw_text(renderer, SC_DP_PAD, list_y,
                            "Connect a device and click Refresh.", 0xaaaaaa);
        } else {
            for (size_t i = 0; i < count; ++i) {
                int y = list_y + (int) i * SC_DP_ROW_H;
                bool row_hover = hover == (int) i;
                SDL_Rect row = {
                    .x = SC_DP_PAD / 2,
                    .y = y,
                    .w = SC_DP_WINDOW_W - SC_DP_PAD,
                    .h = SC_DP_ROW_H - 4,
                };
                sc_dp_set_color(renderer, row_hover ? 0x3a4a6a : 0x2a2a2a);
                SDL_RenderFillRect(renderer, &row);

                char label[256];
                sc_device_picker_format_row(label, sizeof(label), i,
                                            &devices[i]);
                int ty = y + (SC_DP_ROW_H - 4 - SC_DP_CHAR_H) / 2;
                sc_dp_draw_text(renderer, SC_DP_PAD, ty, label, 0xeeeeee);
            }
        }

        int btn_y = sc_device_picker_buttons_y(count);
        int close_x = SC_DP_PAD + SC_DP_BTN_W + SC_DP_BTN_GAP;
        sc_dp_draw_button(renderer, SC_DP_PAD, btn_y, "Refresh",
                          hover == SC_DEVICE_PICKER_HIT_REFRESH);
        sc_dp_draw_button(renderer, close_x, btn_y, "Close",
                          hover == SC_DEVICE_PICKER_HIT_CLOSE);

        SDL_RenderPresent(renderer);

        SDL_Event event;
        if (!SDL_WaitEvent(&event)) {
            result = SC_DEVICE_PICKER_CANCEL;
            break;
        }

        switch (event.type) {
            case SDL_QUIT:
                running = false;
                result = SC_DEVICE_PICKER_CANCEL;
                break;
            case SDL_KEYDOWN:
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    running = false;
                    result = SC_DEVICE_PICKER_CANCEL;
                } else if (event.key.keysym.sym == SDLK_r) {
                    running = false;
                    result = SC_DEVICE_PICKER_REFRESH;
                } else if (event.key.keysym.sym >= SDLK_1
                        && event.key.keysym.sym <= SDLK_9) {
                    int digit = (int) (event.key.keysym.sym - SDLK_1 + 1);
                    int index =
                        sc_device_picker_index_from_digit(digit, count);
                    if (index >= 0) {
                        *selected_index = (size_t) index;
                        running = false;
                        result = SC_DEVICE_PICKER_SELECT;
                    }
                }
                break;
            case SDL_MOUSEMOTION:
                hover = sc_device_picker_hit_test(event.motion.x,
                                                  event.motion.y, count);
                break;
            case SDL_MOUSEBUTTONUP:
                if (event.button.button != SDL_BUTTON_LEFT) {
                    break;
                }
                {
                    int hit = sc_device_picker_hit_test(event.button.x,
                                                        event.button.y,
                                                        count);
                    if (hit >= 0) {
                        *selected_index = (size_t) hit;
                        running = false;
                        result = SC_DEVICE_PICKER_SELECT;
                    } else if (hit == SC_DEVICE_PICKER_HIT_REFRESH) {
                        running = false;
                        result = SC_DEVICE_PICKER_REFRESH;
                    } else if (hit == SC_DEVICE_PICKER_HIT_CLOSE) {
                        running = false;
                        result = SC_DEVICE_PICKER_CANCEL;
                    }
                }
                break;
            default:
                break;
        }
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    return result;
}

bool
sc_device_picker_choose(char **out_serial) {
    assert(out_serial);
    *out_serial = NULL;

    bool ok = sc_adb_start_server(NULL, 0);
    if (!ok) {
        LOGE("Could not start adb server");
        return false;
    }

    bool picker_shown = false;
    for (;;) {
        struct sc_vec_adb_devices vec = SC_VECTOR_INITIALIZER;
        ok = sc_adb_list_devices(NULL, 0, &vec);
        if (!ok) {
            LOGE("Could not list ADB devices");
            return false;
        }

        if (vec.size == 1 && !picker_shown) {
            sc_adb_devices_destroy(&vec);
            return true;
        }

        if (vec.size == 1 && picker_shown) {
            *out_serial = strdup(vec.data[0].serial);
            sc_adb_devices_destroy(&vec);
            if (!*out_serial) {
                LOG_OOM();
                return false;
            }
            LOGI("Selected ADB device: %s", *out_serial);
            return true;
        }

        picker_shown = true;
        size_t selected_index = 0;
        enum sc_device_picker_action action =
            sc_device_picker_run(vec.data, vec.size, &selected_index);

        if (action == SC_DEVICE_PICKER_SELECT) {
            assert(selected_index < vec.size);
            *out_serial = strdup(vec.data[selected_index].serial);
            sc_adb_devices_destroy(&vec);
            if (!*out_serial) {
                LOG_OOM();
                return false;
            }
            LOGI("Selected ADB device: %s", *out_serial);
            return true;
        }

        sc_adb_devices_destroy(&vec);

        if (action == SC_DEVICE_PICKER_REFRESH) {
            continue;
        }

        LOGE("Device selection cancelled");
        return false;
    }
}
