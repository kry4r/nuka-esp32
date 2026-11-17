# Tailscale and public access

For Hub on Windows, macOS and Linux. Complete local pairing first and retain local administrator access. Public access supports viewing management, device/Agent WS and generic notifications. Hooks, Web approval and pairing remain unavailable to public requests. Configuration and secret operations are blocked by default; D30 explicitly enables remote management with both Access identity and a Hub admin session / CSRF. See the [server guide](15-server-deployment.en.md). Replace all example domains and tokens.

## Tailscale

Install/sign in using the [official instructions](https://tailscale.com/download). Windows PowerShell, macOS and Linux can run:

```sh
tailscale status
nuka doctor
```

Expect your nodes to be connected. Allow the Hub TCP port only to the required nodes. Devices do not run Tailscale natively; provide a reachable LAN/subnet route and pair with that Hub URL. The default security.lan_cidrs includes Tailscale CGNAT space. Configure custom networks explicitly.

## cloudflared and Access

Requires Cloudflare administration, a domain and cloudflared installed. Windows PowerShell:

```powershell
cloudflared tunnel login
cloudflared tunnel create nuka-hub
nuka tunnel setup
cloudflared tunnel route dns nuka-hub hub.example.com
cloudflared tunnel --config "<CONFIG_DIR>/cloudflared/config.yml" run nuka-hub
```

macOS:

```sh
cloudflared tunnel login
cloudflared tunnel create nuka-hub
nuka tunnel setup
cloudflared tunnel route dns nuka-hub hub.example.com
cloudflared tunnel --config "<CONFIG_DIR>/cloudflared/config.yml" run nuka-hub
```

Linux:

```sh
cloudflared tunnel login
cloudflared tunnel create nuka-hub
nuka tunnel setup
cloudflared tunnel route dns nuka-hub hub.example.com
cloudflared tunnel --config "<CONFIG_DIR>/cloudflared/config.yml" run nuka-hub
```

The create command reports the Tunnel ID and credential path. Enter that same ID/path, hostname and Access settings in the wizard. Expect cloudflared/config.yml targeting the configured Hub port and ending with http_status:404. The wizard writes configuration; it does not log in or create cloud resources.

Create a Self-hosted Access application for the hostname. Add an allow policy for Web users and separate Service Tokens/Service Auth policies for devices and Agents. Copy the actual team domain and application AUD, then restart Hub:

```toml
[hub]
public_url = "https://hub.example.com"
port = 8790
[security]
web_approve_permissions = false
allow_lan_hooks = false
[security.cf_access]
enabled = true
team_domain = "access.example.com"
aud = "<ACCESS_AUD>"
require_for_ws = true
```

The example team_domain is a placeholder: replace it with the actual Access team domain. Hub verifies RS256, issuer, audience and expiry of edge JWTs; the corresponding admin/device/agent Bearer token remains necessary. Device Service Token credentials are hidden-input USB provisioning fields. Agents use `nuka secret set agent.cf_id` and `nuka secret set agent.cf_secret`; configuration contains only cf_client_id_secret / cf_client_secret_secret references. Credentials go only to the HTTPS origin corresponding to WSS.

Verify: public health returns `{ok,ver,proto}`; other public requests without an Access assertion receive 403. /hook/server and /hook/claude return 404. A local request with `Cf-Ray: fixture` can verify public classification without a real tunnel. Web login does not enable public configuration/approval. Doctor's hub_http probes local service, not cloud policy. See [official Service Token instructions](https://developers.cloudflare.com/cloudflare-one/access-controls/service-credentials/service-tokens/).

## Remove and restore

Stop cloudflared; remove this integration's DNS route, Tunnel, Access application and Service Tokens without affecting other applications. Set cf_access.enabled=false and restart Hub, remove obsolete public_url and re-pair to remove device credentials. Sign out/uninstall Tailscale through its client without altering other nodes' ACLs. Restore config.toml.bak.* when needed.

## Troubleshooting

| Doctor output / symptom | Action |
|---|---|
| `hub_http: degraded`, `Hub not reachable; start nuka hub and check hub.port` | Restore local Hub first, then match tunnel service port |
| `port: error`, `Port occupied by another process` | Change the listen port and regenerate tunnel ingress |
| `mdns: degraded`, `Hub discovery failed; check UDP multicast/firewall or use the paired URL` | Public tunnels do not carry mDNS; provision an explicit WSS URL |
| `firewall: degraded` | Inspect the reported local firewall condition for LAN access; cloudflared needs outbound connectivity, not a public inbound Hub port |
| 403 / 404 | Check Access audience/issuer/policy for 403. Hooks, approval and configuration intentionally return public 404. Do not add proxies to trusted_proxies to bypass policy |

CF headers force loopback traffic into public. Uvicorn does not rewrite proxy headers; the zone layer handles the real TCP peer.
