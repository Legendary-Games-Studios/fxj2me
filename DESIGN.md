# fxj2me — a J2ME emulator for the Casio fx-9750GIII

Goal: run real J2ME (MIDP) games — the `.jad`/`.jar` kind that ran on 2000s phones —
on a Casio fx-9750GIII graphing calculator, as a native `.g1a` add-in built with
fxSDK/gint.

Engine decision (settled): **a true interpreter VM** running on the calculator
itself — class loading from jar at runtime, bytecode interpreter, GC. No offline
bytecode→native translator *yet*; if 58 MHz proves too slow, a translator backend
can be bolted on later (the class/runtime data model below is chosen so this stays
possible: see "Possible future work").

## Hardware reality (fx-9750GIII)

| Resource | Value | Source |
|---|---|---|
| CPU | Renesas SH7305 (SH-4AL-DSP, SH4-A family), ~58 MHz, no FPU | community measurements |
| On-board RAM | **512 KB** (all fx-9860G since the 2009 GII models) | C.Basic devs (sentaro21/CalcLoverHK), gint community |
| Add-in RAM (stock OS) | **32 KB** user-RAM window on SH4 (8 KB on SH3) | `fx9860g.ld` comment in the fxSDK sysroot |
| "User memory" | ~62 KB | *not* our budget — it is the OS "main memory" filesystem for BASIC |
| Storage | ~3 MB flash, USB mass-storage | Casio spec |
| Display | 128×64, 1-bit mono, 2.63" | Casio spec |
| Sound | none | — |
| Network | none (USB is host-file-transfer only) | — |

Key insight (corrected after on-hardware measurement): **the hardware's 512 KB is
not ours.** The stock OS maps a small user-RAM window to each add-in — 32 KB on
SH4 machines (the GII/GIII generation, incl. fx-9750GIII), 8 KB on SH3 — and
that window is the *entire* add-in budget: gint's linker script (`fx9860g.ld`)
exposes 6 KB of it statically and hands the rest to `malloc()`. Early research
suggested add-ins could malloc "most of RAM"; that is not true under the stock
OS, and there is no alternative firmware for the fx-9750GIII/fx-9860GIII to
lift the window (community search, 2026-10). Anything beyond the window lives
within the malloc heap — which measurement (fx-9750GIII, stock OS, 2026-10)
shows is ~40 KB in practice: the OS heap arena tops the window up.

### Memory budget (stock OS, measured model)

```
g1a code + rodata             up to 500 KB   (mapped in flash at 0x00300000 — NOT RAM)
static .data/.bss             6 KB      (all the linker script exposes of the window)
malloc heap (everything)      ~41 KB    (measured on fx-9750GIII stock OS: the OS heap
                                        arena tops the 32k window up; probed at boot)
  ├─ VM + thread structs      6 KB      (vm 1.9 KB, jthread 3.7 KB)
  ├─ class-load headroom    ~20 KB      (the MIDlet's own classes, loaded lazily)
  └─ Java heap (objects)    ~14 KB      (the rest of the arena)
flash storage                 ~3 MB     (jars, RMS record stores, config)
```

CLDC's spec floor (128 KB volatile) is **not** met on the stock OS, so every
kilobyte below is the result of a measurement, not an estimate.

Measured (host harness, KoalaMines' 25 touched classes: 21 from the boot
library, 4 from the app):

```
                     as parsed     packed    + ROM image   + shared   + cp
                                                              text      compact
class file bytes       54.4 KB     — (dropped)
bytecode (Code)            —        12.4 KB     5.8 KB       5.8 KB     5.8 KB
constant pool          57.3 KB      19.1 KB     5.3 KB       5.3 KB     4.9 KB
pool strings           18.7 KB      18.7 KB     5.0 KB       2.0 KB     2.0 KB
method/field tables    33.8 KB      33.8 KB     2.4 KB       2.4 KB     2.4 KB
class headers, masks       —           —        4.2 KB       4.2 KB     4.2 KB
pool-string tables         —           —          —            —        1.3 KB
-------------------------------------------------------------------------------
class memory total    179.1 KB     109.3 KB    24.3 KB      20.2 KB    19.7 KB
whole launch (live)   257.5 KB     187.6 KB   108.3 KB     103.4 KB   103.0 KB
```

Five things got it there. The class file is not kept — after parsing a class
is a compact constant pool (8-byte entries holding indices, not pointers), one
block of pool strings, and one block holding every method's Code bytes and
exception table. The **boot class library is prelinked at build time** into a
const flash image (`tools/gen_bootimg.c` → `bootimg.c`, tables in
`romclass.h`): the VM reads its pool, strings, methods and bytecode in place
from flash, so loading a boot class costs a 108-byte header plus its statics.
The image also carries a **hash table over every boot string**, and the parser
points a MIDlet's pool at those flash copies instead of malloc'ing its own —
61% of KoalaMines' pool text is text the boot library already holds. The pool
itself is **compacted** after parsing: javac emits an entry for every symbol a
class mentions, but only 263 of KoalaMines' 666 entries are reachable from
bytecode and exception tables (plus the operands of the entries they name), so
the rest are dropped in place. And the shell frees its scan/suite lists for
the duration of a launch.

On the 32-bit target (measured: `jclass` 108, `jmethod` 36, `jfield` 20,
`cp_entry` 8, `jblk` 16) the same launch needs ~19 KB of class memory instead
of ~70 KB: ~2.4 KB of boot headers/statics and ~17 KB of app metadata (pool
4.9, code 5.8, strings 2.0, tables 2.4, headers and pool-string tables 1.7).
The image itself is 94 KB of `.rodata` — flash, like the rest of the add-in —
and it replaces the 73 KB boot jar that used to be opened and parsed.

Objects got cheaper too, because a MIDlet allocates ~300 of them and the
per-object overhead was a quarter of its heap: the block header is 16 bytes
instead of 24 (GC colour in the flag word, array element size in the flags),
payloads are 4-byte aligned instead of 8 (every JVM value is a 32-bit slot),
a free block only stores one jref, and the GC mark stack is 128 refs instead
of 256. That is 12.7 KB of objects where the same run used 20 KB, and the
probe now reserves ~20 KB for class loading instead of 8 KB: ~14 KB of objects
plus ~20 KB of classes is what the ~34 KB arena holds.

**Verified on hardware** (fx-9750GIII, stock OS, no BOS): KoalaMines installs
and plays — 4 app classes, 19.7 KB of class metadata, ~300 live objects. That
is the reference launch for future work; anything that makes it need more RAM
is a regression.

Class loading reports the true cause (`out of memory loading class: X` —
including allocation failures inside parsing — / `unreadable class entry` /
`missing class: X`), so each wall is measurable on the machine itself.

## Why this is hard

- 58 MHz, interpreted only, no FPU (soft-float on target). Simple 2D `Canvas`
  games are the realistic target; Mascot Capsule 3D and media are out.
- Display mismatch: MIDlets assume 128×128..240×320 color. We render their
  framebuffer 1-bit with luminance thresholding + ordered dither and scale/crop to
  128×64.
- No sound (`javax.microedition.media` stubs return success and play nothing).
- No networking (`javax.microedition.io.Connector` returns IOException).
- ~41 KB of malloc heap on the stock OS (measured): class metadata, Java heap and
  interpreter stacks all share it (BOS unlocks ~400 KB).

## Architecture

```
+--------------------------- g1a (gint, on-calc) ---------------------------+
|  hal_gint: VRAM blit 128x64, key matrix, timers, Bfile storage            |
+---------------------------------- HAL -----------------------------------+
|  hal.h: present(vram), poll_keys(), storage io, millis()                  |
+------------------------------- VM core ----------------------------------+
|  vm.c       bytecode interpreter: frames, dispatch, exceptions           |
|  mem.c      heap: first-fit free list + mark-sweep GC (jref = 32-bit      |
|             heap offsets, non-moving)                                    |
|  classfile.c .class parser -> runtime class/method/field metadata         |
|  jar.c      ZIP central-directory reader                                  |
|  inflate.c  DEFLATE decompressor (jars are deflate-compressed)            |
|  natives.c  native method registry                                        |
+--------------------- Java class libs (lib/java, javac) ------------------+
|  boot.jar built by tools/build_boot.py from real Java sources:           |
|  java.lang   Object, String, StringBuilder, Integer, Math, Throwable, ... |
|  java.util   Vector, Hashtable, Random, Stack, Enumeration                |
|  java.io     InputStream/OutputStream/IOException stubs                   |
|  javax.microedition.midlet   MIDlet lifecycle                            |
|  javax.microedition.lcdui    Display, Canvas, Graphics, Image, Font,      |
|                              Command, Alert, ...                         |
|  (lcdui rendering is native: midp.c maps Graphics ops to draw.c; PNG     |
|  decode, Image pixel storage is a GC-managed byte[])                     |
+--------------------------------------------------------------------------+
```

### Shell (implemented)

The add-in boots into a front-end, not straight into a VM:

- **Main menu**: Play / Settings. **Play** lists installed *MIDlet suites*
  (AMS records) and offers "Install MIDlet suite..."; the **scanner** walks
  the scan folder (default `/`, i.e. system-wide) for `.jar` files, showing
  live progress with cancel (polled EXIT during the walk).
- **Install** follows the MIDP 2.0 AMS rules (`src/core/ams.c`): a jar plus
  an optional `.jad` beside it (descriptor attributes override the
  manifest), `MIDlet-Jar-Size` matched against the real file, MIDlet-Name/
  Version/Vendor required, the `MIDlet-<n>` list read consecutively from 1,
  the declared profile/configuration checked, and every declared MIDlet class
  required to exist in the jar. Refusals name the reason ("JAD/JAR size
  mismatch", "missing class: X", "unsupported profile", ...). A suite is
  stored as `<suites>/<id>/APP.JAR`, `CLS/`, `APP.JAD`, `SUITE.MF`,
  `DATA/`; the ID is assigned by the device and reused when the same
  name+vendor is reinstalled (in-place upgrade).
- **Install unpacks the jar** into the suite's `CLS/` directory
  (`src/core/pkg.h`): one uncompressed file per entry, with `/` encoded as
  `%` in the file name. The VM loads a class by opening just that file, so a
  launch holds no jar image and no decompression buffer in RAM — only the
  class currently being parsed. Lookup is a path and loading is open+read:
  nothing depends on `lseek` (the calculator's filesystem layer does not make
  it usable for this), and the install reads every unpacked entry back to
  prove it before declaring success. This also removes the old "jar too large
  for the RAM window" limit; peak RAM now depends on the largest single
  class, not the archive. After parsing, only that class's bytecode and a
  packed pool/string block stay resident — the class file itself is dropped
  (see the memory model below).
- **Run** launches any MIDlet of a suite: the suite record supplies the merged
  attributes and the selected `MIDlet-<n>` class, classes stream out of
  `CLS/`, and the MIDP pump drives `paint()`/keys/Commands over the full 128x64 screen
  at up to ~60 fps (EXIT destroys, F1–F6 fire its `Command`s in order). The
  boot class library ships inside the add-in as a prelinked const image
  (flash), so boot classes cost only their headers and statics in RAM, and
  no boot jar is opened at run time.
- **Uninstall** removes the whole suite namespace (jar, descriptor, record,
  and its data directory), like a phone's application manager.
- **Settings**: suites folder, scan folder (both via a folder browser with
  "use here"), and the VM self-test. Settings persist to `/FXJ2ME/CONFIG.TXT`
  (`suites_dir=`/`scan_dir=` lines) and fall back to defaults when absent.
- Storage goes through a tiny **VFS interface** (`src/core/vfs.h`): the
  portable shell core (`fscfg.c`, `scan.c`, `ams.c`) is host-tested via
  `tests/hostvfs.c` (9 ctest suites incl. `shell` and `ams`), while the device backend
  (`fx9860/src/vfs_posix.c`) wraps gint's POSIX layer on the Fugue fs with a
  `gint_world_switch()` per call.

### Object model

- `jref` = `uint32_t` offset into the VM heap (0 = null). Portable between the
  64-bit host and 32-bit SH; GC never moves objects (mark-sweep, first-fit free
  list with coalescing), so refs stay valid.
- Operand stack and locals are 32-bit slots; longs/doubles occupy two slots
  (JVM semantics), so host behavior matches target behavior.
- Object header: `size | mark | class* | fields...`. Arrays add `length`.
- Strings: `java/lang/String` holds a UTF-16 `char[] value` + `int count`
  (a layout contract with `vm_make_string`). Literals go through the intern
  table, so `==` on string literals is reliable.
- Class metadata is allocated outside the GC heap (persistent arena); on target
  that arena is a static region. Class bytes may stay in the jar cache.

### Interpreter

- Switch-threaded dispatch now; GCC computed-goto (`labels as values`) later —
  sh-elf GCC supports it.
- One Java thread (MIDP `Timer` tasks run cooperatively on the main loop).
  `Thread` exists as a class but preemption is out of scope for v1.
- Exceptions (implemented): `athrow` + implicit Java exceptions (NPE, bounds,
  `/0`, negative array size, class cast, OOM) are catchable; handler matching
  walks the class hierarchy, unwinding pops frames to the first handler; an
  uncaught exception is formatted as `java.lang.Foo: message` for the shell.

### Display pipeline

1. MIDlet draws into its virtual framebuffer (e.g. 176×220), color.
2. We render `Graphics` ops directly to a **1-bit** buffer: color → luminance,
   threshold or 4×4 Bayer ordered dither.
3. On `repaint`/`flush`, box-filter scale (or crop, per-MIDlet config) to 128×64
   and blit to VRAM.

Per-MIDlet config (stored next to the jar): canvas scale mode (fit/1:1/crop),
key mapping, heap multiplier.

### Input mapping (default)

| Calculator | MIDP |
|---|---|
| F1 … F6 | soft keys (fire the MIDlet's `Command`s in order) |
| EXE | FIRE / SELECT |
| arrows | UP/DOWN/LEFT/RIGHT game actions |
| ( / ) | `#` / `*` keys (flag, star) |
| DEL | clear / backspace |
| EXIT | back / destroy |
| MENU (F5) | fxj2me menu (scale, keymap, reset) |

### jar/class loading

1. Read ZIP central directory (jars are ZIPs).
2. `META-INF/MANIFEST.MF` + `.jad` for MIDlet attributes (name, vendor, icon,
   MIDlet-1).
3. Class loading on demand: locate `pkg/Name.class`, inflate (DEFLATE) into the
   class cache, parse. Cache evicts cold decompressed bytes under pressure.
4. `Class.getResourceAsStream` serves jar entries the same way.

## Compatibility roadmap (what "runs real games" needs)

1. **Core JVM** — *done*: class file v45-52 parse, verifier-light (trust
   bytecode), integer/float ops, branches, fields, virtual/special/static/
   interface dispatch, arrays, strings (interning included), exceptions.
2. **CLDC 1.1** — *largely done*: `java.lang` core, `java.io` stubs,
   `java.util` (Vector, Hashtable, Random, Stack, Date), floating point. Missing:
   `Timer`/`TimerTask`, `java.io` streams beyond stubs.
3. **MIDP 2.0** — *partial*: `MIDlet` lifecycle, `Display`/`Canvas`/`Form`/
   `Alert`/`Item`/`TextField`/`StringItem`, `Graphics`/`Image`/`Font`/
   `Command` (incl. soft-key dispatch), PNG decode, `callSerially`, key
   events. Missing: `GameCanvas`+`Sprite`+`TiledLayer`+`LayerManager`, RMS,
   `javax.microedition.media` stubs.
4. **Stretch**: `javax.microedition.io.file`, common vendor APIs
   (`com.nokia.mid.*` etc. — stubbed), Mascot Capsule 3D — *out of scope*.

### Runtime status and known limits

- **Host-verified**: 8 ctest suites, incl. `runtime` (strings, interning,
  implicit + explicit exceptions, uncaught reporting), `midp` (a full
  HelloMidlet lifecycle with per-primitive pixel assertions: shapes, arcs,
  triangles, text, images, transforms, RGB, translate/clip, keys, `callSerially`,
  destroy) and `koala` (KoalaMines 1.1, a real 2006 MIDP-1.0 game: launch,
  game keys, the options `Form` with `TextField` editing, modal `Alert`s).
- **On device**: the add-in builds and the `.g1a` verifies (`fxgxa -d`); the
  Run screen boots real jars (manifest → `MIDlet-1`).
- The app jar is never held in RAM (it is unpacked into `CLS/` at install);
  classes are read from flash as they are first used and keep only their
  packed pool, strings and bytecode.
- PNG decode: non-interlaced only; gray/palette/RGB/RGBA at bit depth
  1/2/4/8/16; alpha collapses to white (no compositing), so `drawImage` is
  opaque. Interlaced PNGs are rejected.
- `repaint(x, y, w, h)` repaints the whole canvas (no dirty-region support);
  one Java thread; `monitorenter/exit` are no-ops.

## Performance notes

- Target measured baseline once ported: aim ≥10 fps for a simple sprite game at
  176×208; interpretive dispatch on 58 MHz SH-4A is roughly 1-3 MIPS effective.
- Cheapest wins later: computed-goto dispatch, inline caching for
  `invokevirtual`, constant-pool resolution caching, integer-only rendering
  path, pre-extracted (stored, not deflated) class cache in flash.

## Possible future work (the translator idea — shelved, not forgotten)

A build-time MIDlet→native SH translator would sidestep heap/GC pressure for code
and run 10-50× faster. To keep that door open: runtime classes are immutable after
linking, method bodies are referenced through a `code` pointer indirection, and
natives are registered by name — so a translated build can replace `code` with
native entry points without touching the libraries.

## Dev workflow

- **Host-first**: VM core is portable C11, built with CMake on Linux; `hal_host`
  renders 128×64 frames (PGM/PNG) and scripted key input so real jars can be
  regression-tested off-device.
- **On-calc**: `fxsdk build-fx` via the fxSDK/gint toolchain produces the `.g1a`
  (see `fx9860/` for the add-in side). g1a format: 0x200-byte header ("USBPower"
  magic, sizes, 16-bit word-sum checksum, 30×17 icon) + raw SH code — generated
  by `fxgxa`, not by hand.
- Tests run under ctest on the host; fixtures include a real (deflated) jar and
  javac-generated class files.

## Safety

- Guard all use of "hidden"/unofficial RAM with a model/OS-version check and a
  graceful fallback to a smaller heap; Casio has reused supposedly-free regions
  between OS versions before.
