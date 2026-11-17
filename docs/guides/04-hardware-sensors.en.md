# Hardware sensors

Use Python 3.11+ and an installed nuka-hub on Windows, Linux, or Apple Silicon macOS. psutil supplies CPU, memory, disks, network, and battery when present. Additional collectors supply temperatures and GPUs. Missing hardware is reported as `disabled`; unavailable measurements are omitted. See [collector configuration](../CONFIG.md#3-完整示例与字段说明).

## Windows PowerShell

Download a release ZIP from the [official LibreHardwareMonitor project](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor/releases). Extract it into a directory you control, then run:

```powershell
$lhmExe = Read-Host 'Full path to LibreHardwareMonitor.exe'
Start-Process -FilePath $lhmExe -Verb RunAs
```

In LHM enable its remote Web server, bind it to loopback, and keep port 8085. Verify:

```powershell
(Invoke-RestMethod http://127.0.0.1:8085/data.json).Children.Count
```

Expect a positive count and a sensor tree containing `SensorId`, `Value`, and `Children`. To start LHM at sign-in with sensor permissions, run an elevated PowerShell and register a task:

```powershell
$lhmExe = Read-Host 'Full path to LibreHardwareMonitor.exe'
$taskAction = New-ScheduledTaskAction -Execute $lhmExe
$taskTrigger = New-ScheduledTaskTrigger -AtLogOn
Register-ScheduledTask -TaskName nuka-lhm -Action $taskAction -Trigger $taskTrigger -RunLevel Highest
Get-ScheduledTask -TaskName nuka-lhm | Select-Object TaskName,State
```

Expect `nuka-lhm Ready`. For NVIDIA, install the vendor driver and the Hub's `nvidia` extra. `nvidia-smi --query-gpu=name --format=csv,noheader` must list a GPU before enabling NVML.

## macOS

Apple Silicon and Homebrew are required for the optional [macmon](https://github.com/vladkens/macmon/tree/v0.10.0) collector; Intel Macs still use psutil.

```sh
brew install macmon
macmon --version
macmon pipe --samples 1
```

Expect one JSON sample containing `cpu_power`, `gpu_scaled_ratio`, and `temp`. nuka-hub reads the current 0.10 output format and runs macmon without sudo.

## Linux

For Debian/Ubuntu, install [lm-sensors](https://github.com/lm-sensors/lm-sensors), then inspect the proposed hardware probes interactively:

```sh
sudo apt-get install lm-sensors
sudo sensors-detect
sensors
```

Expect temperature readings; load only the modules identified for your hardware. AMD/Intel GPU metrics come from DRM sysfs when the driver exposes them. NVIDIA requires its driver and the `nvidia` extra; verify with `nvidia-smi --query-gpu=name --format=csv,noheader`.

## Configuration and verification

Use the relevant entries in `enabled` for the platform. Example for Windows:

```toml
[collectors]
enabled = ["psutil", "lhm", "nvml"]
host_name = "example-desktop"

[collectors.lhm]
url = "http://127.0.0.1:8085/data.json"
interval = "2s"

[[collectors.lhm.map]]
field = "cpu.temp_c"
sensor_id = "/intelcpu/0/temperature/0"
```

For macOS use `["psutil", "macmon"]`; for Linux use `["psutil", "linux_sensors", "amd_sysfs", "nvml"]`. A custom LHM mapping wins over built-in matching. Choose the ID from your own sensor tree. Run `nuka doctor` and `nuka hub` on each platform. Expect `psutil` health `ok` and changing Host fields in the Hosts card. Network/disk rates require two samples. LHM fan/temperature readings and supported GPU metrics are added by their collectors.

## Troubleshooting and removal

`nuka doctor` reports each collector's `name`, `health`, and `detail`:

| Diagnostic | Action |
|---|---|
| `nvml disabled: Install the nvidia extra` | Install nuka-hub with the `nvidia` extra; restart Hub. |
| `macmon disabled: macmon requires Apple Silicon` | Remove macmon from `enabled` on this platform. |
| `linux_sensors disabled: No hwmon temperature sensors; install lm-sensors` | Run `sensors`, check kernel hardware support, and install the matching hwmon module. |
| `lhm error` with connection failure | Start LHM, enable the Web server, and verify its configured URL with the command above. |

To roll back, restore the previous `collectors` block and restart Hub. On Windows remove only this guide's task with `Unregister-ScheduledTask -TaskName nuka-lhm -Confirm:$false`, then close LHM. On macOS use `brew uninstall macmon`; on Debian/Ubuntu use `sudo apt-get remove lm-sensors` if no other application needs it. Remove any module-startup changes made by sensors-detect separately; do not unload a driver used by another application.


## Media metadata and playback control (HUB-21)

Prerequisites: run in the signed-in desktop session with a player that publishes a system media session. A Docker Hub does not read the host desktop directly; run an Agent on that desktop. Set `[media] enabled = true`, or `[agent] send_media = true` for the Agent.

Windows PowerShell:

```powershell
uv tool install --upgrade 'nuka-hub[media-win,images]'
nuka doctor
```

macOS Terminal, using [nowplaying-cli](https://github.com/kirtan-shah/nowplaying-cli) 2.0.0 or newer; that release adds the JSON interface used by the Hub ([release notes](https://github.com/kirtan-shah/nowplaying-cli/releases/tag/v2.0.0)):

```sh
brew install nowplaying-cli
uv tool install --upgrade 'nuka-hub[images]'
nowplaying-cli get --json title artist playbackRate
```

Linux desktop terminal, preserving the desktop user's `DBUS_SESSION_BUS_ADDRESS`:

```sh
uv tool install --upgrade 'nuka-hub[media-linux,images]'
nuka doctor
```

Start playback, then `nuka hub` or `nuka agent`. Expect a title and playback state on the Media card, with toggle, previous, and next controls when supported by the player. Artwork becomes a 160×160 baseline JPEG of at most 20 KiB; its download URL includes a SHA-256 hash. Missing artwork remains absent.

Inspect the Web plugin-health entry named `media`. `Install nuka-hub[media-win,images]` or `Install nuka-hub[media-linux,images] and use the desktop D-Bus session` indicates missing dependencies; `No desktop D-Bus session` indicates the wrong process environment. Windows uses [SMTC](https://learn.microsoft.com/en-us/uwp/api/windows.media.control.globalsystemmediatransportcontrolssessionmanager), and Linux players must expose [MPRIS](https://specifications.freedesktop.org/mpris-spec/latest/). An empty session list means no compatible player is registered. The macOS helper uses private system interfaces; check its upstream compatibility after OS updates.

Rollback: set `[media] enabled = false` or `[agent] send_media = false` and restart that process. Remove the macOS helper with `brew uninstall nowplaying-cli`. Optional Python extras are removed with `uv tool uninstall nuka-hub`.
