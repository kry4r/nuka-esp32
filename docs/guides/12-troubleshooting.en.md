# Troubleshooting and `doctor` output reference

This guide collects nuka-esp32's common problems, the meaning of every `nuka doctor` check, and the matching fix with a link to the relevant guide. When something goes wrong, run `doctor` first and use the tables below; `doctor` only inspects — it never changes config or firewall rules.

Prerequisites: `nuka` is installed (see [02-install-hub](02-install-hub-windows.en.md)) and the device is flashed ([01-flash-firmware](01-flash-firmware.en.md)).

## Running doctor

The command is identical on all three platforms:

```sh
nuka doctor
```

Output is one JSON object per line, with fixed fields:

```json
{"name": "usb", "kind": "system", "health": "ok", "detail": "nuka USB CDC available", "doc": "guides/03-pair-device"}
```

- `health`: `ok` (fine), `degraded` (works but needs attention), `disabled` (not enabled, expected), or `error` (needs action).
- `detail`: the concrete reason; read it literally.
- `doc`: the guide path (relative to `docs/guides/`); open it for full steps.

## Doctor check reference

| Check | Meaning | Healthy output | Unhealthy output and fix |
|---|---|---|---|
| `usb` | Whether a nuka USB CDC device enumerates (VID 0x303A / PID 0x4001) | `ok` + `nuka USB CDC available` | `disabled` + `No nuka USB CDC device…`: use a data cable, confirm native USB mode; ROM download mode is not CDC, so `disabled` there is expected — see [01-flash-firmware](01-flash-firmware.en.md) |
| `hub_http` | Whether the local Hub health endpoint responds | `ok` + `Hub health reachable` | `degraded` + `Hub not reachable; start nuka hub and check hub.port`: start the Hub and check the port |
| `port` | Whether the configured port is occupied | `ok` + `Port available` (or `Port is owned by the running Hub`) | `error` + `Port occupied by another process`: change the port or stop the other process, see [02-install-hub](02-install-hub-windows.en.md) |
| `mdns` | Whether the paired Hub is discoverable over mDNS | `ok` + `Paired Hub discovered over _nuka._tcp` | `degraded` + `Hub discovery failed…`: check UDP multicast/firewall or use the paired URL; `disabled` + `mDNS disabled in config` is expected |
| `firewall` | Whether the firewall allows the configured port | `ok` + `Inbound TCP allow rule found` (or firewall off) | `degraded` + `No inbound TCP allow rule found…`: open the port; `…needs administrator permission`: run elevated |
| `lhm_privilege` | (Windows only) whether LibreHardwareMonitor needs admin | (absent — already admin) | `degraded` + `LibreHardwareMonitor needs administrator privileges`: run LHM elevated, see [04-hardware-sensors](04-hardware-sensors.en.md) |
| Plugins (collectors / servers / ai / weather / media) | Health of each data-source plugin | `health: ok` | `health: error` or `disabled` with a reason in `detail`; follow the `doc` guide |

A plugin's `kind` is the adapter type (collector/server/quota/etc.), and `doc` points at the matching guide (e.g. `07-ai-quota`, `05-server-sources`). `disabled` usually means the integration is not configured or an optional dependency is missing — that is expected; only `error` needs a fix.

## Common problems

| Symptom | Common cause | `doctor` output / command | Fix |
|---|---|---|---|
| Screen dark, nothing shown | Not flashed / asset ABI mismatch / under-powered | — | Flash matching-ABI firmware and asset pack per [01-flash-firmware](01-flash-firmware.en.md), confirm USB power |
| Device not found when flashing | Not in ROM download mode / port busy / charge-only cable | `usb: disabled` (expected in ROM) | Hold BOOT (GPIO0) then power/reset; close IDEs, monitors, and pairing commands using the port; use a data cable — see [01-flash-firmware](01-flash-firmware.en.md) |
| `nuka pair` fails | Port busy / not paired / wrong code | `usb: disabled` | Close the serial monitor and retry; check the six-digit code on the device; pending requests expire after 5 minutes, five wrong codes invalidate them — see [03-pair-device](03-pair-device.en.md) |
| Device offline, Hub not pushing | Hub not running / port not open / different network | `hub_http: degraded`, `port: error`, `firewall: degraded`, `mdns: degraded` | Start the Hub, check the port, open the firewall, confirm same network — see [02-install-hub](02-install-hub-windows.en.md) and [03-pair-device](03-pair-device.en.md) |
| AI card shows "Sign in again" | Upstream credential invalid (or an older build misreported `error` as `reauth`) | Plugin `health: ok`; account `status` from the Hub state API | Confirm the account in CPAMP/CPA first; re-authenticate only if truly needed, otherwise upgrade the Hub to the fixed version — see [07-ai-quota](07-ai-quota.en.md) |
| AI provider tripped (`auth_failed`) | Wrong key; circuit breaker prevents IP bans | Plugin `health: error` with reason in `detail` | Fix the key with `nuka secret set`, then press "retry" in the Web UI; **5 failed auths from one IP are banned for 30 minutes** — see [07-ai-quota](07-ai-quota.en.md) |
| Hardware sensor fields missing | LHM (Windows) / lm-sensors (Linux) / macmon (macOS) not installed, or no such hardware | Collector `disabled` + reason | Install and enable the web service per [04-hardware-sensors](04-hardware-sensors.en.md) |
| Server source not updating | Wrong upstream endpoint/token or unreachable | Server plugin `error` + reason | Check the upstream endpoint and token plus Hub config — see [05-server-sources](05-server-sources.en.md) |
| Web UI login fails | Wrong token / expired session | — | Sign in with the admin token printed by `nuka init`/`nuka hub`; recreate it with `nuka token` if lost — see [02-install-hub](02-install-hub-windows.en.md) |
| Public access returns 404 / 403 | Sensitive routes are zone-isolated / missing Access JWT | — | `/hook/*` and `/api/v1/perm/*` must return 404 in the public zone; sign in via Cloudflare Access — see [09-public-access](09-public-access.en.md) |

## Getting more information

- Per-integration detail is in each guide's "Common problems" section.
- Device-side logs come from the USB CDC `diag.info`; the device error page and CDC `info` take precedence over `doctor` (which does not check the device Flash ABI).
- Known limitations and repro notes from system integration are in [the e2e report](../bench/e2e-report.md).

## Uninstall and rollback

If a fix needs a reset: uninstall the Hub with `nuka service uninstall` + `uv tool uninstall nuka-hub`; write back `factory-16m.bin` to restore the device ([01-flash-firmware](01-flash-firmware.en.md)). Neither action burns eFuses or enables Secure Boot / Flash encryption.
