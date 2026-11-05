# fx9860/ — the on-calculator side

This directory builds the fx-9750GIII `.g1a` add-in with the fxSDK/gint
toolchain: the VM core from `../src/core` plus the emulator shell.

## Layout

- `CMakeLists.txt` — fxSDK project file (`GenerateG1A` wraps `fxgxa`). It
  also compiles the boot class library with javac (`tools/build_boot.py`) and
  runs `tools/gen_bootimg.c` to prelink it into a const flash image
  (`bootimg.c`, format in `../src/core/romclass.h`). Needs `javac`, `python3`
  and a host C compiler on the build machine.
- `src/main.c` — the shell: main menu (Play / Settings), installed-suite
  list, the system-wide `.jar` scanner, install/uninstall, folder picker,
  message modals, and the **Run screen** (loads the suite jar, launches the
  selected `MIDlet-<n>`, and pumps paint/keys over the full 128x64 screen;
  EXIT destroys, F1–F6 fire the MIDlet's `Command`s in order, DEL is
  clear/backspace). Settings persist to `/FXJ2ME/CONFIG.TXT` (`suites_dir`,
  `scan_dir`).
- `../src/core/ams.c` — application management: phone-style suite install.
  A jar (plus its `.jad` if one sits beside it) is validated the way MIDP
  AMS does — descriptor attributes override the manifest,
  `MIDlet-Jar-Size` must match the file, `MIDlet-Name`/`Version`/`Vendor`
  are required, `MIDlet-<n>` entries are read consecutively from 1, the
  declared profile/configuration must be supported, and every declared
  MIDlet class must exist in the jar. Suites are stored as
  `<suites>/<id>/APP.JAR`, `APP.JAD`, `SUITE.MF`, `DATA/`; reinstalling the
  same name+vendor upgrades in place, and uninstall removes the namespace.
  Install also unpacks the jar into `CLS/` (one uncompressed file per entry,
  `/` written as `%`); the VM opens each class file as it is needed, so a
  launch holds no jar image in RAM and nothing relies on `lseek`.
- `src/selftest.c` — VM bring-up diagnostics (the interpreter battery against
  the embedded test jar), reachable from Settings ▸ VM self-test. All memory
  (VM, thread, Java heap, class metadata) is released on exit.
- `src/vfs_posix.c` — VFS backend over gint's POSIX layer on the Fugue
  filesystem (fx G-III). Every OS call runs inside `gint_world_switch()`;
  the portable shell core (`fscfg.c`, `scan.c`) is host-tested via
  `tests/hostvfs.c`.
- `src/test_jar_blob.c` — generated from `build/fixtures/test.jar` with
  `tools/bin2c.py`.
- `build-fx/bootimg.c` — generated at build time from `../lib/java`: the
  parsed tables of every boot class as `const` data (94 KB of `.rodata`, i.e.
  flash), plus a hash table over all of its pool strings. `classfile.c` serves
  `java.*`/`javax.*` from it, so a boot class costs a 108-byte `jclass` plus
  its static storage in RAM, and a MIDlet's own pool text that duplicates the
  boot library points into flash instead of being copied.

## Build & deploy

```
fxsdk build-fx        # -> fxj2me.g1a (needs javac + python3 + host cc)
fxgxa -d fxj2me.g1a   # header/checksum sanity check
```

Deployment on G-III models is a file copy over USB mass storage (F1 on the
calculator's USB popup, then drag-and-drop), or `fxsdk send-fx`.

## Notes

- Toolchain: fxSDK + gint (GCC for big-endian SuperH, no FPU — all
  float/double ops are soft-float; keep them out of hot paths).
- Static RAM is only 6 KB (see `fx9860g.ld`): everything large is
  `malloc()`'d at runtime. The stock OS gives add-ins a ~32 KB user-RAM
  window topped up by the OS heap arena — measured budget in `../DESIGN.md`.
- The host build in `../CMakeLists.txt` is the test bench: all ctest suites
  must keep passing before anything is deployed to hardware.
- Running a MIDlet keeps no jar in RAM: classes are unpacked in the suite's
  `CLS/` directory and each one is read from flash when first used, then kept
  as a packed, compacted constant pool + string block + Code block. The
  scan/suite arrays are freed for the duration of a launch, and the probe
  splits the arena ~14 KB (objects) / ~20 KB (class loading). The stock ~41 KB
  heap still bounds what fits; see `../DESIGN.md` for the measured model.
