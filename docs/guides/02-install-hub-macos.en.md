# Install nuka-hub on macOS

Use macOS on Apple Silicon or Intel with Python 3.11 or newer. Install the LaunchAgent as your login user. Firewall changes require administrator privileges. Start with psutil only; disable other upstreams or use local fixtures during validation.

Install uv for your platform. Service commands below apply to macOS.

```powershell
# Windows PowerShell
winget install --id astral-sh.uv -e
```

```sh
# macOS
brew install uv
# Linux
curl -LsSf https://astral.sh/uv/install.sh | sh
```

Check `uv --version`, then install and initialize:

```sh
uv tool install --python 3.12 'nuka-hub[all]==0.1.0'
nuka --version
nuka init
```

Before the first PyPI release, use `uv tool install --python 3.12 './hub[all]'` from the repository root. Expect `0.1.0`. Keep the administrator token shown once. Configuration defaults to `~/Library/Application Support/nuka/config.toml`; Chinese is the initial language.

Replace the documentation address below. A device cannot use the Hub computer's localhost address.

```toml
[hub]
name = "nuka-hub"
host = "0.0.0.0"
port = 8790
lang = "zh-CN"
public_url = "http://192.0.2.10:8790"

[collectors]
enabled = ["psutil"]

[weather]
enabled = false
```

Run in the foreground, then check from another terminal:

```sh
nuka hub
```

```sh
curl --fail http://localhost:8790/api/v1/health
nuka doctor
```

Expect `ok:true`, `proto:1`, and `hub_http: ok`. Open `http://localhost:8790` and sign in. Stop the foreground process and install the service:

```sh
nuka service install
nuka service status
launchctl print "gui/$(id -u)/io.nuka.hub"
```

Expect the `io.nuka.hub` LaunchAgent. Verify health again and after logging out and back in. Do not run a duplicate foreground Hub. See the [sensor guide](04-hardware-sensors.en.md) for macmon and permissions.

Allow inbound connections for the installed Python when macOS asks. To add it explicitly to the application firewall:

```sh
nukaPython="$(uv tool dir)/nuka-hub/bin/python"
sudo /usr/libexec/ApplicationFirewall/socketfilterfw --add "$nukaPython"
sudo /usr/libexec/ApplicationFirewall/socketfilterfw --unblockapp "$nukaPython"
nuka doctor
```

Doctor reports the firewall inspection, but you still need to verify TCP access from another LAN machine. The saved pairing address can be used when mDNS is unavailable. Leave the firewall enabled.

Uninstall or roll back as follows. Configuration and keychain entries remain; back up the data directory before upgrading.

```sh
nuka service uninstall
sudo /usr/libexec/ApplicationFirewall/socketfilterfw --remove "$(uv tool dir)/nuka-hub/bin/python"
uv tool uninstall nuka-hub
# Reinstall an available release:
uv tool install --force --python 3.12 'nuka-hub[all]==0.1.0'
```

| Symptom / doctor output | Action |
|---|---|
| `hub_http: degraded` | Run in the foreground; check the LaunchAgent's configuration and Python paths |
| `port: error`, `Port occupied by another process` | Stop the other Hub, or change the port and reinstall the agent |
| `mdns: degraded`, `Hub discovery failed; check UDP multicast/firewall or use the paired URL` | Check LAN multicast or connect using the saved address |
| `firewall: degraded` | Verify TCP 8790 from another machine and review application firewall/network permissions |
| `usb: disabled` | Check the cable and firmware mode; follow [pairing](03-pair-device.en.md) |

uv commands follow its [official documentation](https://docs.astral.sh/uv/getting-started/installation/). See CONFIG and PROTOCOL for Hub behavior.
