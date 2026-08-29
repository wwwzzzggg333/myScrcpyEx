# Startup Device Picker Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** On scrcpy startup, if the user did not specify a device and ADB reports zero or multiple devices, show a small SDL window to refresh or pick a device.

**Architecture:** Pure layout helpers are unit-tested without SDL. A modal SDL picker runs on the main thread after `sc_server_init()` and before `sc_server_start()`. A selected serial is injected as `server->params.req_serial`, reusing the existing `-s` path.

**Tech Stack:** C11, SDL2 (already a dependency), existing ADB list/parse code, meson test runner.

## Global Constraints

- No new dependencies (no SDL_ttf).
- English UI strings.
- Do not change behavior when `-s`/`-d`/`-e`/`ANDROID_SERIAL`/`--tcpip=<addr>`/remote mode is set, or when `--no-window`.
- OTG mode is out of scope.
- Exactly one device: no picker, auto-select as today.
- Layout constants must match between drawing and hit-testing.

---

### Task 1: Layout helpers + unit tests

**Files:**
- Create: `app/src/device_picker_layout.h`
- Create: `app/src/device_picker_layout.c`
- Create: `app/tests/test_device_picker.c`
- Modify: `app/meson.build` (add source + debug test)

**Interfaces:**
- Consumes: `struct sc_adb_device`, `sc_adb_device_get_type()`
- Produces:
  - `#define SC_DP_WINDOW_W 560`
  - `#define SC_DP_PAD 16`
  - `#define SC_DP_TITLE_H 28`
  - `#define SC_DP_ROW_H 36`
  - `#define SC_DP_EMPTY_MSG_H 36`
  - `#define SC_DP_BTN_W 110`
  - `#define SC_DP_BTN_H 32`
  - `#define SC_DP_BTN_GAP 12`
  - `SC_DEVICE_PICKER_HIT_NONE (-1)`, `REFRESH (-2)`, `CLOSE (-3)`
  - `bool sc_device_picker_should_prompt(bool has_explicit_device, bool window_enabled, size_t device_count);`
  - `int sc_device_picker_window_height(size_t device_count);`
  - `int sc_device_picker_hit_test(int x, int y, size_t device_count);`
  - `size_t sc_device_picker_format_row(char *buf, size_t size, size_t index, const struct sc_adb_device *device);`
  - `int sc_device_picker_index_from_digit(int digit, size_t device_count);`

- [ ] **Step 1: Write the failing tests** in `app/tests/test_device_picker.c` covering should_prompt, format_row, hit_test, index_from_digit, window_height.

- [ ] **Step 2: Implement layout helpers** so tests pass.

- [ ] **Step 3: Register the test in meson** and run it.

- [ ] **Step 4: Commit**

### Task 2: Export `sc_adb_list_devices`

**Files:**
- Modify: `app/src/adb/adb.h`
- Modify: `app/src/adb/adb.c` (drop `static` on `sc_adb_list_devices`)

**Interfaces:**
- Produces: `bool sc_adb_list_devices(struct sc_intr *intr, unsigned flags, struct sc_vec_adb_devices *out_vec);`

- [ ] **Step 1: Make the function public** with the existing implementation.

- [ ] **Step 2: Commit**

### Task 3: SDL picker + orchestration

**Files:**
- Create: `app/src/device_picker.h`
- Create: `app/src/device_picker.c`
- Modify: `app/meson.build` (add `src/device_picker.c` and `src/device_picker_layout.c` to `src`)

**Interfaces:**
- Consumes: layout helpers, `sc_adb_start_server`, `sc_adb_list_devices`, SDL2, `scrcpy_icon_load`
- Produces:
  - `enum sc_device_picker_action { SELECT, REFRESH, CANCEL };`
  - `enum sc_device_picker_action sc_device_picker_run(const struct sc_adb_device *devices, size_t count, size_t *selected_index);`
  - `bool sc_device_picker_choose(char **out_serial);` — heap serial on success, caller frees

- [ ] **Step 1: Implement bitmap text + modal window**
- [ ] **Step 2: Implement `sc_device_picker_choose` loop** (start-server, list, auto-select 1, picker, refresh, cancel)
- [ ] **Step 3: Commit**

### Task 4: Integrate into `scrcpy()`

**Files:**
- Modify: `app/src/scrcpy.c`

Call the picker after `sc_server_init()` and before `sc_server_start()` when conditions match. Store the serial on `s->server.params.req_serial` and free it at `end`.

- [ ] **Step 1: Wire integration + cleanup**
- [ ] **Step 2: Commit**

### Task 5: Docs

**Files:**
- Modify: `doc/connection.md`
- Modify: `FAQ.md`

- [ ] **Step 1: Document the picker**
- [ ] **Step 2: Commit**
