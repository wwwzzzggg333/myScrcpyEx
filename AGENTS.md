# Agent guide

scrcpy is split into two build targets:

- **Client** (`app/`): a C program built with **Meson + Ninja**. Depends on SDL2,
  FFmpeg (`libavcodec/format/util/swresample/device`) and `libusb-1.0`.
- **Server** (`server/`): an Android app (Java) built with **Gradle** and the
  **Android SDK**. It is packaged as an APK and pushed to the device at runtime.

This fork adds an **SDL device picker** shown at startup when zero or multiple
ADB devices are found (see `app/src/device_picker*.c`).

## Build

```bash
# Client + server (requires Android SDK for the server)
meson setup build --buildtype=release
ninja -C build

# Client only (no Android SDK / Java needed)
meson setup build-client -Dcompile_server=false --buildtype=release
ninja -C build-client

# Server only
./gradlew -p server assembleRelease   # -> server/build/outputs/apk/release/
```

`ninja` must run as a **non-root** user. Build output goes to `build/`
(gitignored). See `doc/build.md` for the full upstream instructions.

## Run

```bash
./run build [scrcpy options]      # runs build/app/scrcpy with the built server
./build/app/scrcpy --version      # sanity check (no device needed)
./build/app/scrcpy --help
```

`scrcpy` requires an Android device reachable over ADB (USB or TCP/IP). With no
device attached it opens the device picker ("No ADB device found").

## Cursor Cloud specific instructions

### Environment setup

The Cloud Agent environment is defined by `.cursor/environment.json`, which runs
`.cursor/install.sh`. That script is **idempotent** and:

- installs client build deps via `apt` (Meson/Ninja, SDL2, FFmpeg, libusb, `adb`, JDK);
- installs the Android SDK command-line tools plus `platforms;android-36` and
  `build-tools;36.0.0` into `$HOME/android-sdk` (needed to build the server);
- exports `ANDROID_HOME` in `~/.bashrc` and writes a gitignored `local.properties`
  so Gradle finds the SDK;
- builds client + server with `meson setup build && ninja -C build`.

Re-run it any time with `bash .cursor/install.sh`.

### Testing / demonstrating changes

- **Build + unit-level checks** run headless: `ninja -C build` and
  `./build/app/scrcpy --version` / `--help`.
- **GUI / device-picker changes**: an X server is available on `DISPLAY=:1`.
  Launch the picker with
  `DISPLAY=:1 ./run build` (it stays open on `SDL_WaitEvent`), then capture it.
  - `ffmpeg` **full-screen** grab works; **region** grab (`-i :1.0+X+Y`) is
    offset/unreliable here, so record `-video_size 1920x1200 -i :1.0` and crop
    afterward.
  - `xdotool` drives the mouse/keyboard for interaction (X/`xwininfo` coordinates
    are correct; only `ffmpeg` region offsets are wrong).

### Known limitation: Android emulator does not boot

Running scrcpy against a live device via the Android emulator does **not** work in
this VM. Nested KVM faults during vCPU creation
(`kvm_spurious_fault` in `kvm_vm_ioctl_create_vcpu`, visible in `sudo dmesg`), so
QEMU stays idle with no kernel output regardless of `-gpu`/`-accel` options.
`/dev/kvm` exists and `vmx`/nested are reported enabled, but guest execution
still faults — this is a host virtualization limitation, not a scrcpy issue.

Therefore, do **not** rely on live mirroring for verification in Cloud. Prove
changes with: the successful build, `scrcpy --version`/`--help`, and (for
picker/UI changes) a screenshot or recording of the SDL device picker on
`DISPLAY=:1`. Live mirroring works normally once a real device or a bootable
emulator is attached.
