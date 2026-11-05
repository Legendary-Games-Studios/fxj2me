/* fxj2me: empty boot image.
 *
 * Used where the ROM image cannot be generated (no Python/javac at build
 * time) and as the stub for the generator itself: with no classes in the
 * image, boot classes are loaded from the jar as before.
 */
#include "romclass.h"

const fx_rom_class fx_rom_classes[1] = { { 0 } };
const int fx_rom_nclasses = 0;

/* No image strings: rom_intern() returns NULL without touching the table. */
const char *const fx_rom_strs[1] = { 0 };
const unsigned fx_rom_nstrs = 0;
const uint16_t fx_rom_str_hash[FX_ROM_STR_SLOTS] = { 0xffffu };
