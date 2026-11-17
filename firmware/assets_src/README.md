# Resource sources

`fonts/` contains the original variable TTF files for Inter, JetBrains Mono and
Noto Sans SC, each with its complete SIL OFL 1.1 license. They come from
[Google Fonts](https://github.com/google/fonts). These upstream files retain their
own licenses; generated bitmaps retain the corresponding OFL notices in nukapack.

The clock uses JetBrains Mono Nerd Font Bold, from Nerd Fonts v3.4.0. Its source
is restricted to U+0020–U+007E before inclusion here; Nerd Fonts' additional icon
sets are omitted because their mixed licenses do not meet the project's icon
policy. The Latin outlines retain JetBrains Mono's OFL 1.1 license. The original
patched file's Git blob is `de2d3b3abe5234284849110eb2d555ce0e028b0c` in
[Nerd Fonts](https://github.com/ryanoasis/nerd-fonts/tree/v3.4.0/patched-fonts/JetBrainsMono/Ligatures/Bold).
The source subset was generated with FontTools 4.62.1, `Subsetter` and the
Unicode range above. Chinese uses Noto Sans SC; both languages ship by default,
with Chinese selected at first boot. Installed proprietary fonts are not bundled.

`common-3500.txt` contains the first 3500 characters of the 2013 Table of General
Standard Chinese Characters. The machine-readable transcription comes from
[the MIT-licensed character data](https://github.com/jaywcjlove/table-of-general-standard-chinese-characters),
with its license in `CHARACTERS-LICENSE.txt`. Business vocabulary is taken from
`i18n/strings.zh-CN.json` at build time.

Run `python firmware/tools/gen_fonts.py` and `python firmware/tools/pack_assets.py`.
The converter is pinned to `lv_font_conv@1.5.3`; FontTools instantiates the required
weights and applies the OpenType tabular figures feature before rasterization.
Chinese 22/28 px uses 4 bpp. Set `NUKA_LANG_PACK=en-only` to omit both Chinese fonts.
All generated files go under ignored `build/`; source TTF files and licenses stay
in the repository. This stage supplies functional assets; FE owns artwork.

`pack_assets.py --version 0.1.0` embeds the resource version as an indexed,
NUL-terminated ASCII resource and records it in the JSON manifest. Resource
releases have their own version; the device reports this value separately from
its firmware version. OTA verifies that it matches the offered version before
committing the inactive bank. An unavailable resource pack is reported with an
empty version string while the recovery UI remains available.
