# AI CLI hooks

For Windows, macOS and Linux users with Claude Code, Codex or Gemini CLI installed. Hub must be local or on trusted LAN; public tunnels cannot deliver hooks. In v1 Claude supports device approval; Codex/Gemini report status only. Development verification must use `--home` with a temporary directory, never real maintainer configuration.

## Install and verify

Windows PowerShell:

```powershell
nuka hooks install claude
nuka hooks install codex
nuka hooks install gemini
nuka doctor
```

macOS:

```sh
nuka hooks install claude
nuka hooks install codex
nuka hooks install gemini
nuka doctor
```

Linux:

```sh
nuka hooks install claude
nuka hooks install codex
nuka hooks install gemini
nuka doctor
```

Run only the lines for installed CLIs. The installer backs up original bytes, then merges hooks. Expect `Hooks installed; original file backed up.` Review/trust definitions through Codex `/hooks`. Start an ordinary CLI task and expect its session in Web management. Claude PermissionRequest appears on the device; the first allow/deny wins. Offline devices or timeout return an empty decision so terminal approval continues, without default approval.

Configuration:

```toml
[agents]
enabled_sources = ["claude", "codex", "gemini"]
permission_timeout = "60s"
show_project = "basename"
collect_last_message = true
[security]
allow_lan_hooks = false
web_approve_permissions = false
```

Manual Claude example; use a 2-second timeout for non-approval events:

```json
{"hooks":{"PermissionRequest":[{"hooks":[{"type":"http","url":"http://192.0.2.10:8790/hook/claude","timeout":90,"headers":{"Authorization":"Bearer $NUKA_HOOK_TOKEN"},"allowedEnvVars":["NUKA_HOOK_TOKEN"]}]}]}}
```

Merge into existing settings.json. For another machine, enable allow_lan_hooks on Hub and set agent.hub_url on the CLI machine to that trusted LAN Hub. Create `nuka token create --scope hook --name example`; pass its once-shown value through the process environment NUKA_HOOK_TOKEN. Claude HTTP hooks and the Codex/Gemini forwarder use that variable. Do not place tokens in command arguments or hook JSON. Cloudflare headers make loopback traffic public; tunnel hook requests always receive 404.

Windows uses PowerShell by default and does not need a POSIX shell; the Codex/Gemini command is the shell-free `nuka-hook <source> <event>` and runs directly in PowerShell. Set the token with PowerShell syntax:

```powershell
$env:NUKA_HOOK_TOKEN = '<YOUR_TOKEN>'
```

macOS / Linux use `export NUKA_HOOK_TOKEN='<YOUR_TOKEN>'`.

## Uninstall and restore

All platforms:

```sh
nuka hooks uninstall claude
nuka hooks uninstall codex
nuka hooks uninstall gemini
```

Expect `Original hook configuration restored.` A byte diff against the pre-install file must match exactly. Revoke cross-machine tokens when no longer needed. Avoid editing the same CLI configuration while installed: uninstall restores its pre-install version.

## Troubleshooting

| Doctor output / symptom | Action |
|---|---|
| `hub_http: degraded`, `Hub not reachable; start nuka hub and check hub.port` | Start the correctly configured Hub; disconnected forwarders exit quietly without blocking the CLI |
| `port: error`, `Port occupied by another process` | Change the port and reinstall hooks to update URLs |
| `firewall: degraded`, `No inbound TCP allow rule found for the configured port` | Allow trusted LAN access only; loopback hooks do not need LAN access enabled |
| HTTP 404 / 401 | Check public classification or disabled LAN hooks for 404; check hook token scope/environment for 401. Doctor does not perform actual CLI permission requests |

See [Claude's official hooks documentation](https://code.claude.com/docs/en/hooks). The [Codex/Gemini assessment](../bench/codex-gemini-approval.md) records current approval capabilities; v1 does not extend the contract.
