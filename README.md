# Mecrisp Pocket

A Forth programming environment for Android that runs the real
[Mecrisp-Quintus](https://mecrisp.sourceforge.net/) Forth on an emulated
RISC-V microcontroller board.

**Status:** milestone 3 — the emulator, the Quintus port, a REPL and an editor
with completion and syntax colouring, a Registers view, files and sessions.
Phone devices, the GPIO view, `include` and the USB GPIO breakout come in
later milestones.

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

In the editor:

- **Files** opens the scratch file or a file from your project folder (pick
  the folder once; the app keeps access to it)
- **Send** sends the current line, the selection or the whole file; sending
  stops at the first line that does not answer " ok." and marks it red
- changed files are saved when you leave the app or close their tab; the open
  files, cursor positions and the console text come back after a restart
- with a hardware keyboard: Ctrl+S save, Ctrl+Enter send line, Ctrl+Z / Ctrl+Y
  undo and redo, Ctrl+F find, Ctrl+Space or Tab complete

On a phone the screens are tabs (REPL, Editor, Registers). On a tablet or in
landscape the editor sits on the left and REPL and Registers share the right.

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
between runs, and `-f FILE` sends a source file line by line and stops with
`FILE:N: error` at the first line Forth rejects. Run `qpemu` without arguments
for its options.

Tests:

```sh
tests/forth/run.sh build/emu/qpemu build/core/core.bin build/forth build/core/core.syms
tests/riscv/run.sh path/to/riscv-tests build/emu/isa-runner build/isa
```

The Android app builds with `./gradlew assembleDebug` (JDK 17 and the
Android SDK with NDK); the Gradle build assembles the core itself, so the
RISC-V binutils must be installed.

## Release builds

When the repository has these four Actions secrets, every CI run also builds
a signed release APK (artifact `mecrisp-pocket-release`). A new tag `vX.Y.Z`
publishes it: either push the tag (the description comes from
`docs/releases/vX.Y.Z.md` if it exists), or publish a release with a new tag
on GitHub's Releases page and CI attaches the APK a few minutes later.

| Secret | Value |
| --- | --- |
| `SIGNING_KEYSTORE_BASE64` | The keystore (`.jks`), base64 on one line |
| `SIGNING_STORE_PASSWORD` | Keystore password |
| `SIGNING_KEY_ALIAS` | Key alias |
| `SIGNING_KEY_PASSWORD` | Key password |

Create the key once with
`keytool -genkeypair -keystore mecrisp-pocket.jks -storetype PKCS12 -alias mecrisp-pocket -keyalg RSA -keysize 4096 -validity 10000`
and keep it safe: every update must be signed with the same key. Without the
secrets the release APK is built unsigned (F-Droid signs its own builds).

The debug build is a separate app (`….mecrisppocket.debug`, "Mecrisp Pocket
debug"), so both can be installed at once.

## License

GPL-3.0-or-later. Mecrisp-Quintus is © Matthias Koch, GPL-3.0-or-later.
