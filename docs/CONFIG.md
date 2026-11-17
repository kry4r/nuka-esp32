# nuka-hub 配置（CONFIG）

| 项 | 值 |
|---|---|
| 状态 | 首次本地冻结，C-01（2026-10-17）；待远程 PR 评审；BE 轨道实现为 pydantic 模型；交付事项见 protocol/DECISIONS.md |
| 格式 | TOML（`config.toml`） |
| 生成方式 | `nuka init` 交互式生成；也可以手写 |
| 关联文档 | SPEC §7、PROTOCOL.md |

> 本文中出现的所有地址、令牌和名称都是**占位符**：
> - `192.0.2.x` 是 RFC 5737 保留的文档地址；
> - `*.example.com` / `*.example.lan` 是示例域名；
> - `<…>` 表示需要你自己填写的值。

---

## 1. 位置与加载
| 平台 | 默认路径 |
|---|---|
| Windows | `%APPDATA%\nuka\config.toml` |
| macOS | `~/Library/Application Support/nuka/config.toml` |
| Linux | `~/.config/nuka/config.toml`（遵循 `$XDG_CONFIG_HOME`） |
| Docker | `/config/config.toml` |

- **配置文件路径**的优先级：命令行参数 `--config PATH` > 环境变量 `NUKA_CONFIG` > 上表中的默认路径。
- **数据目录**（存放 SQLite、OTA 文件、令牌哈希）的默认位置：
  - Windows：`%LOCALAPPDATA%\nuka\data`
  - macOS：`~/Library/Application Support/nuka/data`
  - Linux：`~/.local/share/nuka`
  - Docker：`/data`
  - 可以用 `hub.data_dir` 覆盖。
- **加载规则**：
  - 用 pydantic 校验，未知的键直接报错（`extra = "forbid"`），并提示最接近的合法键名。
  - 报错信息中要包含 TOML 的行号。
- **环境变量覆盖**：格式为 `NUKA__<SECTION>__<KEY>`，用双下划线分隔层级。例如 `NUKA__HUB__PORT=8790`、`NUKA__WEATHER__LAT=0`。这主要是为了方便 Docker 部署。
- **热重载**：
  - 配置文件变更后，Hub 重新加载 `servers`、`alerts`、`ai`、`agents`、`weather`、`notify` 这几节，只重启受影响的插件。
  - 修改 `hub.host`、`hub.port` 和 `security` 这一节需要重启 Hub，日志中会给出提示。

## 2. 密钥（secrets）
配置文件中**不保存任何密钥明文**，只用 `*_secret = "<名称>"` 按名称引用。读取时依次查找以下三个位置，找到即停：
1. 环境变量 `NUKA_SECRET_<NAME>`：名称转大写，`.` 和 `-` 都换成 `_`。例如 `ai.cpamp.main` 对应 `NUKA_SECRET_AI_CPAMP_MAIN`。
2. 系统钥匙串：通过 `keyring` 访问，service 名为 `nuka-hub`，username 为密钥名称。
   - Windows 使用凭据管理器；
   - macOS 使用钥匙串；
   - Linux 使用 Secret Service。没有图形桌面的服务器可能没有可用的 Secret Service，这时跳过这一项。
3. `secrets.toml`：与 `config.toml` 放在同一个目录，内容为扁平的 `name = "value"` 键值对；包含点号的名称必须加引号，例如 `"ai.main" = "<YOUR_TOKEN>"`，避免被 TOML 解释为嵌套表。在 POSIX 系统上，文件权限必须是 `600`，否则拒绝读取并报错。Windows 上不检查权限。

用 `nuka secret set <name>` 写入密钥：值从终端隐藏输入，默认写入钥匙串；钥匙串不可用时，提示改用 `--file` 写入 `secrets.toml`。

**约定的密钥名称**（`nuka init` 按这些名称生成）：
| 名称 | 用途 |
|---|---|
| `ai.<provider name>` | CPAMP 管理员密钥，或 CPA 管理密钥 |
| `servers.<source name>` | 服务器数据源的令牌或密码（例如 Beszel、Prometheus 的 basic auth） |
| `collectors.lhm` | LibreHardwareMonitor 远程 Web 服务的密码（可选） |
| `agent.token` | 以 Agent 角色运行时，用来连接 Hub 的令牌 |

> Hub 自己签发的令牌（admin / device / agent / hook / notify）**不是**这里说的 secrets。它们只以哈希形式保存在数据目录中，用 `nuka token` 管理。

---

## 3. 完整示例与字段说明
下面是一份完整的示例文件。注释中带"默认"的项都可以省略。

```toml
# ───────────── 基础 ─────────────
[hub]
name = "my-hub"                 # mDNS 实例名，以及设备上显示的名称；默认取主机名
host = "0.0.0.0"                # 默认
port = 8790                     # 默认
public_url = ""                 # 可选，例如 "https://hub.example.com"。写入配对信息和 OTA URL
lang = "zh-CN"                  # 默认语言：zh-CN | en。决定设备默认语言、通知文案和 Web 默认语言
timezone = "auto"               # IANA 时区，例如 "Asia/Shanghai"；auto 表示使用系统时区
data_dir = ""                   # 可选，覆盖默认的数据目录
log_level = "info"              # debug | info | warning | error

[mdns]
enabled = true                  # 默认

# ───────────── 安全 ─────────────
[security]
lan_cidrs = ["10.0.0.0/8","172.16.0.0/12","192.168.0.0/16","100.64.0.0/10","fc00::/7","fe80::/10"]  # 默认
trusted_proxies = []            # 只有来自这些地址的请求，才采信 X-Forwarded-For
public_detect_headers = ["Cf-Connecting-Ip","Cf-Ray","X-Forwarded-For"]   # 默认；回环请求带有其中任一个头时，判为 public
web_approve_permissions = false # 是否允许在 Web 上审批 Agent 权限（只限 local / lan 区域）
allow_lan_hooks = false         # 是否允许 lan 区域调用 /hook/*；AI hooks 用 hook 令牌，server hook 用 notify 令牌；public 永远禁止
remote_admin = false            # 默认关闭；true 时还必须启用 cf_access 并逐请求验证 Access JWT + Hub admin 会话，写入仍需 CSRF
session_days = 30               # Web 会话的有效期

[security.cf_access]            # 可选：public 区域的请求额外校验 Cloudflare Access JWT
enabled = false
team_domain = "<team>.cloudflareaccess.com"
aud = "<application AUD tag>"
require_for_ws = true           # public 区域的 /ws/device 和 /ws/agent 是否也要求 Service Token

# ───────────── 本机采集器 ─────────────
[collectors]
enabled = ["psutil","nvml","lhm","macmon","linux_sensors","amd_sysfs"]  # 默认：全部尝试启用，不可用的自动禁用
host_name = ""                  # 本机在 Hub 中显示的名称，默认取主机名

[collectors.psutil]
fast_interval = 1.0             # 默认，单位秒：CPU、内存、网速
disk_interval = 60.0            # 默认，单位秒：磁盘用量

[collectors.nvml]
interval = 1.0
gpu_indices = []                # 为空表示采集全部 GPU

[collectors.lhm]                # 仅 Windows
url = "http://127.0.0.1:8085/data.json"   # LHM Web 服务的默认地址
user = ""                       # 可选，配合 password_secret 使用
password_secret = ""            # 例如 "collectors.lhm"
interval = 2.0
# 按 SensorId 的匹配规则覆盖内置规则；可以写多条，从上到下第一个匹配的生效
[[collectors.lhm.map]]
field = "cpu.temp_c"            # 目标字段，取值见 PROTOCOL §5.3 中的 Host 结构
sensor_id = "/amdcpu/0/temperature/2"     # 也可以改用 name_regex = "Tctl|Package"

[collectors.macmon]             # 仅 macOS（Apple Silicon）
path = "macmon"                 # 可执行文件名或完整路径
interval = 2.0

[collectors.linux_sensors]      # 仅 Linux
interval = 2.0
cpu_temp_labels = ["Tctl","Package id 0","k10temp","coretemp"]   # 默认值

[collectors.priority]           # 同一个字段有多个来源时，按以下顺序取值（默认）
"cpu.temp_c" = ["lhm","linux_sensors","macmon"]
"gpus"       = ["nvml","lhm","amd_sysfs","macmon"]

# ───────────── 服务器数据源（可以写多个，每个 type 也可以出现多次）─────────────
[[servers.source]]
type = "serverstatus"
name = "status"                 # 数据源名称，同时用于构成密钥名
url = "http://192.0.2.10:8080/json/stats.json"
interval = 5                    # 默认 5，单位秒
timeout = 5                     # 默认 5，单位秒

[[servers.source]]
type = "uptime_kuma"
name = "kuma"
url = "https://kuma.example.com"
interval = 30
[servers.source.options]
slug = "<status-page-slug>"

[[servers.source]]
type = "beszel"
name = "beszel"
url = "https://beszel.example.com"
auth_secret = "servers.beszel"  # 值的格式为 "email:password"，或 "token:<API token>"
interval = 10

[[servers.source]]
type = "netdata"
name = "nd-web1"
url = "http://192.0.2.11:19999" # 每个 Netdata 节点单独写一个 source
interval = 5

[[servers.source]]
type = "node_exporter"
name = "prom"
url = "http://192.0.2.12:9090"
interval = 15
auth_secret = ""                # 可选：basic auth，值的格式为 "user:pass"；属于 source，不属于 options
[servers.source.options]
mode = "prometheus"             # exporter：直接抓取 {url}/metrics，每个 source 对应一台服务器 | prometheus：执行 PromQL，按 instance 拆分
instance_label = "instance"

[[servers.source]]
type = "generic_json"
name = "custom"
url = "https://monitor.example.com/api/hosts"
interval = 30
headers = { "X-Api-Key" = "secret:servers.custom" }   # 值以 "secret:" 开头时，表示从密钥中读取
[servers.source.options]
items = "$.data.hosts[*]"       # JSONPath，指向服务器数组
[servers.source.options.fields]
name     = "$.hostname"
online   = "$.up"               # 支持布尔值，也支持 1/0、"up"/"down"
cpu_pct  = "$.cpu"
mem_pct  = { path = "$.mem_ratio", scale = 100 }    # scale：读出的值乘以这个系数
disk_pct = "$.disk"

[servers.merge]
by = "name"                     # 不同数据源中的服务器按名称合并：name | none
aliases = { "web-1" = ["web1","web-1.example.com"] }

# ───────────── 告警 ─────────────
[alerts]
builtin = { server_offline = true, cpu_high = true, mem_high = true, disk_high = true, ai_exhausted = true, ai_reauth = true }  # 默认全部开启
repeat = "30m"                  # 同一规则、同一对象再次通知的最短间隔

[[alerts.rule]]
name = "gpu-hot"
scope = "hosts"                 # hosts | servers | ai
when = "gpus[0].temp_c > 85"    # 允许的字段与运算符见第 4 节
for = "2m"
level = "warn"                  # info | warn | critical
title = "{name} GPU {gpus[0].temp_c}°C"   # 可选，支持插入字段值

# ───────────── AI 配额 ─────────────
[ai]
stale_after = "30m"             # 默认
manual_refresh_per_min = 1      # 默认
label_mode = "masked_email"     # 账号标签的默认显示方式：masked_email | index

[[ai.provider]]
type = "cpamp"                  # cpamp | cpa
name = "main"
base_url = "https://cpamp.example.com"
key_secret = "ai.main"
passive_interval = "60s"        # L1，默认 60s
snapshot_interval = "120s"      # L2，默认 120s；只对 cpamp 有效
active_enabled = true           # 是否启用 L3 主动查询
active_min_interval = "600s"    # L3 对同一账号的最小间隔，不得小于 300s（校验时强制）
max_rpm = 20                    # 每分钟最多请求数

# 按账号覆盖显示设置。账号 ID 可以在 Web 的"AI 余额"页面或 `nuka doctor` 的输出中查到
[ai.provider.accounts."a_codex_1"]
label = "Work Codex"
hidden = false

# ───────────── AI Agent hooks ─────────────
[agents]
enabled_sources = ["claude","codex","gemini"]   # 默认
permission_timeout = "60s"      # 必须小于 hooks 安装器写入的超时（90s）
show_project = "basename"       # basename | hidden
collect_last_message = true
idle_after = "10m"
purge_after = "2h"

# ───────────── 媒体 / 天气 ─────────────
[media]
enabled = true                  # 自动选择当前平台的适配器；依赖缺失时自动禁用

[weather]
enabled = false
provider = "open_meteo"         # 默认
lat = 0.0                       # 必须填写 lat/lon，或者填写 city
lon = 0.0
city = ""                       # 例如 "Berlin"；首次启动时经地理编码解析成坐标，然后缓存
units = "metric"                # metric | imperial
interval = "15m"

# ───────────── 通知 ─────────────
[notify]
quiet_hours = ""                # 例如 "23:00-08:00"；这段时间内只推送 critical 级别
dedupe_window = "5m"
keep = 500
hook_rate_per_min = 30          # 每个 notify 令牌每分钟最多 30 次
hook_burst = 10

# ───────────── 设备 ─────────────
[devices]
default_lang = ""               # 为空时使用 hub.lang
push_overrides = {}             # 覆盖 PROTOCOL §3.7 中的推送间隔，例如 { hosts_active = "2s" }
key3_long = { action = "talk" }  # 默认；talk | dnd | always_on | pomodoro | macro | none；macro 时还需 slot = 0..11
key3_double_card = "buddy"       # 默认，取 PROTOCOL 的 App ID
key3_clock_style = true         # 保留；第二阶段 Key3 短按先切换主页两面，不同时切换表盘样式
t_dim = 20                      # 默认，ACTIVE → DIM 的空闲秒数，整数 1–86400
t_off = 10                      # 默认，DIM 后额外空闲秒数，整数 1–86400
home_face = "clock"             # 默认：clock | assistant，主页的默认面，两面均属于 clock App
volume = 50                     # 可选示例值，整数 0–100，0 为静音；省略时不下发，保留设备已有音量
assistant = false               # 默认关闭这台设备的助手；启用仍要求 Hub 已配置助手

[[devices.macro]]               # 下发给设备的宏（slot 取 0–11）
slot = 0
label = "Build"
kind = "keys"                   # keys：组合键 | text：输入一段文字
value = "CTRL+SHIFT+B"

# ───────────── Pi 助手与永久存储（C2-03）─────────────
[assistant]
enabled = false                 # 默认关闭；启用后要求 Node 与锁定的 Pi 核心依赖已安装
name = "Nuka"                   # 默认，助手显示名称，1–64 字符
system_prompt = "你是 Nuka，请简明回答。" # 可自定义，最多 16384 字符；默认使用内置简明助手指令
lang = "zh-CN"                  # zh-CN | en，默认 zh-CN
max_output_tokens = 1024         # 默认，正整数，单轮模型输出预算

[assistant.llm]
base_url = "https://llm.example.com/v1" # 示例；默认空，启用助手时必须填写，HTTP / HTTPS，不含登录凭据
model = "<MODEL>"               # 示例；默认空，启用助手时必须填写
key_secret = "assistant.llm"     # 示例；默认空表示不带 Authorization（例如本地 Ollama）；只存名称，不存密钥
temperature = 0.7               # 默认，0–2
context_window = 32768          # 默认预算，1024–1048576；按实际模型填写，不代表所有模型容量；必须大于 max_output_tokens

[assistant.history]
enabled = true                  # 默认记录；关闭后不写新记录，旧数据须显式删除
context_turns = 20              # 默认，正整数，只限制模型输入；完整存档永久保留，无 TTL

[assistant.memory]
enabled = true                  # 默认，用户明确要求记住且设备授权后才写入；SQLite 检索，最多 8 条 / 8000 字符

[assistant.uploads]
max_bytes = 67108864            # 默认 64 MiB，正整数；按实际读取字节数限制，上传文件不自动执行

# ───────────── OTA / 存储 / Web ─────────────
[ota]
dir = ""                        # 默认为 <data_dir>/ota
auto_offer = false              # 对 ota.dir 中其他新增 manifest 自动通知；显式 nuka ota publish / REST offer 总会通知

[store]
raw_retention = "24h"
downsampled_retention = "30d"
audit_retention = "180d"

[web]
enabled = true                  # 是否启用 Web 管理页
allow_config_edit = true        # 是否允许在 Web 上编辑 config.toml 和设置密钥（无论如何都只限 local / lan 区域）

# ───────────── Agent 角色（只在 `nuka agent` 时读取）─────────────
[agent]
hub_url = "wss://hub.example.com/ws/agent"     # 或 "ws://192.0.2.10:8790/ws/agent"
token_secret = "agent.token"
name = ""                       # 默认取主机名
send_media = true
# 仅当目标 Hub 的 Cloudflare Access 要求 Service Token 时填写；值均为密钥名称引用
cf_client_id_secret = ""        # 可选，例如 "agent.cf_client_id"
cf_client_secret_secret = ""    # 可选，例如 "agent.cf_client_secret"；两项必须同时设置
# 采集器配置沿用上面的 [collectors]
```

---

## 4. 告警表达式语法（`alerts.rule.when`）
```
expr   := or
or     := and ("||" and)*
and    := cmp ("&&" cmp)*
cmp    := value (("=="|"!="|">"|">="|"<"|"<=") value)? | "!" cmp | "(" expr ")"
value  := field | number | string | true | false
field  := ident ("." (ident | "5h") | "[" int "]")*
ident  := (letter | "_") (letter | digit | "_")*
```
- 可以使用的字段是对应 `scope` 的资源结构（PROTOCOL §5.3）中的字段，再加上几个派生字段：
  - `hosts`：`mem.pct`、`disks[i].pct`
  - `servers`：`mem_pct`、`disk_pct`
  - `ai`：`windows.5h.used_pct`、`windows.weekly.used_pct`、`status`
- 引用不存在的字段时，该条件为假，不报错；但在加载配置时，会对未知的字段名给出警告。
- 函数调用、算术运算和正则表达式都**不支持**。

## 5. 时长格式
时长写成字符串，由数字加单位组成：`"500ms"`、`"30s"`、`"10m"`、`"2h"`、`"30d"`。写成纯数字时，单位为秒。

## 6. `nuka init` 向导流程
1. 设置 Hub 名称、语言、端口，并检测端口是否被占用。
2. 生成 admin 令牌：只显示一次，并提示用户保存。
3. 本机采集器：自动探测，列出可用和不可用的项，并给出每一项对应的安装引导链接。
4. 是否启用 AI 配额：
   1. 选择 cpamp 或 cpa；
   2. 输入 `base_url`；
   3. 用隐藏输入接收密钥，写入钥匙串；
   4. **只发 1 个请求做验证**（`GET /v0/management/auth-files`）。返回 401 或 403 时，**不再重试**，并提示用户检查密钥。
5. 服务器数据源：可以添加多个，每添加一个就试抓一次并显示解析结果。
6. 天气：选择城市或填写坐标。
7. Agent hooks：选择要安装的 CLI，然后调用 `nuka hooks install`。
8. 公网访问：可选，调用 `nuka tunnel setup`。
9. 写入 `config.toml`，并询问是否运行 `nuka service install`。

每一步都可以跳过；以后再次运行 `nuka init --section <name>`，可以单独补充配置某一项。

## 7. 冻结时补充的字段约定

- `[devices]` 的按键、超时、`home_face`、`volume` 和 `assistant` 字段与 PROTOCOL `cfg` 一一对应，通过配网后的 WS 下发；`default_lang` 映射为 `cfg.lang`，空值使用 `hub.lang`。设备仍按“本机修改过的项优先”逐项处理；未提供的可选音量不下发。宏定义的 `rev` 由 Hub 根据定义变化维护，不增加用户配置项。
- `devices.push_overrides` 只接受下表中的键，值按第 5 节的时长格式解析；事件消息不改为周期轮询。覆盖值必须为正，`ai_min` 不得小于 10s，`low_power_min` 不得小于 5s，保持 PROTOCOL 的限流要求。

  | 键 | 默认 | 对应 PROTOCOL §3.7 |
  |---|---|---|
  | `hosts_active` | 1s | 亮屏且当前卡为 hosts |
  | `hosts_inactive` | 5s | 亮屏且当前卡不是 hosts |
  | `servers_active` | 5s | 亮屏且当前卡为 servers |
  | `servers_inactive` | 15s | 亮屏且当前卡不是 servers |
  | `detail` | 5s | host_detail / server_detail |
  | `ai_min` | 10s | AI 两次推送的最小间隔 |
  | `low_power_min` | 5s | 低电时周期性推送的最小间隔 |

- `weather.lat` / `lon` 没有隐含的定位默认值；示例中的 0.0 是合法示例坐标。启用天气时必须明确填写这两项，或填写非空 `city`。设备协议的 `_c` 始终为摄氏度；`units` 影响格式化的风速文本和上游查询，Hub 必须把温度归一化为摄氏度后填入 `_c`。
- `[agent]` 的两个 `cf_*_secret` 仅保存名称引用，通过第 2 节取值，仅发送给 `agent.hub_url` 的 HTTPS origin（WSS 按 HTTPS 比较）。它们不改变 Bearer agent 令牌的鉴权要求，也不写入日志。设备侧 Access 凭据由 USB / SoftAP 配网写入 NVS，见 PROTOCOL §4.3.2。
- `security.allow_lan_hooks` 适用于所有 `/hook/*`；令牌作用域由具体 hook 决定。public 区域的通用通知使用 `POST /api/v1/notify`，所有 `/hook/*` 均返回 404。

## 8. Pi 助手、远程管理与数据目录（C2-03）
- Pi 使用锁定版本的 `@earendil-works/pi-agent-core` 和 `@earendil-works/pi-ai`，仅通过 Hub 自带的本机 stdio 桥接程序调用。Node 从 PATH 查找；不提供任意脚本路径配置，不加载编码代理的用户扩展、终端或文件工具。Python Hub 保留鉴权、工具权限、SQLite、ASR / TTS；不引入 LangChain。
- `[assistant]` 是可选配置段，缺省 `enabled=false`。`base_url`、`model` 和 `key_secret` 的代码默认都是空字符串，文档占位符不能作为实际默认值。启用助手时 `base_url` 和 `model` 必须非空；URL 为 HTTP / HTTPS 且不含用户名或密码。填写的 `key_secret` 必须可按第 2 节解析；留空适用于无需 API key 的本地接口。
- 助手密钥只加载到 Hub 与其本机 Pi 子进程，不经 REST、设备或日志返回。`system_prompt`、名字、语言、temperature 和 token 预算只配置模型行为，不授予工具权限。所有外部工具仍由 Hub 白名单及设备确认控制。
- `assistant.llm.context_window` 是用户给当前模型设置的输入 / 输出总预算，默认 32768，整数 1024–1048576；不能当成上游模型容量声明。加载配置时要求 `assistant.max_output_tokens < assistant.llm.context_window`，并在组装上下文时给 system、记忆、工具定义和输出留预算。最近 20 轮不是无条件塞满；必要时减少完整轮次，仍保留工具请求 / 结果配对。
- history / memory / uploads 使用 `hub.data_dir` 下的 SQLite 与附件目录，不新增个人路径默认值。记录永久存档，没有 7 天清理；最近 `context_turns` 仅用于模型输入。关闭 history 后保留旧存档但不写新记录；关闭 memory 后不写入或检索，已有记忆仍可删除。清空、导出、设备隔离和备份恢复见 PROTOCOL §5.8。
- `security.remote_admin=true` 仅在 `security.cf_access.enabled=true` 且其 team_domain / aud 配置完整时合法。运行时必须验证每次请求的 Access JWT，并要求有效 admin 会话，写请求还要 CSRF；只设置开关或伪造请求头不会开放管理接口。`web.allow_config_edit` 继续生效，所有 `/hook/*` 和 Web 审批仍禁止 public。
- 服务器部署使用 `hub.host="127.0.0.1"`，cloudflared 仅转发至该回环端口。主机、域名、Access 配置和密钥由部署配置提供；设备仍需 Bearer device 令牌，公网设备按 cf_access.require_for_ws 执行原有规则。
- ASR / TTS / 搜索 / MCP 的具体配置和音频网络消息属于 C2-02；本批不提前选择 PCM 或 Opus。Pi 的有界 JSONL 内部桥接协议由 Hub 实现记录，不新增设备 proto。
