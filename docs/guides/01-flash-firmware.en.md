# Flashing and restoring firmware

For Waveshare ESP32-S3-Touch-AMOLED-2.16 with 16 MiB Flash. Install uv on Windows PowerShell, macOS Terminal or Linux; close serial monitors, IDE connections and pairing processes. Linux requires serial-device permission. Replace `<PORT>` locally; never commit the real port name. Keep backups off git.

## Back up and verify

The same command works on all three platforms:

```sh
uvx --from esptool==4.11.0 esptool.py --chip esp32s3 --port <PORT> read_flash 0 0x1000000 factory-16m.bin
```

Expect a completed read and exactly **16777216 bytes**. Windows:

```powershell
(Get-Item -LiteralPath factory-16m.bin).Length
Get-FileHash -LiteralPath factory-16m.bin -Algorithm SHA256
```

macOS: `stat -f%z factory-16m.bin` and `shasum -a 256 factory-16m.bin`. Linux: `stat -c%s factory-16m.bin` and `sha256sum factory-16m.bin`. Resolve read failures before flashing. Export a separate encrypted NukaVault backup if the device already has entries.

## Install

Get these five files from a local build or a published release, then run from their directory:

```sh
uvx --from esptool==4.11.0 esptool.py --chip esp32s3 --port <PORT> write_flash 0x0 bootloader.bin 0x8000 partition-table.bin 0xF000 ota_data_initial.bin 0x30000 nuka-esp32.bin 0x830000 nukapack.bin
```

Expect each image to be written and verified. The command writes only the specified regions and preserves NVS/Vault; do not issue a full-chip erase. Initial assets go into bank 0. For later upgrades, use OTA, which verifies the inactive bank before committing its NVS selector. Restart and verify the 112 px clock, mixed Chinese/English text, touch and all three buttons. Firmware and resource ABI must match.

An ESP Web Tools page is also included: confirm the full backup, connect, install, close the serial dialog and pair. Its local vendor is patched to disable full-chip erase and uses the same five separate images. Use desktop Chrome/Edge over HTTPS or localhost. Automated local development uses the command above.

## Provision and verify Hub configuration

Run `nuka hub`, then `nuka pair` in another terminal. Enter Wi-Fi credentials interactively on the local machine and confirm the code on the device. Close serial monitors first. Wi-Fi credentials belong in device NVS, not config.toml or the repository. Hub configuration:

```toml
[hub]
name = "<HUB_NAME>"
host = "0.0.0.0"
port = 8790
public_url = "http://192.0.2.10:8790"
```

The documentation address must be replaced locally. Run `nuka doctor`: expect `usb` and `hub_http` JSON records with `health: "ok"`. Every record also includes kind/detail/doc. Check the device reports a paired, connected Hub.

## Restore / uninstall

Stop Hub serial clients and restore this same device's original backup:

```sh
uvx --from esptool==4.11.0 esptool.py --chip esp32s3 --port <PORT> write_flash 0 factory-16m.bin
```

Restart and confirm the original interface returns. Writing the backup replaces the address range; no separate erase command is needed. Preserve Vault backups before restoring. Never burn eFuses or enable Secure Boot / Flash encryption.

## Troubleshooting

- Device absent: use a data cable. Hold **BOOT (GPIO0)** during power-on or reset, then release it; Key3 (GPIO18) is the Back button. ROM mode does not run the application in Flash and may leave the previous screen visible. If the battery keeps the board powered, unplug USB, disconnect the accessible battery connector, wait 10 seconds, then hold BOOT while reconnecting USB. Unplugging USB alone does not prove power was removed. Confirm ROM communication before writing. `sys.download` has reached ROM, but the complete return-to-system sequence has not passed; see the [integration report](../bench/e2e-report.md). `doctor` reports `usb: disabled` and `No nuka USB CDC device; check USB cable and native USB mode`. ROM mode is not nuka CDC, so disabled is expected there.
- Busy serial port: close the IDE, monitor and pairing command before retrying. `usb: ok` confirms enumeration only; it does not prove exclusive access.
- Hub unreachable: start `nuka hub`, check the configured port and firewall. `hub_http: degraded` reports `Hub not reachable; start nuka hub and check hub.port`. `port: error` with `Port occupied by another process` means a local port conflict.
- Resource error page: rebuild matching firmware and nukapack ABI. Doctor does not inspect Flash ABI; use the device page and CDC `info` response.
