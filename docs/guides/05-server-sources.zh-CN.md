# 服务器数据源

适用于 Windows、macOS、Linux 上的 Python 3.11+ 和 nuka-hub。先准备现有监控系统的只读接口；Hub 不负责安装上游服务。建议通过私网或鉴权 HTTPS 访问，仅在发布接口、创建只读凭据时需要上游管理权限。[be-adapters](../bench/be-adapters.md) 记录了 API 形状与假数据验证；不代表真实部署版本已经实测。

修改前备份 `config.toml`。先加一个 source，重启 `nuka hub`，再运行 `nuka doctor`。下列命令检查自行填写的接口，不会把响应写入仓库：

Windows PowerShell：

```powershell
$monitorUrl = Read-Host '监控系统只读 URL'
Invoke-RestMethod -Uri $monitorUrl
nuka doctor
```

macOS：

```sh
read -r monitor_url
curl --fail "$monitor_url"
nuka doctor
```

Linux：

```sh
read -r monitor_url
curl --fail "$monitor_url"
nuka doctor
```

应看到 JSON（exporter 为 Prometheus 文本），doctor 中 source 的 `health` 为 `ok`。使用本机 Web 管理页的管理员会话查看 `GET /api/v1/servers`，核对名称、在线状态和单位；没有的指标保持缺省。

## ServerStatus

向 Hub 开放 cppla/ServerStatus 的 `json/stats.json` 只读路由。接口本身不要求 Hub 凭据，应通过防火墙或代理限制访问。JSON 应有 `servers`、`online4`、`online6` 及资源指标。[上游客户端](https://github.com/cppla/ServerStatus/blob/master/clients/client-linux.py) 的内存单位为 KiB、磁盘为 MiB、网络为字节/秒，Hub 将容量转换为字节。

```toml
[[servers.source]]
type = "serverstatus"
name = "status"
url = "https://status.example.com/json/stats.json"
interval = "5s"
```

重启后应看到 `src: serverstatus` 和上游名称；离线条目保留身份，不生成虚假的资源值。

## Uptime Kuma

在 Kuma 创建公开状态页，添加需要的监控项并记下 slug。Hub 使用两个[公开状态页 API](https://github.com/louislam/uptime-kuma/wiki/Internal-API)，不需要 Kuma 管理员令牌。

```toml
[[servers.source]]
type = "uptime_kuma"
name = "kuma"
url = "https://kuma.example.com"
interval = "30s"
[servers.source.options]
slug = "example"
```

验证 `/api/status-page/example` 返回分组、`/api/status-page/heartbeat/example` 返回心跳历史。Hub 应显示在线状态、延迟与 24 小时可用率；CPU 与容量字段缺省。

## Beszel

使用有权查看目标系统的账号，或该账号已有的 PocketBase API 令牌。在隐藏提示中填写 `email:password` 或 `token:<YOUR_TOKEN>`：

```sh
nuka secret set servers.beszel
```

该命令适用于 PowerShell、macOS 和 Linux，完成后不应打印密钥。只读路由为 `/api/collections/systems/records`，密码登录使用 `/api/collections/users/auth-with-password`，上游见 [Beszel](https://github.com/henrygd/beszel)。

```toml
[[servers.source]]
type = "beszel"
name = "beszel"
url = "https://beszel.example.com"
auth_secret = "servers.beszel"
interval = "10s"
```

应看到 CPU、内存百分比、磁盘百分比、运行时长和在线状态。systems info 不提供内存/磁盘总容量，因此不伪造字节数，也不把汇总带宽猜成上传或下载速率。

## Netdata

允许 Hub 通过私网读取每个节点的 v1 API，每节点配置一个 source；v1 不支持 Netdata Cloud。需要 `system.cpu`、`system.ram`、`disk_space._`、`system.net` 四张 chart。上游见 [Netdata](https://github.com/netdata/netdata/tree/v2.12.1)。

```toml
[[servers.source]]
type = "netdata"
name = "nd-example"
url = "http://192.0.2.11:19999"
interval = "5s"
```

验证 `/api/v1/info` 有 `hostname`，`/api/v1/data?chart=system.cpu&points=1&after=-5` 有 `labels`、`data`。Hub 中应看到 CPU、统一为字节的容量、上传和下载速率。

## node_exporter / Prometheus

开启 [node_exporter](https://github.com/prometheus/node_exporter/tree/v1.12.1) 的 CPU、内存、文件系统、网络、时间采集器，将 `/metrics` 限制为 Hub 可访问。第一次采样建立基线，第二次才有差分 CPU 和网速。

```toml
[[servers.source]]
type = "node_exporter"
name = "node-example"
url = "http://192.0.2.12:9100"
interval = "15s"
[servers.source.options]
mode = "exporter"
```

也可以把 `url` 改为 `http://192.0.2.12:9090`，设置 `mode = "prometheus"` 和 `instance_label = "instance"`。验证 `/api/v1/query?query=node_memory_MemTotal_bytes` 返回成功的 instant vector；所有指标中的身份标签必须一致。Basic auth 在 source 上设置 `auth_secret = "servers.prom"`，再用 `nuka secret set servers.prom` 隐藏输入 `user:password`。应按 instance 拆分服务器，有 CPU 速率和内存/磁盘字节数；缺少的指标不生成。

## 通用 JSON 与合并

开放 JSON 只读接口或创建只读令牌。JSONPath 选择行与字段，scale 做单位换算。需要 Bearer 令牌时，在 source 上写 `auth_secret = "servers.custom"`，再运行 `nuka secret set servers.custom`。

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

输入 `{"data":{"hosts":[{"hostname":"web1","up":true,"cpu":12.3,"mem_ratio":0.5}]}}` 应得到 `example-web`、CPU 12.3%、内存 50%。同字段优先使用配置顺序较前的数据源，别名归并为一个条目；设置 `by = "none"` 可保留独立条目。

## 常见错误与回滚

| `nuka doctor` 现象 | 排查 |
|---|---|
| source 为 `error`，HTTP 401/403 | 修正只读凭据及权限，不要使用 AI 管理密钥。 |
| source 为 `error`，字段校验失败 | 对照上游版本和 fixture 形状，不伪造缺失字段。 |
| `beszel disabled: Beszel credential is missing` | 运行 `nuka secret set servers.beszel` 并重启。 |
| source 为 `error`，连接超时 | 检查 URL、私网路由、防火墙及配置的 `timeout`。 |

回滚时删除新增的 `[[servers.source]]` 或恢复配置备份，重启 Hub。用 `nuka secret rm servers.beszel` / `servers.prom` / `servers.custom` 删除不用的凭据，并在上游撤销专用令牌；其他客户端仍在使用监控系统时保留上游服务。
