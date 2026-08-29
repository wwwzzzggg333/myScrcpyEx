# Startup ADB Device Picker

Date: 2026-08-29

## Problem

On startup, scrcpy currently fails immediately when ADB reports zero devices or more than one matching device:

- 0 devices: `ERROR: Could not find any ADB device`
- N devices: print the list, tell the user to pass `-s` / `-d` / `-e`, then exit

This is inconvenient for interactive use, especially when launching scrcpy by double-clicking on Windows.

## Goal

When the user has **not** already named a device, show a small SDL window:

- **0 devices:** message “No ADB device found”, buttons **Refresh** and **Close**
- **2+ devices:** clickable list (model, serial, USB/TCPIP); click a row or press `1`–`9` to select

Exactly one device keeps today’s auto-select (no window).

## Non-goals

- OTG (`scrcpy --otg`) USB picker (keep current errors)
- SDL_ttf, system native dialogs, or a general GUI toolkit
- Waiting/polling until a device appears (user must click Refresh)
- Changing behavior when `-s` / `-d` / `-e` / `ANDROID_SERIAL` / `--tcpip=<addr>` / remote mode already selects a target
- Filtering unauthorized/offline devices out of the list (existing `sc_adb_device_check_state()` still runs after selection)

## Approach

List devices on the **main thread** after `sc_server_init()` (ADB already initialized) and **before** `sc_server_start()`. If the picker selects a serial, set `server->params.req_serial` to a heap copy so the server thread uses `SC_ADB_DEVICE_SELECT_SERIAL` (same path as `-s`).

If the picker is not needed, do not change the current flow.

## When the picker runs

Show the picker only when **all** of these are true:

1. `options->window` is true (not `--no-window`)
2. No explicit device: no `-s`, `-d`, `-e`, no `ANDROID_SERIAL`, no `--tcpip=<addr>`
3. Not remote mode (`remote_host == 0`)
4. Device count after `adb devices -l` is 0 or greater than 1
5. SDL video subsystem initializes successfully

Otherwise keep the existing error path in `sc_adb_select_device()`.

If SDL video fails when a picker would have been shown, log the device list and fail the same way as today (no silent hang).

## Window

- Title: `scrcpy`
- Width: 560px; height derived from row count (empty state still reserves one message row plus buttons)
- No new dependencies: 8×8 bitmap ASCII font, drawn with `SDL_RenderFillRect`
- English copy, consistent with existing scrcpy logs

Empty layout:

```
No ADB device found
Connect a device and click Refresh.

[ Refresh ]  [ Close ]
```

Multi-device layout:

```
Select a device

1  (usb)   Pixel_8     0123456789abcdef   device
2  (tcpip) SM_G991B    192.168.1.5:5555   device

[ Refresh ]  [ Close ]
```

Interactions:

| Input | Result |
| --- | --- |
| Click a device row | Select that device and close the picker |
| Keys `1`–`9` | Select the nth device if it exists |
| Click Refresh or `r` / `R` | Re-run `adb devices -l` and redraw |
| Click Close, Escape, or window close | Cancel; scrcpy exits with failure |
| After Refresh, exactly 1 device | Auto-select it (no extra click) |

Hover may highlight the row under the cursor. No keyboard arrow navigation in this version.

Unauthorized or offline devices remain in the list. Selecting one closes the picker and then fails in `sc_adb_device_check_state()`, same as `-s` with a bad state.

## Components

### `device_picker_layout` (no SDL)

Pure layout/decision helpers, unit-tested:

- `sc_device_picker_should_prompt(has_explicit_device, window_enabled, device_count)`
- `sc_device_picker_window_height(device_count)`
- `sc_device_picker_hit_test(x, y, device_count)` → row index, Refresh, Close, or none
- `sc_device_picker_format_row(buf, size, index, device)`
- `sc_device_picker_index_from_digit(digit, device_count)`

### `device_picker` (SDL)

- Modal event loop: draw, handle mouse/keyboard, return select / refresh / cancel
- Orchestrator `sc_device_picker_choose()`: `adb start-server`, list, auto-select if one, otherwise loop picker until select or cancel

### Integration

In `scrcpy()` after `sc_server_init()`, if the picker should be considered (conditions 1–3 above):

1. `SDL_Init(SDL_INIT_VIDEO)` if not already initialized
2. Call `sc_device_picker_choose(&serial)`
3. On success, `server->params.req_serial = serial` (freed at the end of `scrcpy()`)
4. Destroy the picker window before the mirroring window is created
5. On cancel or error, return `SCRCPY_EXIT_FAILURE`

`sc_adb_list_devices()` is made public so the picker can list without going through `sc_adb_select_device()`.

The server thread still calls `adb start-server` and `sc_adb_select_device()`; a picked serial is equivalent to `-s`.

## Error handling

- List failure: log `Could not list ADB devices`, return failure
- User cancel: log `Device selection cancelled`, return failure
- SDL window/renderer failure: log the SDL error, dump the device list at ERROR level, return failure
- `--no-window` with 0 or N devices: unchanged errors from `sc_adb_select_device()`

## Testing

Unit tests (no display required):

- `should_prompt` matrix (explicit, `--no-window`, counts 0/1/2)
- `format_row` for USB vs TCPIP, missing model
- `hit_test` for rows, Refresh, Close, padding misses
- `index_from_digit` for valid and out-of-range

Manual / optional: run `scrcpy` with 0 and 2 adb devices if a display and adb are available.

## Docs

Update `doc/connection.md` and `FAQ.md` to mention the picker when no device is specified.
