# Mecrisp Pocket

A Forth programming environment for Android that runs the real
[Mecrisp-Quintus](https://mecrisp.sourceforge.net/) Forth on an emulated
RISC-V microcontroller board.

**Status:** milestone 1 — the emulator, the Quintus port and a minimal console.
The editor, autocompletion, register and GPIO monitors, phone devices and the
USB GPIO breakout come in later milestones.

## Trying it

Every push builds a debug APK on GitHub Actions. Open the latest run under
**Actions → CI**, download the `mecrisp-pocket-debug` artifact, unzip it and
install the APK (you may need to allow installing from unknown sources).

In the console:

- type Forth and press Enter; the up and down keys recall earlier lines
- **Stop** breaks a running program and returns to the prompt
- **Reset** restarts the board; words compiled with `compiletoflash` stay
- a long press on **Reset** restarts without running `init` (safe boot)
- `eraseflash` removes all flash words

## Layout

| Path | What |
| --- | --- |
| `quintus/common/` | Mecrisp-Quintus 1.1.1d core sources, unmodified (see `quintus/UPSTREAM.md`) |
| `quintus/pocket/` | Our board port, derived from the ULX3S pipeline target |
| `emu/` | The board emulator in C: CPU core, devices, and `qpemu` for Linux |
| `app/` | The Android app (Kotlin) and its JNI bridge |
| `tests/` | RISC-V ISA tests and Forth tests |
| `docs/board.md` | Memory map and device registers |

## Building on Linux

You need GNU binutils for RISC-V (`binutils-riscv64-linux-gnu` on Debian and
Ubuntu), CMake and a C compiler.

```sh
scripts/build-core.sh build/core          # assemble the Forth core
cmake -S emu -B build/emu && cmake --build build/emu
build/emu/qpemu --flash my-flash.img build/core/core.bin
```

`qpemu` gives you the same board in a terminal; `--flash` keeps flash words
between runs. Run `qpemu` without arguments for its options.

Tests:

```sh
tests/forth/run.sh build/emu/qpemu build/core/core.bin build/forth
tests/riscv/run.sh path/to/riscv-tests build/emu/isa-runner build/isa
```

The Android app builds with `./gradlew assembleDebug` (JDK 17 and the
Android SDK with NDK); the Gradle build assembles the core itself, so the
RISC-V binutils must be installed.

## License

GPL-3.0-or-later. Mecrisp-Quintus is © Matthias Koch, GPL-3.0-or-later.
