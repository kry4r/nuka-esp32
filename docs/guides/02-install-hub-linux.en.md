# Install nuka-hub on Linux

Use Linux with systemd user services and Python ≥3.11. Run the Hub as your normal user; firewall rules and persistent user services require sudo. Start with psutil only, leaving external sources disabled or pointing to local fixtures.

Install uv using the command for your platform. The remaining service steps apply to Linux.

```powershell
# Windows PowerShell
winget install --id astral-sh.uv -e
```

```sh
# macOS
brew install uv
# Linux
curl -LsSf https://astral.sh/uv/install.sh | sh
uv --version
uv tool install --python 3.12 'nuka-hub[all]==0.1.0'
nuka --version
nuka init
```

Before the first release, run `uv tool install --python 3.12 './hub[all]'` from the repository root. Expect version `0.1.0`. Configuration is at `~/.config/nuka/config.toml`. Save the administrator token shown once during initialization.

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

Replace the example address with a LAN address reachable by the device. Run `nuka hub` in the foreground, then use a second terminal:

```sh
curl --fail http://localhost:8790/api/v1/health
nuka doctor
```

Expect `ok:true`, `proto:1`, and `hub_http: ok`. Open `http://localhost:8790` and sign in with the administrator token. A headless machine without a desktop keyring can explicitly store secrets in a private file:

```sh
nuka secret set ai.example --file
chmod 600 ~/.config/nuka/secrets.toml
```

Enter the value interactively. Configuration references names only. Keep real AI services unconfigured during development. Stop the foreground process before installing the service:

```sh
nuka service install
nuka service status
systemctl --user status nuka-hub.service
sudo loginctl enable-linger "$USER"
```

Expect an active user service; check health again after logging in or rebooting. For UFW, replace the documentation subnet with your actual LAN subnet before opening the required ports:

```sh
sudo ufw allow from 192.0.2.0/24 to any port 8790 proto tcp
sudo ufw allow from 192.0.2.0/24 to any port 5353 proto udp
nuka doctor
```

Check health from a second LAN machine. If multicast discovery fails, use the paired URL. See [pairing](03-pair-device.en.md) for USB permissions and [sensors](04-hardware-sensors.en.md) for media and hardware collection.

Uninstall / rollback: preserve configuration, secrets and data; back up the data directory before upgrading.

```sh
nuka service uninstall
sudo ufw delete allow from 192.0.2.0/24 to any port 8790 proto tcp
sudo ufw delete allow from 192.0.2.0/24 to any port 5353 proto udp
uv tool uninstall nuka-hub
# Reinstall a released version:
uv tool install --force --python 3.12 'nuka-hub[all]==0.1.0'
```

Use the same actual subnet when removing rules. If linger was enabled solely for nuka, remove it with `sudo loginctl disable-linger "$USER"` when no longer needed.

| Symptom / doctor output | Resolution |
|---|---|
| `hub_http: degraded` | Start in the foreground; check configuration, service paths and port |
| `port: error`, `Port occupied by another process` | Stop the duplicate process; reinstall the service after changing its port |
| `firewall: degraded` | Check UFW / nftables and TCP 8790 from another machine |
| `mdns: degraded`, `Hub discovery failed; check UDP multicast/firewall or use the paired URL` | Check multicast and network isolation; use the paired URL |
| `usb: disabled` | Check the data cable, device mode and serial group membership; log in again |

uv installation follows its [official documentation](https://docs.astral.sh/uv/getting-started/installation/). Keep the system firewall enabled.
