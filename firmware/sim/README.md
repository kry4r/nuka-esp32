# nuka-esp32 SDL simulator

FW-08 builds the same `ui/`, ten App logic/view pairs plus the home expression view, registry, VM store, resource parser, and Vault core as the device. It loads the full or minimal `protocol/examples/device/vm.*` frames. Mouse input stands in for touch; `1`, `2`, `3` stand in for PWR, BOOT and Key3. Hold `1` for the power menu, hold `2` + `3` for recovery, and use Esc to exit. A first press on a dark screen wakes it. Swipe across cards, pull down from the top for quick settings, or pull up from the bottom for notifications.

The platform boundary provides SDL threads/queues, host entropy, a read-only resource-file mapping and volatile RAM NVS. There is no physical USB, Wi-Fi, OTA or eFuse access. HID commands never generate host keystrokes or log their text. Media-cover downloads return no image, and the device performance-HUD switch only prints a diagnostic message because device counters are unavailable. The simulation cannot establish hardware security, persistent power-loss behavior, real power use, panel timing or three-platform HID acceptance. Do not enter real credentials. Closing the process discards its Vault and settings.

CMake pins LVGL 9.6.0 (MIT), SDL 2.32.10 (zlib), cJSON 1.7.19 (MIT), and [Mbed TLS 3.6.7](https://github.com/Mbed-TLS/mbedtls/releases/tag/v3.6.7) (Apache-2.0 OR GPL-2.0-or-later; use the Apache option). Existing ignored source checkouts under `build/` are preferred; otherwise FetchContent downloads the exact tags on initial configuration. These downloads concern open-source build dependencies, never Hub upstream services. The `*_SOURCE_DIR` CMake cache options accept explicit local source checkouts for offline builds.

Prepare the ABI-1 resource pack first, using the firmware resource instructions. The default path is `build/nukapack.bin`; override with `--assets`. The simulator verifies its SHA-256 and ABI through the same parser as the device. It does not invent a substitute font if the pack is missing.

Use CMake 3.24+, Git and a C11 compiler. Windows needs Visual Studio C++ build tools (Developer PowerShell); macOS needs Xcode command-line tools; Linux needs a compiler and SDL window-system development headers. Ninja is optional if the platform's default generator is used.

Windows Developer PowerShell:

```powershell
cmake -S firmware/sim -B build/sim
cmake --build build/sim --config Debug --target nuka_sim
./build/sim/Debug/nuka_sim.exe --card hosts --state normal --screenshot build/hosts.bmp --frames 90
```

With a single-configuration generator the executable is `build/sim/nuka_sim.exe`.

macOS:

```sh
cmake -S firmware/sim -B build/sim -G Ninja
cmake --build build/sim --target nuka_sim
./build/sim/nuka_sim --card hosts --state normal --screenshot build/hosts.bmp --frames 90
```

Linux:

```sh
cmake -S firmware/sim -B build/sim -G Ninja
cmake --build build/sim --target nuka_sim
./build/sim/nuka_sim --card hosts --state normal --screenshot build/hosts.bmp --frames 90
SDL_VIDEODRIVER=dummy ./build/sim/nuka_sim --card ai --state offline --frames 90 --screenshot build/ai-offline.bmp
```

All platforms accept `--state normal|loading|offline|unconfigured`, `--minimal`, `--language zh-CN|en`, `--examples DIR`, `--assets FILE`, `--frames N` and `--screenshot FILE.bmp`. Omitting `--frames` keeps the interactive window open. Screenshots are 480×480 BMP files written after drawing. `--card` accepts the actual registry IDs: clock, hosts, servers, ai, buddy, pomodoro, media, weather, vault and settings. Example `--help` lists the options. A normal host card should show the synthetic `host-example` measurements; the minimum fixture omits optional metrics. Loading, offline and unconfigured flags drive the shared view-state API.

For PLAN-v2 UI review, `--face assistant` opens the other home face. `--pose idle|attention|listening|thinking|speaking|happy|busy|worried|hungry|sleepy|charging|dizzy|offline|error` previews an expression independently of fixture priority. Listening and speaking previews use a synthetic level envelope. These options do not create a network voice session. `--enable-assistant` enables the local Key3 entry for review; real capture and playback remain a firmware integration task.

`--presses 3:200,3:200` replays two Key3 presses of 200 ms each, separated by 100 ms, inside the simulator. Keys 1, 2 and 3 map to PWR, BOOT and Key3. It never sends input to the desktop. `--open recovery` reaches the protected recovery sheet. `--battery 5` reviews the low-power animation policy. At exit, the simulator prints the selected card, face, voice entry state and expression timer state. These are manual review tools rather than additional automated tests.

`--record-dir DIR` records every fourth rendered simulator frame as a numbered BMP into an existing directory; combine the images with a local image tool to review motion. A recording reflects the SDL renderer and its 16 ms loop delay. It does not measure device fps, CPU load, memory, panel timing or power.

No `config.toml` or Hub process is required. The simulator reads only repository fixtures and its supplied resource file; it does not connect to a real or demo Hub. Hub commands are logged, and VM data stays at the loaded fixture snapshot. `nuka doctor` describes actual Hub/device availability and does not check simulator state. Close the window to stop; remove the ignored build directory to uninstall. The firmware, USB device and user configuration are unaffected.

| Problem | Action |
|---|---|
| CMake attempts a download in an offline environment | Supply the matching `LVGL_SOURCE_DIR`, `SDL_SOURCE_DIR`, `CJSON_SOURCE_DIR` and `MBEDTLS_SOURCE_DIR` checkouts. |
| Missing resource input or ABI/hash error | Rebuild the current resource pack and pass its path via `--assets`; do not suppress verification. |
| SDL cannot create a window | Use a working desktop session with platform development headers; on Linux use `SDL_VIDEODRIVER=dummy` for a headless screenshot. |
| `nuka doctor` reports USB disabled while the simulator runs | Expected without a physical nuka device; the simulator never registers a CDC device. |

Actual build and screenshot results are recorded in [REPORT.md](REPORT.md). A portable build definition does not establish acceptance on a platform that has not run it.
