# AGENTS.md：nuka-esp32 协作约束

本文件写给参与 nuka-esp32（nuka 系列，命名规则见 SPEC §1.4）开发的 AI agent（以及人类贡献者），**每次开工前都要先读**。如果与其他文档冲突，以本文件为准；但本文件不能覆盖维护者在对话中给出的直接指令。

## 0. 项目与文档
nuka-esp32 由两部分组成：
- 为 Waveshare ESP32-S3-Touch-AMOLED-2.16 编写的开源固件（ESP-IDF 5.5 + LVGL 9，C 语言）；
- 跨平台伴侣服务 nuka-hub（Python）。

| 文档 | 作用 | 谁可以改 |
|---|---|---|
| `docs/SPEC.md` | 需求与设计，"做什么、为什么" | 维护者 |
| `docs/PROTOCOL.md` + `protocol/` | **设备、Hub、Web 之间的唯一契约** | 走契约变更流程（PLAN §2.3） |
| `docs/CONFIG.md` | Hub 的配置 schema | 走契约变更流程 |
| `docs/PLAN.md` | 轨道划分、任务卡、验收标准、目录归属 | 维护者 |
| `docs/AGENT_PROMPTS.md` | 各轨道 agent 的启动 prompt | 维护者 |

**动手前先找到你的任务卡**（PLAN §4–§9）：读卡上"参考"一栏列出的章节，然后严格按卡上的"产出"和"验收"交付。

## 1. 范围纪律
1. **只做任务卡里写了的事。** 不顺手加功能、不加配置项、不加"以后可能有用"的抽象。
2. 想到新功能或新需求，写进 PR 描述的"建议"一节，或者开一个 issue，**等维护者确认后再做**。
3. **只改你的轨道拥有的目录**（PLAN §2.1）。需要别的轨道配合时提出需求，不要直接改对方的代码。
4. 遇到契约不够用的情况，**停下来提出契约变更**，不要在实现中自行约定新字段。
5. 已经明确排除的功能：GitHub CI 状态卡、FIDO2 / Passkey、Secure Boot、发布模式的 Flash 加密。维护者于 2026-10-24 批准 PLAN-v2 的按键语音助手；不做唤醒词，助手采用 Pi SDK，需求以 `docs/PLAN-v2.md` 的已确认决策为准。

## 2. 零硬编码（项目将在 GitHub 开源）
- 源码、默认配置、示例、测试数据、文档、提交信息中，**禁止**出现任何个人部署值，包括真实的 IP、域名、账号、邮箱、令牌、密钥、串口号和主机名。
- 需要示例值时，统一使用以下占位符：
  - IP：`192.0.2.x` / `198.51.100.x` / `203.0.113.x`
  - 域名：`*.example.com` / `*.example.lan`
  - 需要用户填写的值：`<YOUR_TOKEN>`、`<SSID>`
- 固件里不内置任何地址或凭据，这些只能通过配网写入 NVS。Hub 的一切行为由 `config.toml` 决定，密钥按名称引用，值存放在系统钥匙串中。
- `local/` 目录是维护者的私人笔记，已被 git 忽略。**禁止读取其中的内容并写进仓库的任何位置。**
- 提交前运行 `python scripts/check_no_personal.py`。
- 每新增一个集成，都要在 `docs/guides/` 中补上对应的安装引导，写作要求见 SPEC §11。

## 3. 测试：少而关键
- **只写 SPEC §10.1 中列出的 T1–T9**，不得自行扩充。维护者于 2026-10-24 批准 T3 扩展到语音、LLM、搜索适配器，以及 T9 助手工具权限闸门。如果确实需要新增测试项，先说明理由，由维护者修改 SPEC 后再写。
- 不追求覆盖率。以下内容**不写测试**：
  - getter / setter、简单的字段映射、框架本身的行为；
  - UI 布局与动画；
  - 驱动寄存器读写；
  - CLI 参数解析；
  - 服务安装。
- 不搭层层嵌套的 mock。每个上游适配器只需要 **1 个** fixture 测试。
- 测试**禁止访问任何真实服务**（CPA、CPAMP、服务器探针、天气 API 等）。fixture 中只能放假数据。
- 其他方面的正确性，靠任务卡上的手工验收、`nuka hub --demo` / `sim-device`、SDL 模拟器和 PLAN 附录 B / C 来保证。

## 4. 禁止防御性编程
- **只在信任边界做校验**：网络帧、HTTP 请求、上游 API 响应、USB-CDC 输入、配置文件、NVS 读出的数据、用户输入。
- 边界之内信任调用方：
  - 不重复判空，不对同一个值做二次校验；
  - 不为"不可能发生"的情况写分支或日志；
  - 不写"以防万一"的默认值或兜底逻辑；
  - 不用宽泛的 `try/except` 或 `catch` 把错误吞掉。
- 错误只在一个地方集中处理：
  - **Hub**：插件内部不 catch，由 Scheduler 统一捕获，并把插件标记为 `error`；API 层由统一的异常处理器转换成 PROTOCOL §5.1 规定的错误格式。
  - **固件**：初始化阶段遇到不可恢复的错误时，用 `ESP_ERROR_CHECK` 直接让程序挂掉；运行期的错误只在任务的主循环里处理一次。
- 不写兼容层或"兼容旧版本"的代码，除非 PROTOCOL 明确要求（Hub 需要兼容 proto N−1）。
- **SPEC 中规定的安全与可靠性机制属于需求，不属于防御性编程，必须实现**：AI 熔断和请求白名单（§7.6.5）、区域判定（§7.11）、vault 的防暴力破解（§9.3）、OTA 回滚、看门狗。

## 5. 代码风格
- 与周围的代码保持一致：命名、注释密度、写法都照着已有代码来。注释只写"为什么"，不复述代码在做什么。
- **固件**：
  - C11，不用 C++；
  - 只有 `lvgl` 任务可以调用 LVGL API，其他任务通过 `vm_store` 和事件更新 UI；
  - 不在 PSRAM 中分配 DMA 缓冲；
  - 头文件只暴露最小的 API。
- **Hub**：
  - Python ≥ 3.11，异步 I/O，加上类型注解；
  - 数据模型用 pydantic；
  - 依赖用 uv 管理，并提交 `uv.lock`；
  - 用 ruff 做格式化和 lint；
  - 不引入新的重量级依赖，确有必要时先在 PR 中说明理由。
- **Web 管理页**（归 BE，美观、能看即可）：
  - 原生 ES modules，不需要构建工具；
  - 第三方库 vendored 进 `web/vendor/`，并附上许可证；
  - 不使用 CDN。
- **许可证**（项目为 AGPL-3.0-or-later，SPEC §3 原则 7）：
  - 每个源文件头部写 `SPDX-License-Identifier: AGPL-3.0-or-later`；
  - 依赖必须与 AGPL-3.0 兼容：MIT、BSD、ISC、Apache-2.0、MPL-2.0、LGPL、GPL-3.0、AGPL-3.0 可以；GPL-2.0-only、SSPL、带 NC / ND 条款的协议和专有代码不行；
  - 字体只用 OFL 协议的；
  - 美术与图标必须原创或 CC0；
  - 新增依赖时，在 PR 中写明它的许可证，并更新 `THIRD_PARTY.md`。

## 6. 安全红线（违反任何一条，都必须立即停下并报告）
1. **CPA / CPAMP**：
   - 开发和测试时禁止连接真实实例；
   - 禁止调用 `/v0/management/usage-queue`，因为读取它是破坏性的出队操作；
   - 禁止调用任何写操作类的管理接口；
   - 收到 401 或 403 后立即停止所有请求。原因：同一 IP 认证失败 5 次，会被封禁 30 分钟。
2. **eFuse**：禁止执行任何烧录 eFuse 的命令（`espefuse.py burn_*`，或者固件中的烧录路径），除非维护者在对话中**明确、单独地**确认。
3. **Secure Boot / Flash 加密**：不开启。
4. **刷机**：
   - 第一次往开发板上刷固件之前，必须先用 `esptool.py read_flash 0 0x1000000` 完整备份原厂固件，并且备份文件不进 git；
   - 刷机前确认串口没有被其他程序（例如 Arduino IDE）占用；
   - 不擦除整片 Flash，除非维护者同意。
5. **密钥**：
   - AI 管理密钥只能存在 Hub 的钥匙串中，永远不下发到设备或 Web，也不写进日志；
   - vault 数据永远不经过 Wi-Fi、WS 或 Hub。
6. **Agent 审批接口**：`/hook/*` 和 Web 审批永远不对 public 区域开放。

## 7. Git 与 PR
- 分支命名为 `<track>/<card-id>-<slug>`，例如 `fw/fw-04-keys`。一张任务卡对应一个 PR（必要时可以拆成几个）。
- PR 描述中要包含：
  - 任务卡 ID；
  - 验收项逐条勾选；
  - 手工验证的方法与结果；
  - 是否改动了契约（默认"否"）；
  - 涉及 UI 的 PR 附截图；
  - "建议"一节（可选）。
- 不要 force push 到 `main`；不要跳过 hooks（`--no-verify`）。
- 不要提交 `sdkconfig`（只提交 `sdkconfig.defaults`）、构建产物、`secrets.toml`、`*.nukavault`、备份的固件。

## 8. 常用命令（骨架完成后生效）
```bash
# 契约
python scripts/check_protocol.py
python scripts/check_no_personal.py
python firmware/tools/gen_theme.py

# 固件
cd firmware && idf.py set-target esp32s3 && idf.py build
idf.py -p <PORT> flash monitor
cmake -S firmware/sim -B build/sim && cmake --build build/sim && ./build/sim/nuka_sim

# Hub
cd hub && uv sync --all-extras
uv run nuka hub --demo
uv run nuka sim-device
uv run pytest
uv run ruff check . && uv run ruff format .
```

## 9. 完成任务时的汇报格式
1. 完成了哪些任务卡，验收项的状态（逐条列出：通过、未通过、跳过，未通过和跳过的要写原因）。
2. 手工验证是怎么做的，结果如何。
3. 遇到的问题、做出的假设，以及需要维护者决定的事项。
4. 需要别的轨道配合的地方（如果有）。
5. "建议"（可选）：对需求或设计的改进想法。**这些建议不得自行实施。**
