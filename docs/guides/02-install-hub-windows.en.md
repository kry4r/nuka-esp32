# Install nuka-hub on Windows

Use Windows 10/11 x64 or ARM64 with Python 3.11 or newer. Installation runs as the current user; firewall rules and elevated LHM tasks require administrator privileges. Chinese is the default UI language. During development, disable weather, AI and server upstreams or use local fixtures.

Install uv using the command for your platform. The remaining commands in this guide use PowerShell.

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

Open a new terminal and check `uv --version`. Install a published release:

```powershell
uv tool install --python 3.12 'nuka-hub[all]==0.1.0'
nuka --version
nuka init
```

Before the first PyPI release, use `uv tool install --python 3.12 './hub[all]'` from this repository instead. Do not install an unrelated project with the same name. Expect version `0.1.0`. Choose `zh-CN` or `en`, retain the administrator token shown once, and initially skip optional integrations. Configuration defaults to `%APPDATA%\nuka\config.toml`; use `--config` consistently for a custom path.

The following snippet matches CONFIG. Replace the documentation address with your deployment address before pairing.

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

Run the Hub in the foreground, then check it from another PowerShell:

```powershell
nuka hub
```

```powershell
Invoke-RestMethod http://localhost:8790/api/v1/health
nuka doctor
```

Expect `ok=true`, `proto=1`, and a doctor `hub_http` result of `ok`. Open `http://localhost:8790` and sign in with the administrator token. After checking the management page, stop the foreground process with Ctrl+C and install the login task:

```powershell
nuka service install
nuka service status
schtasks /Run /TN nuka-hub
```

Expect a scheduled task named `nuka-hub`. Check health again. The task starts after login. For LHM, follow the [sensor guide](04-hardware-sensors.en.md); use `nuka service install --elevated` from administrator PowerShell when needed.

To permit LAN connections, run these rules from administrator PowerShell. They apply only to private networks and the local subnet:

```powershell
New-NetFirewallRule -DisplayName 'nuka-hub TCP' -Direction Inbound -Action Allow -Protocol TCP -LocalPort 8790 -RemoteAddress LocalSubnet -Profile Private
New-NetFirewallRule -DisplayName 'nuka-hub mDNS' -Direction Inbound -Action Allow -Protocol UDP -LocalPort 5353 -RemoteAddress LocalSubnet -Profile Private
nuka doctor
```

Expect the firewall check to find an inbound TCP rule. Successful discovery reports `mdns: ok`. Verify the configured address from another LAN machine, then follow [device pairing](03-pair-device.en.md).

Uninstall or roll back with the commands below. Configuration, databases and keychain entries remain. Back them up before upgrading and restore the matching backup when reverting a release.

```powershell
nuka service uninstall
uv tool uninstall nuka-hub
Get-NetFirewallRule -DisplayName 'nuka-hub TCP','nuka-hub mDNS' | Remove-NetFirewallRule
# Reinstall a release that is still available:
uv tool install --force --python 3.12 'nuka-hub[all]==0.1.0'
```

| Symptom / doctor output | Action |
|---|---|
| `hub_http: degraded`, `Hub not reachable; start nuka hub and check hub.port` | Run the Hub in the foreground and check the configuration path used by the task |
| `port: error`, `Port occupied by another process` | Stop the duplicate Hub or change `hub.port` and reinstall the task |
| `firewall: degraded`, `No inbound TCP allow rule found for the configured port` | Check the port, network profile and administrator permissions |
| `lhm_privilege: degraded` | Run LHM elevated; ordinary psutil collection does not need elevation |
| `usb: disabled` | Check the data cable and TinyUSB firmware; ROM download mode is not nuka CDC |

uv installation follows its [official documentation](https://docs.astral.sh/uv/getting-started/installation/). CONFIG and PROTOCOL define the service and security behavior.
