# nuka-esp32 规格说明书（SPEC）

| 项 | 值 |
|---|---|
| 版本 | v0.2（2026-10-17） |
| 状态 | 评审中，尚未开工 |
| 目标硬件 | Waveshare ESP32-S3-Touch-AMOLED-2.16（以下简称"板子"） |
| 配套文档 | [PLAN.md](PLAN.md)（实施计划与任务卡）· [PROTOCOL.md](PROTOCOL.md)（通信契约）· [CONFIG.md](CONFIG.md)（配置 schema） |
| 开源 | 发布到 GitHub，仓库名 `nuka-esp32`；许可证 **AGPL-3.0-or-later**（§3 原则 7、D17） |
| 系列 | nuka 系列的第一款设备（命名规则见 §1.4、D18） |

> **阅读顺序**（项目分为 C、FW、BE、INT、FE 五条轨道，分别交给不同的 agent，见 PLAN §1）：
> - 所有轨道：先读 §1.1，理解这是一次系统级重写，UI 只是其中一层。
> - 契约轨道 C：读 §3、§6.6、PROTOCOL.md 和 CONFIG.md。
> - 固件功能轨道 FW：读 §1–§6、§9、§10。
> - Hub 后端逻辑轨道 BE：读 §1–§3、§7（不含 §7.13 的视觉部分）、§8、§10，并以 PROTOCOL.md / CONFIG.md 为准。
> - 系统联调轨道 INT：读 §5、§6.2.1、§6.10、§6.12、§6.16、§7.7、§7.11、§9，以及 PLAN 附录 B、附录 C。
> - 前端优化轨道 FE（系统联调通过后才开工；**只针对 ESP32 屏幕上的 LVGL UI**：功能完整度、美观度、低功耗打磨）：读 §3、§4.3、§6.3、§6.6–§6.11、§8 中的 NFR-2 和 NFR-3。
> - 每条轨道各自编写自己负责功能的安装引导（§11）。
>
> **约定**：文中"必须 / 禁止"为硬性要求；"应"为强烈建议；"可"为可选。需求编号（FR-x / NFR-x）会在 PLAN 的任务卡中被引用。

---

## 目录
1. 背景、目标、非目标、nuka 系列命名
2. 需求清单
3. 开源与可配置原则
4. 硬件事实
5. 系统架构与部署形态
6. 固件详细设计
7. Hub 详细设计
8. 非功能需求
9. 安全设计
10. 测试策略
11. 安装引导要求
12. 决策记录
13. 开放问题

附录 A 术语

---

## 1. 背景、目标、非目标

### 1.1 背景
板子的原厂固件基于 esp-brookesia：体积大，内存占用高，交互也不符合小屏直觉。

nuka-esp32 由两部分组成：一套从零编写的开源固件，以及一个可以装在任意电脑或服务器上的伴侣服务（**nuka-hub**）。两者配合，把板子变成一个"开发者桌面终端"：
- 显示电脑与服务器的状态；
- 显示 AI 订阅的余额；
- 显示 AI 编程 Agent 的运行状态，并在设备上完成工具调用审批；
- 兼作 TOTP 验证器和密码键入器。

**这是一次系统级重写，不是给原厂固件换一套 UI 框架。** 原厂固件和 esp-brookesia 的代码一律不沿用，只参考官方 BSP 中已经验证过的硬件事实（例如 §6.3.3 的 CO5300 初始化表）。整个系统包括：

| 层面 | 内容 | 详见 |
|---|---|---|
| 设备：启动与系统核心 | 分区表、启动顺序、FreeRTOS 任务模型、事件总线、视图模型存储、NVS 存储、日志、看门狗、重启原因 | §6.1、§6.2、§6.4、§6.15、§6.17 |
| 设备：板级驱动 | 显示（CO5300）、触摸、PMU、IMU、RTC、三键 | §4、§6.3、§6.5 |
| 设备：系统服务 | 电源状态机与睡眠、电池管理、输入、网络与配网、Hub 客户端、USB 复合设备、Vault、OTA、国际化、资源包 | §6.9–§6.17 |
| 设备：UI 框架与 App | 导航、覆盖层、三键焦点导航、组件库、11 个 App | §6.6–§6.8 |
| 伴侣服务 nuka-hub | 采集、数据源、告警、AI 配额、Agent hooks、通知、设备管理、OTA 托管、鉴权与区域、存储、CLI、服务安装、简单 Web 管理页 | §7 |
| 契约 | 设备 ↔ Hub 的 WS 协议、USB-CDC 协议、REST、配置 schema、设计 token | PROTOCOL、CONFIG、§6.6.1 |
| 交付 | CI、网页刷机、打包发布、安装引导、零硬编码检查 | §7.14、§11 |

UI 只是其中一层。功能版 UI 由固件轨道完成，视觉、动效和功耗的打磨放在整个系统联调通过之后（PLAN §1.2）。

### 1.2 目标
- **固件轻量**：内部 RAM 占用低，镜像小，空闲时几乎不占 CPU。
- **UI 精致**：黑底 + 牛油果绿，手势符合 480×480 小方屏的直觉，动画流畅。
- **省电**：熄屏后进入 light sleep；关机由 PMU 真断电；电池管理完善。
- **Hub 跨平台**：Windows / macOS / Linux 都能安装，一条命令注册为开机服务，并提供一个简单的 Web 管理页（配置与管理）。
- **集成全部可选**：服务器探针、硬件传感器、AI 配额、Agent hook、公网隧道都是**可选插件**，由配置启用，并且各自配有安装引导。

### 1.3 非目标（v1 不做）
- 设备端本地大模型（仍排除）；按键语音助手已由 D21 纳入，详见 PLAN-v2
- GitHub / CI 状态卡（已明确排除）
- FIDO2 / Passkey 安全钥（已明确排除）
- 设备上动态加载第三方 App、应用商店
- 设备上的触屏全键盘文字输入（PIN 数字键盘除外）
- Secure Boot 与发布模式 Flash 加密（不可逆，v1 不开启）

---

### 1.4 nuka 系列命名
nuka-esp32 是 nuka 系列的第一款设备。系列内的命名规则：

| 对象 | 名称 |
|---|---|
| 设备固件 / 仓库 | `nuka-<平台>`，本项目为 `nuka-esp32` |
| 伴侣服务（Python 包） | `nuka-hub`，导入名 `nuka_hub` |
| 命令行 | `nuka`；AI CLI 的 hook 转发器为 `nuka-hook` |
| 协议标识 | mDNS `_nuka._tcp`，WS 子协议 `nuka.v1`，请求头 `X-Nuka-*`，环境变量 `NUKA_*` |
| 设备侧 | SoftAP `Nuka-XXXX`，USB 产品字符串 `nuka-esp32`，资源包 `nukapack`，密码库 NukaVault |
| 视觉 | 黑底 + 牛油果绿（主题 token 名 `color.avo*` 保持不变），主页采用原创矢量眼表情，D26 取代像素桌宠 |

本版本只定义命名规则。系列中其他设备如何接入同一个 nuka-hub，不在 v1 范围内。

## 2. 需求清单

"来源"一列中，**U** 表示用户原始需求，**C** 表示调研后提出、已经用户确认的需求。

### 2.1 功能需求（FR）
| 编号 | 需求 | 来源 | 优先级 | 详见 |
|---|---|---|---|---|
| FR-1 | 基于 LVGL 的 UI 框架：卡片轮播、快捷面板、通知中心、详情页、弹层 | U | P0 | §6.6–6.8 |
| FR-2 | 黑 / 牛油果绿视觉主题；主题 token 只有一个来源，固件与 Web 共用 | U | P0 | §6.6 |
| FR-3 | 三个物理键（PWR / BOOT / Key3）全部利用：开关机、亮灭屏、翻页、返回、确认、卡片主操作、审批、恢复菜单；不用触摸也能完成核心操作 | U | P0 | §6.9 |
| FR-4 | 电池 / 电源管理：充电策略、电量计、电源来源、低电保护、状态上报 | U | P0 | §6.10 |
| FR-5 | 熄屏、自动调暗、light sleep、唤醒源 | U | P0 | §6.10 |
| FR-6 | Hub：本机 / 多机硬件监控，WebSocket 推送到设备显示；简单的 Web 管理页 | U | P0 | §7 |
| FR-7 | Hub 支持 Windows / macOS / Linux，一键注册开机服务 | U | P0 | §7.14 |
| FR-8 | AI 订阅余额：经 CLIProxyAPI（CPA）或 CPA Manager Plus（CPAMP）获取配额与用量 | U | P0 | §7.6 |
| FR-9 | 服务器监控：可配置多个数据源（ServerStatus、Uptime Kuma、Beszel、Netdata、node_exporter、通用 JSON） | U+C | P1 | §7.5 |
| FR-10 | 服务器推送 hook（`POST /hook/server`）与阈值规则告警 | U | P1 | §7.5 |
| FR-11 | Agent Buddy：显示 Claude Code / Codex / Gemini CLI 的状态，在设备上审批工具权限 | C | P1 | §6.8.5、§7.7 |
| FR-12 | 主页助手表情：由 Agent 状态、配额、IMU 与语音会话驱动 | C | P1 | §6.8.6、PLAN-v2 §2.4 |
| FR-13 | IMU 交互：抬起亮屏、扣下熄屏 / 勿扰、摇一摇关闭通知 | C | P1 | §6.11 |
| FR-14 | 番茄钟：翻转开始 / 暂停，联动勿扰 | C | P1 | §6.8.7 |
| FR-15 | 正在播放：显示与控制（三平台媒体会话） | C | P2 | §6.8.8、§7.8 |
| FR-16 | 天气卡与离线表盘（RTC 兜底） | C | P2 | §6.8.9、§7.8 |
| FR-17 | USB HID 宏键盘（插着 USB 时可用） | C | P2 | §6.13 |
| FR-18 | NukaVault：TOTP 验证器、密码库 + USB 键入、硬件随机密码生成器 | U | P1 | §6.14、§9.3 |
| FR-19 | 设备配网与配对：USB-CDC 下发，SoftAP 网页配网备用 | C | P0 | §6.12 |
| FR-20 | OTA 升级：Hub 托管固件，设备双分区升级，失败自动回滚 | C | P1 | §6.16 |
| FR-21 | 公网访问：Hub 可选经 cloudflared + Cloudflare Access 暴露，敏感接口禁止公网访问 | U | P1 | §7.11、§9.2 |
| FR-22 | 国际化：固件与 Web 都支持 zh-CN 和 en | C（开源需要） | P1 | §6.6.4 |
| FR-23 | 网页刷机：基于 ESP Web Tools 的浏览器刷机页，托管在 GitHub Pages | C（开源需要） | P2 | §11 |

### 2.2 非功能需求（NFR，具体数值见 §8）
| 编号 | 需求 |
|---|---|
| NFR-1 | 内存预算（内部 SRAM、PSRAM、镜像大小） |
| NFR-2 | 渲染性能（局部动画帧率、页面切换帧率、触摸延迟） |
| NFR-3 | 功耗预算（亮屏、熄屏、关机） |
| NFR-4 | 可靠性（断线重连、无内存泄漏、看门狗、OTA 回滚） |
| NFR-5 | 安全（密钥不上设备、敏感接口按区域隔离、vault 不经网络） |
| NFR-6 | 可移植、可配置（零硬编码、支持三平台） |
| NFR-7 | 可维护：契约文档、JSON Schema、CI；代码简洁直接，只写少量关键测试（§3 第 9 条、§10） |

---

## 3. 开源与可配置原则（硬性）

1. **零硬编码**：源码、默认配置、示例和文档中**禁止**出现任何个人部署值，包括 IP、域名、约定端口以外的地址、账号、密钥、串口号。示例一律使用占位符，例如 `hub.example.lan`、`https://cpamp.example.com`、`<YOUR_TOKEN>`、`192.0.2.10`（RFC 5737 文档保留地址）。
2. **固件不内置任何地址**：Wi-Fi、Hub 地址、令牌只能经配网写入 NVS（§6.12）；Kconfig 中这几项默认留空。
3. **Hub 的一切都可配置**：所有集成都在 `config.toml` 中声明（schema 见 CONFIG.md），由 `nuka init` 交互式向导生成。密钥在配置里只按名称引用，实际值存放在系统钥匙串中（§7.3）。
4. **插件化 + 自动降级**：每个采集器、数据源、配额提供者、媒体适配器都是独立插件。依赖缺失或探测失败时，只禁用这个插件，并在 `nuka doctor` 和 Web 中给出原因与修复指引，不影响其他功能。
5. **每个集成都必须有安装引导**（清单与写作要求见 §11）。没有引导的集成不得进入发布版本。
6. **个人部署信息隔离**：仓库根目录下的 `local/` 存放个人部署笔记（已加入 `.gitignore`），其他任何位置都不得引用其中的值。
7. **许可证（AGPL-3.0-or-later）**：
   - 整个仓库（固件、Hub、Web、脚本、文档）统一采用 AGPL-3.0-or-later。根目录放 `LICENSE` 全文，每个源文件头部写 `SPDX-License-Identifier: AGPL-3.0-or-later`；
   - 第三方依赖必须与 AGPL-3.0 兼容：MIT、BSD、ISC、Apache-2.0、MPL-2.0、LGPL、GPL-3.0、AGPL-3.0 都可以；**不能**用 GPL-2.0-only、SSPL、带 NC / ND 条款的 CC 协议，也不能用专有代码；
   - 字体使用 OFL 协议的 Inter、JetBrains Mono、Noto Sans SC；
   - 桌宠、图标和其他美术资源必须是原创（随仓库以 AGPL 发布）或 CC0；
   - Web 前端依赖必须 vendored 进仓库（不走 CDN），并附上各自的许可证；
   - `THIRD_PARTY.md` 列出所有依赖及其许可证，新增依赖时同步更新；
   - **网络服务条款（AGPL §13）**：nuka-hub 是网络服务，Web 管理页的页脚要显示版本号和"源代码"链接，链接取自包元数据 `project.urls.Source`；固件的"关于"页显示许可证名称和源代码地址。
8. **契约优先**：设备、Hub、Web 之间的消息以 PROTOCOL.md 和 `protocol/schemas/*.json`（JSON Schema）为准。任何一方要修改契约，都必须先改文档和 schema；不兼容改动提升协议版本号，新增可选字段或消息类型等向后兼容改动不升版本号（PROTOCOL 总则）。
9. **工程风格（硬性）**：
   - **禁止过多单元测试**：
     - 只给"出错代价高、又难以手工验证"的纯逻辑写测试，完整清单见 §10。
     - 不追求覆盖率，不设覆盖率门槛。
     - 不测框架行为、getter/setter、简单的字段映射或 UI 布局。
     - 不搭层层嵌套的 mock。
     - 每个上游适配器只需要 1 个基于 fixture 的解析测试。
   - **禁止防御性编程**：
     - 只在**信任边界**做校验：网络帧、HTTP 请求、上游 API 响应、配置文件加载、用户输入。
     - 内部函数之间信任调用方：不重复判空，不层层 `try/except` 或返回码透传，不为"不可能发生"的情况写分支，不写"以防万一"的兜底默认值。
     - 错误要么在一个统一的地方处理，要么直接暴露出来：
       - Hub：插件调度器统一捕获异常，并把插件标记为 `error`；
       - 固件：初始化阶段遇到不可恢复的错误，直接用 `ESP_ERROR_CHECK` 让程序挂掉，交给看门狗或用户重启。
     - 不吞掉错误。
   - **不做过度设计**：不添加用不到的配置项、抽象层、兼容层或扩展点；三处以上重复时才考虑抽取公共代码。
   - **不属于防御性编程的部分**：本文明确规定的安全与可靠性机制属于需求，必须实现。例如 §7.6.5 的熔断、§7.11 的区域判定、§9 的安全措施、OTA 回滚。

---

## 4. 硬件事实

### 4.1 关键芯片
| 部件 | 型号 | 接口 | 备注 |
|---|---|---|---|
| SoC | ESP32-S3R8 | — | 双核 240MHz，8MB Octal PSRAM，16MB QIO Flash |
| 屏幕 | 2.16" AMOLED 480×480，驱动 IC **CO5300** | QSPI | 屏幕四角为圆角 |
| 触摸 | **CST9220** | I2C | 驱动使用 `waveshare/esp_lcd_touch_cst9217` |
| PMU | **AXP2101**（I2C 地址 0x34） | I2C | 负责供电、充电、电量计、PWR 键 |
| IMU | QMI8658（0x6B） | I2C | 带两根中断线 |
| RTC | PCF85063（0x51） | I2C | 带一根中断线 |
| 音频 | ES8311（播放）/ ES7210（双麦） | I2C + I2S | v1 只用于提示音；由 ALDO1 供电 |
| 存储卡 | TF 卡槽 | SDMMC 1-bit | v1 不使用 |

### 4.2 引脚表
| GPIO | 功能 | GPIO | 功能 |
|---|---|---|---|
| 4/5/6/7 | LCD QSPI D0–D3 | 38 | LCD QSPI CLK |
| 12 | LCD CS | 39 | LCD RESET |
| 14 | I2C SCL（共享总线） | 15 | I2C SDA（共享总线） |
| 11 | 触摸 INT（低电平有效） | 40 | 触摸 RESET |
| 16 | SYS_OUT，反映 PWR 键状态（**按下 = 高电平**） | 18 | Key3 用户键（10K 上拉，按下 = 低电平） |
| 0 | BOOT 键（运行时作为用户键，按下 = 低电平；见 H12） | 13 | RTC INT |
| 17 / 21 | IMU INT1 / INT2 | 46 | 功放使能 |
| 42/9/45/8/10 | I2S MCLK / BCLK / LRCK / DOUT / DIN | 1/2/3/41 | TF CMD / CLK / D0 / CS |
| 19/20 | USB D- / D+（原生 USB，禁止占用） | 43/44 | UART0 TX / RX |

### 4.3 影响设计的硬件事实
| # | 事实 | 对设计的影响 |
|---|---|---|
| H1 | QSPI 时钟上限是 **40MHz**：CO5300 的最小写周期为 20ns，而 S3 的下一档时钟是 80MHz，超出规格 | 带宽为 20MB/s，一帧全屏 RGB565（460,800B）约需 23ms，因此**全屏刷新上限约 40fps** |
| H2 | **TE（撕裂同步）引脚没有接到 ESP32** | 无法与面板刷新同步，全屏快速运动会撕裂，所以动画规范要限制全屏运动（§6.6.5） |
| H3 | CO5300 要求窗口起止坐标**偶数对齐**；本板列偏移为 0（驱动默认的 +6 是错的）；还需要厂商页 0x20 的初始化序列 | 必须注册 rounder 回调，并使用经过验证的初始化表（§6.3.3） |
| H4 | 亮度由命令 0x51 设置（0–255），0x53=0x20 时生效 | 调节亮度不需要 PWM，也没有渲染开销，可以直接用来做淡入淡出 |
| H5 | **AXP2101 的 IRQ 线没有接到 ESP32** | 只能通过 I2C 轮询 PMU 的中断标志 |
| H6 | PWR 键经 BSS138 反相后接到 GPIO16 | 实时按键检测和 light-sleep 唤醒都使用 GPIO16 |
| H7 | 置位 AXP2101 的 reg0x10 bit0 会关断所有电源轨，只保留 RTC 域；按 PWR 键重新上电 | "关机"是真断电，电流接近零 |
| H8 | DCDC1 给整个系统供电，ALDO1 只给音频芯片供电 | 音频空闲时关闭 ALDO1 |
| H9 | 板上没有电池 NTC（热敏电阻） | 必须关闭 AXP 的 TS 引脚检测，否则不会充电 |
| H10 | 用 PSRAM 作为 SPI DMA 的数据源做大块全屏传输时会 underflow，进而导致驱动卡死 | **draw buffer 只能放在内部 DMA RAM** |
| H11 | 所有 I2C 设备共用一条总线 | 由唯一的 I2C 总线管理器加互斥锁访问 |
| H12 | GPIO0 是 strapping 引脚，板上有上拉，BOOT 键把它拉低。复位或上电时按住 BOOT 会进入 ROM 下载模式；启动完成后可以当作普通输入使用 | BOOT 在运行时作为第三个用户键（§6.9）；只配置为输入，**禁止**配置成输出；可以作为 light-sleep 唤醒源（低电平触发）。"开机时按住 BOOT 进入刷机模式"这一行为保留不变，并写进刷机引导 |

---

## 5. 系统架构与部署形态

### 5.1 组件
```
┌───────────────────── nuka-esp32 固件（ESP32-S3）─────────────────────┐
│ App 层 : Clock · Hosts · Servers · AI · Buddy · Pet · Pomodoro      │
│          Media · Weather · Vault · Settings                         │
│ UI 层  : theme · nav（卡片 / 详情 / 弹层 / 面板）· widgets · anim    │
│ 服务层 : power · battery · input · imu_gesture · net · hub_client    │
│          vm_store · notify · usb（CDC+HID）· vault · ota · i18n      │
│ 板级层 : i2c_bus · co5300 · cst9220 · axp2101 · qmi8658 · pcf85063  │
│          keys · panel_power                                         │
│ ESP-IDF 5.5.x · LVGL 9.x · esp_lvgl_port · TinyUSB · mbedTLS         │
└──────────────▲──────────────────────────────────────▲─────────────┘
               │ WebSocket（PROTOCOL §3）              │ USB-CDC（PROTOCOL §4）
┌──────────────┴───────────── nuka-hub（Python）───┴─────────────┐
│ 接入层 : HTTP（REST / Web）· WS（device / web / agent）· hooks · mDNS │
│ 领域层 : hosts · servers · alerts · ai_quota · agents · notify       │
│          media · weather · devices · ota                            │
│ 插件层 : collectors/*（本机）· servers/*（数据源）· ai/*（配额）      │
│          media/*（三平台）· weather/*                                 │
│ 基础层 : config · secrets · store（SQLite）· bus · scheduler · auth   │
└──────────────▲──────────────────────────────────────────────────────┘
               │ WS /ws/agent
        ┌──────┴─────────────┐
        │ Hub 的 Agent 角色   │  其他机器：只采集本机指标并上报
        └────────────────────┘
```

### 5.2 部署形态
| 形态 | 描述 | 典型场景 |
|---|---|---|
| D1 单机 | Hub 和设备在同一局域网，Hub 自带本机采集 | 一台主力电脑 |
| D2 多机 | 一台 Hub，其余机器以 Agent 角色上报 | 台式机 + 笔记本 + 家庭服务器 |
| D3 公网 | 在 D1 / D2 的基础上，Hub 经 cloudflared 隧道暴露 Web，并用 Cloudflare Access 保护 | 外出时用手机查看；设备不在同一网络 |
| D4 服务器 Hub | Hub 跑在 Linux 服务器或 Docker 中，没有本机 GUI 类数据 | 7×24 运行，桌面机以 Agent 角色接入 |

Hub 的任何能力都**不能假定** Hub 与 AI CLI、媒体播放器运行在同一台机器上。媒体信息和本机 Agent hook 由它们所在机器上的 Hub 或 Agent 角色提供，统一汇聚到 Hub（§7.7、§7.8）。

### 5.3 数据流原则
- **设备是瘦终端**：所有抓取、解析、聚合都在 Hub 完成。Hub 下发的是已经排好版的"视图模型"（PROTOCOL §3.4），设备不做任何业务计算；倒计时这类纯展示计算除外。
- **推送节流**：设备亮屏时，Hub 按各主题的频率推送；熄屏时只推送事件类消息（权限请求、critical 通知、OTA）。
- **密钥不出 Hub，vault 不出设备**：AI 配额密钥和数据源凭据只存在 Hub；vault 内容只存在设备。

---

## 6. 固件详细设计

### 6.1 工程与构建
- ESP-IDF 以 **v5.5.x** 的最新补丁版为基线；CI 同时用 v6.0 编译一遍，提前发现兼容问题。
- LVGL 使用 **9.x** 最新稳定版，经 `esp_lvgl_port` 接入。不用 `esp_lvgl_adapter`：它更重，而且要求 IDF ≥ 5.5.5。
- 组件依赖（写在 `idf_component.yml`，版本必须锁定在 `dependencies.lock` 中）：
  - `lvgl/lvgl ^9`
  - `espressif/esp_lvgl_port`
  - `espressif/esp_lcd_co5300`
  - `waveshare/esp_lcd_touch_cst9217`
  - `espressif/esp_websocket_client`
  - `espressif/mdns`
  - `espressif/esp_tinyusb`
- 语言：C11，不用 C++，以降低体积和复杂度。
- 目录：
```
firmware/
  CMakeLists.txt  sdkconfig.defaults  partitions.csv  idf_component.yml
  main/                 app_main.c（启动顺序）、app_registry.c（卡片注册表）
  components/
    board/              pins.h, i2c_bus.c, co5300_panel.c, cst9220_touch.c, axp2101.c,
                        qmi8658.c, pcf85063.c, keys.c
    gfx/                gfx.c（LVGL 接入、rounder、亮度）、perf_hud.c
    ui/                 theme.c + theme_tokens.h（生成）、nav.c、overlay.c、keynav.c（三键导航）、panel_quick.c,
                        notify_center.c, widgets/{ring,stat,chip,button,pager,statusbar,pinpad,hold_ring}.c,
                        anim.c, i18n.c + strings_{zh,en}.c（生成）
    apps/<name>/        每个 App 拆成两个文件：
                          <name>_app.c   逻辑：订阅 vm_store、处理按键动作、调用 svc（归 FW 轨道）
                          <name>_view.c  视图：创建控件、布局、动画、视觉状态（FW 先做功能版，FE 轨道负责打磨）
                        App 列表：clock, hosts, servers, ai, buddy, pet, pomodoro, media, weather, vault, settings
    svc/                power.c, battery.c, input.c, imu_gesture.c, net.c, hub_client.c, proto.c,
                        vm_store.c, notify.c, usb.c, macro.c, vault.c, totp.c, ota.c, store.c, log.c
    assets/             nukapack.c（读取资源分区）
  sim/                  基于 SDL2 的 PC 模拟器工程（复用 ui/ 与 apps/，用 mock 替代 svc）
  tools/                gen_fonts.py, gen_theme.py, gen_strings.py, pack_assets.py
```

**App 与 View 的接口**（`apps/app.h`）。它是 FW 和 FE 两条轨道之间的分界线：FE 只改 `*_view.c`、`ui/`、`assets/` 和 `design/`，不改这个接口和 `*_app.c`。
```c
typedef enum { VIEW_NORMAL, VIEW_LOADING, VIEW_OFFLINE, VIEW_UNCONFIGURED } view_state_t;
typedef enum { ACT_PRIMARY, ACT_SECONDARY, ACT_OPEN, ACT_BACK } app_action_t;   // 由按键或触摸映射而来，见 §6.9

typedef struct {
    void (*create)(lv_obj_t *card);                         // 创建 L0 卡片的控件
    void (*apply)(const void *vm, view_state_t st);         // 根据最新的视图模型刷新控件（只在 lvgl 任务中调用）
    void (*open_detail)(lv_obj_t *page);                    // 创建 L1 详情页；可以为 NULL
} app_view_t;

typedef struct {
    const char       *id;          // 例如 "ai"
    vm_topic_t        topic;       // 订阅的视图模型主题
    const app_view_t *view;
    void (*on_action)(app_action_t a);   // 卡片主操作等，实现在 *_app.c
} app_def_t;
```

### 6.2 任务模型（FreeRTOS）
| 任务 | 核 | 优先级 | 栈（内部 RAM） | 职责 |
|---|---|---|---|---|
| `lvgl` | 1 | 4 | 8 KB | 处理 LVGL 定时器并渲染；**唯一可以调用 LVGL API 的任务** |
| `input` | 1 | 6 | 3 KB | 按键中断事件的消抖、手势归类，投递到事件总线 |
| `power` | 0 | 5 | 4 KB | 电源状态机、亮度渐变、进入与退出睡眠 |
| `pmu_poll` | 0 | 3 | 3 KB | 轮询 AXP2101（亮屏 1Hz，熄屏 0.1Hz）；IMU 中断后读取数据 |
| `net` | 0 | 5 | 6 KB | Wi-Fi、mDNS、WS 客户端、协议解析，结果写入 vm_store |
| `usb` | 0 | 5 | 4 KB | TinyUSB 任务、CDC 行协议、HID 发送 |
| `ota` | 0 | 3 | 6 KB | 需要时才创建，升级结束后删除 |

- 模块之间通过 `esp_event` 的自定义事件循环 `nuka_bus` 解耦；事件定义集中在 `svc/nuka_events.h`。
- 其他任务**禁止**直接调用 LVGL。要更新 UI 时：先把数据写入 `vm_store`（带版本号）并发出事件；再由 `lvgl` 任务中的 `lv_timer` 或 `lv_async_call` 读取最新的视图模型，更新控件。
- 看门狗：启用任务看门狗（TWDT），监控 `lvgl` 和 `net` 两个任务；超过 10s 没有喂狗就重启，并把原因记到 NVS。

#### 6.2.1 启动顺序（`main/app_main.c`）
固件从 `app_main` 开始按以下顺序初始化；第 1–7 步失败属于不可恢复错误，直接 `ESP_ERROR_CHECK`。
1. NVS 初始化（`nvs` 分区；页满或版本不符时擦除后重建），读取 `sys` 命名空间，`boot_cnt` 加 1；
2. 读取重启原因（`esp_reset_reason()` 和 NVS 中的 `wdt_reason`），留给第 9 步显示和 `diag.info` 上报；
3. 创建事件循环 `nuka_bus` 和 `vm_store`；
4. I2C 总线 → AXP2101：读取开机原因、配置电源轨与充电参数（§6.10.3）；
5. 面板上电 → CO5300 初始化 → LVGL 接入，亮度保持 0；
6. 映射资源包并校验 `nukapack.abi`。不匹配时，用编译进固件的 LVGL 内置字体显示英文提示页，不执行第 8 步（不创建 App），其余步骤照常进行，以便通过 OTA 或刷机更新资源包；
7. 触摸、三键、RTC（用 RTC 时间设置系统时间）、IMU；
8. 注册 App、创建 UI，显示表盘，亮度渐变到设置值；
9. 启动 `power`、`pmu_poll`、`input` 任务；只有在异常重启（看门狗、过温、低电）之后，才弹出开机原因提示；
10. 启动 USB（CDC + HID）；
11. 启动网络：有配网信息就连接 Wi-Fi 并寻找 Hub（§6.12.2）；没有就停在"未配网"引导页；
12. 如果当前是 OTA 后第一次启动，开始 60s 的确认计时（§6.16）。

第 11 步与 UI 并行，不阻塞表盘显示；表盘先显示 RTC 时间，各数据卡片显示 `VIEW_LOADING` 或 `VIEW_UNCONFIGURED`。

### 6.3 显示子系统

#### 6.3.1 渲染配置
| 项 | 值 |
|---|---|
| 色深 | RGB16（RGB565），`swap_bytes = true` |
| QSPI 时钟 | 40 MHz |
| draw buffer | **2 × 480×48×2B ≈ 90 KB，放在内部 DMA RAM，双缓冲**（`buff_dma=true, buff_spiram=false, double_buffer=true`） |
| 渲染模式 | PARTIAL |
| 刷新周期 | `LV_DEF_REFR_PERIOD = 16`（上限约 60Hz） |
| 空闲行为 | 唯一 `lvgl` 任务运行 `lv_timer_handler`，最大等待 500 ms；初始化、显示/输入注册与 UI 创建也在该任务内；熄屏暂停该任务渲染循环 |
| LVGL 内存 | `LV_USE_CLIB_MALLOC`，并用自定义的 `lv_malloc` 从 PSRAM 分配（`heap_caps_malloc(MALLOC_CAP_SPIRAM)`）；draw buffer 不走这里 |
| 绘制单元 | `LV_DRAW_SW_DRAW_UNIT_CNT = 1`（第二个核留给网络；实测需要时再评估改为 2） |
| 缓存 | `LV_OBJ_STYLE_CACHE = 1`；图片缓存 256 KB，放在 PSRAM |

#### 6.3.2 sdkconfig 关键项（写入 `sdkconfig.defaults`）
| 类别 | 配置项 |
|---|---|
| Flash / PSRAM | `CONFIG_ESPTOOLPY_FLASHMODE_QIO`、`CONFIG_ESPTOOLPY_FLASHSIZE_16MB`、`CONFIG_SPIRAM_MODE_OCT`、`CONFIG_SPIRAM_SPEED_80M` |
| 性能 | `CONFIG_COMPILER_OPTIMIZATION_PERF`、`CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240`、`CONFIG_ESP32S3_DATA_CACHE_64KB`、`CONFIG_ESP32S3_DATA_CACHE_LINE_64B`、`CONFIG_ESP32S3_INSTRUCTION_CACHE_32KB`、`CONFIG_FREERTOS_HZ=1000` |
| 功耗 | `CONFIG_PM_ENABLE`、`CONFIG_FREERTOS_USE_TICKLESS_IDLE` |
| 内存 | `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP`（Wi-Fi / LWIP 缓冲尽量放到 PSRAM） |
| LVGL | `CONFIG_LV_OS_FREERTOS`、`CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM=n` |
| 可靠性 | `CONFIG_ESP_TASK_WDT_TIMEOUT_S=10`、`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` |

内部 RAM 预算：LVGL 9.6 的快速代码实测占用约 110 KiB IRAM，启用时启动后内部空闲 RAM 约 42 KiB，无法容纳完整 USB、Wi-Fi 与 Vault。经维护者确认，LVGL 快速代码从 Flash 执行；两个内部 DMA 绘图缓冲保持原规格。

#### 6.3.3 CO5300 初始化表（已在官方 BSP 上验证，必须原样使用）
```c
{0x11,{0},0,600}, {0xFE,{0x20},1,0}, {0x19,{0x10},1,0}, {0x1C,{0xA0},1,0}, {0xFE,{0x00},1,0},
{0xC4,{0x80},1,0}, {0x3A,{0x55},1,0}, {0x35,{0x00},1,0}, {0x53,{0x20},1,0}, {0x51,{0xFF},1,0},
{0x63,{0xFF},1,0}, {0x2A,{0,0,0x01,0xDF},4,0}, {0x2B,{0,0,0x01,0xDF},4,0}, {0x36,{0xA0},1,0},
{0x29,{0},0,600}
```
- **rounder**：挂在 `LV_EVENT_INVALIDATE_AREA` 上，执行 `x1 &= ~1; y1 &= ~1; x2 |= 1; y2 |= 1;`。
- **亮度**：`gfx_set_brightness(uint8_t level)` 发送 0x51 命令。UI 层使用感知亮度百分比（0–100），经伽马 2.2 曲线映射到 8–255。
- **面板睡眠**：
  - 熄屏：依次发送 `0x28`（display off）和 `0x10`（sleep in）。
  - 唤醒：发送 `0x11`，等待 120ms，再发送 `0x29`。
  - 熄屏时**不使用** deep standby（`0x4F`）：退出它需要复位并重跑初始化，唤醒太慢。只在关机流程中使用。

#### 6.3.4 已知风险
卡片轮播的整屏滑动在没有 TE 同步的情况下可能撕裂。如果 FW-07 的基准测试显示撕裂明显，就改用"淡出 + 小位移"的过渡（§6.6.5 中的 B 方案）。

### 6.4 内存与分区

#### 6.4.1 分区表（`partitions.csv`）
| 名称 | 类型 | 偏移 | 大小 | 说明 |
|---|---|---|---|---|
| nvs | data/nvs | 0x9000 | 0x6000（24K） | 设置、配网、令牌 |
| otadata | data/ota | 0xF000 | 0x2000 | |
| phy_init | data/phy | 0x11000 | 0x1000 | |
| vault | data/nvs | 0x12000 | 0x10000（64K） | NukaVault，独立加密（§9.3） |
| ota_0 | app/ota_0 | 0x30000 | 0x400000（4M） | |
| ota_1 | app/ota_1 | 0x430000 | 0x400000（4M） | |
| assets | data/0x40 | 0x830000 | 0x600000（6M） | 两个 3 MiB 资源 bank；当前 nukapack 以 mmap 方式只读访问 |
| storage | data/littlefs | 0xE30000 | 0x1D0000（约 1.8M） | 环形日志、统计数据 |

#### 6.4.2 资源包 nukapack
- 由 `tools/pack_assets.py` 生成，结构为：文件头 + 索引表（名称哈希 → 偏移、长度、类型）+ 原始的 LVGL 字体 / 图片描述符数据。
- 运行时用 `esp_partition_mmap` 映射，零拷贝访问；LVGL 字体与图片直接引用映射后的地址（XIP）。
- 资源包与固件分开版本化。固件启动时校验 `nukapack.abi`；不匹配时提示用户通过 OTA 或刷机更新资源包。
- `assets` 内部划分为两个 3 MiB bank，单包构建上限为 3 MiB；分区偏移和总大小不变。活动 bank 由 `sys.asset_slot` 的 NVS 原子提交选择，首次刷机写 bank 0；更新流程见 §6.16。

#### 6.4.3 内存预算（NFR-1）
| 项 | 预算 |
|---|---|
| draw buffer（内部 DMA RAM） | 90 KB |
| Wi-Fi + LWIP（内部 RAM 部分） | ≤ 60 KB（其余放 PSRAM） |
| 各任务栈合计 | ≤ 100 KiB（包含 ESP-IDF、Wi-Fi、LVGL、TinyUSB 及按需任务） |
| 运行时内部 RAM 剩余 | 普通联网 **≥ 100 KB**；语音会话期间 **≥ 64 KB**，结束后恢复普通预算（PLAN-v2 Q3） |
| PSRAM 占用 | ≤ 2 MB |
| 固件 app 镜像 | ≤ 3 MB |
| 单个资源包 | ≤ 3 MiB；assets 总计 6 MiB（两种字号的中文字体约 2 MB，另一 bank 保留旧包） |

维护者于 2026-10-18 批准调整任务栈预算：完整系统未配网时，应用任务栈为 38,912 字节、框架任务栈为 39,680 字节，合计 78,592 字节，原 40 KB 限制无法容纳既定任务模型。按需 OTA、WebSocket 等任务也计入新预算；运行时内部 RAM 剩余验收线、内部 DMA 缓冲与敏感数据的存放要求不变。

### 6.5 板级驱动接口（草案）
每个驱动只暴露最小的 API，并且全部通过 `i2c_bus` 访问总线。
```c
// i2c_bus.h
esp_err_t i2c_bus_init(void);                              // GPIO14/15, 400kHz
esp_err_t i2c_bus_rd(uint8_t addr, uint8_t reg, uint8_t *buf, size_t n);
esp_err_t i2c_bus_wr(uint8_t addr, uint8_t reg, const uint8_t *buf, size_t n);

// axp2101.h
esp_err_t axp_init(const axp_cfg_t *cfg);                  // 电源轨、充电参数、按键时序、关闭 TS 检测
esp_err_t axp_read_status(axp_status_t *out);              // vbat_mv, vbus_mv, pct, charging, vbus_present, batt_present, die_temp
uint32_t  axp_take_irq(void);                              // 读取并清除中断标志：PKEY_SHORT/LONG、VBUS/BAT 插拔、充电开始/完成
esp_err_t axp_set_aldo1(bool on);                          // 音频电源轨
axp_boot_reason_t axp_boot_reason(void);                   // 读取 reg0x20/0x21
void      axp_power_off(void) __attribute__((noreturn));   // 置位 reg0x10 bit0

// co5300_panel.h / gfx.h
esp_err_t gfx_init(void);                                  // 初始化面板、触摸、LVGL
void      gfx_set_brightness(uint8_t pct);                 // 0..100，内部做伽马映射
void      gfx_fade_to(uint8_t pct, uint32_t ms);           // 亮度渐变，不触发重绘
esp_err_t gfx_sleep(bool sleep);                           // 0x28/0x10 ↔ 0x11/0x29，同时暂停 / 恢复 LVGL

// keys.h
typedef enum { KEY_PWR, KEY_USER } key_id_t;
typedef enum { KEV_SHORT, KEV_LONG, KEV_DOUBLE, KEV_HOLD_2S } key_ev_t;

// qmi8658.h
esp_err_t imu_init(void);
esp_err_t imu_enable_wom(uint8_t threshold_mg);            // wake-on-motion，中断输出到 INT1（GPIO17）
esp_err_t imu_read(imu_sample_t *out);                     // 加速度 + 陀螺仪

// pcf85063.h
esp_err_t rtc_get(struct tm *t);
esp_err_t rtc_set(const struct tm *t);
```

### 6.6 UI 设计系统

#### 6.6.1 设计 token（唯一来源：`design/theme.json`）
`firmware/tools/gen_theme.py` 用它同时生成 `firmware/components/ui/theme_tokens.h` 和 `hub/src/nuka_hub/web/theme.css`，保证固件与 Web 的视觉一致。

| Token | 值 | 用途 |
|---|---|---|
| `color.bg` | `#000000` | 全局背景（AMOLED 的黑色像素不发光） |
| `color.surface` | `#10140C` | 卡片底色 |
| `color.surface_hi` | `#1B2213` | 按下态、次级容器 |
| `color.outline` | `#2A341C` | 分隔线、未激活的描边 |
| `color.avo` | `#A4C93C` | 主强调色（牛油果果肉绿） |
| `color.avo_soft` | `#C9DE7A` | 高亮、进度条端点、焦点 |
| `color.avo_deep` | `#4E6B1E` | 圆环底轨、禁用态 |
| `color.pit` | `#7A5230` | 牛油果核的棕色，只做少量点缀 |
| `color.text` | `#EEF3E2` | 主文字 |
| `color.text_dim` | `#8C9878` | 次要文字 |
| `color.warn` | `#F0B84A` | 警告：配额 > 80%、温度偏高、电量 < 20% |
| `color.danger` | `#EF6A55` | 错误、耗尽、离线 |
| `color.info` | `#7FB8D6` | 中性信息（少用） |
| `radius.card` / `radius.btn` / `radius.chip` | 28 / 20 / 999 | |
| `space.xs/s/m/l/xl` | 4 / 8 / 16 / 24 / 32 | |
| `safe_inset` | 24 | 四角圆角的安全区，可交互元素不得进入 |

对比度要求：`text` 和 `text_dim` 在 `bg` 与 `surface` 上的对比度分别不低于 7:1 和 4.5:1。`gen_theme.py` 会校验，不达标则构建失败。

#### 6.6.2 字体与字号（本屏约 314 ppi）
| 级别 | 字号 px | 字体 | 用途 |
|---|---|---|---|
| display | 112 | JetBrains Mono Nerd Font Bold 的 OFL 拉丁子集（只包含数字和冒号） | 表盘时间 |
| h1 | 56 | Inter SemiBold + 等宽数字 | 卡片主数值 |
| h2 | 40 | Inter SemiBold | 次要数值 |
| title | 32 | Inter SemiBold / Noto Sans SC Medium | 卡片标题 |
| body | 28 | Inter Regular / Noto Sans SC Regular | 正文 |
| caption | 22 | Inter Medium / Noto Sans SC Regular | 辅助信息 |
| 下限 | 20 | — | 任何文字都不得小于这个字号 |

- 中文只提供 **22 和 28** 两个字号的位图字体，放在 nukapack 中（常用 3500 字 + 业务词表，4bpp）。更大的中文标题也用 28 号，不做缩放。
- 数字统一用等宽字形（tabular figures），避免数值变化时宽度抖动、引起大面积重绘。
- 构建选项 `NUKA_LANG_PACK=en-only` 可以去掉中文字体，节省约 2MB。

#### 6.6.3 组件库
| 组件 | 规格 |
|---|---|
| `ring` | 弧形进度条：线宽 14 或 20px，底轨为 `avo_deep`；前景颜色按阈值变化（< 60% 用 avo，60–85% 用 warn，> 85% 用 danger），端点为圆头；只重绘变化的弧段 |
| `dual_ring` | 内外两层环：外环表示 5h 窗口，内环表示周窗口，中心放数值 |
| `stat` | 数值 + 单位 + 标签；数值变化时用 180ms 滚动或淡入，只重绘自身区域 |
| `chip` | 状态胶囊（在线 / 离线 / 冷却 / 需重登等），带颜色圆点 |
| `button` | 主操作按钮最小 80×80px；按下时缩放到 0.96、底色变为 `surface_hi`；可选按键提示音 |
| `pager` | 底部的页面指示点，当前页显示为 avo 色长条 |
| `statusbar` | 时间、Wi-Fi 信号、Hub 连接状态、电量与充电图标；高 40px |
| `pinpad` | 3×4 数字键盘，每个键 112×80px，用于 vault 解锁 |
| `toast` | 底部浮层，3s 后自动消失 |
| `sheet` | 从底部升起的操作面板，用于关机菜单、权限审批 |
| `list` | 详情页中的列表，行高 72px，支持惯性滚动 |

#### 6.6.4 国际化
- 字符串表为 `i18n/strings.{zh-CN,en}.json`，由 `gen_strings.py` 生成 C 表。运行时按 NVS 中保存的语言选择；Hub 会在 `welcome` 消息中下发默认语言。
- 视图模型中由 Hub 生成的文本（例如通知标题）按设备语言由 Hub 生成。

#### 6.6.5 动画规范（NFR-2 的设计保障）
| 规则 | 内容 |
|---|---|
| 禁止 | 全屏半透明渐变、大面积阴影或模糊、全屏不透明度动画、大尺寸透明叠层、渐变背景 |
| A / B 方案的选择 | 由 FE 轨道在任务 FE-D4 中根据实机测量结果决定，并写入决策记录 |
| 页面切换 A 方案（默认） | 卡片轮播使用跟手的整屏滑动，松手后用 240ms `ease_out` 回弹或翻页；目标 ≥ 30fps |
| 页面切换 B 方案（撕裂时的备选） | 旧页内容 120ms 淡出，新页元素从 24px 外 160ms 移入；背景保持纯黑，不重绘 |
| 进入详情 | 卡片中被点中的元素放大并移动到详情页标题的位置（共享元素动画，200ms），其他元素淡入 |
| 弹层 / sheet | 从底部用 220ms `ease_out` 升起。底层页面不做变暗动画（那会变成全屏透明度动画），而是直接替换成一张静态的 80% 不透明 `#000000` 底 |
| 数值变化 | 局部重绘，180ms |
| 按压反馈 | 缩放到 0.96，80ms |
| 熄屏 / 亮屏 | 用 0x51 做 200ms 亮度渐变，没有渲染开销 |
| 时长常量 | `ANIM_FAST=120`、`ANIM_BASE=180`、`ANIM_SLOW=240`（单位 ms），全部定义在 `ui/anim.h` |
| 低电模式 | 电量 < 10% 时动画时长减半，并关闭共享元素动画 |

### 6.7 导航与交互

#### 6.7.1 层级
```
L0 卡片轮播：[ 表盘 | 主机 | 服务器 | AI 余额 | Buddy | 番茄钟 | 媒体 | 天气 | Vault ]（顺序可编辑，卡片可隐藏）
L1 详情页：在卡片上点击进入；右滑或按 Key3 返回
覆盖层：快捷面板（顶部下拉）· 通知中心（底部上滑）· sheet（关机 / 审批）· toast
```
- 最多两层，不做深层菜单。
- 设置作为一张卡片放在轮播最后，也可以从快捷面板进入。

#### 6.7.2 手势参数
| 手势 | 判定条件 | 动作 |
|---|---|---|
| 横滑 | 位移 > 40px 且以水平方向为主，或速度 > 0.6 px/ms | 在 L0 翻页 |
| 顶部下拉 | 起点 y < 48px，下拉距离 > 60px | 打开快捷面板 |
| 底部上滑 | 起点 y > 432px，上滑距离 > 60px | 打开通知中心 |
| 右滑返回 | 在 L1 中起点 x < 48px，右滑距离 > 80px | 返回 L0 |
| 点击 | 位移 < 12px，时长 < 400ms | 激活 |
| 长按 | 位移 < 12px，时长 ≥ 600ms | 在 L0 进入卡片编排；在列表项上弹出更多操作 |
| 双击 | 两次点击的间隔 < 300ms | 在表盘上切换表盘样式 |

#### 6.7.3 弹层优先级（高优先级会抢占低优先级）
1. 关机 sheet
2. Agent 权限审批 sheet（会点亮屏幕）
3. critical 通知，例如服务器离线、配额耗尽（会点亮屏幕）
4. 低电量提示
5. 一般 toast

勿扰模式只拦截第 3–5 级，**不拦截**第 1–2 级。

#### 6.7.4 快捷面板
2×3 网格：
- 亮度滑条（占满一整行）
- 勿扰
- 常亮（只有插着电时可用）
- Wi-Fi 状态（点击进入详情）
- 番茄钟快速开始
- 关机

#### 6.7.5 通知中心
- 最多保留 20 条（存在内存中），按时间倒序排列；左滑删除单条，底部有"全部清除"。
- 每条通知显示来源图标、标题、摘要和相对时间；带操作的通知（例如审批）会显示按钮。

### 6.8 各卡片 / App 规格
第二阶段的状态栏按 PLAN-v2 §2.1 改为顶部居中标题，左右放时间与状态图标，内容向屏幕边缘内收至少 48px；L1 返回箭头与标题一起居中，主页两面不显示标题。

每张卡片都必须实现四种状态：
- **正常**；
- **加载中**：骨架屏；
- **离线**：显示最后一次的数据，加上"离线"chip 和数据时间；
- **未配置**：显示引导文案，例如"在 Hub 中启用 xx，参见文档 xx"。

#### 6.8.1 表盘 Clock
- 第二阶段升级为主页：时钟面与助手表情面，Key3 短按切换并记住上次选择；默认面由 `cfg.home_face` 指定（PLAN-v2 §2.3）。
- 内容：112px 等宽时间、日期与星期、电量环（绕屏幕一周的细环），以及可选的下一条最重要的提醒（来自通知）。
- 时间源：联网时用 SNTP（NTP 服务器可配置，可由 Hub 下发，否则用默认池）；离线时用 RTC。Hub 的 `time` 消息用于校准。
- 防烧屏：每 60s 整体随机偏移 ±2px。熄屏时不显示内容（v1 不做 AOD 常亮显示）。

#### 6.8.2 主机 Hosts
- L0：当前主机名 + 四个小环（CPU 负载、CPU 温度、GPU 负载、GPU 温度）+ 内存条 + 网速（上行 / 下行）。有多台主机时，在卡片内部左右滑动切换（由 pager 指示）。
- L1：完整指标列表（每核负载可选、显存、功耗、风扇、磁盘），以及 24h 迷你折线图（Hub 下发降采样后的数据，最多 48 个点）。
- 字段缺失时（例如 macOS 没有 GPU 温度）隐藏对应控件，而不是显示 0。

#### 6.8.3 服务器 Servers
- L0：服务器列表，同时最多显示 6 行，可滚动。每行显示名称、在线圆点、CPU / 内存 / 磁盘三根细条和告警角标。
- L1：单台服务器的详情，并标明数据来源（ServerStatus / Uptime Kuma / …）。

#### 6.8.4 AI 余额 AI
- 第二阶段布局由 PLAN-v2 §2.2 取代：provider / 账号 / 状态置顶，双环居中，今日请求、Token、失败率置底；L1 为账号迷你双环和刷新。不存在的按模型明细不展示，熔断只在 Hub 提供明确状态时显示。
- L0：
  - 顶部汇总今日请求数、token 数和失败率；
  - 主体是当前账号的双环（外环 5h 窗口，内环周窗口），中心显示**剩余百分比**；
  - 下方显示最近一个窗口的重置倒计时，设备根据 `reset_at` 在本地倒数；
  - 有多个账号时左右分页。
- L1：账号列表（provider 图标、标签、套餐、状态 chip、各窗口用量），以及数据新鲜度（例如"被动 3 分钟前 / 快照 / 主动"）。
- 状态颜色：
  - 耗尽：danger 色；
  - 冷却中：warn 色，并显示恢复时间；
  - 需要重新登录：danger 色，并带"需重登"chip；
  - 已停用：灰色。

#### 6.8.5 Agent Buddy
- **L0**：活跃会话列表，同时最多显示 4 个。每行显示来源图标（Claude / Codex / Gemini）、项目名、状态和持续时间。
  - 项目名默认只显示 cwd 的 basename，可以配置为隐藏。
  - 状态有：空闲、思考中、正在运行某个工具（如 `Bash`）、等待审批、完成、出错。
- **权限审批 sheet**：
  - 内容：来源、项目、工具名、摘要（命令的前 120 个字符，等宽字体），以及剩余时间环。
  - 按钮：左边是**拒绝**（描边样式），右边是**允许**（avo 实心），尺寸不小于 160×88px。
  - 为防误触，触摸需要**按住 0.5s** 才生效，按住时显示进度环。
  - 也可以用按键操作：BOOT 按住 1s 为允许，Key3 按住 1s 为拒绝（§6.9.3）。
  - sheet 弹出时会点亮屏幕，并播放一声短促的提示音（如果开启了提示音）。
- **完成通知**：显示截断到前 140 个字符的 `last_assistant_message`，3s 后收起为 toast。

#### 6.8.6 主页助手表情（D26 取代像素桌宠）
不再提供独立桌宠卡和像素精灵。主页表情面使用 LVGL 基础对象绘制两只发光圆角眼，通过眼形、眨眼、视线和弹跳表达情绪；无嘴、无全屏阴影。状态表、过渡、触摸、字幕和停动画策略见 PLAN-v2 §2.4。原 `pet` 主题继续作为情绪数据源。

以下旧状态仍为情绪输入；其像素美术要求已由 D26 取代：
- 8 种状态：
  - `sleep`：没有会话，或深夜；
  - `idle`：空闲；
  - `busy`：有 Agent 正在运行；
  - `attention`：有请求在等待审批；
  - `celebrate`：任务完成，或每累计 50K token；
  - `dizzy`：被摇一摇；
  - `hungry`：任一配额超过 90% 或已耗尽；
  - `charging`：插着电在充电。
- 状态冲突时按以下优先级显示：attention > hungry > dizzy > celebrate > busy > charging > idle > sleep。
- 只重绘眼部和字幕区域；熄屏和低电停止动画，亮屏空闲降低刷新频率；实测帧率和 CPU 记录在 `docs/bench/face-perf.md`。

#### 6.8.7 番茄钟 Pomodoro
- 默认时长：专注 25 分钟、短休 5 分钟、长休 15 分钟（可配置）；每 4 轮专注后进入一次长休。
- 操作：
  - 屏幕朝上平放时开始计时；
  - 扣下时暂停，并开启勿扰（可配置）；
  - 拿起来时显示剩余时间；
  - 按键：BOOT 长按开始或暂停，Key3 长按重置（§6.9.3）。
- 计时在设备本地运行，离线也能用；状态通过 `pomo` 消息上报 Hub。v1 不做与系统专注模式的联动（§7.8）。
- 结束时：亮屏，播放提示音（打开 ALDO1，响 0.5s 后关闭），宠物进入 `celebrate`。

#### 6.8.8 媒体 Media
- 内容：160×160 封面（由 Hub 缩放并编码为 baseline JPEG，设备用 `esp_jpeg` 解码到 PSRAM）、标题、艺术家、进度条（设备在本地推进），以及上一首 / 播放暂停 / 下一首按钮。
- 可以同时有多个媒体源（多台机器），默认显示最近活跃的那个。

#### 6.8.9 天气 Weather
- 内容：当前温度、天气图标（自绘的 12 个线性图标）、体感温度、湿度、风，以及未来 3 天的最高 / 最低温度。
- 离线时显示最后一次的数据和更新时间。

#### 6.8.10 Vault
见 §6.14。

#### 6.8.11 设置 Settings
| 类别 | 设置项 |
|---|---|
| 显示 | 语言；亮度；自动调暗时间与熄屏时间 |
| 交互 | 抬手亮屏开关；扣下时的动作（熄屏 / 勿扰 / 无）；卡片编排 |
| 电源 | 充电模式（护电 4.1V / 满充 4.2V） |
| 网络 | Wi-Fi 与 Hub 信息（只读），以及重新配网入口 |
| 系统 | 存储与固件版本；检查 OTA；恢复出厂（清空 NVS，可选同时清空 vault）；关于（开源许可） |

### 6.9 三键交互（FR-3）
三个物理键全部使用。不碰触摸屏，也能完成核心操作：开关机、亮灭屏、翻页、进入与返回、卡片主操作、审批、恢复。

| 键 | 硬件 | 角色 |
|---|---|---|
| **PWR** | AXP2101 PWRON，状态由 GPIO16 读取 | **电源键**：开关机、亮灭屏 |
| **BOOT** | GPIO0（H12） | **前进 / 确认键**：下一项、执行主操作 |
| **Key3** | GPIO18 | **返回 / 取消键**：返回、关闭、快捷动作 |

> 三个键在机身上的物理位置，在硬件在环测试（FW-01）时拍照标注，写进 `docs/guides/00-overview` 和刷机引导。

#### 6.9.1 通用规则
1. **熄屏时按下的第一下只负责唤醒**：唤醒后会吞掉这一次按键事件，不触发任何其他动作。PWR 例外：熄屏时按住 PWR 照常计时，用于关机。
2. **判定参数**：
   - 消抖 20ms；
   - 短按：松开时按住时长 < 600ms；Key3 的 `talk` 模式按 PLAN-v2 §2.3 改为 <400ms；
   - 长按：按住 ≥ 1s，在按住期间触发，不等松开；
   - 双击：两次短按的间隔 < 300ms。
   - 只有声明了双击动作的键才启用双击检测，以免其他键的短按凭空多出 300ms 延迟。目前**只有 Key3 启用双击**；BOOT 和 PWR 的短按都是立即响应。
3. **视觉反馈**：
   - 凡是需要按住才生效的操作，都在屏幕边缘（贴着圆角安全区）显示 `hold_ring` 进度环；进度满时生效，中途松开则取消。
   - 用按键操作时，L1 列表和 sheet 中会显示焦点框（2px `avo_soft` 描边）；触摸操作后 5s 内没有按键，焦点框自动隐藏。
4. **实现**：所有按键判定都在 `input` 任务中完成，再映射成语义事件（`KEYEV_*`），投递到 `nuka_bus`。由 `ui/keynav.c` 根据当前所在的层级与上下文，把语义事件转换成 `app_action_t`，或者转换成 LVGL keypad 输入设备事件（`LV_KEY_NEXT` / `LV_KEY_ENTER` / `LV_KEY_ESC`，作用于当前的 `lv_group`）。

#### 6.9.2 PWR 电源键
| 情境 | 操作 | 动作 |
|---|---|---|
| 关机状态 | 按住约 1s | 开机（AXP 的开机时间设为 1s，防止误开机） |
| 亮屏 | 短按 | 熄屏 |
| 熄屏 | 短按 | 亮屏 |
| 任意 | 按住 2s | 弹出电源 sheet，包含"关机 / 重启 / 取消"，同时在屏幕中央显示"继续按住关机"的进度环 |
| 电源 sheet 已弹出 | 继续按住到 4s | 立即关机，不需要再确认 |
| 电源 sheet 已弹出 | 松开后，用触摸、BOOT 或 Key3 操作 | 选择对应的选项 |
| 任意，包括死机时 | 按住 6s | AXP 硬件强制断电（兜底，不经过软件） |
| 开机时同时按住 BOOT | — | 进入下载模式（ROM 行为，H12） |

AXP2101 时序寄存器：
- reg0x27：开机时间 1s，IRQ 判定时间 1.5s，关机时间 6s；
- reg0x22：允许长按关机（关机，而不是重启），允许过温关机。

#### 6.9.3 BOOT 与 Key3：各情境下的映射
| 情境 | BOOT 短按 | BOOT 长按 1s | Key3 短按 | Key3 长按 1s | Key3 双击 |
|---|---|---|---|---|---|
| L0 卡片轮播 | 下一张卡片（到最后一张后循环） | 卡片主操作 `ACT_PRIMARY`（见下表） | 主页两面切换；其他卡回到上次的主页面 | 默认按住400ms说话，按下即预录、松开发送，最长30s；勿扰移到快捷面板 | 跳到 Buddy 卡 |
| L1 详情页 | 焦点移到下一项（`LV_KEY_NEXT`） | 激活焦点项（`LV_KEY_ENTER`） | 返回 L0 | 默认按住400ms说话 | — |
| 快捷面板 / 通知中心 | 焦点移到下一项 | 激活焦点项；在通知中心里则删除焦点所在的通知 | 关闭覆盖层 | — | — |
| 一般 sheet（电源 sheet 等） | 焦点移到下一个选项 | 确认焦点选项 | 取消（等同于选择"取消"） | — | — |
| **权限审批 sheet** | 无（防误触） | **按住 1s = 允许**（显示进度环） | 无 | **按住 1s = 拒绝** | — |
| Vault 已解锁 | 下一个条目 | 键入或显示当前条目（键入时需要再按住 0.5s 确认，§6.14） | 返回 | — | **立即上锁** |
| Vault 的 PIN 输入 | 不可用（PIN 只能通过触摸输入） | — | 取消输入 | — | — |
| toast 正在显示 | （按当前所在情境处理） | — | 关闭 toast，不触发返回 | — | — |

**各卡片的主操作 `ACT_PRIMARY`（BOOT 长按）**：
| 卡片 | 主操作 |
|---|---|
| 表盘 | 打开快捷面板 |
| 主机 / 服务器 | 切换到下一台主机或服务器 |
| AI 余额 | 切换到下一账号；刷新放在 L1（发送 `ai.refresh`，受限流约束） |
| Buddy | 有待审批的请求时打开审批 sheet；否则切换到下一个会话 |
| 番茄钟 | 开始或暂停；`ACT_SECONDARY`（在番茄钟卡上 Key3 长按）为重置 |
| 媒体 | 播放 / 暂停 |
| 天气 | 无 |
| Vault | 进入 PIN 解锁 |
| 设置 | 进入设置详情页 |

#### 6.9.4 组合键
| 组合 | 动作 |
|---|---|
| BOOT + Key3 同时按住 3s | 打开**恢复菜单**（不依赖触摸）：重新配网 / 进入 USB 管理模式 / 重启到下载模式 / 恢复出厂。用 BOOT 和 Key3 选择，恢复出厂还需要再按住 3s 确认 |
| PWR + Key3 | 不定义（避免与 AXP 的硬件计时冲突） |

#### 6.9.5 可配置项
可以在设置中修改，也可以由 Hub 下发：
- Key3 长按的快捷动作：说话（默认）/ 勿扰 / 常亮 / 番茄钟 / 执行宏 n / 无；选其他动作时关闭按住说话；
- Key3 双击的目标卡片；
- 是否启用"Key3 短按在表盘上切换表盘样式"。

其余映射固定不变，以保持肌肉记忆。

助手回答期间 Key3 短按打断，按住则打断后开始新一轮；审批、Vault、PIN、恢复菜单中禁用说话，保留原键映射。说话唤醒亮屏并暂停熄屏计时，结束后恢复（PLAN-v2 §2.3）。

### 6.10 电源与电池

#### 6.10.1 电源状态机
| 当前状态 | 事件 | 下一个状态 | 动作 |
|---|---|---|---|
| OFF | PWR 按住 1s | STARTING | AXP 上电 |
| STARTING | 初始化完成 | ACTIVE | 只在异常时显示开机原因，例如上次因过温或低电关机 |
| ACTIVE | 无操作超过 `t_dim`（默认 20s） | DIM | 亮度渐变到 20%（如果当前亮度更低则不变） |
| DIM | 任意输入 | ACTIVE | 恢复亮度 |
| DIM | 再无操作超过 `t_off`（默认 10s） | SCREEN_OFF | 亮度降到 0 → 面板睡眠 → 暂停 `lvgl` 任务的渲染循环 → 上报 `screen:false` → 允许进入 light sleep |
| ACTIVE / DIM | PWR 短按 | SCREEN_OFF | 同上 |
| SCREEN_OFF | 触摸中断 / PWR / BOOT / Key3 / 抬手（IMU）/ 高优先级推送 | ACTIVE | 唤醒面板 → 恢复 `lvgl` 任务的渲染循环 → 亮度渐变恢复 → 上报 `screen:true`；吞掉这次唤醒按键（§6.9.1） |
| 任意 | PWR 按住 2s 后，在 sheet 中选择"关机"，或继续按住到 4s | SHUTDOWN | 保存状态 → 断开 WS → 关闭面板 → `axp_power_off()` |
| 任意 | 在 sheet 中选择"重启" | STARTING | `esp_restart()` |
| 任意 | 电量 < 5% 且未充电 | SHUTDOWN | 提示 3s 后关机 |
| 任意 | AXP 报告过温 | SHUTDOWN | 记录原因 |

- **常亮模式**：插着 USB 并在快捷面板中打开"常亮"时，跳过 DIM 和 SCREEN_OFF；亮度上限为 40%，防烧屏偏移改为每 30s 一次。
- 有 AI Agent 正在运行，且有会话处于 `waiting_permission` 状态时，`t_off` 延长到 60s。

#### 6.10.2 睡眠与唤醒
- 调用 `esp_pm_configure({max_freq_mhz=240, min_freq_mhz=80, light_sleep_enable=true})`。
- 熄屏后 Wi-Fi 切换到 `WIFI_PS_MAX_MODEM`（listen interval 为 3），WS 连接保持不断；这期间 Hub 只推送事件类消息。
- 唤醒源：
  - 触摸：`gpio_wakeup_enable(GPIO11, LOW)`；
  - PWR：`GPIO16, HIGH`；
  - BOOT：`GPIO0, LOW`；
  - Key3：`GPIO18, LOW`；
  - 抬手：`GPIO17`（有效电平取决于 IMU 的配置）。
- 熄屏时，触摸芯片要保持在低功耗监测模式。不能让它进入最深的睡眠，否则 INT 无法唤醒系统。

#### 6.10.3 AXP2101 配置
| 参数 | 值 |
|---|---|
| DC1 | 3.3V，开启（系统供电） |
| ALDO1 | 3.3V，默认关闭，只在播放提示音时打开 |
| 其他未使用的电源轨 | 关闭 |
| TS 检测 | 关闭（板上没有 NTC） |
| 恒流充电电流 | **300mA**（设置中可选 100 / 200 / 300 / 400 / 500mA） |
| 充电截止电压 | **4.1V**（护电模式，默认）/ 4.2V（满充模式） |
| 预充电流 / 截止电流 | 50mA / 25mA |
| 电量计 | 开启 |
| 过温关机 | 开启 |
| VBUS 限流 | 1500mA |

#### 6.10.4 电量与续航估算
- **百分比**：
  - 读取 AXP 电量计（reg0xA4），每 1s 采样一次，经过一阶低通滤波（α = 0.1）。
  - 充电状态切换后的 30s 内不更新显示，避免跳变。
  - 显示值保持单调：放电时不上升，充电时不下降；插拔电源时重新开始。
- **剩余时间**：用最近 10 分钟的放电速率，再按当前是否亮屏加权来估算。设置页显示"亮屏约 X 小时 / 熄屏待机约 Y 小时"，数据来自累计统计，存在 `storage` 分区。

#### 6.10.5 保护策略
| 条件 | 动作 |
|---|---|
| 电量 < 20% 且未充电 | 状态栏电量变成 warn 色；亮度上限 50%；`t_dim` 和 `t_off` 减半 |
| 电量 < 10% 且未充电 | 弹窗提示一次；关闭抬手亮屏；动画时长减半；请求 Hub 把推送频率降到 0.2Hz |
| 电量 < 5% 且未充电 | 自动关机 |
| 插入 VBUS | 亮屏 2s，在屏幕底部 120px 区域显示充电动画（局部刷新） |
| 充满 | 通知一次 |
| 芯片温度 > 60°C | 把充电电流降到 100mA；超过 75°C 时关机 |

#### 6.10.6 上报
`hello` 和 `state` 消息中带有 `bat` 字段（PROTOCOL §3.5.2），包括百分比、电压、充电状态、电源来源和估算的剩余时间。Hub 把这些数据存成 24h 曲线，在 Web 上展示。

### 6.11 IMU 手势（QMI8658）
| 手势 | 判定条件 | 动作（可配置） |
|---|---|---|
| 抬起 | 收到 wake-on-motion 中断后读取姿态：1s 内屏幕法线与重力方向的夹角从 > 60° 变为 < 40° | 亮屏 |
| 扣下 | 屏幕朝下（z 轴 < −0.8g）并持续 1.5s | 熄屏，或开启勿扰 / 暂停番茄钟 |
| 摇一摇 | 1s 内出现至少 3 次加速度模值 > 1.8g 的峰值 | 关闭当前通知或 toast；宠物进入 `dizzy` |
| 平放静止 | 加速度方差小于阈值并持续 5s | 作为番茄钟"朝上开始"的判定条件 |

- 亮屏时，IMU 传感器使用硬件支持的 31.25Hz 模式，`pmu_poll` 任务按 25Hz 读取和处理。
- 熄屏时，只开启 wake-on-motion（IMU 使用硬件支持的 21Hz 低功耗采样）。GPIO17 唤醒系统后再判定是否"抬起"；不是则继续睡眠。
- 电量低于 10% 时关闭抬起亮屏。

### 6.12 网络、配网与配对

#### 6.12.1 配网方式
1. **USB 配网（推荐）**：
   1. 把设备插到装有 Hub 的电脑上，运行 `nuka pair`。
   2. Hub 按 VID `0x303A` 找到设备的 CDC 端口，下发 `prov.set`：Wi-Fi 列表、Hub 地址列表、设备令牌。
   3. 设备屏幕弹出确认 sheet，显示 Hub 名称和 6 位配对码（与 CLI 上显示的一致）。
   4. 用户点击确认后，设备把配置写入 NVS。
2. **SoftAP 网页配网（备用）**：
   1. 在设置中选择"重新配网"，设备开启热点 `Nuka-XXXX`，屏幕上显示二维码（Wi-Fi 连接二维码 + 配网页 URL）。
   2. 手机连上热点后打开配网页，填写 Wi-Fi 和 Hub 地址。
   3. 在 Hub 的 Web 页面上输入设备屏幕上显示的 6 位配对码，完成配对。
- 设备最多保存 3 组 Wi-Fi。Hub 地址列表按优先级排列，例如：局域网地址、Tailscale 地址、公网 `wss://` 地址。

#### 6.12.2 发现与连接顺序
1. 用 mDNS 查询 `_nuka._tcp`，按 TXT 记录中的 `id` 匹配已配对的 Hub。
2. NVS 中缓存的、上次成功连接的 IP。
3. 配网时写入的 Hub 地址列表，按顺序尝试。

连接失败后指数退避：1s → 2s → … → 上限 30s，并加 ±20% 的随机抖动；熄屏时上限放宽到 120s。

#### 6.12.3 WebSocket 客户端
- 使用 `esp_websocket_client`，请求头带 `Authorization: Bearer <device_token>`，子协议为 `nuka.v1`。
- TLS（`wss://`）使用 ESP-IDF 自带的证书包 `esp_crt_bundle`。局域网内允许使用 `ws://`，但 Web 和日志中会提示"未加密"。
- 心跳：每 25s 发一次 ping；连续 2 次 pong 超时即判定断线。
- 收到的帧交给 `proto.c` 解析（cJSON），校验 `v` 和 `t` 字段后写入 `vm_store`。单帧上限 8KB，超出则丢弃并记录日志。

### 6.13 USB 复合设备
- TinyUSB 复合设备包含三个接口：
  - **CDC-ACM**：配网、诊断、vault 导入导出；
  - **HID 键盘**：宏键盘、vault 键入；
  - **HID Consumer Control**：媒体键。
- 启用 TinyUSB 后，原生的 USB-Serial-JTAG 就不能用了：
  - 日志改走 CDC 上的诊断通道（`diag.log`）。
  - 烧录需要先进入下载模式：按住 BOOT 再上电，或者经 CDC 发送 `sys.download` 命令，让设备重启进入下载模式（PROTOCOL §4.5，在 FW-15 中验证）。**这一点必须写进刷机引导。**
- 宏键盘：插着 USB 时，Buddy 卡和快捷面板上提供可配置的按键宏（组合键或字符串）。宏定义由 Hub 下发（`macro.set`，经 WS 或 CDC），存在 NVS 中。宏**不得**包含 vault 中的内容。
- 键盘布局：v1 只支持 US 布局。Hub 下发宏时可以先按目标布局转换好。

### 6.14 NukaVault（设备端）
安全模型见 §9.3，这里只描述功能与交互。
- **解锁**：
  - 进入 Vault 卡，在 pinpad 上输入 6–8 位 PIN。
  - 解锁状态保持 2 分钟（可配置为 1–10 分钟）。
  - 熄屏、双击 Key3、离开 Vault 卡超过 30s，都会立即上锁。
- **TOTP**：
  - 列表中每个条目显示名称、当前的 6 位验证码（等宽 40px）和 30s 倒计时环。点击条目后大字显示，并可以选择"键入"，经 HID 把验证码打出来。
  - 时间来源是已同步的 SNTP 或 RTC。距离上次时间同步超过 7 天时，显示"时间可能不准"的 warn 提示。
- **密码库**：
  - 每个条目包含名称、用户名、密码、URL（只用于显示）和可选的备注。
  - 点击条目后，可以选择"键入用户名""键入密码"或"键入 用户名⇥密码⏎"，再**按住 0.5s 确认**，设备才会经 HID 键入。
  - 只有插着 USB 时才能使用。
- **密码生成器**：
  - 长度 12–64，字符集可选（大写、小写、数字、符号，可排除易混淆字符）。
  - 熵源为硬件 RNG（`esp_fill_random`），要求射频已开启，或已启用 bootloader 随机源。
  - 生成后可以"保存为新条目"或"直接键入"。
- **导入 / 导出**：
  - 只能走 USB-CDC（PROTOCOL §4.4）。
  - 设备必须已解锁，并且用户先在设备上手动进入"USB 管理模式"（有效期 2 分钟）。
  - 导入时在屏幕上逐条确认，或一次性确认"导入 N 条"。
- **屏幕上默认不显示明文密码**，只有在用户长按"显示"时才显示 5s。

### 6.15 存储（NVS 命名空间）
| 命名空间 | 键 | 说明 |
|---|---|---|
| `sys` | `lang`、`bright`、`t_dim`、`t_off`、`raise`、`flip_act`、`chg_mode`、`chg_ma`、`cards`（卡片顺序位图）、`last_ip`、`boot_cnt`、`wdt_reason`、`asset_slot`（0 / 1，首次为 0） | 设置与资源 bank 选择 |
| `prov` | `wifi0..2`（ssid、pass）、`hubs`（JSON 列表）、`dev_id`、`dev_tok`、`hub_id`、可选 `cf_access`（PROTOCOL §4.3.2） | 配网与配对；Access 凭据按 origin 限定发送 |
| `macro` | `m0..m11` | 宏定义 |
| `vault`（独立分区） | 见 §9.3 | 加密存储 |

### 6.16 OTA
1. Hub 下发 `ota.offer`，包含版本、URL、sha256 和文件大小。
2. 设备在快捷面板和设置中提示有新版本。用户确认后，并且设备插着电或电量 > 50% 时，才通过 HTTPS 或 HTTP 从 Hub 下载。
3. 用 `esp_http_client` 下载、`esp_ota_*` 写入非活动分区，校验 sha256 后选择启动分区并重启。只访问配对 Hub 的 origin，拒绝重定向。ESP-IDF 5.5.6 的 `esp_https_ota` 会在其连接循环中自行跟随重定向，即使 HTTP client 禁用了自动跳转；经维护者确认改用底层 OTA API，避免设备令牌跨 origin 发送。
4. 新固件启动后 60s 内与 Hub 握手成功，就调用 `esp_ota_mark_app_valid_cancel_rollback()` 确认这个版本；否则自动回滚到旧分区。
- 资源包 nukapack 单独升级，只写 `assets` 内的非活动 bank，单包不得超过 3 MiB。完整下载后验证 sha256 和资源 ABI，成功才原子提交 `sys.asset_slot` 并重新映射；校验失败或写入中断不切换，旧 bank 保留。不得擦写 UI 当前映射的 bank。

### 6.17 日志与诊断
- `ESP_LOG` 同时输出到 CDC 诊断通道和 `storage` 分区中的 64KB 环形日志。
- `diag.info` 返回：固件版本、运行时长、内部堆的当前剩余与历史最低值、PSRAM 剩余、各任务栈的水位、Wi-Fi RSSI、最近一次重启的原因。
- 性能 HUD（在设置中打开）：屏幕右上角显示 FPS、CPU 占用和内部堆，用于验收测试。

---

## 7. Hub 详细设计

### 7.1 角色与 CLI
包名为 `nuka-hub`，提供两个命令行入口：
- `nuka`：主程序；
- `nuka-hook`：供 AI CLI 调用的 hook 转发器。

| 命令 | 说明 |
|---|---|
| `nuka init` | 交互式向导：生成 `config.toml` 和管理员令牌，并逐项询问要启用哪些集成 |
| `nuka hub [--config PATH] [--demo]` | 以 Hub 角色运行。`--demo` 会生成假数据，方便在没有真实数据源时开发固件和 Web |
| `nuka agent --hub URL --token T` | 以 Agent 角色运行，只采集本机数据并上报给 Hub |
| `nuka doctor` | 检查各插件是否可用、权限是否足够、端口是否被占用、mDNS 与防火墙状况，并给出文档链接 |
| `nuka secret set\|get\|rm NAME` | 管理密钥（写入系统钥匙串） |
| `nuka token create --scope device\|agent\|hook\|notify\|admin [--name]`、`token list`、`token revoke` | 管理令牌 |
| `nuka pair` | 通过 USB 给设备配网并完成配对 |
| `nuka service install\|uninstall\|status [--role hub\|agent]` | 注册、删除或查看开机自启服务 |
| `nuka hooks install\|uninstall claude\|codex\|gemini` | 为 AI CLI 写入 hook 配置：先备份原文件，再合并写入，不覆盖已有内容 |
| `nuka tunnel setup` | 生成 cloudflared 配置，并给出 Cloudflare Access 的配置步骤 |
| `nuka vault import\|export` | 经 USB-CDC 与设备交换 vault 数据（§6.14） |
| `nuka sim-device [--count N]` | 设备模拟器：按 PROTOCOL 接入 Hub，打印收到的视图模型，可以模拟审批操作，供后端独立开发 |
| `nuka ota publish FILE` | 把固件放进 OTA 目录，并通知在线的设备 |

### 7.2 运行时结构
- Python ≥ 3.11，`asyncio` 单进程。HTTP 和 WS 用 FastAPI + uvicorn；所有 I/O 都是异步的（`httpx.AsyncClient`）；阻塞型的采集（psutil、NVML、pywinrt）放进线程池执行。
- **核心对象**：
  | 对象 | 职责 |
  |---|---|
  | `Config` | pydantic 模型，schema 见 CONFIG.md |
  | `Secrets` | 访问系统钥匙串 |
  | `Bus` | 进程内的发布 / 订阅，主题包括 `hosts`、`servers`、`ai`、`agents`、`notify`、`media`、`weather`、`devices` |
  | `Scheduler` | 按各插件声明的间隔调度采集，带随机抖动和超时；单个插件超时或出错不影响其他插件，并会更新它的健康状态 |
  | `StateStore` | 每个主题的最新值 + 版本号 |
  | `History` | SQLite 历史数据，见 §7.12 |
  | `DeviceHub` | 管理设备的 WS 连接，按订阅主题和亮屏状态节流推送 |
- **插件健康状态**：
  - `ok`：正常；
  - `degraded`：部分字段缺失；
  - `disabled`：依赖缺失，附带原因；
  - `error`：附带最近一次错误和下次重试时间。

  `nuka doctor`、Web 设置页和 `GET /api/v1/plugins` 都能看到这些状态。

### 7.3 配置与密钥
- 配置文件的位置（由 `platformdirs` 决定）：
  | 平台 | 路径 |
  |---|---|
  | Windows | `%APPDATA%\nuka\config.toml` |
  | macOS | `~/Library/Application Support/nuka/config.toml` |
  | Linux | `~/.config/nuka/config.toml` |
  | Docker | `/config/config.toml` |
- 数据目录（SQLite、OTA 固件、令牌哈希）位于同级的 data 目录中。
- 密钥在配置中按名称引用，例如 `key_secret = "ai.cpamp.main"`。读取顺序：
  1. 环境变量 `NUKA_SECRET_<NAME>`（名称转大写，点号换成下划线）；
  2. 系统钥匙串（`keyring`，service 名为 `nuka-hub`）；
  3. `secrets.toml`（文件权限必须为 600，否则拒绝读取）。
- 配置支持热重载：文件变更后，只重新加载受影响的插件；修改监听地址这类配置需要重启。
- 详见 CONFIG.md。

### 7.4 本机采集器（collectors）

#### 7.4.1 接口
```python
class Collector(Protocol):
    name: str                       # 例如 "psutil"、"nvml"
    platforms: set[str]             # {"windows","darwin","linux"}
    interval_s: float
    async def probe(self) -> Health          # 检查依赖与权限，决定是否启用
    async def sample(self) -> HostPartial     # 返回部分主机指标，由 HostAggregator 合并
```
`HostAggregator` 把各采集器返回的部分结果合并成一个 `Host` 模型（字段定义见 PROTOCOL §3.4.2）。合并**以字段为粒度**：如果先注册的采集器已经提供了更精确的字段，后注册的采集器不能覆盖它。优先级可以在 CONFIG 中调整。

#### 7.4.2 平台矩阵
| 指标 | Windows | macOS | Linux | 依赖 / 权限 |
|---|---|---|---|---|
| CPU 负载与频率、内存、Swap、磁盘用量与 I/O、网速、本机电池 | psutil | psutil | psutil | 无 |
| NVIDIA GPU：负载、温度、显存、功耗、风扇、频率 | `nvidia-ml-py` | — | `nvidia-ml-py` | 需要已安装 NVIDIA 驱动 |
| AMD / Intel GPU | LibreHardwareMonitor | — | sysfs：`/sys/class/drm/card*/device/{gpu_busy_percent,mem_info_vram_*,hwmon}` | Linux 上不需要 root |
| CPU 温度、主板、风扇 | LibreHardwareMonitor 的 Web 服务（`/data.json`，地址可配置） | `macmon`（Apple Silicon，无需 sudo）；回退到 `powermetrics`（需要 sudo，默认不启用） | `psutil.sensors_temperatures()` / `sensors_fans()`（依赖 lm-sensors 和 hwmon 驱动） | Windows 上 LHM 需要以管理员身份运行 |
| Apple Silicon 的 CPU / GPU 功耗与负载 | — | `macmon` | — | 可选 |

- LHM 的 `data.json` 是一棵嵌套树：遍历 `Children[]`，通过 `SensorId`（例如 `/amdcpu/0/temperature/2`）匹配传感器。匹配规则做成可配置项，并内置 Intel / AMD 常见型号的默认规则。
- 采集频率：快速变化的指标 1s，温度与风扇 2s，磁盘用量 60s。
- 多机：Agent 角色运行同样的采集器，经 `/ws/agent` 上报 `host.sample`（PROTOCOL §6）。

### 7.5 服务器数据源与告警（FR-9、FR-10）

#### 7.5.1 数据源插件接口
```python
class ServerSource(Protocol):
    type: str                                  # "serverstatus" | "uptime_kuma" | "beszel" | "netdata" | "node_exporter" | "generic_json"
    async def probe(self) -> Health
    async def fetch(self) -> list[Server]      # 统一模型见 PROTOCOL §3.4.3
```
每个数据源在配置中对应一个 `[[servers.source]]` 块，包含 `type`、`name`、`url`、`interval`，以及可选的 `auth_secret` 和 `options`。不同数据源中的同一台服务器，按 `servers.merge` 规则（按名称或别名匹配）合并。

#### 7.5.2 内置适配器
| type | 数据来源 | 映射要点 |
|---|---|---|
| `serverstatus` | cppla/ServerStatus 风格的 `GET {url}`（通常是 `/json/stats.json`） | `servers[]` 中的 `name/alias`、`online4\|online6`、`cpu`、`memory_used/total`、`hdd_used/total`、`network_rx/tx`、`load_1`、`uptime` |
| `uptime_kuma` | 公开状态页 API：`/api/status-page/{slug}` + `/api/status-page/heartbeat/{slug}` | 只有在线状态、延迟、可用率，资源指标留空 |
| `beszel` | Beszel Hub（基于 PocketBase）的 API：`/api/collections/systems/records`，用账号或令牌鉴权 | `info.cpu/mp/dp/b`（CPU、内存 %、磁盘 %、带宽）、`status` |
| `netdata` | 每个节点的 `/api/v1/info` + `/api/v1/data?chart=system.cpu\|system.ram\|disk_space._\|system.net&points=1&after=-5`；v1 不支持 Netdata Cloud | 每个 URL 对应一台服务器 |
| `node_exporter` | Prometheus 文本格式：直接抓取 `/metrics`，或向 Prometheus 服务器执行 PromQL 查询（`options.mode = "exporter" \| "prometheus"`） | 由 `node_cpu_seconds_total` 差分得到 CPU %；内存用 `node_memory_MemAvailable_bytes` 等 |
| `generic_json` | 任意 JSON：`options.items` 用 JSONPath 指向服务器数组，`options.fields` 为每个统一字段指定 JSONPath 和单位换算 | 用于没有内置适配器的工具 |

> **实现注意**：各上游的 API 字段会随版本变化。每个适配器都必须带一份**用假数据录制的 fixture** 和契约测试，并在文档中注明测试过的上游版本。

#### 7.5.3 推送入口 `/hook/server`
- 鉴权：使用 `notify` 作用域的令牌。public 区域永远返回 404；lan 需要 `security.allow_lan_hooks=true`。公网通用通知使用 `/api/v1/notify`，与 AGENTS §6 和 PROTOCOL §5.5 一致。
- 请求体见 PROTOCOL §5.5。限流：每个令牌每分钟 30 次，允许突发 10 次。
- 推送的事件进入通知管道（§7.9）；如果带有 `server` 字段，还会更新这台服务器的告警角标。

#### 7.5.4 规则告警
```toml
[[alerts.rule]]
name = "server-offline"
when = "server.online == false"
for  = "60s"
level = "critical"
```
- 内置规则（默认开启，可以单独关闭）：
  | 规则 | 级别 |
  |---|---|
  | 服务器离线超过 60s | critical |
  | CPU > 90% 持续 5 分钟 | warn |
  | 内存 > 90% 持续 5 分钟 | warn |
  | 磁盘 > 90% | warn |
  | AI 账号配额耗尽 | warn |
  | AI 账号需要重新登录 | critical |
- 表达式只支持白名单中的字段，以及比较和逻辑运算符。需要自己写一个小型解析器，**禁止使用 `eval`**。
- 去抖：条件持续满足 `for` 指定的时长才触发；恢复时发一次"已恢复"（info 级别）；同一规则、同一对象触发后，30 分钟内不重复通知（间隔可通过 `repeat` 配置）。

### 7.6 AI 配额（FR-8）

#### 7.6.1 背景知识（来自调研；实施时以上游源码为准）
- **CLIProxyAPI（CPA）**：
  - 管理 API 路径为 `/v0/management/*`；v8 另外提供了 `/v8/management/*` 的映射。
  - 鉴权头为 `Authorization: Bearer <management key>` 或 `X-Management-Key`；远程访问需要服务端开启 `allow-remote`。
  - **同一 IP 认证失败 5 次，会被封禁 30 分钟。**
  - 旧的 `/usage` 统计接口已经移除，改为 usage queue，而读取 usage queue 是破坏性的出队操作。
- **CPA Manager Plus（CPAMP）**：独立的 Manager Server，持有 CPA 的管理密钥，用自己的管理员密钥做鉴权，并把 `/v0/management/*` 透传给 CPA；另外还提供配额快照、header 快照、聚合用量等接口。
- **配额数据分三个层级**：
  - **L1 被动**：`GET /v0/management/auth-files` 中每个账号的 `status`、`unavailable`、`next_retry_after`、`id_token.plan_type` 和 `quota.signals`。`quota.signals` 是 CPA 记录下来的最新上游限流响应头，例如 Codex 的 `x-codex-primary-used-percent`、`x-codex-primary-reset-at`，Claude 的 `anthropic-ratelimit-unified-*`。
  - **L2 快照（仅 CPAMP 提供）**：`POST /v0/management/quota-snapshots/query`、`GET /v0/management/monitoring/header-snapshots`、`GET /v0/management/usage`。
  - **L3 主动**：调用 `POST /v0/management/api-call`，带上 `auth_index`，并在 header 中用 `$TOKEN$` 占位，由服务端替换成该账号的 OAuth 令牌，从而访问上游的用量接口：
    - Codex：`GET https://chatgpt.com/backend-api/wham/usage`
    - Claude：`GET https://api.anthropic.com/api/oauth/usage`
    - Antigravity：`POST …/v1internal:retrieveUserQuotaSummary`

#### 7.6.2 统一领域模型（下发给设备的形状见 PROTOCOL §3.4.4）
```python
class QuotaWindow(BaseModel):
    kind: Literal["5h","daily","weekly","monthly","other"]
    label: str | None             # 例如 "Opus 周"
    used_pct: float               # 0..100
    reset_at: datetime | None
    source: Literal["passive","snapshot","active"]
    observed_at: datetime
    stale: bool                   # observed_at 早于 stale_after（默认 30 分钟）

class AiAccount(BaseModel):
    id: str                       # 由 provider 名和 auth_index 派生的稳定 ID，不包含邮箱
    provider_kind: Literal["codex","claude","antigravity","kimi","other"]
    label: str                    # 默认为脱敏后的邮箱，例如 a***@example.com；可在配置中重命名
    plan: str | None
    status: Literal["ok","cooldown","exhausted","reauth","disabled","unknown"]
    recover_at: datetime | None
    windows: list[QuotaWindow]
    credits: Credits | None       # 余额与币种（上游提供时才有）
    today: UsageToday | None      # 请求数、token 数、失败数（仅 CPAMP 提供）
```

#### 7.6.3 提供者插件
| type | 配置 | 使用的接口 |
|---|---|---|
| `cpamp` | `base_url`、`key_secret`（CPAMP 管理员密钥） | L1（经 CPAMP 透传）+ L2 + L3（经 CPAMP 透传） |
| `cpa` | `base_url`、`key_secret`（CPA 管理密钥） | L1 + L3 |

调度规则：
- L1 每隔 `passive_interval`（默认 60s）请求一次。
- L2 每隔 `snapshot_interval`（默认 120s）请求一次。
- L3 每个账号至少间隔 `active_min_interval`（默认 600s），并且只在两种情况下触发：该账号的所有窗口都已 `stale`；或者用户在 Web 上手动刷新（手动刷新全局限流为每分钟 1 次）。
- 全局上限：每个 provider 每分钟最多 20 个请求。

#### 7.6.4 L3 请求配方
配方是白名单，写成数据文件 `ai/recipes.toml`，上游变化时可以单独更新。

| 账号类型 | 方法与 URL | 额外的 header | 解析的字段 |
|---|---|---|---|
| codex | `GET https://chatgpt.com/backend-api/wham/usage` | `Authorization: Bearer $TOKEN$`、`Chatgpt-Account-Id: {id_token.chatgpt_account_id}`、`User-Agent` | `rate_limit.primary_window` / `secondary_window` 中的 `used_percent`、`reset_at`、`limit_window_seconds`（18000 对应 5h，604800 对应 weekly）；`credits.balance` |
| claude | `GET https://api.anthropic.com/api/oauth/usage` | `Authorization: Bearer $TOKEN$`、`anthropic-beta: oauth-2025-04-20` | `five_hour`、`seven_day`、`seven_day_opus`、`seven_day_sonnet` 中的 `utilization` 和 `resets_at`；`extra_usage` |
| antigravity | `POST https://daily-cloudcode-pa.googleapis.com/v1internal:retrieveUserQuotaSummary`，body 为 `{"project": "{project_id}"}` | `Authorization: Bearer $TOKEN$` | `groups[].buckets[]` 中的 `remainingFraction` 和 `resetTime` |

- **只允许**访问配方表中列出的 URL。`api-call` 的请求体由配方渲染生成，不接受任何外部输入拼接进来。
- 上游返回的原始 body 不入库、不下发，只保存解析出来的字段。

#### 7.6.5 熔断与安全（硬性）
1. 收到 401 / 403：provider 立即进入 `auth_failed` 状态，**停止所有请求**，直到配置或密钥发生变更，或用户在 Web 上点击"重试"（重试也只发 1 个请求）。这是为了避免触发 CPA 的封禁（失败 5 次封 30 分钟）。
2. 收到 429、5xx 或超时：指数退避，1 → 2 → 4 → … → 最长 30 分钟。
3. **禁止**调用 `/usage-queue`。它是破坏性出队，会抢走 CPAMP 的统计数据。
4. **禁止**调用任何写操作类的管理接口（修改配置、上传或删除认证文件等）。HTTP 客户端只放行 GET 请求，以及白名单中的 POST 路径（`api-call`、`quota-snapshots/query`）。
5. 日志中的密钥、令牌和邮箱都要脱敏。
6. 测试**禁止**访问真实服务，只能使用假数据 fixture。

### 7.7 Agent hooks（FR-11）

#### 7.7.1 会话状态模型
```python
class AgentSession(BaseModel):
    sid: str                      # 会话 ID（hash 后截断）
    source: Literal["claude","codex","gemini"]
    host: str                     # 产生事件的机器（Hub 本机或 Agent 名）
    project: str | None           # 默认为 cwd 的 basename；由配置 show_project = "basename"|"hidden" 控制
    state: Literal["idle","thinking","tool","waiting_permission","done","error"]
    tool: str | None              # 例如 "Bash"
    since: datetime
    last_message: str | None      # 截断到 140 个字符；可以配置为不收集
```

Claude Code 事件对应的状态转换：
| 事件 | 新状态 |
|---|---|
| `SessionStart` | idle |
| `UserPromptSubmit` | thinking |
| `PreToolUse` | tool（记录 tool_name） |
| `PostToolUse` | thinking |
| `PermissionRequest` | waiting_permission |
| `Notification`（`idle_prompt`） | idle |
| `Stop` | done（30s 后转为 idle） |
| `StopFailure` | error |
| `SessionEnd` | 删除会话 |

会话超过 10 分钟没有任何事件时转为 idle，超过 2 小时没有事件则被清理。

#### 7.7.2 Claude Code（原生 HTTP hook）
- **安装**：`nuka hooks install claude` 在 `~/.claude/settings.json` 的 `hooks` 中合并写入 `PermissionRequest`、`Notification`、`Stop`、`StopFailure`、`PreToolUse`、`PostToolUse`、`UserPromptSubmit`、`SessionStart`、`SessionEnd` 这些事件。
  - 全部为 `"type": "http"`，URL 指向 `http://127.0.0.1:<port>/hook/claude`。
  - 如果 Hub 在另一台机器上，URL 改为指向 Hub 的地址，并使用 `hook` 令牌和 `allowedEnvVars`。
- **非审批事件**：Hub 必须在 **50ms 以内**返回空的 2xx，绝不能拖慢 Claude Code。
- **审批流程（PermissionRequest）**：
  1. Hub 生成一个 `rid`，并生成摘要：tool_name 加上截断后的 `tool_input`，例如 Bash 显示命令，Edit 显示文件名。然后以高优先级向所有在线设备推送 `perm.req`。
  2. 如果没有任何在线设备，**立即**返回空的 2xx，Claude Code 会回退到终端询问。
  3. 等待设备回复 `perm.decide`，或者 Web 上的审批。Web 审批只有在 `security.web_approve_permissions = true` 且请求来自 local / lan 区域时才允许。最长等待 `agents.permission_timeout`（默认 60s），这个值必须小于 hook 配置中的 timeout（安装器会把后者设为 90s）。
  4. 根据结果返回：
     - 允许：`{"hookSpecificOutput":{"hookEventName":"PermissionRequest","decision":{"behavior":"allow"}}}`
     - 拒绝：`behavior: "deny"`，并附上 `message: "Denied on nuka-esp32"`
     - 超时：空的 2xx，回退到终端询问
  5. 向设备推送 `perm.done`（注明由谁处理：device / web / timeout / terminal），并写入审计日志（§7.12）。
  6. 同一个 `rid` 只接受第一个决定。
- **安全**：`/hook/*` 默认只接受 local 区域（回环地址）的请求。跨机器使用时，需要在配置中显式允许 lan 区域，并携带 `hook` 令牌。**永远不允许 public 区域访问。**

#### 7.7.3 Codex 与 Gemini CLI
- 这两个 CLI 的 hook 都是命令类型。安装器写入的命令是 `nuka-hook codex <event>` 或 `nuka-hook gemini <event>`。
  - `nuka-hook` 是随包安装的 Python 小程序：从 stdin 读取 JSON，POST 到 `/hook/codex` 或 `/hook/gemini`（超时 2s）。
  - **失败时静默退出，返回码为 0**，不影响 CLI 本身。
- v1 中 Codex 和 Gemini 只做**状态显示**。等确认了两者的 hook 输出格式之后，再在 v1.1 中加入审批功能（任务卡 HUB-19 中有对应的评估项）。
- 配置位置：
  - Codex：`~/.codex/hooks.json`，或 `config.toml` 中的 `[hooks]`。安装后需要用户在 Codex 中运行 `/hooks` 授信，**必须写进引导**。
  - Gemini：`~/.gemini/settings.json`，其中 `timeout` 的单位是毫秒。
- 事件映射：
  - Codex：`SessionStart`、`PreToolUse`、`PostToolUse`、`Stop`、`SessionEnd`
  - Gemini：`SessionStart`、`BeforeTool`、`AfterTool`、`AfterAgent`、`SessionEnd`、`Notification`

### 7.8 媒体、天气、专注模式
| 能力 | Windows | macOS | Linux |
|---|---|---|---|
| 正在播放 + 控制 | `winrt-Windows.Media.Control`（SMTC） | `nowplaying-cli`（可选依赖，用 Homebrew 安装） | MPRIS over D-Bus（`dbus-next`） |
| 封面 | 读取缩略图流 → 用 Pillow 缩放到 160×160 → 编码为 baseline JPEG（q=80） | 同左 | 读取 `mpris:artUrl` |
| 番茄钟联动系统专注模式 | v1 不做，只做设备侧的勿扰 | 同左 | 同左 |

- **天气**：默认提供者为 **Open-Meteo**（免费、无需密钥，适合开源项目）。位置在配置中用 `lat` / `lon` 或城市名指定；城市名经 Open-Meteo 地理编码 API 解析一次后缓存。每 15 分钟更新一次。天气也是插件，以后可以加其他提供者。
- **媒体只对所在机器有效**：Hub 本机的媒体由 Hub 采集；其他机器的媒体由该机器上的 Agent 角色采集并上报（`media.state`）。控制命令按 `source` 路由回对应的机器。

### 7.9 通知管道
```python
class Notification(BaseModel):
    nid: str
    source: str
    level: Literal["info","warn","critical"]
    title: str
    body: str | None
    ts: datetime
    dedupe_key: str | None        # 同一个 key 在去重窗口内合并
    actions: list[Action] = []    # v1 只有 dismiss；审批走单独的 perm 流程
    ttl_s: int = 86400
```
- **来源**：规则告警、`/hook/server`、`/api/v1/notify`、AI 账号状态变化、Agent 任务完成、设备电量（只发给 Web，不发回设备本身）。
- **设备低电量**：仅 `bat.src=bat` 且 `chg=none` 时，跨入 <20% 发 warn、<10% 发 critical，与 §6.10.5 的现有保护阈值一致；切换 USB、充电 / 充满或恢复 ≥20% 后重置，不向设备回推。
- **勿扰时段**：由 `notify.quiet_hours` 配置，期间只推送 critical 级别的通知。
- **去重**：同一个 `dedupe_key` 在 5 分钟内只保留最新的一条。
- **存储**：最近 500 条存在 SQLite 中；设备只接收最新的 20 条。

### 7.10 设备管理
- 第二阶段以 USB 即插即配为主要流程：本机 Hub 自动发现、扫描 Wi-Fi、网页配置、设备 BOOT 按住1s授权；原命令行与 SoftAP 保留。联网后 Wi-Fi 密码只走 USB 串口，其余设备设置可经认证网页下发（PLAN-v2 §2.7）。
- 每台设备有一个 `device` 令牌，由 `nuka token create --scope device` 创建，或在 `pair` 时自动创建。令牌以 SHA-256 哈希形式保存，明文只显示一次。
- 设备注册表记录：`dev_id`、名称、固件版本、最后在线时间、最近一次的 `state`（电量等）和 IP。
- 支持多台设备同时连接，每台设备可以有自己的订阅主题和推送频率。
- **OTA**：`ota.dir` 中按版本存放 `nuka-esp32-<ver>.bin` 和 `nukapack-<ver>.bin`，并附带 `manifest.json`（版本、sha256、大小、发行说明）。设备通过 `GET /api/v1/ota/{file}`（需要 device 令牌）下载。

### 7.11 网络区域与鉴权（FR-21）

**区域判定**：
| 区域 | 判定条件 |
|---|---|
| `local` | 来源是回环地址，**并且**请求中不带任何 `security.public_detect_headers` 中列出的头（默认为 `Cf-Connecting-Ip`、`Cf-Ray`、`X-Forwarded-For`） |
| `lan` | 来源 IP（或可信代理给出的真实 IP）属于 `security.lan_cidrs`。默认包括 RFC1918 私有网段、`100.64.0.0/10`（Tailscale）、`fc00::/7`、`fe80::/10` |
| `public` | 其他所有情况。**经 cloudflared 进来的请求，即使 TCP 来源是回环地址，也因为带有 CF 头而被判为 public** |

可信代理：只有当请求来源属于 `security.trusted_proxies` 时，才采信 `X-Forwarded-For` 中的真实 IP。

**接口访问矩阵**（✓ 允许，✗ 禁止；括号内为需要的令牌作用域）：
| 接口 | local | lan | public |
|---|---|---|---|
| `GET /`（Web 页面）、`/api/v1/*` 只读接口 | ✓（admin 会话） | ✓（admin 会话） | ✓（admin 会话；还可以要求 CF Access JWT） |
| `POST /api/v1/ai/refresh` | ✓ | ✓ | ✓（admin，限流） |
| `POST /api/v1/perm/{rid}`（Web 审批） | 配置开启时 ✓ | 配置开启时 ✓ | ✗ |
| `/hook/claude\|codex\|gemini` | ✓（hook 令牌可选） | 配置开启时 ✓（需要 hook 令牌） | ✗ |
| `GET/POST /hook/server` | ✓（notify） | 配置开启时 ✓（notify） | ✗ |
| `POST /api/v1/notify` | ✓（notify） | ✓（notify） | ✓（notify，限流） |
| `/ws/device` | ✓（device） | ✓（device） | ✓（device；还可以要求 CF Access Service Token） |
| `/ws/agent` | ✓（agent） | ✓（agent） | ✓（agent；还可以要求 CF Access Service Token） |
| `GET/PUT /api/v1/config/raw`、`GET /api/v1/secrets`、`PUT/DELETE /api/v1/secrets/{name}` | ✓（admin） | ✓（admin） | 默认✗；D30远程配置显式开启后，需要Access身份与admin会话，写入还需CSRF，详见PROTOCOL §5.8 |
| 配对接口 `/api/v1/pair/*` | ✓ | ✓ | ✗ |
| `GET /api/v1/health` | ✓ | ✓ | ✓（不需要令牌，只返回 `{ok, ver, proto}`） |

- **Web 登录**：用户输入 admin 令牌，换取一个 HttpOnly、SameSite=Strict 的会话 cookie（有效期 30 天，可以撤销）。所有会改变状态的请求都需要带 CSRF token。
- **Cloudflare Access**：配置 `security.cf_access.enabled = true` 以及 `team_domain` 和 `aud` 之后，public 区域的请求必须带有效的 `Cf-Access-Jwt-Assertion`（用 Cloudflare 公钥校验）。这是在 admin 会话之外**额外**的一道校验。

### 7.12 存储（SQLite，WAL 模式）
助手完整对话与长期记忆永久保存在 Hub 数据目录，最近20轮只约束模型上下文；可关闭、导出或清空。原始录音默认不持久保存。上传附件与在线一致性备份遵循 PLAN-v2 §2.8，Vault 数据不进入 Hub。

| 表 | 字段 | 保留时间 |
|---|---|---|
| `metric_points` | host_id, ts, key, value | 原始数据 24h；5 分钟粒度的降采样数据保留 30 天 |
| `server_points` | server_id, ts, cpu, mem, disk, online | 同上 |
| `device_battery` | dev_id, ts, pct, mv, charging, src | 30 天 |
| `ai_windows` | account_id, ts, kind, used_pct, reset_at, source | 30 天 |
| `notifications` | nid, ts, level, source, title, body, dedupe_key | 最近 500 条 |
| `audit` | ts, kind（perm / token / pair / ota）, actor, detail | 180 天 |
| `tokens` | id, scope, name, sha256, created, last_used, revoked | 永久 |
| `devices` | dev_id, name, fw, last_seen, last_state_json | 永久 |

降采样和过期清理由后台任务每小时执行一次。

### 7.13 Web 管理页（简单的配置与管理）
- **定位**：Web 只是 Hub 的**配置与管理入口**，不是仪表盘。数据的主要展示界面是设备屏幕（LVGL UI）。Web 不做图表，不做视觉打磨，不做动效。
- **归属**：BE 轨道负责（PLAN HUB-31），与后端逻辑一起交付。
- **技术**：
  - 服务端渲染的简单 HTML，或者原生 JS 加 `fetch`；不需要构建工具，不引入前端框架和图表库。
  - 视觉要求是**美观、能看即可**：整洁的卡片和表格布局，统一的间距，沿用黑 / 牛油果绿主题色（颜色变量来自生成的 `theme.css`），只提供深色主题。只用一份很小的 CSS，不做额外的打磨。
  - 支持中英文；在手机上能正常使用即可。
  - 页面通过轮询 `GET /api/v1/state` 刷新（间隔 5s），不使用 WebSocket。
- **页面**：
  | 页面 | 内容 |
  |---|---|
  | 登录 | 输入 admin 令牌 |
  | 状态 | 用纯文本表格显示：各主机、服务器、AI 账号、Agent 会话、设备的当前状态；插件健康状态（内容与 `doctor` 的输出一致，并附上安装引导的链接） |
  | 设备 | 设备列表（名称、固件版本、电量、最后在线时间）；改名、删除；输入配对码完成 SoftAP 配对；OTA 推送 |
  | 配置 | 第二阶段采用由配置 schema 生成的表单，TOML 保留在高级入口；保存校验、保留注释并热重载。密钥只写不读。默认仅 local / lan；远程配置需显式启用并同时通过 Cloudflare Access 与 admin 会话 / CSRF（PLAN-v2 §2.8） |
  | 助手 | 角色、模型、音色、工具、测试对话、永久记录和记忆管理，见 PLAN-v2 §2.6、§2.8 |
  | 令牌 | 创建（明文只显示一次）、列出、撤销 |
  | 审批 | 待审批的 Agent 权限请求，带允许和拒绝按钮。只有在配置开启、并且请求来自 local / lan 区域时才显示这个页面 |
  | 通知 | 最近的通知列表；发送测试通知 |
- **不做**：历史曲线、深浅主题切换、复杂的响应式布局、Lighthouse 指标。

### 7.14 打包与发布
- **PyPI 包** `nuka-hub`（名称需在发布前确认可用）。依赖分组如下：
  | 分组 | 内容 |
  |---|---|
  | 核心 | fastapi、uvicorn、httpx、pydantic、platformdirs、keyring、zeroconf、psutil、pyserial |
  | `[nvidia]` | nvidia-ml-py |
  | `[media-win]` | winrt-Windows.Media.Control |
  | `[media-linux]` | dbus-next |
  | `[images]` | Pillow |
  | `[all]` | 以上全部 |

  安装方式：`uv tool install "nuka-hub[all]"`，或者用 `pipx install`。
- **Docker 镜像** `ghcr.io/<owner>/nuka-hub`（linux/amd64 + arm64），主要用于 D4 部署形态。容器内读不到宿主机硬件时，`doctor` 会提示改为在宿主机上运行 Agent 角色。
- **`nuka service install`** 在各平台的实现：
  | 平台 | 实现 |
  |---|---|
  | Windows | 计划任务：用户登录时启动，失败后自动重启。`--elevated` 选项用于 LHM 场景，创建"以最高权限运行"的任务 |
  | macOS | `~/Library/LaunchAgents/io.nuka.hub.plist`（`KeepAlive`） |
  | Linux | `~/.config/systemd/user/nuka-hub.service`，并提示执行 `loginctl enable-linger`。`--system` 选项生成系统级 unit |
- **版本号**：Hub 和固件各自遵循 semver。协议版本 `proto` 是一个整数，Hub 同时兼容版本 N 和 N−1。

---

## 8. 非功能需求（验收数值）

| 编号 | 指标 | 目标 | 测量方法 |
|---|---|---|---|
| NFR-1.1 | 运行时内部 RAM 剩余 | ≥ 100 KB | 浏览完所有卡片后读取 `diag.info` |
| NFR-1.2 | 内部 RAM 历史最低值 | ≥ 60 KB | 24h 浸泡测试 |
| NFR-1.3 | PSRAM 占用 | ≤ 2 MB | 同上 |
| NFR-1.4 | app 镜像 / 单个资源包 | ≤ 3 MB / ≤ 3 MiB（assets 合计 6 MiB） | 看构建产物大小 |
| NFR-2.1 | 局部动画（圆环、数值、精灵） | ≥ 55 fps | 性能 HUD |
| NFR-2.2 | 卡片轮播滑动 | ≥ 30 fps 且没有明显撕裂（否则改用 B 方案） | 性能 HUD + 慢动作录像 |
| NFR-2.3 | 从触摸到第一帧响应 | ≤ 50 ms | GPIO 打点后用示波器或逻辑分析仪测量（也可用 240fps 录像） |
| NFR-2.4 | 亮屏空闲时（数据 1Hz 刷新）的 CPU 占用 | ≤ 5% | FreeRTOS 运行时统计 |
| NFR-3.1 | 亮屏功耗（50% 亮度，表盘） | 实测并记录（预计 60–120 mA） | USB 电流表或电池电压曲线 |
| NFR-3.2 | 熄屏 + Wi-Fi 保持连接 | < 25 mA（目标值，实测后修订） | 同上 |
| NFR-3.3 | 关机 | < 50 μA | 万用表串接在电池回路中 |
| NFR-3.4 | 从熄屏到可交互的唤醒时间 | ≤ 300 ms | 录像 |
| NFR-4.1 | Hub 重启后设备自动重连 | 亮屏或连续熄屏≤30分钟时 ≤30s；连续熄屏>30分钟后退避上限120s，亮屏立即恢复快速重连 | 手动测试 |
| NFR-4.2 | 24h 浸泡测试 | 不重启，内存没有下降趋势 | 日志 |
| NFR-4.3 | OTA 失败回滚 | 100% 成功回滚 | 故意推送一个坏固件 |
| NFR-4.4 | Hub 对非审批 hook 的响应时间 | p99 ≤ 50 ms | pytest 基准测试 |
| NFR-4.5 | Hub 常驻内存 | RSS ≤ 150 MB（开启全部插件） | 运行 1h 后测量 |
| NFR-4.6 | 冷启动：从按下 PWR 到表盘可见 | ≤ 2.5 s（目标值，实测后修订） | 录像 |
| NFR-5.x | 安全要求 | 见 §9 | 安全评审（INT-03） |
| NFR-6.1 | 零硬编码 | 仓库中搜不到个人部署值 | CI 检查脚本（使用通用模式；个人关键词黑名单只放在 `local/`） |
| NFR-7.1 | 测试 | 只覆盖 §10 关键测试清单中的项；不设覆盖率指标 | 评审 |

---

## 9. 安全设计

### 9.1 威胁模型（摘要）
| 资产 | 威胁 | 缓解措施 |
|---|---|---|
| AI 管理密钥（CPA / CPAMP） | 泄露后，攻击者可以控制 CPA，并以账号的 OAuth 身份访问上游 | 只存在 Hub 的系统钥匙串里；永不下发到设备或 Web；日志脱敏；HTTP 客户端只放行白名单路径 |
| Agent 审批 | 远程攻击者伪造"允许"，让 Agent 执行任意命令 | `/hook/*` 和 Web 审批**禁止**公网访问；设备上需要按住确认；`rid` 只能使用一次；记录审计日志 |
| 设备令牌 | 被盗后可以冒充设备读取监控数据 | 设备令牌只能读取视图模型、上报设备状态；可以单独撤销；建议使用 TLS |
| vault 数据 | Flash 被整片读出、网络嗅探、恶意宏 | 见 §9.3 |
| Hub Web | 公网暴力破解、CSRF | admin 令牌为 32 字节随机值；会话 cookie；CSRF token；Cloudflare Access；登录限流 |

### 9.2 Hub 安全要求
1. 区域判定与接口访问矩阵见 §7.11，由中间件统一实现，并配有单元测试。测试必须覆盖"经 cloudflared 进来的回环请求要判为 public"这类情况。
2. 令牌只保存哈希；CLI 生成令牌后只显示一次；每个令牌都有作用域；记录 `last_used`。
3. 依赖版本锁定在 `uv.lock` 中，CI 中运行 `pip-audit`。
4. 默认监听 `0.0.0.0` 是为了让设备能连上；`doctor` 会检查操作系统防火墙，并提示只对私有网络开放端口。

### 9.3 NukaVault 安全设计
- **密钥层级**：
  ```
  K_hw    = HMAC_eFuse(KEY_BLOCK_n, "nukavault-v1")         // ESP32-S3 HMAC 外设；eFuse 中的密钥软件读不出来
  K_pin   = PBKDF2-HMAC-SHA256(PIN, salt, iter)            // iter 由启动时的基准测试决定，使单次解锁耗时约 300ms
  K_vault = HKDF-SHA256(K_hw || K_pin, info="vault")
  每个条目：AES-256-GCM(K_vault, 随机 96 位 nonce, aad = entry_id || version)
  ```
- **不烧录 eFuse 时的降级模式**：用存在 NVS 中的随机值代替 `K_hw`。这种模式下 Flash 一旦被读出，就可以离线暴力破解 PIN，设置中会用红色明确提示。
  - **烧录 eFuse HMAC 密钥块是不可逆操作。必须由用户在设备上二次确认后才执行；开发过程中执行之前，也必须先征得项目维护者同意。**
- **防暴力破解**：
  - 失败计数存在 vault 分区中，先递增并写入，再验证 PIN，因此掉电不会重置计数。
  - 连续失败 5 次锁定 5 分钟；失败 10 次清空 vault（阈值可配置为 10 / 20 / 关闭）。
- **内存卫生**：明文只在解锁期间存在于内部 RAM 中，不进 PSRAM；上锁时用 `mbedtls_platform_zeroize` 清零；绝不写入日志。
- **通道限制**：
  - vault 数据**永远不**经过 Wi-Fi、WS 或 Hub 的 Web。
  - USB-CDC 导入导出要求设备已解锁，并且用户已在设备上手动进入管理模式（有效期 2 分钟）。
  - 导出的备份文件用 `K_backup = scrypt(备份口令)` 加密，与设备密钥无关，因此可以恢复到另一台设备上。scrypt 保持 `N=32768,r=8,p=1`，由本机 USB CLI 派生；仅将派生密钥经已授权 CDC 管理通道交给设备，设备执行 AES-256-GCM，不承担 32 MiB 的 KDF 工作表。CLI 不解密备份，设备和 CLI 用后清零派生密钥，具体字段见 PROTOCOL §4.4。
- **HID 键入**：每次都需要在设备上按住确认；键入时屏幕显示目标条目的名称；宏系统无法读取 vault。
- **不在防护范围内**：物理探针攻击、侧信道攻击、Secure Boot。v1 不开启 Secure Boot，所以能刷入恶意固件的攻击者可以在用户下次解锁时截获 PIN。**这一点必须写进安全文档。**

---

## 10. 测试策略（少而关键，见 §3 第 9 条）

### 10.1 自动化测试：只写下表中列出的项
新增测试项必须先改本表，并说明"为什么手工验证不够"。

| # | 对象 | 理由 | 形式 |
|---|---|---|---|
| T1 | 区域判定 + 接口访问矩阵（§7.11），包括"经 cloudflared 进来的回环请求判为 public" | 出错就是安全漏洞 | pytest，表驱动，1 个文件 |
| T2 | AI 熔断：401 后停止所有请求；路径白名单；不调用 `/usage-queue`（§7.6.5） | 出错会导致 IP 被封、数据被抢 | pytest + respx，1 个文件 |
| T3 | 每个上游适配器（6 种服务器数据源 + 3 种 L3 配方 + L1 / L2，以及 ASR / TTS / Pi LLM / 搜索）的解析 | 上游格式容易变 | 每个适配器 1 个 fixture，1 个测试；不访问真实服务 |
| T4 | 告警表达式解析器（§7.5.4） | 自己写的解析器，而且不能用 eval | pytest，表驱动 |
| T5 | Claude 审批流程：无设备时立即返回、超时、允许、拒绝、同一 rid 只接受第一个决定 | 会直接影响用户的 Claude Code | pytest，1 个文件 |
| T6 | 协议 schema：`protocol/examples/*.json` 全部通过 schema 校验；Hub 的 demo 模式输出的帧全部通过 schema 校验 | 契约是三条轨道之间唯一的约束 | CI 脚本 |
| T7 | TOTP（RFC 6238 测试向量）、vault 加解密往返、备份格式往返 | 加密逻辑出错会丢数据 | 固件 Unity，linux target |
| T8 | 固件协议解析：用 `protocol/examples` 中的帧解析出视图模型 | 与 T6 共用同一批样例 | 固件 Unity，linux target |
| T9 | 助手工具权限闸门：敏感工具未经设备允许、拒绝 / 超时 / 取消均不执行；公网测试对话的身份与区域约束 | 模型与外部 MCP 的声明不能构成授权，单次手验无法覆盖并发决定和取消竞态 | pytest，假模型与假工具，1 个文件 |

以下内容**不写**自动化测试：
- UI 布局、动画；
- 驱动寄存器读写；
- CLI 参数解析；
- 服务安装；
- 简单的数据转换；
- 采集器（靠 `doctor` 和手工验证）。

### 10.2 手工验证
| 对象 | 方法 |
|---|---|
| 固件硬件与交互 | 硬件在环检查清单（PLAN 附录 B），每个里程碑执行一次 |
| 固件 UI | SDL 模拟器 + 实机目测；FE 轨道每张卡片产出四种状态的截图，附在 PR 中供评审，不做像素比对测试 |
| 端到端 | `nuka hub --demo` + `nuka sim-device`，或接上真实设备，按 PLAN 附录 C 的场景走一遍 |
| 跨平台 | CI 只在 windows / macos / ubuntu 上各跑一次：安装 + 导入 + T1–T6（Python 3.12）；三平台的服务安装靠手工验证 |
| 安全 | 发布前按 §9 的清单做一次评审（INT-03） |

---

## 11. 安装引导要求（硬性）
所有引导放在 `docs/guides/` 下，提供中英两个版本（`*.zh-CN.md` / `*.en.md`）。**每篇引导都必须包含**：
1. 适用场景与前置条件（操作系统、版本、所需权限）；
2. 分平台、可以直接复制粘贴的命令（分别给出 Windows PowerShell、macOS 和 Linux 版本）；
3. 每一步之后的**验证方法**（"你应该看到……"，并附截图或示例输出）；
4. 与 `config.toml` 对应的配置片段（全部使用占位符）；
5. 卸载 / 回滚步骤；
6. 至少 3 条常见错误及排查方法，以及对应的 `nuka doctor` 输出。

| 引导 | 内容要点 |
|---|---|
| `00-overview` | nuka-esp32 是什么、如何在 D1–D4 部署形态中选择、需要哪些硬件 |
| `01-flash-firmware` | 网页刷机（ESP Web Tools）和 esptool 命令行两种方式；**刷机前完整备份原厂固件**；如何恢复原厂固件；TinyUSB 模式下如何进入下载模式 |
| `02-install-hub-windows` / `-macos` / `-linux` / `-docker` | 安装 uv 或 pipx，安装 Hub，运行 `nuka init` 和 `service install`，在防火墙中放行端口 |
| `03-pair-device` | USB 配网与 SoftAP 配网；配置多个 Hub 地址（局域网 / Tailscale / 公网） |
| `04-hardware-sensors` | Windows：LibreHardwareMonitor（下载、开启 Web 服务、设置为以管理员身份开机自启）；Linux：lm-sensors（`sensors-detect`）；macOS：macmon；检查 NVIDIA 驱动 |
| `05-server-sources` | ServerStatus、Uptime Kuma、Beszel、Netdata、node_exporter / Prometheus、通用 JSON 各一节，每节包括：上游需要做什么（开放哪个接口、创建什么令牌）、Hub 配置、如何验证 |
| `06-server-push-hook` | 创建 notify 令牌；bash、PowerShell、cron、systemd OnFailure、GitHub Actions 的示例 |
| `07-ai-quota` | CPA / CPAMP 是什么；如何获取管理密钥；CPA 侧需要的配置（`allow-remote`、`secret-key`）；**认证失败会封禁 IP 的警告**；`nuka secret set`；如何验证 |
| `08-agent-hooks` | Claude Code（`hooks install claude` 以及手动配置方法）、Codex（`/hooks` 授信）、Gemini；跨机器使用；如何关闭 |
| `09-public-access` | Tailscale；cloudflared 隧道 + Cloudflare Access（应用、策略、Service Token）；区域划分与安全说明 |
| `10-nukavault` | 安全模型、eFuse 不可逆警告、PIN 与备份口令、导入 otpauth、导出备份与恢复 |
| `11-build-from-source` | 在三个平台上安装 ESP-IDF；生成字体、主题和资源包；编译与烧录；SDL 模拟器；Hub 的开发环境 |
| `12-troubleshooting` | 常见问题与 doctor 输出的对照表 |

---

## 12. 决策记录
| # | 决策 | 日期 |
|---|---|---|
| D1 | 固件使用 ESP-IDF 5.5 + LVGL 9 + esp_lvgl_port，C 语言，板级层自己写 | 2026-10-17 |
| D2 | draw buffer 放在内部 DMA RAM，QSPI 40MHz | 2026-10-17 |
| D3 | 主题为黑底 + 牛油果绿，token 只有一个来源 | 2026-10-17 |
| D4 | 关机通过 AXP2101 真断电；PWR 按住 2s 弹出菜单，按住 6s 硬件强制关机 | 2026-10-17 |
| D5 | 电池使用安全默认值：300mA、4.1V | 2026-10-17 |
| D6 | Hub 用 Python 实现，支持三平台，分为 Hub 和 Agent 两种角色 | 2026-10-17 |
| D7 | AI 配额首选 CPAMP 管理员密钥，同时支持直连 CPA；带熔断与白名单 | 2026-10-17 |
| D8 | 公网访问使用 cloudflared + Cloudflare Access，敏感接口禁止公网访问 | 2026-10-17 |
| D9 | v1 范围：Agent Buddy、桌宠、IMU + 番茄钟、媒体 / 天气 / HID、NukaVault（TOTP + 密码库 + 生成器）、6 种服务器数据源与推送 hook | 2026-10-17 |
| D10 | 排除：GitHub CI 状态、FIDO2；语音助手的排除由 D21 解除 | 2026-10-24 |
| D11 | 允许使用 eFuse HMAC，但真正烧录前必须再次确认；v1 不开启 Secure Boot | 2026-10-17 |
| D12 | 开源发布；零硬编码；每个集成都必须有安装引导；个人部署信息放在 git 忽略的 `local/` 中 | 2026-10-17 |
| D13 | 后端（Hub）和 Web 由其他 agent 实现，以 PROTOCOL / CONFIG 为契约 | 2026-10-17 |
| D14 | 三个物理键全部利用：PWR 是电源键，BOOT 是前进 / 确认键，Key3 是返回 / 取消键；BOOT + Key3 组合键打开恢复菜单 | 2026-10-17 |
| D15 | 禁止过多单元测试与防御性编程：只写 §10.1 列出的测试，只在信任边界做校验 | 2026-10-17 |
| D16 | 分为四条轨道：契约 C、固件功能 FW、Hub 后端逻辑 BE（包括简单的 Web 管理页）、前端优化 FE（**只做 ESP32 上的 LVGL UI**：功能完整度、视觉、动效、渲染性能、低功耗打磨、桌宠美术），分别交给不同的 agent；FE 作为最后的大项任务 | 2026-10-17 |
| D17 | 许可证为 AGPL-3.0-or-later；依赖必须与之兼容；Hub 的 Web 页脚和固件的"关于"页提供源代码链接（AGPL §13） | 2026-10-17 |
| D18 | 项目名定为 nuka-esp32，属于 nuka 系列；Hub 为 `nuka-hub`，CLI 为 `nuka`，协议标识统一用 `nuka` 前缀（§1.4）；主题仍是黑底 + 牛油果绿 | 2026-10-17 |
| D19 | 原像素桌宠方案由 D26 的主页矢量眼表情取代，独立桌宠卡删除 | 2026-10-24 |
| D20 | 本项目是系统级重写（§1.1），不沿用原厂固件代码；先由 C / FW / BE 做出功能正确的完整系统，经系统联调（PLAN M6.5、INT-01）通过后，才进入 FE 的 UI 与功耗打磨 | 2026-10-17 |

第二阶段 D21–D32 的完整决策及本轮维护者裁定见 [PLAN-v2 §1](PLAN-v2.md#1-决策记录接续-spec-12-的-d1d20)，包括按键语音、Pi SDK、USB配网、服务器认证与永久记忆。

## 13. 开放问题
1. GitHub 仓库的 owner / 组织名：文档中暂时写作 `<owner>`（用于 Docker 镜像名 `ghcr.io/<owner>/nuka-hub`、网页刷机页地址、`project.urls.Source`），在 C-03 建仓时由维护者填入。
2. 发布到 PyPI 前，确认 `nuka-hub` 这个包名没有被占用。

---

## 附录 A 术语
| 术语 | 含义 |
|---|---|
| Hub | nuka-esp32 伴侣服务的主角色，负责聚合数据、提供 Web 和设备接入 |
| Agent（Hub 角色） | 只采集本机数据并上报给 Hub 的角色；注意与"AI Agent"区分 |
| AI Agent / Agent CLI | Claude Code、Codex、Gemini CLI 等 AI 编程工具 |
| CPA | CLIProxyAPI，一个把 AI 订阅包装成 API 的代理服务 |
| CPAMP | CPA Manager Plus，CPA 的管理面板与 Manager Server |
| 视图模型（VM） | Hub 为设备排好版的小份 JSON 数据 |
| L0 / L1 | 卡片轮播层 / 详情层 |
| nukapack | 固件的资源包格式 |
| 区域（zone） | Hub 对请求来源的分类：local / lan / public |
