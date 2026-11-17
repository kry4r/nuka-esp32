# nukapack ABI 1

All integers are little endian. A pack occupies at most 3145728 bytes in one of
the two banks of the existing assets partition. `sys.asset_slot` selects the active
bank. Firmware updates write and verify the inactive bank before committing that
NVS value. The reader never writes Flash.

Header (48 bytes): `NUKA`, u16 ABI, u16 entry count, u32 index offset (48),
u32 total length, SHA-256 of the bytes after the header. Index entries are 16 bytes:
FNV-1a u32 name hash, u32 offset, u32 length, u32 type (0 raw/license, 1 font).
Entries are 4-byte aligned; offsets are relative to the pack start. The packer
rejects name hash collisions and writes a separate JSON size / SHA-256 / ABI file.

Font records use `NFN1`, u16 reserved, u16 line height, i16 baseline, i16 underline
position, i8 underline thickness, u8 bpp, u16 codepoint count, and four u32 values:
glyph descriptor offset, Unicode list offset, bitmap offset, bitmap length.
Descriptors use LVGL's large fmt_txt layout (`u32 bitmap_index, u32 advance_16,
u16 width, u16 height, i16 x, i16 y`). Descriptor zero is reserved. The sorted
u16 Unicode list maps directly to descriptors 1..N. Bitmaps are raw 4 bpp.

Enable `LV_FONT_FMT_TXT_LARGE`: the 28 px Chinese bitmap can exceed the smaller
layout's 1 MiB address limit. `nukapack_init` validates the external resource once,
maps it with `esp_partition_mmap`, and rebuilds only font/cmap pointer descriptors.
Glyph metadata, Unicode tables and bitmap bytes continue to refer to the mapped
Flash. No full bitmap copy or dynamic font loader is used. Only the LVGL task
calls `nuka_font`; supported sizes are 22, 28, 32, 40, 56 and 112.
`nuka_mono_font()` returns the 40 px Nerd Font digit face used by Vault TOTP.

`tr` requires a generated translation key. Call `i18n_set_language` after reading
NVS or accepting welcome/config language; rebuilding the active view applies the
change immediately. `gen_strings.py --check` verifies generated tables are fresh.
