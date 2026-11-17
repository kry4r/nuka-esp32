# C-01：草案矛盾与本地冻结决定

维护者在本次会话中授权自主推进至 UI 优化任务之前，保留“禁止防御性编程”的约束。本文件记录首次本地冻结的具体决定；对应字段已同步至 PROTOCOL、CONFIG 和 schema。proto=1 尚未发布，初稿修订未提升版本，未创建兼容层。下述资源冲突已通过流程修订解决；远程 PR 评审与合入尚未完成，因此不宣称 M0 已合入。

| 原稿中的存疑点 | 本次决定与位置 |
|---|---|
| REST Server 未定义 mem_pct / disk_pct，但 Beszel、generic_json 和告警依赖百分比 | 补两个可选百分比字段（0..100，步长0.1）；没有字节数时只发百分比，不伪造容量。维护者已授权，未发布 proto=1 首稿补齐 |
| 除信封外，多数消息只给展示样例，无法确定最少字段 | PROTOCOL §9 逐对象列出必选、可选、类型与约束；每个 VM 含 full / min 两例。嵌套对象存在时满足自己的必选字段 |
| “缺失不发 null / 0”与 `topic:null`、`recover_at:0`、`until:0` 冲突 | `topic:null` 是明确的取消操作；没有恢复 / 结束时间时省略字段；真实的测量零值与计数零值仍合法 |
| 要求所有显示文本以 `_txt` 结尾，但 title / body / msg 等已使用其他名字 | 保留已经定义的字段名，格式化指标用 `_txt`，设备不解析展示文本 |
| SPEC §3.8 要求任何修改升版本，PROTOCOL 允许兼容增加不升版本 | 统一以兼容性决定：不兼容才升 proto；同步 SPEC 对应条文 |
| 第一个版本的 N−1 兼容范围未定义 | 没有 proto=0；首版只支持 1，发布 2 后再支持 2 / 1 |
| AGENTS 禁止 public 访问 `/hook/*`，SPEC 却允许 `/hook/server` | 所有 hooks 在 public 返回 404；lan 受 allow_lan_hooks 控制；server hook 用 notify 作用域。public 通知走 `/api/v1/notify` |
| `cfg` 没有定义按键配置的名字、类型或 Hub 配置来源 | 定义 key3_long / key3_double_card / key3_clock_style / t_dim / t_off，补 CONFIG devices 对应字段。局部更新，设备本机修改优先 |
| Hub 重启、设备重连后 rev 是否可从头计数不明确 | rev 作用域为一次 WS 连接内的消息类型；welcome 后重置已接收版本。详情切换对象仍延续该详情消息类型的递增值 |
| 首次快照与熄屏时“不推送 VM”冲突 | welcome 后的首次完整快照为显式例外；后续执行熄屏节流 |
| CDC diag.info、导入返回、错误码和请求 ID 不完整 | 定义 info_response / diag_response / import_response；栈水位单位为字节；CDC 增加 rejected / timeout 错误码，主动 log 不带请求 ID |
| Vault 步骤写“Hub 发送”，与 vault 不经过 Hub 的红线冲突 | 发起方明确为 USB CLI 进程；Hub 服务与 Web 不接触数据。明确小端 u32、16B GCM tag、逐块 base64 解码后拼接 |
| Vault scrypt 工作表约 32 MiB，超过设备 8 MB PSRAM 与内存预算 | 保留 NVB1 与 scrypt N=32768,r=8,p=1；本机 USB CLI 仅派生备份密钥、传输密文，设备 AES-GCM 加解密。CDC 改为 key_b64 / salt_b64，不再定义 passphrase。设备明文与备份密钥仅在内部 RAM，完成 / 取消 / 超时零化，详见 PROTOCOL §4.4 |
| 单个 6 MiB assets 分区无法暂存最大包并保留旧资源 | 分区 offset / size 保持不变，内部两个 3 MiB bank；单包≤3145728字节，只写 inactive bank，大小 / SHA-256 / ABI 全部通过后原子提交 sys.asset_slot。失败或提交前掉电保留旧 bank，详见 PROTOCOL §3.6 |
| REST 的 Credits / UsageToday / Action 只有类型名；部分列表有时裸数组、有时 items | 补齐资源定义，列表统一 items 包装；token / secret 列表显式禁止明文 token / value |
| REST 媒体和天气时间、历史缺测点、config.mtime 未定义 | REST 绝对时间统一 UTC ISO 8601；历史只返回已返回序列都有值的点，不填零；mtime 保留服务器精度并原样回传。Agent 的媒体 at 仍为 Unix 秒 |
| /config 的“密钥显示 ***”与只保存名称引用不一致 | 返回名称引用，不加载或返回密钥值。/secrets 仍只列名称和来源；public 禁止读取 secrets 清单 |
| Cloudflare Access 要求 Service Token，但设备 / Agent 没有提供凭据的契约 | 增加可选配网 cf_access 和 Agent 的密钥名称引用；凭据仅发给指定 origin 的 HTTPS / WSS，不经 WS 回传，不记录日志 |
| OTA 示例只有相对路径，但 CONFIG.public_url 要写入 OTA 地址 | 允许相对路径或 Hub 的绝对地址，限制为已配对 Hub 的 origin，避免把设备令牌发给其他站点 |
| Agent host.sample 和媒体内联封面的类型与 8KB 上限不明确 | host.sample 使用 REST Host 时间，省略 id / role；媒体使用 Unix at；Agent 封面帧不受设备 8KB 上限约束，解码图像仍 ≤20KB |
| WMO 代码不能与 12 图标一一对应，缺少昼夜规则 | 映射 28 个上游代码；0 / 1 在夜间用 clear_night；wind 无对应代码，不添加猜测的风速阈值 |
| SPEC 天气预报为未来 3 天，PROTOCOL 没有限制 days 长度 | 设备 VM 和 REST 均限制为最多 3 项，补入 PROTOCOL §3.4.8 / §9 |
| CONFIG 中 node_exporter 的 auth_secret 因 TOML 表头落入 options；windows.5h 不符合原表达式语法 | auth_secret 移回 source；语法允许派生字段的 5h 路径段 |
| SPEC 的主题生成器路径与 PLAN 不一致；示例邮箱使用非 example 域 | 统一为 firmware/tools/gen_theme.py；邮箱示例改为 example.com |

原先的 Vault KDF 与 assets 恢复冲突均已作出上述决定，没有降低 KDF 成本、调整分区 offset 或增加兼容路径。没有待裁定的协议字段。实测与实现验收由后续任务完成，不以 schema 通过替代硬件和加密验证。

Vault 导入的“重复”补充为 `(type, name)` UTF-8 大小写精确比较：跳过已有条目和同批次前面的条目。此项按维护者“都可以，你自己决定”的首版裁定授权补齐；不新增帧字段。

`macro.set` 补充为完整替换快照：未列出的 slot 清空，空数组清空全部，使从 Hub 配置移除的宏不会在设备上保留。此项同属维护者授权的首版语义补齐，不新增字段。

列表分页补充自然键：plugins.name、ota.file、secrets.name、pair/pending.dev_id，其余列表使用 id。显式 `nuka ota publish` / REST offer 始终通知在线设备，`ota.auto_offer` 仅控制其他新增 manifest 的后台自动通知。均为首版遗漏的语义补齐，不新增字段。

设置“检查更新”缺少设备发起入口，补充 `ota.check`：设备发送带 `id` 的空消息体，Hub 用同一 `id` 回复现有 `ok`，根据 manifest 和本次 `hello` 中的 `fw` / `nukapack` 发送零个或多个现有 `ota.offer`。`ver` 保持字符串，按对应 kind 当前版本做精确不等比较，允许回退；同 kind 可以多个，按 manifest 路径稳定排序，不引入语义版本规则。显式检查独立于 `ota.auto_offer`，不增加 ACK 字段；仍须设备本地确认后才安装。此项由维护者按首版自主裁定授权确认，仍为未发布的 proto=1，不增加旧版本兼容路径。

FW 在生成 C 边界校验时发现 `cfg.t_dim` / `t_off` 原本没有有限上限，而设备存储有整数容量。按维护者对首版 `cfg` 补齐的裁定授权，C 将二者规定为 1–86400 秒；对应 CONFIG、schema、字段表同步，FW 用 uint32 存储，BE 在配置边界接受相同范围。未新增字段或兼容路径，仍为未发布的 proto=1 草案；样例值 20 / 10 保持有效。

配网容量遗漏同属首版补齐：SSID 为 1–32 UTF-8 字节、密码为 0–63 UTF-8 字节，禁止 U+0000；WS URI 为 ASCII、最长 511 字符，与 Wi-Fi 配置和设备 URL 容量一致。schema 用 `wifi-ssid` / `wifi-passphrase` 格式声明字节限制，T6 识别这两个格式；USB 与 SoftAP 边界遵守相同范围。重新配网未提供可选 Access 凭据时清除旧值，避免两条配网路径语义不同。不新增字段、不增加兼容层。

RTC 范围与时区容量补齐：`time` 及 `welcome` 内复用的时间结构将 `epoch` 限定为 946684800–4102444799（2000–2099 年），避免合法网络帧超出 PCF85063 年份编码；信封 `ts` 仍允许同步前的 0。`tz_posix` 限定为 0–127 个可打印 ASCII 字符，空串此前合法，保留其 UTC 语义，NVS 默认仍为 `UTC0`。NVS 与 SNTP 输出在各自输入边界校验，不在已通过协议校验的内部调用中重复检查。此项由维护者按硬件事实裁定，未发布首版仍为 proto=1。

设备 C 字符串边界补齐：设备 WS 双向、CDC、SoftAP 解码后表单的全部字符串与对象键禁止 U+0000，包含未知字段和任意嵌套数组 / 对象；纯 Hub / Web REST、Agent WS 与 CONFIG 不加此限制。schema 仅在这三个传输根引用 `common.json` 的递归 `no_nul`，不逐字段重复约束。固件在原始 JSON 边界识别实际转义后的 U+0000，不能将包含字面反斜杠的 `\\u0000` 误判；SoftAP 在表单解码时拒绝空字节。该项是维护者按 C 字符串表示能力裁定的首版输入约束，不增加业务功能。

媒体默认选择补齐现有需求：Hub 按源实际活动变更的内部时间倒序输出媒体 `items`，普通轮询不刷新全部源的活动时间；设备默认首项，用户手动选择后按 `src_id` 保持至该源移除。不使用可选进度基点 `at` 猜测活跃顺序，不新增网络字段或配置。此项由维护者裁定，落实 SPEC 已有的“默认最近活跃”行为。

ID 全字符串匹配澄清：Python JSON Schema 的 `$` 可在末尾 LF 之前匹配，原有锚定表达式因此可能误收 `example\n`。所有协议 ID schema 保留原长度表达式，并通过 `not: {pattern: "[^a-z0-9_-]"}` 排除整个字符串中的集合外字符；允许集合仍严格等于 1–32 个 `[a-z0-9_-]`，不新增 ID 形式。T6 在设备 WS、Agent WS、CDC 和 REST 的现有边界校验中覆盖短 ID / 32 字符 ID 后附 LF，以及长度上下界；FW 固定匹配表同步负字符类。此项由维护者授权作为未发布首版的契约实现一致性修订。

## C2-01：第二阶段契约批次 A（2026-10-24）

维护者确认 PLAN-v2 后启动本批次；全部为可选字段或枚举值增补，仍为 proto=1。当前仓库尚无首个提交或远程 PR，本地校验不表示已经远程合入。业务实现由 FW / BE 分别更新。

| 原契约与第二阶段需求 | 本批决定 |
|---|---|
| 主页两面、音量、设备助手开关没有下发字段 | `cfg` 增加可选 `home_face`（clock / assistant）、`volume`（整数 0–100）、`assistant`（bool）；局部更新仍是省略不改、本机修改优先。CONFIG `[devices]` 同步字段来源 |
| Key3 长按仍默认勿扰 | `key3_long.action` 增加 `talk`，第二阶段新设备默认 talk；其他动作保留，选择其他动作时关闭按住说话。短按双面切换优先于原表盘样式动作 |
| 桌宠卡删除，但 `pet` 仍是主题和已定义 App 值 | 主页两面继续使用 `clock` App ID，`pet` 主题保留为情绪源。schema 保留既有 card 取值，避免在可选增补中删枚举；新固件不再提供独立 pet 卡 |
| 表情状态多于六种 Hub mood | Hub mood 枚举不变；充电、摇晃、离线、告警、配额接近耗尽和待审批由设备本地及已有 VM 叠加，语音状态等 C2-02，不混入 mood |
| AI 页需要区别供应方熔断与账号状态 | 经维护者本轮授权裁定，`vm.ai.accounts[].breaker_open` 增加为可选 bool；Hub 确知提供方熔断状态时填写。省略表示未知，不把 disabled / unknown / reauth 映射为熔断，status 不变 |
| 计划中的按模型 Top 3 没有 VM 数据 | PLAN-v2 已限定有数据才显示；本批不添加模型明细字段，不伪造 Top 3 |

`cfg.full` / `cfg.min` / `cfg.talk` 与 `vm.ai.full` 覆盖本批字段；T6 同时检查 cfg 的满字段样例和输入边界。第二批语音、USB 即插即配与配置表单接口仍等待 FW2-05 的实测结论；Wi-Fi 密码仅经串口的本轮裁定会在第二批同步。永久存储、服务器认证与 Pi SDK 决策由后续计划增补定义，不在本批自行约定。

## C2-03：Pi 助手、服务器与永久存储（2026-10-24）

维护者要求使用 Pi SDK 自写助手、不采用 LangChain，并授权服务器持久化、上传和网页配置。本批与音频编码独立，不等待 C2-02；仅增加可选配置和新的 REST，不改设备 proto。

| 契约项 | 决定 |
|---|---|
| 运行边界 | Python Hub 持有鉴权、工具权限和 SQLite，Pi 核心仅通过本机 stdio 承接模型与工具循环；Node 从 PATH 查找，不配置任意脚本。内部有界 JSONL 由 Hub 桥接实现记录，不成为设备网络协议 |
| 非音频配置 | `[assistant]` 默认关闭，定义 llm、history、memory、uploads；模型默认地址 / 名称 / 密钥名为空。无需密钥的本地接口允许 key_secret 为空；启用时要求地址和模型完整 |
| 输入预算 | `llm.context_window` 默认 32768、范围 1024–1048576，按实际模型填写；配置边界要求 max_output_tokens 小于该预算。最近轮数、system、工具和记忆一起受预算控制，不宣称所有模型都有相同容量 |
| 文字与工具 | POST chat 返回 SSE，GET status 返回开关；断开请求即取消模型、待执行工具与待确认操作。工具只来自 Hub 启用白名单，记忆写入须设备确认，确认缺失不能绕过 |
| 永久记录 | 20 轮仅限制模型输入；存档无 TTL、幂等、重启恢复。关闭记录不写新数据，不删除既有数据。删除会话级联其工具生命周期及来源记忆，附件独立显式管理 |
| 长期记忆 | 明确授权的事实单独保存，最多 2000 字符；按设备检索最多 8 条 / 8000 字符。无后台自动推断或删除后回填；模型写 / 删均须设备许可，显式 Web 管理删除沿用 admin / CSRF |
| 附件 | 原始 body + 百分号编码 X-Filename，默认每个文件 64 MiB；按实际累计字节限制。服务端 opaque ID，显示名不作路径，归属校验，强制附件下载，不自动执行或解包；Vault 不进入 Hub |
| 远程管理 | `security.remote_admin` 默认 false；仅 CF Access JWT 验证通过且 admin 会话有效时开放明确配置 / 助手路径，写请求仍需 CSRF。网络区域仍是 public，hooks、Web 审批、USB、配对不开放 |
| 备份 | SQLite 在线 backup API 加附件归档，停服验证完整性后恢复；不能复制正在写入的 WAL 数据库，不把备份加入仓库 |

本批新增 schema、每个 HTTP 接口的占位符样例、SSE 各事件样例和 JSONL 导出样例；T6 覆盖新字段上限、ID、配置前置条件和样例满字段。运行边界的所有权、敏感工具权限和真实服务器部署由 BE / INT 验证；契约校验通过不表示实现或上线验收完成。

以下是后续轨道和交付环境的验证事项：

- BE 的 HUB-07 实际 demo 输出须接入现有 T6；检查器已经支持 JSONL。
- FW 必须在网络 / CDC 输入边界处理请求关联、rev 和分块顺序，验证 JPEG 下载内容；Vault 的加密往返属于 T7。
- FW-05 须实际打包并确认字体、图标与桌宠资源总大小≤3 MiB；FW-25 须验证 inactive bank 升级、校验失败和掉电恢复。资源包 header / manifest 的 ABI 是固件内部格式，不新增网络字段。
- HUB-30 须确保 CLI 不解密 Vault，口令只在本机隐藏输入，不出现在参数、配置、日志中；FW-23 须验证取消 / 超时零化及管理授权。
- Python CLI 只承诺清零自己持有的可变缓冲；getpass / KDF / JSON 的不可变临时副本无法逐一保证擦除，不能把删除引用表述为完整零化。CLI 传输后退出；设备内部 RAM 的明文和密钥仍须实际零化。
- 当前仓库没有提交或 GitHub remote，因此无法创建三个任务卡 PR、实际运行 GitHub 三平台 workflow 或宣称 M0 已合入。此稿完成后仍需按卡拆 PR 评审。
