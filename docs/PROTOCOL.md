# nuka-esp32 通信协议（PROTOCOL）

| 项 | 值 |
|---|---|
| 协议版本 | `proto = 1` |
| 状态 | proto=1 本地冻结；C2-01 / C2-03（2026-10-24）增补设备设置、文字助手与永久存储；待远程 PR 评审，决定见 protocol/DECISIONS.md |
| 机器可读版本 | `protocol/schemas/*.json`（JSON Schema 2020-12）+ `protocol/examples/*.json` |
| 关联文档 | SPEC §5–§7、CONFIG.md |

> **本文是 FW、BE、FE 三条轨道之间唯一的契约。**（FE 只做设备 UI，主要关心 §3.4 中的视图模型字段。）
> - 任何改动必须**先改本文和 schema**，再改代码。
> - 不兼容的改动要把 `proto` 加 1；Hub 同时支持 `proto` N 和 N−1。
> - 向后兼容的改动（新增可选字段、新增消息类型）不升版本号；收到未知字段或未知消息时，接收方**直接忽略**。

`proto=1` 是第一个已定义版本，没有 proto=0；初版 Hub 只需支持 1。发布 proto=2 后再同时支持 2 / 1，不创建未定义版本的兼容层。

---

## 1. 总则
| 项 | 约定 |
|---|---|
| 编码 | UTF-8 JSON；字段名一律 `snake_case` |
| 时间 | **设备侧（WS / CDC）**：Unix 秒，整数。**REST / Web**：ISO 8601 UTC 字符串（`2026-10-17T08:00:00Z`） |
| 百分比 | 浮点数，0–100，保留 1 位小数 |
| 温度 / 功率 | `_c`（摄氏度）/ `_w`（瓦），数值类型 |
| 文本 | 由 Hub 按设备语言生成，设备原样显示。格式化指标用 `_txt`；本文已经定义的 `name`、`label`、`title`、`body`、`msg`、`summary` 等字段保持原名 |
| 缺失 | 没有的数据**直接省略这个字段**，不发送 `null` 或伪造的 0。真实测量值和计数可以为 0。唯一的显式空值是取消详情订阅的 `topic:null`（§3.5.3）。可选字段及嵌套对象的必选字段见 §9 |
| ID | 字符串，只用 `[a-z0-9_-]`，长度 1–32，稳定且不含个人信息。业务 ID 由 Hub 生成；`dev_id` 由设备首次启动时生成并持久化；`agent_id` 由 Agent 首次运行时生成并持久化 |
| 帧大小 | 设备收发的单帧 ≤ **8 KB**；超出时，Hub 负责截断列表，例如只发前 N 项，再附上 `more: <剩余数量>` |

- 8 KB 指 8192 个 UTF-8 字节，CDC 包括行尾 LF。Agent 的封面内联帧不适用设备的 8 KB 上限。
- 设备参与的 JSON（设备 WS 双向、CDC，以及 SoftAP 解码后的表单）中，所有字符串及对象键都禁止 U+0000，包括未知字段和数组内的嵌套内容，以便设备使用 C 字符串。JSON 转义 `\u0000` 解码后是 U+0000，必须拒绝；转义后的字面文本 `\\u0000` 不含 U+0000，仍然合法。此限制不扩展到纯 Hub / Web 的 REST、Agent WS 或 CONFIG。
- ID 的字符集合与 1–32 长度适用于整个字符串，末尾换行也不合法；schema 显式排除集合外字符，避免不同正则实现对 `$` 的末尾换行语义产生差异。
- 已知消息允许未知字段；运行时直接忽略未知字段和未知消息。schema 与 T6 校验的是本版本允许发送的已知消息，因此未知 `t` 在 T6 中报错。
- §9 是字段必选性、嵌套结构和未在展示示例中展开的字段的规范表；与对应 schema 同时维护。百分比允许整数或一位小数（`multipleOf:0.1`），时长和字节数非负；温度可以为负。

---

## 2. 发现（mDNS）
- 服务类型为 `_nuka._tcp`，端口取 Hub 的 HTTP 监听端口（默认 8790）。
- 实例名取自 `hub.name`。
- TXT 记录：
  | 键 | 示例 | 含义 |
  |---|---|---|
  | `id` | `hub_3f9a1c` | Hub 的唯一 ID，在 `nuka init` 时生成 |
  | `ver` | `0.1.0` | Hub 版本 |
  | `proto` | `1` | 支持的最高协议版本 |
  | `tls` | `0` / `1` | 本端口是否使用 TLS |
  | `ws` | `/ws/device` | 设备 WS 路径 |
- 设备只连接 `id` 与配对时保存的 `hub_id` 一致的 Hub。

---

## 3. 设备 WebSocket：`/ws/device`

### 3.1 连接
```
GET /ws/device HTTP/1.1
Authorization: Bearer <device_token>
Sec-WebSocket-Protocol: nuka.v1
```
- 配对阶段还没有令牌，这时改用 `X-Nuka-Pair: <dev_id>:<pair_code>`（见 §4.3.2）。
- 鉴权失败时，Hub 用对应的关闭码关闭连接（§3.8）。

### 3.2 信封
所有帧都使用同一个信封：
```json
{"v":1,"t":"vm.ai","id":17,"ts":1792224000,"d":{ }}
```
| 字段 | 必选 | 说明 |
|---|---|---|
| `v` | 是 | 协议版本 |
| `t` | 是 | 消息类型 |
| `id` | 否 | 请求 ID，整数；需要应答的请求才带。应答消息使用相同的 `id` |
| `ts` | 是 | 发送方的时间。设备在时间同步之前发 0 |
| `d` | 是 | 消息体；没有内容时发 `{}` |

通用应答：
- 成功：`{"t":"ok","id":N,"d":{}}`
- 失败：`{"t":"err","id":N,"d":{"code":"rate_limited","msg":"..."}}`

`hello` / `welcome`、`perm.decide`、`ai.refresh`、`macro.get`、`ota.check`、`ok`、`err` 必须带 `id`。对 `macro.get` 的 `macro.set` 回复使用相同的 `id`；主动下发的 `macro.set` 可以不带。其他事件不要求应答。`id` 是整数请求序号，不适用字符串 ID 的格式规则。

### 3.3 握手
连接建立后，**设备先发** `hello`：
```json
{"v":1,"t":"hello","id":1,"ts":0,"d":{
  "dev_id":"nuka_7c2e91","fw":"0.1.0","nukapack":"0.1.0","proto":1,
  "lang":"zh-CN","screen":true,
  "caps":["imu","hid","vault","audio"],
  "topics":["hosts","servers","ai","agents","notify","media","weather","pet"],
  "bat":{"pct":82,"mv":3987,"chg":"charging","src":"usb","eta_min":95}
}}
```
Hub 回复 `welcome`。回复后，Hub 会立即推送所有已订阅主题的完整快照。
```json
{"v":1,"t":"welcome","id":1,"ts":1792224000,"d":{
  "hub_id":"hub_3f9a1c","hub_name":"my-hub","hub_ver":"0.1.0","proto":1,
  "dev_name":"desk-1","lang":"zh-CN",
  "time":{"epoch":1792224000,"tz_posix":"CST-8"},
  "ntp":["pool.ntp.org"],
  "features":{"ai":true,"servers":true,"agents":true,"media":false,"weather":true},
  "macros_rev":3
}}
```
- `features` 表示 Hub 是否启用了对应的集成。设备据此显示卡片的"未配置"状态（SPEC §6.8）。
- `tz_posix` 是 POSIX TZ 字符串，由 Hub 根据配置中的 IANA 时区转换得到，设备直接传给 `setenv("TZ")`。
- 如果 `macros_rev` 与设备本地保存的版本不一致，设备应发送 `macro.get` 拉取宏定义。

### 3.4 视图模型（Hub → 设备）
- 视图模型的消息类型统一为 `vm.<topic>`。每次发送**整个主题的完整内容**，设备直接替换旧值，不需要合并。
- 每个视图模型都带 `rev`，在同一条 WS 连接、同一消息类型内单调递增。设备丢弃 `rev` 小于或等于当前值的消息。收到新连接的 `welcome` 后重置各主题的已接收版本；允许保留旧内容作离线展示，首个新快照必须能替换它。详情切换对象时也继续递增同一详情消息类型的 `rev`。
- 所有字段都在 `d` 中。

#### 3.4.1 列表上限
| 主题 | 上限 |
|---|---|
| hosts | 6 |
| servers | 12 |
| ai.accounts | 8 |
| agents.sessions | 6 |
| notify | 20 |
| media | 3 |

超出上限时，Hub 按重要性排序截断，并带上 `more` 字段。

#### 3.4.2 `vm.hosts`
```json
{"rev":120,"items":[{
  "id":"h_desktop","name":"desktop","online":true,"os":"windows",
  "cpu":{"pct":23.5,"temp_c":54,"freq_mhz":4700},
  "gpu":{"name":"RTX","pct":12.0,"temp_c":41,"mem_pct":30.2,"power_w":45},
  "mem":{"pct":61.2,"txt":"19.1 / 31.0 GB"},
  "net":{"up_txt":"1.2 MB/s","down_txt":"8.4 MB/s"},
  "disks":[{"label":"C:","pct":72.0}],
  "bat":{"pct":90,"chg":true}
}]}
```
- `gpu`、`bat`、`cpu.temp_c` 等字段，没有数据时整个省略。
- `disks` 最多 3 项。

**详情（L1）**：设备进入主机详情页时发送 `sub.detail`，内容为 `{"topic":"hosts","id":"h_desktop"}`。随后 Hub 每 5s 推送一次 `vm.host_detail`，直到设备发送 `sub.detail`，内容为 `{"topic":null}`：
```json
{"rev":3,"id":"h_desktop",
 "rows":[{"k":"CPU","v_txt":"23.5% · 54°C · 4.7 GHz"},{"k":"GPU","v_txt":"12% · 41°C · 45 W"}],
 "spark":{"cpu":[12,15,30],"gpu":[5,5,8],"mem":[60,61,61]},
 "spark_span_txt":"24h"}
```
`spark` 中的每个数组最多 48 个点，取值 0–100。

#### 3.4.3 `vm.servers`
```json
{"rev":40,"items":[{
  "id":"s_web1","name":"web-1","online":true,"src":"serverstatus",
  "cpu_pct":12.0,"mem_pct":45.3,"disk_pct":71.0,"load1":0.42,
  "net_txt":"↑0.3 ↓1.1 MB/s","uptime_txt":"12d","alerts":0
}],"more":0}
```
- 没有资源指标的数据源（例如 Uptime Kuma）只提供 `online`，再加上 `latency_ms` 和 `uptime_pct`。
- 详情页的机制与主机相同：设备发 `sub.detail`，内容为 `{"topic":"servers","id":...}`；Hub 推送 `vm.server_detail`，结构与 `vm.host_detail` 相同。

#### 3.4.4 `vm.ai`
```json
{"rev":9,
 "summary":{"req_today":1234,"tok_today_txt":"3.2M","fail_pct":1.2},
 "accounts":[{
   "id":"a_codex_1","kind":"codex","label":"a***@example.com","plan":"plus",
   "status":"cooldown","recover_at":1792231200,
   "windows":[
     {"kind":"5h","used_pct":42.0,"reset_at":1792231200,"src":"passive","stale":false},
     {"kind":"weekly","used_pct":18.5,"reset_at":1792700000,"src":"active","stale":false}
   ],
   "credits_txt":"$5.00",
   "fresh_txt":"3 分钟前"
 }]}
```
| 字段 | 取值 |
|---|---|
| `kind` | `codex` / `claude` / `antigravity` / `kimi` / `other` |
| `status` | `ok` / `cooldown` / `exhausted` / `reauth` / `disabled` / `unknown` |
| `windows[].kind` | `5h` / `daily` / `weekly` / `monthly` / `other`；为 `other` 时带 `label` |
| `windows[].src` | `passive` / `snapshot` / `active` |

- 设备的双环规则：外环取第一个 `5h` 窗口，内环取第一个 `weekly` 窗口；没有对应窗口时，那一圈环不画。
- `summary` 只有在使用 CPAMP 时才有，没有时省略。
- `accounts[].breaker_open` 是可选布尔值，表示该账号所属提供方的请求熔断器是否开启，由 Hub 根据实际的提供方状态填入；账号的 `status` 保持原义。`true` 时设备显示“熔断”；字段省略时表示没有该信息，不能由 `unknown`、`disabled` 或缺测配额推断熔断。
- 第二阶段 AI 余额页沿用上述字段；账号状态、恢复时间、双环和今日三个汇总指标都有对应数据。当前 VM 没有按模型的用量明细，因此不显示 Top 3，也不从格式化文本推算。

#### 3.4.5 `vm.agents`
```json
{"rev":77,"pending":1,"sessions":[{
  "sid":"c_1a2b3c","src":"claude","host":"desktop","project":"my-app",
  "state":"tool","tool":"Bash","since":1792223990,"msg":"..."
}]}
```
- `state` 的取值：`idle` / `thinking` / `tool` / `waiting_permission` / `done` / `error`。
- 配置隐藏项目名时，省略 `project`；不收集消息时，省略 `msg`（最多 140 个字符）。

#### 3.4.6 `vm.notify`
```json
{"rev":31,"items":[{"nid":"n_9f","level":"warn","src":"alerts","title":"web-1 磁盘 91%","body":"...","ts":1792223000}]}
```
- 按时间倒序排列，最多 20 条；`body` 最多 200 个字符。

#### 3.4.7 `vm.media`
```json
{"rev":5,"items":[{
  "src_id":"m_desktop_spotify","host":"desktop","app":"Spotify",
  "title":"Song","artist":"Artist","album":"Album",
  "state":"playing","pos_s":61,"dur_s":215,"at":1792224000,
  "cover":"/api/v1/media/cover/7f3a9c.jpg",
  "can":{"toggle":true,"next":true,"prev":true}
}]}
```
- 设备根据 `pos_s + (now - at)` 在本地推进进度条；Hub 只在曲目、状态或位置发生跳变时推送。
- Hub 按最近活跃时间倒序输出 `items`，设备默认显示首项；用户手动选择后按 `src_id` 保持，直到该源被移除。活跃时间由 Hub 根据源的播放、曲目或状态等活动变更记录，不能因每次轮询而刷新全部源的活跃时间；此时间是 Hub 内部状态，不新增网络字段。
- **封面**不经 WS 传输。设备通过 HTTP `GET {cover}` 下载（带上 device 令牌），格式为 160×160 的 baseline JPEG，大小不超过 20 KB。URL 中包含内容哈希，可以长期缓存。

#### 3.4.8 `vm.weather`
```json
{"rev":2,"loc_txt":"City","updated":1792220000,
 "now":{"temp_c":18.5,"feels_c":17.0,"hum_pct":60,"wind_txt":"NE 3 级","icon":"partly_cloudy","desc_txt":"多云"},
 "days":[{"date":"2026-10-18","hi_c":22,"lo_c":13,"icon":"rain"}]}
```
- `icon` 共 12 种：`clear_day`、`clear_night`、`partly_cloudy`、`cloudy`、`fog`、`drizzle`、`rain`、`heavy_rain`、`snow`、`sleet`、`thunder`、`wind`。
- 由 Hub 把 WMO 天气代码映射到这 12 种图标（映射表在 C-01 中给出）。
- `days` 最多 3 项，对应 SPEC §6.8.9 的未来 3 天；REST 天气结构使用相同上限。

#### 3.4.9 `vm.pet`
```json
{"rev":12,"mood":"busy","reason":"agent"}
```
- `mood` 的取值：`sleep` / `idle` / `busy` / `attention` / `celebrate` / `hungry`。
- Hub 综合 Agent 状态、配额和时间（深夜时为 `sleep`）计算出 `mood`。第二阶段保留 `pet` 主题，作为主页表情面的情绪数据源；不再提供独立的桌宠卡。显示规则见 PLAN-v2 §2.4。
- 上述六种 Hub 情绪分别对应犯困、空闲、忙碌、注意、庆祝和饿了。设备在其上叠加本地状态：充电、摇晃、与 Hub 断开、待审批，以及已有服务器告警和 AI 配额 VM 所确定的担心状态。聆听、思考、说话和语音错误由第二批契约的语音会话状态提供，不新增 `mood` 值。
- `celebrate` 带 `until`（秒级时间戳），过了这个时间后，设备回落到 Hub 下一次给出的 `mood`。
- `until` 不存在时省略，不用 0 表示。Hub 在庆祝结束时推送下一次情绪，设备不自行推算 Hub 的其他情绪。

#### 3.4.10 `time`（Hub → 设备，事件）
```json
{"epoch":1792224000,"tz_posix":"CST-8"}
```
- 连接建立时、每 6 小时，以及时区配置变更时发送。设备收到后校准系统时间并写入 RTC。
- `epoch` 限定为 946684800–4102444799，即 RTC 支持的 2000–2099 年；`welcome.time` 复用相同约束。`tz_posix` 为最多 127 个可打印 ASCII 字符，保留空串表示 UTC 的既有语义。此范围不影响信封 `ts` 在时间同步前发 0。

### 3.5 设备 → Hub

#### 3.5.1 `sub`：设置订阅的主题
```json
{"topics":["hosts","ai","agents","notify","pet"]}
```
- 默认订阅 `hello.topics` 中列出的全部主题；用户隐藏了某张卡片后，设备重新发送 `sub`。

#### 3.5.2 `state`：设备状态
设备在状态变化时发送，此外每 60s 发送一次心跳。
```json
{"screen":true,"dnd":false,"card":"ai",
 "bat":{"pct":82,"mv":3987,"chg":"charging","src":"usb","eta_min":95},
 "rssi":-58,"heap_int":143360,"heap_int_min":98304,"up_s":3600}
```
| 字段 | 说明 |
|---|---|
| `bat.chg` | `none` / `charging` / `full` |
| `bat.src` | `usb` / `bat` |
| `bat.eta_min` | 放电时表示剩余时间，充电时表示充满还需要的时间；无法估算时省略 |
| `card` | 当前显示的卡片 ID，Hub 据此提高该卡片对应主题的推送频率（§3.7） |

第二阶段的主页两面都上报 `card:"clock"`，通过设备设置 `cfg.home_face` 选择默认面；切换到表情面不改 WS 主题名。新固件的卡片列表不再包含 `pet`，但 proto=1 的 `state.card` 和 `cfg.key3_double_card` 字段仍保留原有取值；删除枚举值会成为不兼容变更，不能随本批可选字段增补收紧。

#### 3.5.3 `sub.detail`
见 §3.4.2。

#### 3.5.4 `perm.decide`：审批决定（需要应答）
```json
{"rid":"r_5e1f","behavior":"allow"}
```
- `behavior` 为 `allow` 或 `deny`。
- 应答为 `ok`；如果请求已经被处理过或已经过期，则返回 `err`，`code` 为 `perm_gone`。

#### 3.5.5 `media.cmd`
```json
{"src_id":"m_desktop_spotify","cmd":"toggle"}
```
- `cmd` 为 `toggle`、`next` 或 `prev`。

#### 3.5.6 `ai.refresh`（需要应答）
- 消息体为 `{}`，可能返回的错误 `code` 为 `rate_limited`。

#### 3.5.7 `notify.dismiss`
- 消息体为 `{"nid":"n_9f"}` 或 `{"all":true}`。
- 只影响这台设备上的通知列表，Hub 不会删除通知历史。

#### 3.5.8 `pomo`：番茄钟事件
```json
{"phase":"focus","state":"running","ends_at":1792225500,"round":2}
```
- `phase` 为 `focus`、`short_break` 或 `long_break`；`state` 为 `running`、`paused`、`done` 或 `reset`。
- `ends_at` 只在存在预计结束时间时提供；暂停、结束或重置后可省略，不能用 0 代替未知时间。
- Hub 只记录这些事件、在 Web 上展示，并据此让宠物在专注期间显示 `busy`。

#### 3.5.9 `macro.get`
- 消息体为 `{}`，Hub 回复 `macro.set`。

#### 3.5.10 OTA 进度
- `ota.progress`：`{"ver":"0.2.0","kind":"app","pct":40}`
- `ota.result`：`{"ver":"0.2.0","kind":"app","ok":false,"reason":"sha_mismatch"}`

#### 3.5.11 `ota.check`：检查更新（需要应答）
- 设备在用户选择“检查更新”时发送 `ota.check`，消息体为 `{}`，必须带请求 `id`。
- Hub 用相同的 `id` 回复 `ok`，并根据可用 manifest 和本次 `hello` 中的 `fw` / `nukapack`，发送零个或多个现有的 `ota.offer`。没有可用更新时只回复 `ok`，不增加响应字段。
- `ver` 按字符串精确比较：`app` manifest 与 `hello` 的 `fw` 比较，`assets` 与 `nukapack` 比较，不相等即可 offer，允许回退。同一 `kind` 可以有多个 offer，按 manifest 路径的稳定顺序发送，不做语义版本排序。
- 这是用户显式发起的检查，独立于 `ota.auto_offer` 开关；下载与安装仍按现有 `ota.offer` 的本地确认流程执行。

### 3.6 Hub → 设备事件
| 类型 | 消息体 | 设备行为 |
|---|---|---|
| `perm.req` | `{"rid":"r_5e1f","sid":"c_1a2b3c","src":"claude","host":"desktop","project":"my-app","tool":"Bash","summary":"rm -rf build && npm run …","expires_at":1792224060}` | 点亮屏幕，弹出审批 sheet。`summary` 不超过 120 个字符 |
| `perm.done` | `{"rid":"r_5e1f","result":"allow","by":"web"}` | 关闭对应的 sheet；如果是在别处处理的，显示一个 toast，例如"已在 Web 上允许"。`result` 为 `allow` / `deny` / `timeout` / `cancelled`；`by` 为 `device` / `web` / `timeout` / `terminal` |
| `notify.push` | 一条通知，结构同 `vm.notify.items[]`，另外带 `wake: true/false` | `level` 为 `critical` 时，设备点亮屏幕并弹窗；其他级别只更新角标或显示 toast |
| `ota.offer` | `{"ver":"0.2.0","kind":"app","url":"/api/v1/ota/nuka-esp32-0.2.0.bin","sha256":"…","size":1843200,"notes_txt":"…"}` | 提示用户（SPEC §6.16）。`kind` 为 `app` 或 `assets` |
| `macro.set` | `{"rev":3,"macros":[{"slot":0,"label":"Build","kind":"keys","value":"CTRL+SHIFT+B"},{"slot":1,"label":"Sig","kind":"text","value":"Best regards"}]}` | 完整替换快照并写入 NVS，未列出的 slot 清空，空数组清空全部。`slot` 取 0–11；`kind` 为 `keys` 或 `text`；`text` 最长 256 个字符 |
| `time` | 见 §3.4.10 | 校准时间 |
| `cfg` | `{"lang":"en"}`，以及按键、熄屏时间、主页默认面、音量和助手开关（逐字段见 §9） | 设备写入 NVS；**只有在用户没有在本机修改过该项时才采用** |

`cfg` 为局部设置更新，不是 VM 快照；省略的项保持原值，不因 schema 的默认值而重置。允许的设置是 `lang`、`key3_long`、`key3_double_card`、`key3_clock_style`、`t_dim`、`t_off`、`home_face`、`volume`、`assistant`，逐字段见 §9。

- `key3_long={"action":"macro","slot":0}` 是一个原子设置；其他 action 为 `talk`、`dnd`、`always_on`、`pomodoro`、`none`。第二阶段新设备的默认 action 为 `talk`，即 PLAN-v2 §2.3 的按住说话；选择其他 action 后不启用按住说话。
- `key3_double_card` 使用 App ID（`buddy`、`pomodoro`），不是 WS 主题名（`agents`、`pomo`）。`key3_clock_style` 保留；第二阶段的 Key3 短按先执行主页双面切换，不能同时切换表盘样式。
- `t_dim` 是 ACTIVE 到 DIM 的空闲秒数；`t_off` 是进入 DIM 后的额外空闲秒数，均为 1–86400 的整数，默认 20 / 10。
- `home_face` 为 `clock` 或 `assistant`，指定主页的默认面，新设备默认 `clock`；两面均归属于 `clock` App。
- `volume` 为 0–100 的整数，0 表示静音；省略时保留设备已有音量。
- `assistant` 为布尔值，控制这台设备是否启用助手，新设备默认 `false`；设为 `true` 仍要求 Hub 已配置并启用助手。该项不包含模型、地址或任何 API 密钥。

`ota.offer.url` 可以是本 Hub 的 `/api/v1/ota/<file>.bin` 相对路径，也可以是 CONFIG `hub.public_url` 构成的 HTTP / HTTPS 绝对地址。相对路径以当前 WS 连接对应的 HTTP / HTTPS origin 解析；设备只接受已配对 Hub 地址列表中的 origin，Bearer 设备令牌和可选 Access 请求头均不得发往其他 origin。

`kind=assets` 的 `size` 不超过 **3145728 字节（3 MiB）**，REST OTA manifest 使用相同上限。SPEC 的 6 MiB `assets` 分区内部固定划分两个 3 MiB bank，网络只发送一个资源包，不传 bank 编号。设备只写入 inactive bank，完整下载并校验 `size`、SHA-256 和资源包 header / manifest ABI 后，才原子提交 NVS `sys.asset_slot`（0 / 1）选择新 bank；不能直接覆盖正在映射的 bank。下载、校验失败或提交前掉电继续使用旧 bank。首次刷入的资源 bin 写入 bank 0，未设置 `asset_slot` 时使用 bank 0；启动读取 NVS 时在该输入边界验证所选 bank 的资源包。资源包内部格式由 FW-05 定义，不增加本协议字段。

### 3.7 推送节流（Hub 侧实现）
| 主题 | 亮屏，并且是当前卡片 | 亮屏，但不是当前卡片 | 熄屏 |
|---|---|---|---|
| hosts | 1s | 5s | 不推送 |
| host_detail / server_detail | 5s | — | 不推送 |
| servers | 5s | 15s | 不推送 |
| ai | 有变化时推送，最快 10s 一次 | 同左 | 不推送 |
| agents | 有变化时立即推送 | 同左 | 不推送 |
| notify | 有变化时立即推送 | 同左 | 只推送 `notify.push` 中 `critical` 级别的通知 |
| media | 有变化时推送 | 同左 | 不推送 |
| weather / pet | 有变化时推送 | 同左 | 不推送 |
| perm.req / perm.done / ota.offer / time | 立即推送 | 立即推送 | **立即推送** |

- 设备上报 `screen:true` 后，Hub 立即补发所有已订阅主题的完整快照。
- 初次 `welcome` 后的完整快照不受熄屏节流限制，用来建立初始状态；后续推送按本表节流。
- 设备进入低电模式时，会在 `state` 中带上 `"low_power":true`；这时 Hub 把所有周期性推送统一放慢到至少 5s 一次。

### 3.8 错误与关闭码
| 关闭码 | 含义 | 设备行为 |
|---|---|---|
| 4001 | 令牌无效 | 停止自动重连，屏幕上提示"需要重新配对" |
| 4002 | 协议版本不支持 | 提示升级固件或 Hub，每 10 分钟重试一次 |
| 4003 | 令牌已被撤销 | 同 4001 |
| 4004 | 配对码错误或已过期 | 回到配网界面 |
| 4008 | 同一个 `dev_id` 有了新连接，旧连接被替换 | 不重连 |
| 1001 / 1006 | Hub 重启或网络断开 | 按指数退避重连（SPEC §6.12.2） |

`err` 中 `code` 的取值：`bad_request`、`rate_limited`、`perm_gone`、`not_found`、`forbidden`、`internal`。

---

## 4. USB-CDC 协议

### 4.1 帧格式
- 一行一个 JSON 对象，以 `\n` 结尾，单行最长 8 KB。字段与 WS 信封相同，但省略 `v` 和 `ts`：
  - 请求：`{"t":"…","id":N,"d":{…}}`
  - 应答：`{"t":"ok","id":N,"d":{…}}` 或 `{"t":"err","id":N,"d":{"code":"…","msg":"…"}}`
- 设备主动发出的消息（例如日志）不带 `id`。
- 波特率会被忽略（CDC 没有物理串口）；主机端统一按 115200 打开端口。
- Hub 按 USB VID `0x303A` 加上产品字符串 `nuka-esp32` 来识别设备。

### 4.2 `info`
- 请求体为 `{}`，应答示例：
```json
{"dev_id":"nuka_7c2e91","fw":"0.1.0","nukapack":"0.1.0","proto":1,"paired":true,"hub_id":"hub_3f9a1c","mac":"…","unlocked":false,"usb_admin":false}
```

### 4.3 配网与配对

#### 4.3.1 USB 配对（`nuka pair`）
1. Hub 发送 `info`，确认找到的是 nuka-esp32 设备。
2. Hub 发送 `prov.set`：
```json
{"wifi":[{"ssid":"<SSID>","pass":"<PASS>"}],
 "hubs":["ws://192.0.2.10:8790/ws/device","wss://hub.example.com/ws/device"],
 "hub_id":"hub_3f9a1c","hub_name":"my-hub","dev_token":"<TOKEN>","code":"482913"}
```
3. 设备显示确认 sheet，内容为 Hub 名称和配对码；用户在屏幕上点击确认，或 BOOT 长按。**在用户确认之前，设备不回复这个请求。**
4. 设备回复：`ok`，或者 `err`，`code` 为 `rejected` 或 `timeout`（60s 内没有确认）。
5. 设备写入 NVS 后，立即尝试连接。Hub 在 CLI 中显示连接结果。

#### 4.3.2 SoftAP 网页配对
1. 设备开启热点 `Nuka-XXXX`（XXXX 为 MAC 地址的后 4 位），并在 `http://192.168.4.1/` 提供配网页。页面提交 `POST /prov`，表单字段为 `ssid`、`pass` 和 `hub`（Hub 的 WS 地址）。
2. 设备连上 Wi-Fi 后，用 `X-Nuka-Pair: <dev_id>:<code>` 连接 Hub，其中 `code` 是设备生成并显示在屏幕上的 6 位配对码。
3. Hub 把这个连接放进待配对列表（5 分钟后过期），然后向它发送 `{"t":"pair.wait","d":{}}`。
4. 用户在 Web 的"设备"页面输入配对码（只允许 local / lan 区域）。Hub 发送 `{"t":"pair.ok","d":{"dev_token":"…","hub_id":"…","hub_name":"…"}}`；设备保存后断开，再用正式令牌重新连接。
5. 配对码输错 5 次后，这个待配对连接作废（关闭码 4004）。

**Cloudflare Access（仅启用 Service Token 时）**：USB `prov.set` 可选带 `cf_access={"origin":"https://hub.example.com","client_id":"<CF_CLIENT_ID>","client_secret":"<CF_CLIENT_SECRET>"}`；SoftAP 表单可选带 `cf_origin`、`cf_client_id`、`cf_client_secret` 三项，必须同时填写。字段写入设备 NVS，不经 WS 回传，不出现在日志或 `info` 中。设备仅向匹配 `origin` 的 HTTPS / WSS 请求添加 `CF-Access-Client-Id` 和 `CF-Access-Client-Secret`；WSS 比较 origin 时按 HTTPS 处理，也适用于该 origin 的 OTA / 封面下载。不得把凭据发送给列表中的其他 Hub 地址。Cloudflare 在边缘验证 Service Token，Hub 验证转发的 `Cf-Access-Jwt-Assertion`；Bearer 设备令牌仍然必需。[请求头约定来源](https://developers.cloudflare.com/cloudflare-one/access-controls/service-credentials/service-tokens/)

SoftAP `POST /prov` 的表单字段由 `protocol/schemas/provisioning.json` 表示其解码后的 JSON 结构；表单成功返回 200 `{}` 并尝试联网，输入不合法返回 400，错误体同 §5.1。

两种配网输入使用相同容量：SSID 为 1–32 个 UTF-8 字节，密码为 0–63 个 UTF-8 字节（空串表示开放网络），二者禁止 U+0000；Hub 的每个 WS URI 为 ASCII，最长 511 字符。schema 的 `wifi-ssid` / `wifi-passphrase` 格式校验上述字节长度，`maxLength` 同时限制字符数。USB CLI 在发送前将 URL 规范化为 URI。重新配网省略可选的 Cloudflare Access 凭据时，清除设备上旧的 Access 凭据。

### 4.4 Vault 导入 / 导出
前提：设备已解锁 vault，并且用户已在设备上手动进入"USB 管理模式"（`info.usb_admin == true`）。否则所有 `vault.*` 请求都返回 `err`，`code` 为 `forbidden`。

本节的发起方是本机 **nuka USB CLI 进程**，不经过 nuka-hub 服务、Web、HTTP 或 WS。

**导出**：
1. USB CLI 用隐藏输入取得备份口令，生成 16 字节随机 salt，在本机执行 `K_backup = scrypt(口令, salt, N=32768, r=8, p=1, dkLen=32)`。向设备发送 `vault.export`，内容为 `{"key_b64":"<32字节密钥的base64>","salt_b64":"<16字节salt的base64>"}`。口令不离开 CLI，不放入命令行参数、配置或日志。
2. 设备在屏幕上确认"导出 N 条"后，用收到的密钥和设备生成的 12 字节随机 nonce 执行 AES-256-GCM，按块回复：
   - 先发若干条 `{"t":"vault.chunk","id":N,"d":{"seq":0,"b64":"…","last":false}}`；
   - 最后发一条 `last: true` 的块，接着发 `ok`。
3. 按顺序分别解码每块 base64，再拼接二进制内容，得到备份文件 `*.nukavault`，格式如下：
```
magic "NVB1" | u8 kdf=1(scrypt) | u32 N | u8 r | u8 p | 16B salt | 12B nonce | AES-256-GCM(ciphertext||tag)
plaintext = JSON {"ver":1,"entries":[{"type":"totp","name":"…","secret_b32":"…","digits":6,"period":30,"algo":"SHA1"},
                                     {"type":"pass","name":"…","user":"…","pass":"…","url":"…","note":"…"}]}
```
NVB1 v1 的 KDF 参数固定为 scrypt `N=32768,r=8,p=1,dkLen=32`，头部的 kdf / N / r / p 必须分别为 1 / 32768 / 8 / 1；不协商其他参数。按 [RFC 7914 §5](https://www.rfc-editor.org/rfc/rfc7914.html#section-5) 的 ROMix 工作表推算，该参数需 `128 × r × N = 32 MiB`。KDF 因此只在本机 CLI 执行，设备不执行 scrypt，也不降低成本。CLI 只派生密钥、收发密文，不解密或处理 Vault 明文 JSON；设备执行加密与解密。

`u32 N` 使用小端序；GCM tag 为 16 字节，无额外 AAD。每个 `b64` 独立编码一个二进制块，按 `seq=0,1,...` 逐块解码后拼接，不能先连接带 padding 的 base64 文本。`size` 是完整加密备份的字节数，至少 55 字节（39 字节头部加 16 字节 tag）。`key_b64`、`salt_b64` 分别是 32 / 16 字节的规范 base64 编码。明文 JSON 的字段见 `vault-backup.json` 和 §9；v1 支持 TOTP 的 SHA1 / SHA256 / SHA512、6 / 8 位，`period` 为正整数，默认导入参数为 SHA1 / 6 / 30。USB 管理授权覆盖整个导入 / 导出，取消或超时终止传输，不向服务进程转交内容。设备的备份密钥、解密后的条目和明文缓冲仅在内部 RAM 中存在；传输完成、取消或超时即零化。CLI 在派生完成后清零持有的可变口令缓冲，在传输结束后清零持有的可变密钥缓冲，不持久化密钥或将其写入日志。Python 的 getpass、KDF 和 JSON 编码可能产生不可变临时副本，不能承诺从 CPython 内存中逐一擦除；删除引用不等同于零化，CLI 传输完成后退出进程。此限制不放宽设备内部 RAM 的明文与密钥零化要求。

**导入**：
1. USB CLI 在文件输入边界检查 NVB1 魔数、固定 v1 KDF 参数及最小文件长度，从头部读取 salt；用隐藏输入取得口令并按上述固定参数在本机派生密钥。发送 `vault.import.begin`，内容为 `{"key_b64":"<32字节密钥的base64>","size":N}`。
2. USB CLI 发送若干条 `vault.import.chunk`，内容为 `{"seq":i,"b64":"…"}`。
3. USB CLI 发送 `vault.import.end`。
4. 设备在 USB-CDC 输入边界校验分块序号、累计长度与声明的 `size` 一致，以及 NVB1 头部固定参数；使用收到的密钥和文件 nonce 解密，认证通过后再解析明文 JSON。在屏幕上确认"导入 N 条（重复 M 条将跳过）"，然后回复 `ok`，内容为 `{"added":N,"skipped":M}`。重复按 `(type, name)` 的 UTF-8 字节精确比较，区分大小写；跳过设备已有条目与本次导入中已经出现的条目。头部、长度、认证或 JSON 不合法时返回 `err`，`code` 为 `bad_request`，不改动现有条目。

本次修改属于尚未发布的 proto=1 初稿补齐：CDC 不定义 `passphrase` 字段，不实现旧口令请求的兼容路径。

**导入 otpauth URI**：USB CLI 发送 `vault.import.otpauth`，内容为 `{"uris":["otpauth://totp/…"]}`。设备逐条确认，或者一次性确认全部。

### 4.5 诊断与系统
| 请求 | 说明 |
|---|---|
| `diag.info` | 返回 SPEC §6.17 中列出的全部字段 |
| `diag.log` | 消息体为 `{"on":true}` 时，设备开始持续发送 `{"t":"log","d":{"lvl":"I","tag":"net","msg":"…","ts":…}}`；为 `{"on":false}` 时停止 |
| `diag.dump_log` | 分块返回 storage 分区中的环形日志（复用 `vault.chunk` 的分块格式，类型名改为 `log.chunk`） |
| `sys.reboot` | 重启 |
| `sys.download` | 重启进入 ROM 下载模式，方便 esptool 直接烧录（不用手动按 BOOT）。固件需要在 FW-21 中验证这一功能 |
| `macro.set` | 与 WS 中的同名消息相同（§3.6），用于在离线时配置宏 |

所有空请求的 `d` 为 `{}`。`info`、`diag.info` 和导入完成均使用 `t:"ok"` 应答：`info_response`、`diag_response`、`import_response` 的完整字段在 §9 和 `cdc.json.$defs` 中。`diag_response.tasks[].free_b` 单位为字节，RSSI 在尚未联网时省略。`diag.dump_log` 的块与终止 `ok` 共用请求 `id`。USB `err.code` 在 §3.8 的集合上增加 `rejected` 和 `timeout`。

---

## 5. Hub HTTP 接口（REST + Web WS + hooks）

### 5.1 约定
- 基础路径为 `/api/v1`，请求和响应都是 JSON。
- 错误统一返回：`{"error":{"code":"forbidden","message":"…"}}`，并配上对应的 HTTP 状态码（400 / 401 / 403 / 404 / 409 / 413 / 429 / 500 / 502 / 503）。
- 鉴权方式：
  - Web 使用会话 cookie `nuka_session`，会改变状态的请求还要带 `X-CSRF-Token`；
  - 脚本使用 `Authorization: Bearer <token>`。
- 区域限制见 SPEC §7.11；被区域禁止的请求一律返回 **404**，不暴露接口是否存在。
- 列表接口统一返回 `{"items":[…]}`；数据量大时，用 `?limit=&before=` 分页。
- `before` 为上一页最后一项的键：通常是 `id`，plugins 使用 `name`、ota 使用 `file`、secrets 使用 `name`、pair/pending 使用 `dev_id`。列表接口包括 hosts、servers、notifications、devices、plugins、tokens、audit、pair/pending、ota、secrets；下表中的 `Host[]` 等是 `items` 的元素类型。`GET /plugins` 也使用包装对象。

### 5.2 接口清单
| 方法与路径 | 作用域 | 说明 |
|---|---|---|
| `GET /api/v1/health` | 无 | 返回 `{"ok":true,"ver":"0.1.0","proto":1}` |
| `POST /api/v1/login` | 无（限流：每个 IP 每分钟 5 次） | 请求体为 `{"token":"<admin token>"}`，成功后通过 Set-Cookie 设置会话，并返回 `{"csrf":"…"}` |
| `POST /api/v1/logout` | 会话 | |
| `GET /api/v1/me` | 会话 | 返回 `{"zone":"lan","csrf":"…","features":{…},"lang":"zh-CN"}` |
| `GET /api/v1/state` | admin | 返回所有主题的完整快照：`{"hosts":[…],"servers":[…],"ai":{…},"agents":{…},"notify":[…],"devices":[…],"media":[…],"weather":{…},"plugins":[…]}` |
| `GET /api/v1/hosts` | admin | `Host[]` |
| `GET /api/v1/hosts/{id}/history?keys=cpu.pct,gpu.pct&span=24h` | admin | 返回 `{"ts":[…],"series":{"cpu.pct":[…]}}`。`span` 取 `1h`、`24h` 或 `30d` |
| `GET /api/v1/servers`、`GET /api/v1/servers/{id}/history` | admin | 同上 |
| `GET /api/v1/ai` | admin | 返回 `{"summary":…,"accounts":[AiAccount],"providers":[ProviderHealth]}` |
| `POST /api/v1/ai/refresh` | admin（全局每分钟 1 次） | 返回 202 |
| `POST /api/v1/ai/providers/{name}/retry` | admin | 熔断后手动重试，只发 1 个请求 |
| `GET /api/v1/agents` | admin | 返回 `{"sessions":[AgentSession],"pending":[PermRequest]}` |
| `POST /api/v1/perm/{rid}` | admin，**只允许 local / lan 区域，并且需要在配置中开启** | 请求体为 `{"behavior":"allow"\|"deny"}` |
| `GET /api/v1/audit?kind=perm` | admin | 审计日志 |
| `GET /api/v1/notifications` | admin | `Notification[]` |
| `POST /api/v1/notify` | notify | 请求体见 §5.6 |
| `GET /api/v1/devices`、`PATCH /api/v1/devices/{dev_id}` | admin | PATCH 可修改 `{"name":"…"}` |
| `DELETE /api/v1/devices/{dev_id}` | admin | 删除设备，同时撤销它的令牌 |
| `GET /api/v1/pair/pending`、`POST /api/v1/pair/confirm` | admin，只允许 local / lan 区域 | 确认时的请求体为 `{"dev_id":"…","code":"482913","name":"desk-1"}` |
| `GET /api/v1/ota` | admin | 列出 manifest |
| `POST /api/v1/ota/offer` | admin | 请求体为 `{"ver":"…","kind":"app","dev_ids":[…]}`，向指定设备发送 OTA 通知 |
| `GET /api/v1/ota/{file}` | device | 下载固件文件 |
| `GET /api/v1/media/cover/{hash}.jpg` | device 或 admin | 获取封面图 |
| `POST /api/v1/media/{src_id}/cmd` | admin | 请求体为 `{"cmd":"toggle"}` |
| `GET /api/v1/plugins` | admin | 返回 `{"items":[{"kind":"collector","name":"nvml","health":"ok","detail":"…","doc":"guides/04-hardware-sensors#nvidia"}]}` |
| `GET /api/v1/tokens`、`POST /api/v1/tokens`、`DELETE /api/v1/tokens/{id}` | admin | 创建令牌时，明文只在这一次响应中返回 |
| `GET /api/v1/config` | admin | 返回 CONFIG 的 JSON 只读视图，仅保留密钥名称引用，不含任何明文密钥 |
| `GET /api/v1/config/schema` | admin，local / lan 或 §5.8 的远程管理例外 | 返回当前 pydantic Config 的 JSON Schema，包含字段类型、枚举、默认值和嵌套定义；没有密钥值 |
| `PUT /api/v1/config` | admin，local / lan 或 §5.8 的远程管理例外 | `config-structured-put`：config 为完整配置对象，mtime 同 raw 接口。校验、409 并发检查、旧文件备份与热重载沿用 raw；用 tomlkit 写回，保留未改字段的注释 / 顺序；成功 200 `{}` |
| `GET /api/v1/config/raw` | admin，只允许 local / lan 区域 | 返回 `{"toml":"…","path":"…","mtime":"…"}`。`mtime` 为 UTC ISO 8601，保留服务端精度并原样回传；配置文件中不含密钥明文 |
| `PUT /api/v1/config/raw` | admin，只允许 local / lan 区域 | 请求体为 `{"toml":"…","mtime":"…"}`。`mtime` 与文件当前的修改时间不一致时，返回 409。服务端先校验，校验失败时返回 400，并在 `error.details` 中给出 `[{"line":12,"msg":"…"}]`；校验通过后备份旧文件（`config.toml.bak.<ts>`），写入新文件并热重载 |
| `GET /api/v1/secrets` | admin，只允许 local / lan 区域 | 只返回密钥名称和来源（env / keyring / file），**不返回值** |
| `PUT /api/v1/secrets/{name}`、`DELETE /api/v1/secrets/{name}` | admin，只允许 local / lan 区域 | PUT 的请求体为 `{"value":"…"}`，写入钥匙串（钥匙串不可用时写入 `secrets.toml`）。写入后，重载引用了这个密钥的插件 |

C2-03 的显式远程管理例外及助手、记录、记忆、文件接口见 §5.8；它不开放配对、USB、Agent hooks 或 Web 审批。

结构化 PUT 是完整替换，不是合并补丁；省略的配置项回到 CONFIG 的默认值。GET /config 的形状保持纯配置对象，mtime 从 GET /config/raw 获取并原样回传。字段校验错误沿用 `error.details` 的 line / msg；msg 中包含字段路径，便于表单定位。以上两个表单接口从 C2-02 提前到与音频无关的 C2-03。

### 5.3 资源结构（REST）

`Server` 可分别提供 `mem_pct` / `disk_pct`（0–100，最多一位小数）；上游只有百分比时，不伪造 `*_used_b` / `*_total_b`。同时提供字节数时，Hub 可由字节数派生百分比；字段定义见 §9.5。
REST 返回完整的领域模型，比设备的视图模型更详细。Web 管理页和脚本都使用这些结构。

各结构的必选 / 可选字段见 §9，所有 REST 绝对时间使用以 `Z` 结尾的 UTC ISO 8601 字符串（包括媒体 `at`、天气 `updated`）；`Device.state` 保留 §3.5.2 的原始设备快照。`Credits={balance,currency}`、`UsageToday={requests,tokens,failures}`、`Action={kind:"dismiss"}`。`GET /ai` 的可选 `summary` 是跨账号聚合的 UsageToday；没有 CPAMP 数据时省略。

资源模型中的 Python `None` 表示序列化时省略，而不是发送 JSON null。`plugins` 健康值为 `ok` / `degraded` / `error` / `disabled`；AI 提供者单独使用 ProviderHealth 的状态枚举。媒体状态为 `playing` / `paused` / `stopped`。历史 `ts` 与各 `series` 数组等长；只返回各已返回序列都有观测值的时间点，不用 0 或 null 填补缺测。

```jsonc
// Host
{"id":"h_desktop","name":"desktop","os":"windows","online":true,"last_seen":"…","role":"hub|agent",
 "cpu":{"pct":23.5,"per_core":[…],"temp_c":54,"freq_mhz":4700,"power_w":65},
 "gpus":[{"name":"…","pct":12,"temp_c":41,"mem_used_b":…,"mem_total_b":…,"power_w":45,"fan_pct":30,"clock_mhz":…}],
 "mem":{"used_b":…,"total_b":…,"swap_used_b":…,"swap_total_b":…},
 "disks":[{"mount":"C:","used_b":…,"total_b":…,"read_bps":…,"write_bps":…}],
 "net":{"up_bps":…,"down_bps":…},
 "fans":[{"label":"…","rpm":…}],"temps":[{"label":"…","c":…}],
 "bat":{"pct":90,"charging":true},
 "plugins":{"nvml":"ok","lhm":"disabled"}}

// Server
{"id":"s_web1","name":"web-1","src":"serverstatus","src_name":"<source name>","online":true,"last_seen":"…",
 "cpu_pct":12,"mem_used_b":…,"mem_total_b":…,"disk_used_b":…,"disk_total_b":…,"load":[0.4,0.3,0.2],
 "net_up_bps":…,"net_down_bps":…,"uptime_s":…,"latency_ms":…,"uptime_pct":…,"alerts":[{"rule":"…","level":"warn","since":"…"}]}

// AiAccount：字段同 SPEC §7.6.2，时间用 ISO 8601。另外有 "today"（来自 CPAMP 的 {"requests":…,"tokens":…,"failures":…}）
// ProviderHealth
{"name":"main","type":"cpamp","health":"ok|degraded|auth_failed|backoff|disabled","last_ok":"…","next_try":"…","detail":"…"}

// AgentSession：字段同 SPEC §7.7.1
// PermRequest
{"rid":"r_5e1f","sid":"…","src":"claude","host":"…","project":"…","tool":"Bash","summary":"…","created":"…","expires":"…"}

// Notification：字段同 SPEC §7.9
// Device
{"dev_id":"nuka_7c2e91","name":"desk-1","fw":"0.1.0","online":true,"last_seen":"…","ip":"…",
 "state":{"screen":true,"bat":{…},"rssi":-58,"heap_int":…}}
```

### 5.4 Web 管理页的数据获取
Web 管理页不使用 WebSocket，而是每 5s 轮询一次 `GET /api/v1/state`；各项操作通过 §5.2 中的 REST 接口完成（SPEC §7.13）。

### 5.5 `POST /hook/server`
- 鉴权：`Authorization: Bearer <notify token>`，或者在 URL 中带 `?token=`（为了方便那些只能配置 URL 的工具；文档中要提示这样令牌会出现在访问日志里）。
- 区域：local 允许；lan 需要 `security.allow_lan_hooks=true`；public 永远返回 404。此规则覆盖 GET 和 POST。public 的通用通知使用 `/api/v1/notify`，不用 `/hook/*`。
- 支持三种请求体：
  - `application/json`：
    ```json
    {"title":"Disk almost full","body":"/ is 91%","level":"warn","server":"web-1","source":"cron","dedupe_key":"web-1-disk","ttl_s":86400}
    ```
    - 只有 `title` 必填，最多 120 个字符；`body` 最多 2000 个字符。
    - `level` 默认为 `info`。
    - `server` 用于匹配服务器的名称或别名，匹配上的话会更新它的告警角标。
  - `text/plain`：第一行作为 `title`，其余部分作为 `body`。
  - 查询参数形式：`?title=…&level=…`（给只能发 GET 请求的工具用，所以同时支持 `GET /hook/server`）。
- 响应：
  - 成功返回 `202 {"nid":"n_…"}`；
  - 超过限流返回 `429`，带 `Retry-After` 头。

### 5.6 `POST /api/v1/notify`
- 请求体与 §5.5 相同，`server` 字段除外。用于通用通知（脚本、CI 之外的各种工具）。

### 5.7 AI CLI hooks
| 路径 | 调用方 | 请求体 | 响应 |
|---|---|---|---|
| `POST /hook/claude` | Claude Code 原生 HTTP hook | Claude Code 的 hook 输入 JSON，原样转发（`session_id`、`cwd`、`hook_event_name`、`tool_name`、`tool_input`、`message`、`last_assistant_message`…） | 非审批事件立即返回 `200 {}`；`PermissionRequest` 事件见 SPEC §7.7.2 |
| `POST /hook/codex` | `nuka-hook codex <event>` | `{"event":"PreToolUse","host":"<hostname>","payload":<stdin 的原始 JSON>}` | `200 {}` |
| `POST /hook/gemini` | `nuka-hook gemini <event>` | 结构同上 | `200 {}` |

- 跨机器调用时，请求要带 `Authorization: Bearer <hook token>`；对于 Claude Code，令牌通过 `headers` 加 `allowedEnvVars` 注入。
- Hub 只读取它需要的字段，其余字段不入库。`tool_input` 只用来生成摘要，不保存原文。

### 5.8 Pi 助手、永久记录与文件（C2-03）

#### 5.8.1 身份与数据归属
- 以下所有接口需要 admin 身份。local / lan 沿用 §5.1 的会话 cookie / Bearer 及 CSRF 规则；助手页面和接口不向普通 public 请求开放。
- 唯一的远程管理例外是：`security.remote_admin=true`、`security.cf_access.enabled=true`、当前请求的 Cloudflare Access JWT 已通过签名、issuer、audience 和时效验证，且当前请求持有有效的 Hub admin **会话 cookie**。写请求仍须 `X-CSRF-Token`。不能仅凭 CF 请求头、Access 身份或 admin Bearer 令牌启用该例外；不满足区域条件返回 404，不把 public 改判为 lan。
- 该例外只开放配置页面、`/api/v1/config` 及其子路径、`/api/v1/secrets` 及其子路径、助手页面和本节 `/api/v1/assistant/*`。`web.allow_config_edit=false` 仍禁止配置和密钥写入。Access 保护下的登录入口按既有登录流程创建会话；登录前不能读取受保护数据。
- `/hook/*`、`/approve` 和 `/api/v1/perm/*` 在 public 永远返回 404；USB 和配对接口也不在远程管理例外内。
- `dev_id` 必须是本 Hub 注册的设备 ID，demo 模式只可额外使用其固定 demo 设备。客户端给出一个 ID 不构成授权。会话、来源消息、记忆、附件都须属于同一 `dev_id`；不属于该设备的资源和不存在的资源同样返回 404。Web 不能用设备令牌调用本节接口。

#### 5.8.2 文字对话与 SSE

| 方法与路径 | 请求 | 响应 |
|---|---|---|
| `GET /api/v1/assistant/status` | 无 | `assistant-status`：enabled、history_enabled、memory_enabled |
| `POST /api/v1/assistant/chat` | `assistant-chat-request`：dev_id、text，可选 session_id、file_ids | 200 `text/event-stream`，事件见下表 |
| `GET /api/v1/assistant/history` | 必填 dev_id，可选 session_id、limit、before | `assistant-history`：items，可选 next_before |
| `DELETE /api/v1/assistant/history` | 必填 dev_id 和 session_id 查询参数 | 200 `{}`，事务删除该会话 |
| `GET /api/v1/assistant/history/export` | 必填 dev_id，可选 session_id | `application/x-ndjson`，每行一个 assistant-message，按创建顺序；强制附件下载 |
| `GET /api/v1/assistant/memories` | 必填 dev_id，可选 q、limit、before | `assistant-memories`：items，可选 next_before |
| `POST /api/v1/assistant/memories` | `assistant-memory-create`：dev_id、text，可选 source_message_id | 202 `assistant-pending`，等待设备确认；未授权不得写入 |
| `DELETE /api/v1/assistant/memories/{id}` | 必填 dev_id 查询参数 | 200 `{}`，显式管理删除 |
| `GET /api/v1/assistant/files` | 必填 dev_id，可选 limit、before | `assistant-files`：items，可选 next_before |
| `POST /api/v1/assistant/files` | 必填 dev_id 查询参数；原始文件字节、X-Filename 头 | 201 `assistant-file` |
| `GET /api/v1/assistant/files/{id}/download` | 必填 dev_id 查询参数 | 原始文件，强制附件下载 |
| `DELETE /api/v1/assistant/files/{id}` | 必填 dev_id 查询参数 | 200 `{}`，删除元数据及存储内容 |

- `text` 为 1–16384 个字符；`file_ids` 最多 8 个、不重复，只能引用该设备已有附件。服务端为新会话生成 `session_id`；给定会话必须属于该设备。每个会话同一时间只允许一轮，重复发起返回 409，不并发写同一上下文。
- 助手未启用或 Pi 运行依赖不可用时返回 503；配置无效、请求字段不合法返回 400，文件超限返回 413，模型上游失败在流建立前返回 502。不得在错误中回显令牌或上游完整响应。
- SSE 使用 UTF-8，事件格式为 `event: <name>\ndata: <JSON>\n\n`；每条 data 是单行 JSON。只通过 POST 请求体传递文字，不把文字或密钥放到 URL。样例中的 `assistant-event.*.json` 是便于 T6 校验的 `{event,data}` 表示，线上仍使用 SSE。

| event | data 必选字段 | 说明 |
|---|---|---|
| start | dev_id、session_id | 第一条事件，确定会话 |
| delta | text | 一段助手文本，按顺序追加 |
| tool | tool_call_id、name、state | state 为 requested / waiting_confirmation / running / completed / denied / cancelled / error；可选 text 表示结果或说明 |
| done | session_id | 正常结束；可选 cancelled=true 表示取消 |
| error | code、message | 流建立后的集中错误处理，之后结束流，不继续执行工具 |

- HTTP 断开或任务取消时，Hub 同时停止 Pi 请求、未完成的工具与待确认操作；不能在断开后继续等待并执行敏感工具。语音打断和设备确认的网络消息由 C2-02 定义，本批不选择音频编码。
- 工具只来自 Hub 明确注册并启用的列表。模型与 MCP 的权限提示仅作资料，不能授权执行。写入长期记忆属于敏感工具；Web 新增记忆也要设备确认。确认通道不可用返回 409；拒绝、过期或取消时不写入。`assistant-pending.request_id` 由 Hub 生成，不能由客户端自行许可。
- Web 的显式 DELETE 管理操作由 admin 会话 / CSRF 授权；模型发起的删除仍是敏感工具，必须走设备确认，不能借用管理接口绕过。

#### 5.8.3 永久记录、记忆与备份
- 使用 SQLite WAL，完整记录按设备 / 会话 / 消息标识持久化，消息 ID 幂等。记录用户文本、助手文本和工具请求 / 结果 / 终态，所有时间为 UTC ISO 8601。原始音频只在内存中流转；不得进入通用日志或默认落盘。
- `assistant.history.context_turns` 默认 20，只限制模型输入中的最近轮次，不是保留期限。完整存档没有 TTL；构造上下文时保留工具请求和结果的关联，不选出孤立的工具结果。
- 模型总预算取 `assistant.llm.context_window`（默认 32768，按实际模型配置），输出预算取 `assistant.max_output_tokens`；配置边界要求输出预算严格小于总预算。system、工具和记忆也占上下文，无法容纳最近 20 轮时减少完整轮次，不截出孤立工具结果；不能宣称所有自定义模型都有相同容量。
- `assistant.history.enabled=false` 后不写入新记录，已保存数据仍可查看、导出和删除；进程内仍可维持本次会话的有限上下文。关闭记录不等于清空历史。
- history / memories / files 列表按创建顺序倒序，`limit` 为 1–100（默认 50），`before` 是上一页末项的 opaque ID；响应仅在还有下一页时带 `next_before`，可直接用作下次 before。导出则按创建顺序正序输出，不套 items 包装。
- 删除会话事务删除其消息、工具生命周期及来源消息位于该会话的长期记忆，取消该会话的未完成轮次。附件独立管理，不随会话删除；删除附件后旧记录可以保留其 ID，但不能再下载。删除不由历史自动回填。
- 长期记忆只保存用户明确要求记住且已获设备授权的事实，单条 text 为 1–2000 个字符，带创建 / 更新时间及可选来源消息；来源消息必须属于该设备。`assistant.memory.enabled=false` 后不写入或检索新记忆，已保存记忆仍可管理删除。
- 向模型检索记忆时按设备隔离，最多 8 条、合计最多 8000 个字符；按 SQLite 文本检索，不引入向量服务、自动提炼或后台推断。记忆是资料，不能覆盖 system 指令、工具白名单或设备确认。
- 备份通过 SQLite 在线 backup API 生成一致快照，附件目录与其元数据一同归档；不能直接复制写入中的 WAL 数据库。恢复须显式停服，验证数据库完整性和附件大小 / SHA-256，恢复成功后再启动；不自动合并别的 Hub 设备或令牌。数据库、附件和导出文件均位于数据 / 备份目录，不进入源码仓库。

#### 5.8.4 有界附件传输
- 上传请求使用 `Content-Type: application/octet-stream` 的原始 body，不使用 multipart。`X-Filename` 是显示文件名的 UTF-8 百分号编码（与 `encodeURIComponent` 一致）；服务端严格解码，拒绝空名、U+0000、CR/LF、`/`、`\\`、`.`、`..`，解码后的 UTF-8 长度不超过 255 字节。
- 服务端同时检查 Content-Length（如果提供）和流式累计字节数，单文件上限来自 `assistant.uploads.max_bytes`，默认 **67108864 字节（64 MiB）**。超限返回 413 并删除未完成临时文件；客户端声明长度不能取代实际计数。
- 服务端生成文件 ID 和存储路径，客户端文件名仅作显示；请求、响应和下载中不提供或接受磁盘路径。不自动解包、运行或导入上传文件；模型只能经 Hub 启用的工具访问已授权附件。
- 响应包含 id、dev_id、name、size、sha256、created_at；下载使用 `Content-Disposition: attachment` 和 `X-Content-Type-Options: nosniff`。不存在、已删除或归属不同的文件均返回 404。
- Vault 数据不进入此接口，继续仅在设备 USB CLI 管理流程中流转。

---

## 6. Agent 角色 WebSocket：`/ws/agent`
- 鉴权：`Authorization: Bearer <agent token>`。信封与 §3.2 相同。

| 方向 | 类型 | 消息体 |
|---|---|---|
| Agent → Hub | `hello` | `{"agent_id":"ag_laptop","name":"laptop","os":"darwin","ver":"0.1.0","proto":1,"caps":["host","media"]}` |
| Hub → Agent | `welcome` | `{"hub_id":"…","intervals":{"host_fast":1,"host_slow":2,"disk":60}}` |
| Agent → Hub | `host.sample` | 一个完整的 `Host` 对象（§5.3），其中省略 `id` 和 `role`，由 Hub 补全。发送频率取 welcome 中给出的间隔 |
| Agent → Hub | `media.state` | `{"items":[…]}`，结构同 `vm.media.items`。封面以 `cover_b64` 字段内联（160×160 JPEG），只在封面变化时发送 |
| Hub → Agent | `media.cmd` | `{"src_id":"…","cmd":"toggle"}` |

- Agent 断开超过 30s 后，Hub 把这台主机标记为 `online:false`。
- Agent 握手的 `hello` / `welcome` 必须带相同的整数 `id`。嵌入的 `host.sample` 保留 REST Host 的 UTC 字符串时间，省略由 Hub 补全的 `id` / `role`；`media.state` 的 `at` 按设备媒体协议使用 Unix 秒。
- `cover_b64` 是独立的 160×160 baseline JPEG 的 base64，解码后不超过 20 KB。它替代设备侧 `cover` 路径，Hub 缓存后提供 HTTP 下载；schema 验证 base64 字符形状，图像尺寸和编码由接收边界验证。

---

## 7. Schema 与样例的组织方式（任务 C-01 的产出）
```
protocol/
  schemas/
    envelope.json
    ws-device.json        # 用 oneOf 列出 §3 中的全部消息类型，以 t 字段区分
    ws-agent.json
    cdc.json
    device-data.json      # §3 的消息体与嵌套结构
    agent-data.json
    cf-access.json
    provisioning.json     # SoftAP 表单解码后的字段
    vault-backup.json     # 加密前 / 解密后的明文结构，仅 USB CLI / 设备使用
    wmo-icon-map.json
    rest/*.json           # §5 的资源、请求与响应
  examples/
    device/*.json         # 每种消息至少 1 个样例，文件名为 <t>.json；有变体的话命名为 <t>.<variant>.json
    agent/*.json
    cdc/*.json
    rest/*.json
    backup/*.json
    provisioning/*.json
  wmo_icon_map.json       # WMO 天气代码 → 12 种图标
  README.md               # 如何校验（scripts/check_protocol.py）以及修改流程
```

---

## 8. 版本记录
| proto | 日期 | 变更 |
|---|---|---|
| 1 | 2026-10-17 | 首次本地冻结；含 USB CLI 派生 Vault 备份密钥与 3 MiB assets OTA 上限，待远程 PR 评审 |

---

## 9. 字段规范表（C-01 本地冻结）

以下表格与 schema 逐字段对应。`ID` 表示 `[a-z0-9_-]{1,32}`；`integer≥0` 包括真实的零值。可选字段不接受 null，除非枚举明确列出 null。嵌套对象出现时，必须满足它自己的必选列；表中“禁止”字段不能出现在对应资源中。除 WMO 固定映射外，未列出的字段均按未知字段忽略。

条件约束：`quota_window.kind=other` 要求 `label`；`vm.pet.mood=celebrate` 要求 `until`；媒体 `pos_s` 要求 `at`；订阅非空的 `sub.detail.topic` 要求 `id`；`notify.dismiss` 的 `nid` 与 `all:true` 二选一；`ota.result.ok=false` 要求 `reason`；`ota.offer` 和 REST `ota_manifest` 的 `kind=assets` 时 `size≤3145728`；宏 `kind=text` 的 `value` 最长 256 字符；`key3_long.action=macro` 要求 `slot`；配对后的 `info_response` 要求 `hub_id`。

`cdc.ok` 的业务响应形状由正在等待应答的请求决定，通用信封本身不能从 `id` 推断请求类型；解析方按请求选择 `info_response` / `diag_response` / `import_response`。T6 也对三个响应样例分别应用对应定义。CDC 的 `log` 禁止 `id`，所有其他已定义 CDC 消息必须带 `id`；CDC 外层禁止 `v` / `ts`。Agent 的 `host.sample` 禁止 `id` / `role`。


### 9.1 WS 信封

| 对象 | 必选字段（类型 / 约束） | 可选字段（类型 / 约束） |
|---|---|---|
| `envelope` | `v` =1<br>`t` string（最短1）<br>`ts` integer（≥0）<br>`d` object | `id` integer |

### 9.2 设备消息体与嵌套结构

| 对象 | 必选字段（类型 / 约束） | 可选字段（类型 / 约束） |
|---|---|---|
| `battery` | `pct` number（≥0，≤100，步长0.1）<br>`mv` integer（≥0）<br>`chg` none / charging / full<br>`src` usb / bat | `eta_min` integer（≥0） |
| `features` | `ai` boolean<br>`servers` boolean<br>`agents` boolean<br>`media` boolean<br>`weather` boolean | — |
| `time` | `epoch` integer（≥946684800，≤4102444799）<br>`tz_posix` string（可打印ASCII，最长127；允许空串） | — |
| `hello` | `dev_id` ID<br>`fw` string<br>`nukapack` string<br>`proto` =1<br>`lang` zh-CN / en<br>`screen` boolean<br>`caps` [string]（不重复）<br>`topics` [hosts / servers / ai / agents / notify / media / weather / pet]（不重复）<br>`bat` device-data.json:battery | — |
| `welcome` | `hub_id` ID<br>`hub_name` string<br>`hub_ver` string<br>`proto` =1<br>`dev_name` string<br>`lang` zh-CN / en<br>`time` device-data.json:time<br>`features` device-data.json:features<br>`macros_rev` integer（≥0） | `ntp` [string] |
| `host_cpu` | — | `pct` number（≥0，≤100，步长0.1）<br>`temp_c` number<br>`freq_mhz` number（≥0） |
| `host_gpu` | — | `name` string<br>`pct` number（≥0，≤100，步长0.1）<br>`temp_c` number<br>`mem_pct` number（≥0，≤100，步长0.1）<br>`power_w` number（≥0） |
| `host_mem` | — | `pct` number（≥0，≤100，步长0.1）<br>`txt` string |
| `host_net` | — | `up_txt` string<br>`down_txt` string |
| `host_disk` | `label` string<br>`pct` number（≥0，≤100，步长0.1） | — |
| `host_bat` | `pct` number（≥0，≤100，步长0.1）<br>`chg` boolean | — |
| `host` | `id` ID<br>`name` string<br>`online` boolean | `os` string<br>`cpu` device-data.json:host_cpu<br>`gpu` device-data.json:host_gpu<br>`mem` device-data.json:host_mem<br>`net` device-data.json:host_net<br>`disks` [device-data.json:host_disk]（最多3项）<br>`bat` device-data.json:host_bat |
| `vm.hosts` | `rev` integer（≥0）<br>`items` [device-data.json:host]（最多6项） | `more` integer（≥0） |
| `detail_row` | `k` string<br>`v_txt` string | — |
| `spark` | — | `cpu` [number（≥0，≤100，步长0.1）]（最多48项）<br>`gpu` [number（≥0，≤100，步长0.1）]（最多48项）<br>`mem` [number（≥0，≤100，步长0.1）]（最多48项） |
| `vm.host_detail` | `rev` integer（≥0）<br>`id` ID<br>`rows` [device-data.json:detail_row] | `spark` device-data.json:spark<br>`spark_span_txt` string |
| `vm.server_detail` | `rev` integer（≥0）<br>`id` ID<br>`rows` [device-data.json:detail_row] | `spark` device-data.json:spark<br>`spark_span_txt` string |
| `server` | `id` ID<br>`name` string<br>`online` boolean<br>`src` string | `cpu_pct` number（≥0，≤100，步长0.1）<br>`mem_pct` number（≥0，≤100，步长0.1）<br>`disk_pct` number（≥0，≤100，步长0.1）<br>`load1` number（≥0）<br>`net_txt` string<br>`uptime_txt` string<br>`alerts` integer（≥0）<br>`latency_ms` number（≥0）<br>`uptime_pct` number（≥0，≤100，步长0.1） |
| `vm.servers` | `rev` integer（≥0）<br>`items` [device-data.json:server]（最多12项） | `more` integer（≥0） |
| `quota_window` | `kind` 5h / daily / weekly / monthly / other<br>`used_pct` number（≥0，≤100，步长0.1）<br>`src` passive / snapshot / active<br>`stale` boolean | `label` string<br>`reset_at` integer（≥1） |
| `ai_summary` | `req_today` integer（≥0）<br>`tok_today_txt` string<br>`fail_pct` number（≥0，≤100，步长0.1） | — |
| `ai_account` | `id` ID<br>`kind` codex / claude / antigravity / kimi / other<br>`label` string<br>`status` ok / cooldown / exhausted / reauth / disabled / unknown<br>`windows` [device-data.json:quota_window] | `plan` string<br>`recover_at` integer（≥1）<br>`credits_txt` string<br>`fresh_txt` string<br>`breaker_open` boolean |
| `vm.ai` | `rev` integer（≥0）<br>`accounts` [device-data.json:ai_account]（最多8项） | `summary` device-data.json:ai_summary<br>`more` integer（≥0） |
| `session` | `sid` ID<br>`src` claude / codex / gemini<br>`host` string<br>`state` idle / thinking / tool / waiting_permission / done / error<br>`since` integer（≥0） | `project` string<br>`tool` string<br>`msg` string（最长140） |
| `vm.agents` | `rev` integer（≥0）<br>`pending` integer（≥0）<br>`sessions` [device-data.json:session]（最多6项） | `more` integer（≥0） |
| `notification` | `nid` ID<br>`level` info / warn / critical<br>`src` string<br>`title` string（最长120）<br>`ts` integer（≥0） | `body` string（最长200） |
| `vm.notify` | `rev` integer（≥0）<br>`items` [device-data.json:notification]（最多20项） | `more` integer（≥0） |
| `media_can` | `toggle` boolean<br>`next` boolean<br>`prev` boolean | — |
| `media_item` | `src_id` ID<br>`host` string<br>`app` string<br>`state` playing / paused / stopped<br>`can` device-data.json:media_can | `title` string<br>`artist` string<br>`album` string<br>`pos_s` number（≥0）<br>`dur_s` number（≥0）<br>`at` integer（≥0）<br>`cover` string（pattern=^/api/v1/media/cover/[a-f0-9]+\.jpg$） |
| `vm.media` | `rev` integer（≥0）<br>`items` [device-data.json:media_item]（最多3项） | `more` integer（≥0） |
| `weather_now` | — | `temp_c` number<br>`feels_c` number<br>`hum_pct` number（≥0，≤100，步长0.1）<br>`wind_txt` string<br>`icon` clear_day / clear_night / partly_cloudy / cloudy / fog / drizzle / rain / heavy_rain / snow / sleet / thunder / wind<br>`desc_txt` string |
| `weather_day` | `date` string（date） | `hi_c` number<br>`lo_c` number<br>`icon` clear_day / clear_night / partly_cloudy / cloudy / fog / drizzle / rain / heavy_rain / snow / sleet / thunder / wind |
| `vm.weather` | `rev` integer（≥0） | `loc_txt` string<br>`updated` integer（≥0）<br>`now` device-data.json:weather_now<br>`days` [device-data.json:weather_day]（最多3项） |
| `vm.pet` | `rev` integer（≥0）<br>`mood` sleep / idle / busy / attention / celebrate / hungry | `reason` string<br>`until` integer（≥1） |
| `sub` | `topics` [hosts / servers / ai / agents / notify / media / weather / pet]（不重复） | — |
| `state` | `screen` boolean<br>`dnd` boolean<br>`card` clock / hosts / servers / ai / buddy / pet / pomodoro / media / weather / vault / settings<br>`bat` device-data.json:battery | `rssi` integer<br>`heap_int` integer（≥0）<br>`heap_int_min` integer（≥0）<br>`up_s` integer（≥0）<br>`low_power` boolean |
| `sub.detail` | `topic` hosts / servers / null | `id` ID |
| `perm.decide` | `rid` ID<br>`behavior` allow / deny | — |
| `media.cmd` | `src_id` ID<br>`cmd` toggle / next / prev | — |
| `ai.refresh` | — | — |
| `notify.dismiss` | — | `nid` ID<br>`all` =true |
| `pomo` | `phase` focus / short_break / long_break<br>`state` running / paused / done / reset<br>`round` integer（≥0） | `ends_at` integer（≥0） |
| `macro.get` | — | — |
| `ota.check` | — | — |
| `ota.progress` | `ver` string<br>`kind` app / assets<br>`pct` number（≥0，≤100，步长0.1） | — |
| `ota.result` | `ver` string<br>`kind` app / assets<br>`ok` boolean | `reason` string |
| `perm.req` | `rid` ID<br>`sid` ID<br>`src` claude / codex / gemini<br>`host` string<br>`tool` string<br>`summary` string（最长120）<br>`expires_at` integer（≥0） | `project` string |
| `perm.done` | `rid` ID<br>`result` allow / deny / timeout / cancelled<br>`by` device / web / timeout / terminal | — |
| `notify.push` | `nid` ID<br>`level` info / warn / critical<br>`src` string<br>`title` string（最长120）<br>`ts` integer（≥0）<br>`wake` boolean | `body` string（最长200） |
| `ota.offer` | `ver` string<br>`kind` app / assets<br>`url` anyOf(string（pattern=^/api/v1/ota/[^/]+\.bin$）, string（uri，pattern=^https?://[^/?#]+/api/v1/ota/[^/]+\.bin$）)<br>`sha256` string（pattern=^[a-f0-9]{64}$）<br>`size` integer（≥0；assets时≤3145728） | `notes_txt` string |
| `macro` | `slot` integer（≥0，≤11）<br>`label` string<br>`kind` keys / text<br>`value` string | — |
| `macro.set` | `rev` integer（≥0）<br>`macros` [device-data.json:macro]（最多12项） | — |
| `key3_long` | `action` talk / dnd / always_on / pomodoro / macro / none（新设备默认 talk） | `slot` integer（≥0，≤11） |
| `cfg` | — | `lang` zh-CN / en<br>`key3_long` device-data.json:key3_long<br>`key3_double_card` clock / hosts / servers / ai / buddy / pet / pomodoro / media / weather / vault / settings<br>`key3_clock_style` boolean<br>`t_dim` integer（≥1，≤86400）<br>`t_off` integer（≥1，≤86400）<br>`home_face` clock / assistant（新设备默认 clock）<br>`volume` integer（≥0，≤100）<br>`assistant` boolean（新设备默认 false） |
| `pair.wait` | — | — |
| `pair.ok` | `dev_token` string<br>`hub_id` ID<br>`hub_name` string | — |
| `ok` | — | — |
| `err` | `code` bad_request / rate_limited / perm_gone / not_found / forbidden / internal<br>`msg` string | — |

### 9.3 Agent 消息体

| 对象 | 必选字段（类型 / 约束） | 可选字段（类型 / 约束） |
|---|---|---|
| `hello` | `agent_id` ID<br>`name` string<br>`os` string<br>`ver` string<br>`proto` =1<br>`caps` [string]（不重复） | — |
| `intervals` | `host_fast` number（>0）<br>`host_slow` number（>0）<br>`disk` number（>0） | — |
| `welcome` | `hub_id` ID<br>`intervals` agent-data.json:intervals | — |
| `host.sample` | `name` string<br>`online` boolean | `os` string<br>`last_seen` string（date-time，pattern=Z$）<br>`cpu` rest/resources.json:cpu<br>`gpus` [rest/resources.json:gpu]<br>`mem` rest/resources.json:mem<br>`disks` [rest/resources.json:disk]<br>`net` rest/resources.json:net<br>`fans` [rest/resources.json:fan]<br>`temps` [rest/resources.json:temp]<br>`bat` rest/resources.json:battery<br>`plugins` object（动态值:ok / degraded / error / disabled） |
| `media_item` | `src_id` ID<br>`host` string<br>`app` string<br>`state` playing / paused / stopped<br>`can` device-data.json:media_can | `title` string<br>`artist` string<br>`album` string<br>`pos_s` number（≥0）<br>`dur_s` number（≥0）<br>`at` integer（≥0）<br>`cover_b64` string（base64，pattern=^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==&#124;[A-Za-z0-9+/]{3}=)?$） |
| `media.state` | `items` [agent-data.json:media_item] | — |

### 9.4 USB-CDC 消息体与响应

| 对象 | 必选字段（类型 / 约束） | 可选字段（类型 / 约束） |
|---|---|---|
| `info` | — | — |
| `info_response` | `dev_id` ID<br>`fw` string<br>`nukapack` string<br>`proto` =1<br>`paired` boolean<br>`mac` string（pattern=^(?:[a-fA-F0-9]{2}:){5}[a-fA-F0-9]{2}$）<br>`unlocked` boolean<br>`usb_admin` boolean | `hub_id` ID |
| `wifi` | `ssid` string（wifi-ssid，1–32 UTF-8字节，禁止U+0000）<br>`pass` string（wifi-passphrase，0–63 UTF-8字节，禁止U+0000） | — |
| `prov.set` | `wifi` [cdc.json:wifi]（至少1项，最多3项）<br>`hubs` [string（uri，pattern=^wss?://，最长511字符）]（至少1项）<br>`hub_id` ID<br>`hub_name` string<br>`dev_token` string<br>`code` string（pattern=^[0-9]{6}$） | `cf_access` cf-access.json |
| `vault.export` | `key_b64` string（规范 base64，解码后32字节）<br>`salt_b64` string（规范 base64，解码后16字节） | — |
| `backup_key` | string（规范 base64，解码后32字节） | — |
| `backup_salt` | string（规范 base64，解码后16字节） | — |
| `vault.chunk` | `seq` integer（≥0）<br>`b64` string（base64，pattern=^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==&#124;[A-Za-z0-9+/]{3}=)?$）<br>`last` boolean | — |
| `vault.import.begin` | `key_b64` string（规范 base64，解码后32字节）<br>`size` integer（≥55） | — |
| `vault.import.chunk` | `seq` integer（≥0）<br>`b64` string（base64，pattern=^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==&#124;[A-Za-z0-9+/]{3}=)?$） | — |
| `vault.import.end` | — | — |
| `vault.import.otpauth` | `uris` [string（uri，pattern=^otpauth://totp/）]（至少1项） | — |
| `import_response` | `added` integer（≥0）<br>`skipped` integer（≥0） | — |
| `task_stack` | `name` string<br>`free_b` integer（≥0） | — |
| `diag_response` | `fw` string<br>`up_s` integer（≥0）<br>`heap_int` integer（≥0）<br>`heap_int_min` integer（≥0）<br>`psram_free` integer（≥0）<br>`tasks` [cdc.json:task_stack]<br>`reset_reason` string | `rssi` integer |
| `diag.info` | — | — |
| `diag.dump_log` | — | — |
| `sys.reboot` | — | — |
| `sys.download` | — | — |
| `diag.log` | `on` boolean | — |
| `log` | `lvl` E / W / I / D / V<br>`tag` string<br>`msg` string<br>`ts` integer（≥0） | — |
| `log.chunk` | `seq` integer（≥0）<br>`b64` string（base64，pattern=^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==&#124;[A-Za-z0-9+/]{3}=)?$）<br>`last` boolean | — |
| `err` | `code` bad_request / rate_limited / perm_gone / not_found / forbidden / internal / rejected / timeout<br>`msg` string | — |
| `ok` | — | `dev_id` ID<br>`fw` string<br>`nukapack` string<br>`proto` =1<br>`paired` boolean<br>`hub_id` ID<br>`mac` string（pattern=^(?:[a-fA-F0-9]{2}:){5}[a-fA-F0-9]{2}$）<br>`unlocked` boolean<br>`usb_admin` boolean<br>`up_s` integer（≥0）<br>`heap_int` integer（≥0）<br>`heap_int_min` integer（≥0）<br>`psram_free` integer（≥0）<br>`tasks` [cdc.json:task_stack]<br>`rssi` integer<br>`reset_reason` string<br>`added` integer（≥0）<br>`skipped` integer（≥0） |

### 9.5 REST 资源

| 对象 | 必选字段（类型 / 约束） | 可选字段（类型 / 约束） |
|---|---|---|
| `cpu` | — | `pct` number（≥0，≤100，步长0.1）<br>`per_core` [number（≥0，≤100，步长0.1）]<br>`temp_c` number<br>`freq_mhz` number（≥0）<br>`power_w` number（≥0） |
| `gpu` | `name` string | `pct` number（≥0，≤100，步长0.1）<br>`temp_c` number<br>`mem_used_b` integer（≥0）<br>`mem_total_b` integer（≥0）<br>`power_w` number（≥0）<br>`fan_pct` number（≥0，≤100，步长0.1）<br>`clock_mhz` number（≥0） |
| `mem` | — | `used_b` integer（≥0）<br>`total_b` integer（≥0）<br>`swap_used_b` integer（≥0）<br>`swap_total_b` integer（≥0） |
| `disk` | `mount` string | `used_b` integer（≥0）<br>`total_b` integer（≥0）<br>`read_bps` number（≥0）<br>`write_bps` number（≥0） |
| `net` | — | `up_bps` number（≥0）<br>`down_bps` number（≥0） |
| `fan` | `label` string<br>`rpm` integer（≥0） | — |
| `temp` | `label` string<br>`c` number | — |
| `battery` | `pct` number（≥0，≤100，步长0.1）<br>`charging` boolean | — |
| `host` | `id` ID<br>`name` string<br>`online` boolean<br>`role` hub / agent | `os` string<br>`last_seen` string（date-time，pattern=Z$）<br>`cpu` resources.json:cpu<br>`gpus` [resources.json:gpu]<br>`mem` resources.json:mem<br>`disks` [resources.json:disk]<br>`net` resources.json:net<br>`fans` [resources.json:fan]<br>`temps` [resources.json:temp]<br>`bat` resources.json:battery<br>`plugins` object（动态值:ok / degraded / error / disabled） |
| `server_alert` | `rule` string<br>`level` info / warn / critical<br>`since` string（date-time，pattern=Z$） | — |
| `server` | `id` ID<br>`name` string<br>`src` string<br>`src_name` string<br>`online` boolean<br>`alerts` [resources.json:server_alert] | `last_seen` string（date-time，pattern=Z$）<br>`cpu_pct` number（≥0，≤100，步长0.1）<br>`mem_pct` number（≥0，≤100，步长0.1）<br>`mem_used_b` integer（≥0）<br>`mem_total_b` integer（≥0）<br>`disk_pct` number（≥0，≤100，步长0.1）<br>`disk_used_b` integer（≥0）<br>`disk_total_b` integer（≥0）<br>`load` [number（≥0）]（至少3项，最多3项）<br>`net_up_bps` number（≥0）<br>`net_down_bps` number（≥0）<br>`uptime_s` integer（≥0）<br>`latency_ms` number（≥0）<br>`uptime_pct` number（≥0，≤100，步长0.1） |
| `quota_window` | `kind` 5h / daily / weekly / monthly / other<br>`used_pct` number（≥0，≤100，步长0.1）<br>`source` passive / snapshot / active<br>`observed_at` string（date-time，pattern=Z$）<br>`stale` boolean | `label` string<br>`reset_at` string（date-time，pattern=Z$） |
| `credits` | `balance` number<br>`currency` string | — |
| `usage_today` | `requests` integer（≥0）<br>`tokens` integer（≥0）<br>`failures` integer（≥0） | — |
| `ai_account` | `id` ID<br>`provider_kind` codex / claude / antigravity / kimi / other<br>`label` string<br>`status` ok / cooldown / exhausted / reauth / disabled / unknown<br>`windows` [resources.json:quota_window] | `plan` string<br>`recover_at` string（date-time，pattern=Z$）<br>`credits` resources.json:credits<br>`today` resources.json:usage_today |
| `provider_health` | `name` string<br>`type` cpa / cpamp<br>`health` ok / degraded / auth_failed / backoff / disabled | `last_ok` string（date-time，pattern=Z$）<br>`next_try` string（date-time，pattern=Z$）<br>`detail` string |
| `agent_session` | `sid` ID<br>`source` claude / codex / gemini<br>`host` string<br>`state` idle / thinking / tool / waiting_permission / done / error<br>`since` string（date-time，pattern=Z$） | `project` string<br>`tool` string<br>`last_message` string（最长140） |
| `perm_request` | `rid` ID<br>`sid` ID<br>`src` claude / codex / gemini<br>`host` string<br>`tool` string<br>`summary` string（最长120）<br>`created` string（date-time，pattern=Z$）<br>`expires` string（date-time，pattern=Z$） | `project` string |
| `action` | `kind` ="dismiss" | — |
| `notification` | `nid` ID<br>`source` string<br>`level` info / warn / critical<br>`title` string（最长120）<br>`ts` string（date-time，pattern=Z$）<br>`actions` [resources.json:action]<br>`ttl_s` integer（≥0） | `body` string（最长2000）<br>`dedupe_key` string |
| `device` | `dev_id` ID<br>`name` string<br>`fw` string<br>`online` boolean | `last_seen` string（date-time，pattern=Z$）<br>`ip` string（ipv4 / ipv6）<br>`state` ../device-data.json:state |
| `plugin` | `kind` collector / server / ai / media / weather<br>`name` string<br>`health` ok / degraded / error / disabled<br>`doc` string | `detail` string |
| `token` | `id` ID<br>`name` string<br>`scope` admin / device / agent / hook / notify<br>`created` string（date-time，pattern=Z$） | `last_used` string（date-time，pattern=Z$）<br>`token` 禁止 |
| `audit` | `id` ID<br>`kind` string<br>`ts` string（date-time，pattern=Z$） | `rid` ID<br>`result` string<br>`by` string |
| `pending_pair` | `dev_id` ID<br>`fw` string<br>`expires` string（date-time，pattern=Z$） | — |
| `ota_manifest` | `ver` string<br>`kind` app / assets<br>`file` string<br>`sha256` string（pattern=^[a-f0-9]{64}$）<br>`size` integer（≥0；assets时≤3145728） | `notes_txt` string |
| `secret` | `name` string<br>`source` env / keyring / file | `value` 禁止 |

### 9.6 REST 请求与响应

| 对象 | 必选字段（类型 / 约束） | 可选字段（类型 / 约束） |
|---|---|---|
| `ack` | — | — |
| `agents` | `sessions` [resources.json:agent_session]<br>`pending` [resources.json:perm_request] | — |
| `ai` | `accounts` [resources.json:ai_account]<br>`providers` [resources.json:provider_health] | `summary` resources.json:usage_today |
| `audit-log` | `items` [resources.json:audit] | — |
| `claude-hook` | `session_id` string<br>`hook_event_name` SessionStart / UserPromptSubmit / PreToolUse / PostToolUse / PermissionRequest / Notification / Stop / StopFailure / SessionEnd | `cwd` string<br>`tool_name` string<br>`tool_input` object<br>`message` string<br>`last_assistant_message` string |
| `cli-hook` | `event` string<br>`host` string<br>`payload` object | — |
| `config-put` | `toml` string<br>`mtime` string（date-time，pattern=Z$） | — |
| `config-raw` | `toml` string<br>`path` string<br>`mtime` string（date-time，pattern=Z$） | — |
| `device-patch` | `name` string | — |
| `devices` | `items` [resources.json:device] | — |
| `error-detail` | `line` integer（≥1）<br>`msg` string | — |
| `error` | `error` object | — |
| `error.error` | `code` string<br>`message` string | `details` [error-detail.json] |
| `health` | `ok` =true<br>`ver` string<br>`proto` =1 | — |
| `history` | `ts` [string（date-time，pattern=Z$）]<br>`series` object（动态值:[number]） | — |
| `hook-response` | — | `hookSpecificOutput` object |
| `hook-response.hookSpecificOutput` | `hookEventName` ="PermissionRequest"<br>`decision` object | — |
| `hook-response.hookSpecificOutput.decision` | `behavior` allow / deny | `message` string |
| `hosts` | `items` [resources.json:host] | — |
| `login-request` | `token` string | — |
| `login-response` | `csrf` string | — |
| `me` | `zone` local / lan / public<br>`csrf` string<br>`features` ../device-data.json:features<br>`lang` zh-CN / en | — |
| `media-cmd` | `cmd` toggle / next / prev | — |
| `media` | `src_id` ID<br>`host` string<br>`app` string<br>`state` playing / paused / stopped<br>`can` ../device-data.json:media_can | `title` string<br>`artist` string<br>`album` string<br>`pos_s` number（≥0）<br>`dur_s` number（≥0）<br>`at` string（date-time，pattern=Z$）<br>`cover` string（pattern=^/api/v1/media/cover/[a-f0-9]+\.jpg$） |
| `notifications` | `items` [resources.json:notification] | — |
| `notify-request` | `title` string（最长120） | `body` string（最长2000）<br>`level` info / warn / critical<br>`source` string<br>`dedupe_key` string<br>`ttl_s` integer（≥0） |
| `notify-response` | `nid` ID | — |
| `ota-offer` | `ver` string<br>`kind` app / assets<br>`dev_ids` [ID]（至少1项，不重复） | — |
| `ota` | `items` [resources.json:ota_manifest] | — |
| `pair-confirm` | `dev_id` ID<br>`code` string（pattern=^[0-9]{6}$）<br>`name` string | — |
| `pair-pending` | `items` [resources.json:pending_pair] | — |
| `perm-decide` | `behavior` allow / deny | — |
| `plugins` | `items` [resources.json:plugin] | — |
| `secret-put` | `value` string | — |
| `secrets` | `items` [resources.json:secret] | — |
| `server-push` | `title` string（最长120） | `body` string（最长2000）<br>`level` info / warn / critical<br>`server` string<br>`source` string<br>`dedupe_key` string<br>`ttl_s` integer（≥0） |
| `servers` | `items` [resources.json:server] | — |
| `state` | `hosts` [resources.json:host]<br>`servers` [resources.json:server]<br>`ai` ai.json<br>`agents` agents.json<br>`notify` [resources.json:notification]<br>`devices` [resources.json:device]<br>`media` [media.json]<br>`weather` weather.json<br>`plugins` [resources.json:plugin] | — |
| `token-create` | `name` string<br>`scope` admin / device / agent / hook / notify | — |
| `token-created` | `id` ID<br>`name` string<br>`scope` admin / device / agent / hook / notify<br>`created` string（date-time，pattern=Z$）<br>`token` string | `last_used` string（date-time，pattern=Z$） |
| `tokens` | `items` [resources.json:token] | — |
| `weather` | — | `loc_txt` string<br>`updated` string（date-time，pattern=Z$）<br>`now` ../device-data.json:weather_now<br>`days` [../device-data.json:weather_day]（最多3项） |

### 9.7 配网与备份

| 对象 | 必选字段（类型 / 约束） | 可选字段（类型 / 约束） |
|---|---|---|
| `cf_access` | `origin` string（uri，pattern=^https://[a-zA-Z0-9.-]+(?::[0-9]+)?$）<br>`client_id` string<br>`client_secret` string | — |
| `provisioning` | `ssid` string（wifi-ssid，1–32 UTF-8字节，禁止U+0000）<br>`pass` string（wifi-passphrase，0–63 UTF-8字节，禁止U+0000）<br>`hub` string（uri，pattern=^wss?://，最长511字符） | `cf_origin` string（uri，pattern=^https://[a-zA-Z0-9.-]+(?::[0-9]+)?$）<br>`cf_client_id` string<br>`cf_client_secret` string |
| `vault_backup` | `ver` =1<br>`entries` [oneOf(vault-backup.json:totp, vault-backup.json:password)] | — |
| `totp` | `type` ="totp"<br>`name` string<br>`secret_b32` string（pattern=^[A-Z2-7]+={0,6}$）<br>`digits` 6 / 8<br>`period` integer（≥1）<br>`algo` SHA1 / SHA256 / SHA512 | — |
| `password` | `type` ="pass"<br>`name` string<br>`user` string<br>`pass` string<br>`url` string | `note` string |

### 9.8 WMO 图标映射

`protocol/wmo_icon_map.json` 覆盖 Open-Meteo 使用的 28 个 WMO 代码。映射是本项目对 12 种图标的归类，不是新增的 WMO 标准。代码 0 / 1 在白天用 clear_day，当前天气的上游 is_day=false 时用 clear_night；日预报按白天展示。wind 没有对应的这组 WMO 代码，本次不添加风速阈值或覆盖规则。未知代码属于上游响应错误，在插件调度边界处理，不映射成伪造的晴天。代码说明见 [Open-Meteo 官方文档](https://open-meteo.com/en/docs)。

| WMO 代码 | 图标 |
|---|---|
| 0 / 1 | `clear_day` |
| 2 | `partly_cloudy` |
| 3 | `cloudy` |
| 45 / 48 | `fog` |
| 51 / 53 / 55 | `drizzle` |
| 56 / 57 / 66 / 67 | `sleet` |
| 61 / 63 / 80 / 81 | `rain` |
| 65 / 82 | `heavy_rain` |
| 71 / 73 / 75 / 77 / 85 / 86 | `snow` |
| 95 / 96 / 99 | `thunder` |

### 9.9 助手 REST 结构（C2-03）

字段语义与边界见 §5.8，机器定义为 `rest/assistant-data.json` 及 `rest/assistant-*.json`。所有业务 ID 沿用 §1，时间为 UTC ISO 8601，不返回密钥、磁盘路径或原始音频。

| 对象 | 必选字段 | 可选字段 |
|---|---|---|
| assistant-status | enabled、history_enabled、memory_enabled（均 bool） | — |
| assistant-chat-request | dev_id ID；text string（1–16384 字符） | session_id ID；file_ids [ID]（最多8项，不重复） |
| assistant-message | id / dev_id / session_id ID；role user / assistant / tool；text string；created_at UTC | tool_call_id ID；tool_name string；tool_state requested / waiting_confirmation / running / completed / denied / cancelled / error；file_ids [ID]（最多8项，不重复） |
| assistant-history | items [assistant-message]（最多100项） | next_before ID |
| assistant-memory-create | dev_id ID；text string（1–2000 字符） | source_message_id ID |
| assistant-memory | id / dev_id ID；text string（1–2000 字符）；created_at / updated_at UTC | source_message_id ID |
| assistant-memories | items [assistant-memory]（最多100项） | next_before ID |
| assistant-file | id / dev_id ID；name string（UTF-8 ≤255字节）；size integer（≥0）；sha256 64位小写十六进制；created_at UTC | — |
| assistant-files | items [assistant-file]（最多100项） | next_before ID |
| assistant-pending | request_id ID；status = pending_confirmation | — |
| assistant-query（查询参数的离线表示） | dev_id ID | session_id ID；q string（≤2000字符）；limit integer（1–100，默认50）；before ID；各路径只采用 §5.8 表列出的参数 |

新建 chat / memory 的请求不接受未定义字段；响应允许忽略额外字段。`source_message_id` 与附件 ID 的设备归属、文件名 UTF-8 字节上限和动态文件大小上限由 HTTP 边界校验；JSON Schema 的字符串长度上限不能代替 UTF-8 字节检查。
