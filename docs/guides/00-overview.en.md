# Overview: what nuka-esp32 is and how to deploy it

nuka-esp32 is open-source firmware for the Waveshare ESP32-S3-Touch-AMOLED-2.16, paired with the cross-platform companion service **nuka-hub**. Together they turn the 2.16" AMOLED square screen into a "developer desk terminal": computer and server status, AI subscription quotas, the state of your AI coding agents (with on-device tool-call approval), plus a TOTP authenticator and password typer. The firmware (ESP-IDF 5.5 + LVGL 9) and the Hub (Python 3.11+) communicate only through [PROTOCOL](../PROTOCOL.md), [CONFIG](../CONFIG.md), and the schemas in `protocol/`.

This guide helps you decide which hardware you need, pick one of the D1–D4 deployment forms, and take the fastest path to a working setup. Before installing, read the root [README](../../README.md) (and [AGENTS.md](../AGENTS.md) if you contribute).

## Scope and prerequisites

| Item | Requirement |
|---|---|
| Device | Waveshare ESP32-S3-Touch-AMOLED-2.16 (ESP32-S3R8, 16 MiB Flash, 8 MB Octal PSRAM, 480×480 AMOLED) |
| Battery | Optional; the board has its own charger and fuel gauge (AXP2101) and also runs from USB power |
| Computer | Windows 10/11, macOS, or Linux; Python 3.11+ and [uv](https://docs.astral.sh/uv/) |
| Permissions | Linux needs serial-port access (join `dialout`/`uucp`, then re-login); LibreHardwareMonitor on Windows needs admin |
| Network | Device and Hub on the same LAN (D1/D2/D4), or reachable via Tailscale/tunnel (D3) |

Nothing is installed on the device; flashing is done from the browser or `esptool` (see [01-flash-firmware](01-flash-firmware.en.md)).

## Deployment forms: how to choose

| Form | Description | Typical case | Entry |
|---|---|---|---|
| D1 single machine | Hub and device on the same LAN; the Hub collects this machine's metrics | One main computer | [02-install-hub](02-install-hub-windows.en.md) |
| D2 multi-machine | One Hub; other machines report as Agents | Desktop + laptop + home server | [02-install-hub](02-install-hub-linux.en.md) |
| D3 public | On top of D1/D2, the Hub exposes its Web UI via cloudflared + Cloudflare Access | Check from your phone, device off-LAN | [09-public-access](09-public-access.en.md) |
| D4 server Hub | Hub on a Linux server or in Docker, with no local GUI data | 24/7, desktop machines join as Agents | [02-install-hub-docker](02-install-hub-docker.en.md) |

Rule of thumb: get D1 working first, add a second machine to reach D2, add D3 only when you need remote access, and use D4 when the Hub does not need local GUI data (temperatures/fans).

## Quick start (synthetic data, no real upstream)

Try every card with synthetic data before touching the device:

```sh
cd hub
uv sync --all-extras
uv run nuka hub --demo
```

The admin token is printed at startup. Open `http://localhost:8790` in a browser and sign in with it. In another terminal, run `uv run nuka sim-device --token <YOUR_TOKEN>` using the device token printed by demo. Both the Web UI and the simulated device should receive all cards; demo never contacts real services. Press `Ctrl+C` to stop.

## From-scratch path

1. [01-flash-firmware](01-flash-firmware.en.md): back up the factory firmware → flash firmware and asset pack → verify the clock face.
2. [02-install-hub](02-install-hub-windows.en.md): install the Hub, `nuka init`, `nuka service install`, open the firewall port.
3. [03-pair-device](03-pair-device.en.md): provision over USB or SoftAP, pair, and the device shows real data.
4. Enable optional integrations as needed: server sources ([05-server-sources](05-server-sources.en.md)), AI quota ([07-ai-quota](07-ai-quota.en.md)), agent approval ([08-agent-hooks](08-agent-hooks.en.md)), public access ([09-public-access](09-public-access.en.md)), Vault ([10-nukavault](10-nukavault.en.md)).
5. Run `nuka doctor` after each step; see [12-troubleshooting](12-troubleshooting.en.md) for the full symptom table.

## Minimal configuration

After `nuka init` writes `config.toml`, a minimal working setup is (placeholders only):

```toml
[hub]
name = "<HUB_NAME>"
host = "0.0.0.0"
port = 8790
lang = "en"
timezone = "auto"
```

`192.0.2.x` / `198.51.100.x` / `203.0.113.x` are documentation placeholders. In a real deployment, addresses live only in your local `config.toml` or device NVS, never in the repository. Per-integration config snippets are in the matching guide.

## Uninstall and rollback

- Hub: `nuka service uninstall` removes the service, `uv tool uninstall nuka-hub` removes the command; the data directory is under the platform user config directory — export any Vault backup before deleting it.
- Device: the factory firmware was backed up in [01-flash-firmware](01-flash-firmware.en.md); writing back `factory-16m.bin` restores the factory UI.

## Common problems

- Not sure where to start: run the quick start first to confirm the Hub works, then follow step 1 of the from-scratch path.
- Can't read `nuka doctor` output: `health` is one of `ok`/`degraded`/`disabled`/`error`, `detail` is the reason, and `doc` points at the relevant guide; cross-reference [12-troubleshooting](12-troubleshooting.en.md).
- Device can't reach the Hub: confirm they share a network and the Hub port is open; check `hub_http`/`port`/`mdns`/`firewall` in `doctor` one by one.
