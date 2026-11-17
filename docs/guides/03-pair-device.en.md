# Device pairing and USB management

Requires nuka-hub on Windows, macOS or Linux and a device running nuka firmware. Use a data cable and close serial monitors. Start the Hub and retain its administrator token first. Pairing does not flash, erase Flash or configure eFuse.

## USB pairing

Windows PowerShell:

```powershell
nuka doctor
nuka pair
```

macOS:

```sh
nuka doctor
nuka pair
```

Linux:

```sh
nuka doctor
nuka pair
```

Expect `usb: ok` and `hub_http: ok`. The CLI discovers the single device by VID/product; no port name is saved. Enter an SSID and a hidden Wi-Fi password, up to three networks. Supply WS URLs, for example `ws://192.0.2.10:8790/ws/device,wss://hub.example.com/ws/device`. Compare the Hub name and six-digit code on the device, then confirm there. Rejection or the 60-second timeout does not save provisioning. Expect `Device connected successfully.` and an online device in Web management. LAN WS is supported but carries unencrypted traffic.

Hub configuration:

```toml
[hub]
name = "example"
port = 8790
public_url = "https://hub.example.com"
[mdns]
enabled = true
```

Wi-Fi credentials, device token and Hub URLs belong in device NVS, not Hub TOML. Order URLs by LAN, Tailscale and public access; the paired Hub ID must match. Cloudflare Access optionally prompts for an HTTPS origin and Service Token credentials. Those credentials are sent only to that origin; the device Bearer token remains required.

## SoftAP pairing

Join the `Nuka-XXXX` hotspot shown by the device and open its displayed configuration address. Enter SSID, password and one Hub WS URL. Submit and return to your normal network. On the local/LAN Web device page, select the pending device and enter its displayed six-digit code and a name. Expect reconnection and online status. Pending requests expire after five minutes; five wrong codes invalidate them. Public Web cannot pair devices.

## Vault import/export

Vault uses a local USB process only; data never passes through Hub, Wi-Fi, Web or WS. Unlock Vault on the device and enable its two-minute USB management mode:

```sh
nuka vault export backup.nukavault
nuka vault import backup.nukavault
nuka vault import example-otpauth.txt --otpauth
```

Commands are identical on all three platforms. Passphrases use hidden input; fixed scrypt N=32768/r=8/p=1 derives a key in the CLI, while the device performs AES-GCM. The CLI does not decrypt backups. Verify imported entries on the device. Mutable passphrase/key buffers are cleared and the CLI exits, but CPython/getpass temporary immutable copies cannot be guaranteed erased in place. Do not claim complete process-memory zeroization. Keep plaintext URI lists in a private directory outside Git.

## Revoke and roll back

Delete a device in local Web management to revoke its token and disconnect it. Run pair again to replace provisioning. Before using the device recovery menu, retain an encrypted Vault backup. Do not erase the entire Flash. Users manage deletion of obsolete backup files.

## Troubleshooting

| Doctor output | Action |
|---|---|
| `usb: disabled`, `No nuka USB CDC device; check USB cable and native USB mode` | Check cable, nuka firmware and native USB mode; connect one device and close serial monitors |
| `hub_http: degraded`, `Hub not reachable; start nuka hub and check hub.port` | Start Hub and use the same configuration directory for pair |
| `port: error`, `Port occupied by another process` | Stop the conflicting process or change hub.port and restart Hub |
| `mdns: degraded`, `Hub discovery failed; check UDP multicast/firewall or use the paired URL` | Allow multicast or use an explicit paired URL; devices do not automatically trust a different Hub |

Rejection, timeout and a locked Vault are device errors; doctor only probes USB availability. Consult bench reports for actual hardware round-trip evidence.
