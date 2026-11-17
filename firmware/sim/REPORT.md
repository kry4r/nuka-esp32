# FW-08 verification

## Acceptance

| Item | Result | Evidence / limit |
|---|---|---|
| Reuse firmware UI and Apps | Passed | The target compiles the shared eleven App pairs, registry, navigation, overlays, theme, VM store, resource parser and Vault core. Hardware services are replaced at the platform boundary. |
| Full/minimum protocol examples | Passed | Windows and Linux each ran eleven cards × four states × two fixture sizes: 88 successful exits per platform. |
| Four states selected through CLI | Passed | `normal`, `loading`, `offline`, `unconfigured`; 44 screenshots per platform. |
| Screenshot | Passed | SDL software rendering produced 480×480 BMP files; Chinese Clock, Hosts, Buddy and Vault were inspected. |
| Windows compilation and execution | Passed | Visual Studio 2022 x64, Debug; `SDL_VIDEODRIVER=dummy`. |
| Linux compilation and execution | Passed | Ubuntu 24.04 under WSL, GCC 13.3, Ninja, Debug; `SDL_VIDEODRIVER=dummy`. |
| macOS compilation and execution | Skipped | No macOS build/runtime environment is available. CMake support is present but has not been verified there. |
| Keyboard / mouse interaction | Implemented; interactive acceptance pending | Keys 1/2/3 map to PWR/BOOT/Key3, including wake consumption, PWR 2/4s, Key3 double press and recovery chord. Mouse input uses the shared `nav_touch` and LVGL pointer API. Headless screenshots do not establish interactive acceptance. |
| English-only resources | Passed | English Buddy and minimum Clock start with the English-only pack. Preformatted fixture strings retain their original language; an English-only pack requires English VM text. |

The full pack used was `build/nukapack-current.bin`: 2,321,274 bytes, SHA-256 `d75223bcc6aee6f52ab94211a4ee059316176eafdbfa667064fcf48642d69a35`. The English-only pack was 181,330 bytes, SHA-256 `cd7e5e5546755ef539118ea61d1460ecf50cf1715c741cd37a704e68e6d0e8fd`. Both contain ABI-1 version metadata.

## Commands

Windows Developer PowerShell, from the repository root:

```powershell
cmake -S firmware/sim -B build/sim-win -G 'Visual Studio 17 2022' -A x64
cmake --build build/sim-win --config Debug --target nuka_sim -j 8
$env:SDL_VIDEODRIVER='dummy'
./build/sim-win/Debug/nuka_sim.exe --assets build/nukapack-current.bin --card hosts --state normal --frames 90 --screenshot build/sim-win-hosts.bmp
./build/sim-win/Debug/nuka_sim.exe --assets build/nukapack-en-current.bin --language en --card clock --minimal --frames 12 --screenshot build/sim-win-en-clock.bmp
```

Linux, from the repository root:

```sh
cmake -S firmware/sim -B /var/tmp/nuka-sim-build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build /var/tmp/nuka-sim-build --target nuka_sim -j 8
SDL_VIDEODRIVER=dummy /var/tmp/nuka-sim-build/nuka_sim --assets build/nukapack-current.bin --card hosts --frames 90 --screenshot build/sim-linux-hosts.bmp
```

The commands were repeated manually for all eleven card IDs, all four states, with and without `--minimal`. Generated images remain ignored under `build/sim-smoke/` and `build/sim-linux-shots/`; no UI test suite or screenshot comparisons were added. Following the Pomodoro service change, Windows additionally reran its eight state/fixture combinations successfully. Changed Clock, Buddy, Pomodoro and Vault view sources also passed the actual Xtensa compile flags with `-Wall -Wextra -Werror -fsyntax-only`.

`python scripts/check_license.py` and `python scripts/check_no_personal.py` passed with zero failures.

After the network/time APIs and overlay changes, Windows and Linux incrementally rebuilt again and captured Clock or Settings successfully. Platform mocks keep those changes local to the process; they perform no SNTP requests or host-clock writes.

## Functional fixes found during verification

- FW corrected default child text colors to the theme text token, so body text is readable on black.
- Vault now renders inside its own child container, preserving the navigation title.
- Clock keeps its runtime seconds style separate from the configured Key3 enable flag; weekday names use translations.
- Buddy rotates the displayed sessions from the selected entry and translates protocol state names.
- Pomodoro exposes a locked snapshot and a service tick independent of LVGL; the device power task drives expiry and the 0.5s tone. Its detail page saves three durations to NVS. Actual screen-off expiry and tone timing still require device acceptance.
- AI keeps the selected account by ID and offers touch paging. L0 draws at most one 5h ring and one weekly ring; L1 lists every window, its translated source and optional reset/stale information. Account status names use translations.
- Media follows the Hub's activity order until the user selects a source, then retains that source ID until removal. Controls address that selected source and respect its capabilities. Cover loading remains a service boundary.

After these AI/Media changes, Windows and Linux each ran their full/minimum fixtures in all four states successfully: 16 runs per platform. Additional local fixture copies containing two accounts, duplicated window kinds and two media sources rendered successfully on both platforms; their screenshots are under `build/sim-functional/`. The copies use placeholder data and are ignored build artifacts. These runs establish rendering, not interactive paging or command routing; those interactions remain acceptance work. An attempt to address the native SDL window from this execution environment could not obtain its window handle and was stopped.

## Limits

This simulator uses protocol fixtures only. It does not connect to `nuka hub --demo` or a real Hub. VM snapshots do not update from a network, and outgoing commands are diagnostic messages. Media-cover HTTP/JPEG loading returns no image. The hardware performance HUD is a diagnostic message; it does not invent device CPU/RAM measurements. The tone service performs no audio output, battery runtime estimates are unknown, and LittleFS capacity returns `ESP_ERR_NOT_SUPPORTED`. Provisioning requests display the shared QR overlay with placeholder network information after a local readiness event.

Vault and settings are process-local RAM. No real HID typing, CDC, Wi-Fi, OTA, persistent flash, eFuse, panel DMA, battery or power behavior is established by these runs. Those remain firmware/device and INT acceptance work. No visual polish or power tuning was performed.
