# Server data sources

Use nuka-hub with Python 3.11+ on Windows, macOS, or Linux. Configure an existing monitor's read endpoint; the Hub does not install upstream servers. Access through a private network or authenticated HTTPS. Administrative access to the monitor is needed only to publish its endpoint or create a read credential. API shapes and fixture verification are recorded in [be-adapters](../bench/be-adapters.md); these are synthetic fixture checks, not certification of deployed versions.

Back up `config.toml` before editing. Start with one source, restart `nuka hub`, and run `nuka doctor`. The commands below inspect your chosen endpoint without storing its response in the repository:

Windows PowerShell:

```powershell
$monitorUrl = Read-Host 'Monitor read URL'
Invoke-RestMethod -Uri $monitorUrl
nuka doctor
```

macOS:

```sh
read -r monitor_url
curl --fail "$monitor_url"
nuka doctor
```

Linux:

```sh
read -r monitor_url
curl --fail "$monitor_url"
nuka doctor
```

Expect JSON, or Prometheus text for an exporter, and source `health: ok`. With an administrator token, inspect `GET /api/v1/servers` in the local Web management page's API session. Confirm server names, online state, and units; absent fields stay absent.

## ServerStatus

Publish cppla/ServerStatus's `json/stats.json` read route to the Hub. It has no built-in Hub credential requirement; restrict it at the network or proxy. Verify that the JSON contains `servers` with `online4`, `online6`, and resource counters. The client format uses KiB for memory, MiB for disk, and bytes/s for network; Hub converts capacities to bytes. See [upstream client](https://github.com/cppla/ServerStatus/blob/master/clients/client-linux.py).

```toml
[[servers.source]]
type = "serverstatus"
name = "status"
url = "https://status.example.com/json/stats.json"
interval = "5s"
```

After restart, expect `src: serverstatus` and the published names. Offline entries retain identity without invented resource values.

## Uptime Kuma

Create a public status page in Kuma, add the desired monitors, and copy its slug. Hub reads the two [public status-page APIs](https://github.com/louislam/uptime-kuma/wiki/Internal-API); no Kuma administrator token is needed.

```toml
[[servers.source]]
type = "uptime_kuma"
name = "kuma"
url = "https://kuma.example.com"
interval = "30s"
[servers.source.options]
slug = "example"
```

Verify `/api/status-page/example` returns groups and `/api/status-page/heartbeat/example` returns heartbeat history. Expect online status, latency, and 24-hour availability. CPU and capacity fields are omitted.

## Beszel

Use an account allowed to view the target systems, or an existing PocketBase API token for that account. Store `email:password` or `token:<YOUR_TOKEN>` at a hidden prompt:

```sh
nuka secret set servers.beszel
```

The command works in PowerShell, macOS, and Linux. It should complete without printing the secret. The read route is `/api/collections/systems/records`; password authentication uses `/api/collections/users/auth-with-password`. See [Beszel](https://github.com/henrygd/beszel).

```toml
[[servers.source]]
type = "beszel"
name = "beszel"
url = "https://beszel.example.com"
auth_secret = "servers.beszel"
interval = "10s"
```

Expect CPU, memory percentage, disk percentage, uptime, and online state. The systems info contains percentages rather than memory/disk capacities, so Hub does not invent byte totals or split aggregate bandwidth into directional rates.

## Netdata

Allow the Hub to read each node's v1 API on its private address. Configure one source per node; Netdata Cloud is outside v1 scope. Required charts are `system.cpu`, `system.ram`, `disk_space._`, and `system.net`. See [Netdata source](https://github.com/netdata/netdata/tree/v2.12.1).

```toml
[[servers.source]]
type = "netdata"
name = "nd-example"
url = "http://192.0.2.11:19999"
interval = "5s"
```

Verify `/api/v1/info` has `hostname`; `/api/v1/data?chart=system.cpu&points=1&after=-5` has `labels` and `data`. Expect CPU plus byte capacities and directional network rates after Hub normalization.

## node_exporter and Prometheus

Enable the [node_exporter](https://github.com/prometheus/node_exporter/tree/v1.12.1) CPU, memory, filesystem, network, and time collectors. Restrict `/metrics` to the Hub. The first exporter sample supplies a baseline; the second supplies CPU/network deltas.

```toml
[[servers.source]]
type = "node_exporter"
name = "node-example"
url = "http://192.0.2.12:9100"
interval = "15s"
[servers.source.options]
mode = "exporter"
```

Alternatively point `url` to `http://192.0.2.12:9090`, set `mode = "prometheus"`, and `instance_label = "instance"`. Verify `/api/v1/query?query=node_memory_MemTotal_bytes` returns a successful instant vector. The label must identify the same machine in all series. For Basic auth set `auth_secret = "servers.prom"` on the source and enter `user:password` with `nuka secret set servers.prom`. Expect one entry per instance with CPU rate and memory/disk bytes; targets lacking a metric omit that field.

## Generic JSON and merging

Expose a read JSON route or create a read token. JSONPath selects rows and fields; scaling converts the source unit. If required, use `auth_secret = "servers.custom"` for a Bearer token and store it with `nuka secret set servers.custom`.

```toml
[[servers.source]]
type = "generic_json"
name = "custom"
url = "https://monitor.example.com/api/hosts"
interval = "30s"
[servers.source.options]
items = "$.data.hosts[*]"
[servers.source.options.fields]
name = "$.hostname"
online = "$.up"
cpu_pct = "$.cpu"
mem_pct = { path = "$.mem_ratio", scale = 100 }

[servers.merge]
by = "name"
aliases = { "example-web" = ["web1", "web1.example.com"] }
```

An input `{"data":{"hosts":[{"hostname":"web1","up":true,"cpu":12.3,"mem_ratio":0.5}]}}` produces `example-web`, CPU 12.3%, memory 50%. Earlier configured sources win for fields present in multiple sources; aliases merge into one entry. Set `by = "none"` to keep sources separate.

## Troubleshooting and rollback

| `nuka doctor` finding | Check |
|---|---|
| Source `error` with HTTP 401/403 | Correct the read credential and scope; do not use an AI management key. |
| Source `error` with validation detail | Compare the upstream version and documented fixture shape; do not fabricate missing fields. |
| `beszel disabled: Beszel credential is missing` | Run `nuka secret set servers.beszel` and restart Hub. |
| Source `error` with connection timeout | Verify URL, private route, firewall, and configured `timeout`. |

Remove the added `[[servers.source]]` blocks or restore the backup, then restart Hub. Delete unused credentials with `nuka secret rm servers.beszel` / `servers.prom` / `servers.custom`. Revoke any upstream token created for this integration; leave the upstream monitoring service installed if other clients use it.
