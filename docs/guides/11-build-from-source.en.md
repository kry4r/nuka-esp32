# Building from source

For contributors on Windows PowerShell, macOS or Linux. Install Git, Python 3.11+, uv, Node.js 20+, CMake, Ninja and ESP-IDF **v5.5.6**. Follow [the official ESP-IDF setup](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32s3/get-started/). The simulator requires Visual Studio C++ tools on Windows, Xcode command-line tools on macOS, or a native C compiler and window-system development headers on Linux. Its CMake fetches pinned SDL2 and other dependency sources; local source paths support offline builds. No original vendor firmware is reused.

## Toolchain

Clone into a separate development directory:

```sh
git clone --recursive --branch v5.5.6 https://github.com/espressif/esp-idf.git esp-idf
```

Windows PowerShell:

```powershell
cd esp-idf
.\install.ps1 esp32s3
.\export.ps1
```

macOS / Linux:

```sh
cd esp-idf
./install.sh esp32s3
. ./export.sh
```

Return to the repository and check `idf.py --version`: expect v5.5.6. An official download mirror supported by IDF tools may be used; keep TLS and checksum validation enabled.

## Generate and build


```sh
npm ci --prefix firmware/tools --ignore-scripts
uv run --with-requirements firmware/tools/requirements.txt python firmware/tools/gen_fonts.py
python firmware/tools/gen_strings.py
python firmware/tools/gen_theme.py
python firmware/tools/pack_assets.py --version 0.1.0
idf.py -C firmware set-target esp32s3
idf.py -C firmware build
```

Expect `build/nukapack.bin`, an adjacent version/ABI/size/SHA-256 JSON file, and `firmware/build/nuka-esp32.bin`. Set `--version` to the actual resource release version; the device reports the value embedded in the pack. The current bilingual pack is about 2.32 MB; the packer enforces a 3 MiB bank limit. Read each build's actual size. Original OFL fonts and complete licenses live in assets_src/fonts; generated artifacts remain under ignored build directories. Check generated files with `gen_strings.py --check` and `gen_theme.py --check`. Numeric glyph advances are tabular.

For English-only assets, set `$env:NUKA_LANG_PACK='en-only'` in PowerShell or `export NUKA_LANG_PACK=en-only` in macOS/Linux. Clear the variable and rebuild to restore Chinese. Firmware and pack ABI must match. Back up all 16 MiB before first flash, then use [the partition flashing guide](01-flash-firmware.en.md). Never erase the whole chip or burn eFuses.

## Simulator and Hub

From a VS C++ tool terminal on Windows, or a native terminal on macOS/Linux:

```sh
cmake -S firmware/sim -B build/sim
cmake --build build/sim
```

Run `build/sim/nuka_sim` (the generated nuka_sim.exe path on Windows). Expect a 480×480 functional interface driven by protocol/examples. See the [simulator instructions](../../firmware/sim/README.md) for arguments, screenshots and platform dependencies. Use the demo Hub separately to verify actual device connectivity.

All three platforms use the same Hub development commands:

```sh
cd hub
uv sync --all-extras --locked
uv run nuka init
uv run nuka hub --demo
```

In another terminal run `uv run nuka sim-device --duration 60`. Expect continuous VM frames; feed captures into existing T6. Development integrations use fixtures only, never real CPA/CPAMP, probes or weather services.

Example config.toml:

```toml
[hub]
name = "<HUB_NAME>"
host = "0.0.0.0"
port = 8790
```

Use `uv run pytest` for T1–T6 only, and T7/T8 on firmware. Other acceptance is manual. Run `uv run ruff check .` and `uv run ruff format --check .`; at the repository root run both personal-value and license scanners.

## Uninstall / rollback

Stop the simulator and development Hub, preserving configuration and encrypted backups. Remove installed services with `nuka service uninstall`; remove a global uv tool with `uv tool uninstall nuka-hub`. Return the working tree to a known revision and regenerate matching assets before flashing.

## Troubleshooting

- Missing idf.py: export IDF in the current terminal and check the version. Doctor diagnoses running Hub/device connectivity, not compiler installation.
- Pack exceeds 3 MiB: remove unintended font sizes or full Chinese character sets. The generator fails instead of truncating. English-only is supported. Doctor does not inspect Flash ABI.
- SDL2 build failure: check window-system development headers and use a matching native tool terminal. For download failures, provide local dependency sources as described in the simulator instructions.
- Hub absent: doctor reports `hub_http: degraded`, `Hub not reachable; start nuka hub and check hub.port`. Port conflicts report `port: error`, `Port occupied by another process`.
- USB absent: doctor reports `usb: disabled`, `No nuka USB CDC device; check USB cable and native USB mode`; check data cable, ROM versus CDC mode and serial clients.
