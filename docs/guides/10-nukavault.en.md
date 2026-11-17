# NukaVault

Use project firmware on the ESP32-S3, a USB data cable, and the installed Python 3.11+ nuka CLI on Windows, macOS or Linux. Vault commands run in the local USB CLI; the Hub service need not run. The current user needs CDC device access. On Linux, grant serial-device access using the distribution's group policy and log in again. No eFuse programming is part of setup.

Open NukaVault on the device, set and repeat a 6–8 digit PIN, unlock, and enter USB management mode. Authorization lasts two minutes. The PIN protects this device; a separate backup passphrase protects exported files and is needed when restoring on another device. Start with synthetic entries.

The default red software-mode warning means a random root key resides in NVS. A Flash reader can guess the PIN offline. A previously provisioned eFuse HMAC slot has a read-only code path; this development workflow never programs it. eFuse programming is irreversible and requires separate explicit maintainer authorization. Secure Boot and Flash encryption are off. Someone able to install malicious firmware can capture the next PIN; physical probes and side channels are outside the threat model.

Attempts are persisted before PIN verification. Five failures cause a five-minute cooldown; ten erase the independent Vault partition by default. The erase threshold supports twenty or disabled. A reboot restarts the full cooldown instead of bypassing it. Screen off, double Key3, leaving the Vault card for thirty seconds, and unlock expiry lock the Vault. Erasing the Vault is not a backup.

## Configuration and connection

Vault has no `config.toml` section and needs no Hub URL or secret. Keep the current Hub configuration:

```toml
# NukaVault is configured locally on the device; it has no TOML section.
```

Run `nuka doctor`. A connected device should yield an `ok` USB check as line-delimited JSON: `{"name":"usb","kind":"system","health":"ok","detail":"nuka USB CDC available","doc":"guides/03-pair-device"}`. Doctor checks CDC availability; it never reads entries or verifies a PIN or backup passphrase. Close serial monitors, then unlock and enable USB management on the device.

## Export, restore and otpauth

Before each command, check the device is unlocked and management authorization remains valid. Export requires local confirmation. Import authenticates and parses first, previews added/skipped counts, and writes only after local approval.

Windows PowerShell:

```powershell
nuka doctor
nuka vault export "$env:TEMP/example.nukavault"
Get-Item "$env:TEMP/example.nukavault" | Select-Object Length
nuka vault import "$env:TEMP/example.nukavault"
nuka vault import "$env:TEMP/example-otpauth.txt" --otpauth
```

macOS:

```sh
nuka doctor
nuka vault export /tmp/example.nukavault
wc -c /tmp/example.nukavault
nuka vault import /tmp/example.nukavault
nuka vault import /tmp/example-otpauth.txt --otpauth
```

Linux:

```sh
nuka doctor
nuka vault export /tmp/example.nukavault
wc -c /tmp/example.nukavault
nuka vault import /tmp/example.nukavault
nuka vault import /tmp/example-otpauth.txt --otpauth
```

Enter the backup passphrase at the hidden prompt. Successful export prints `Encrypted backup written.` and produces at least 55 bytes. Successful import prints counts such as `{"added":2,"skipped":0}`. Existing entries with the same case-sensitive type/name pair are skipped. To verify recovery, export first, deliberately erase NukaVault in the device recovery menu, set a new PIN, enable management mode, restore, and compare entries and TOTP. Erasing destroys the current library; verify the backup and passphrase first.

Keep `example-otpauth.txt` private, with one URI per line. This public RFC test key is suitable only for demonstration:

```text
otpauth://totp/Example:demo?secret=GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ&issuer=Example&algorithm=SHA1&digits=6&period=30
```

NVB1 v1 fixes scrypt at `N=32768,r=8,p=1`. The CLI derives a key and transports ciphertext; AES-GCM decryption and entry parsing run in device internal RAM. Data never crosses the Hub, network or Web. The device zeroes plaintext and keys after use. CPython immutable temporary copies cannot all be individually wiped, so the CLI exits after transfer. The current Vault partition is 64 KiB; the device rejects a declared import larger than its actual partition capacity and leaves existing entries unchanged on insufficient space.

Open a synthetic document in a text editor, select username, password or combined typing, and hold the device confirmation for at least 0.5 seconds. Combined typing sends username, Tab, password, Enter. Check input focus locally every time. Typing requires USB. Passwords stay hidden until a long press reveals them for five seconds.

## Troubleshooting and rollback

| Symptom / doctor output | Action |
|---|---|
| USB check is not `ok` | Check the data cable, serial-device permission and USB mode. Close other serial programs and repeat doctor. |
| USB is `ok`, but the command requests unlock/management mode | Touch-enter the PIN and enable management mode locally; renew it after two minutes. |
| USB is `ok`, import returns `bad_request` | Check the passphrase, NVB1 file, size and otpauth algorithm. Authentication failure leaves entries intact. Doctor does not inspect files. |
| USB is `ok`, the device reports cooldown | Wait for the full countdown; power cycling cannot bypass it. Never repeatedly guess against a real library. |
| USB is `ok`, TOTP reports stale time | Synchronize the configured SNTP source or RTC. Seven days without synchronization triggers a warning. |

Cancel local confirmation or let management authorization expire to abort a transfer. Restore from a verified encrypted backup when needed; import skips matching entries rather than overwriting them. Removing the CLI does not erase device entries. Use the device recovery menu to erase intentionally. Remove temporary private URI/backup files after a demonstration; keep production backups elsewhere securely and never commit them to git.
