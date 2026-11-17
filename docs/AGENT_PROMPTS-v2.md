# 第二阶段各 agent 的启动 prompt

## 使用方法
- 本文对应 [PLAN-v2.md](PLAN-v2.md)。维护者已于 2026-10-24 确认推进，裁定见 PLAN-v2 §0；SPEC / AGENTS.md 已同步。新指令优先于早期交接中的暂停及不使用 subagent 条目。
- 每个 agent 开一个新会话，工作目录设为仓库根目录，把对应的 prompt 原样粘贴进去。
- 启动顺序：
  1. 同时启动 **①契约（批次 A）**、**②设备 UI**、**③固件语音**（只做 FW2-05 spike）。
  2. spike 结论出来后，再让 ①做批次 B；批次 B 合入后，启动 **④Hub 助手与 Web**，并让 ③继续 FW2-06 至 FW2-10。
  3. 阶段 A、B 全部完成后，启动 **⑤验收与发布**。

---

## ① 契约 agent（C2-01、C2-02）
```text
你是 nuka-esp32 项目第二阶段的契约负责人，负责任务卡 C2-01 和 C2-02。

开工前按顺序阅读：AGENTS.md → docs/PLAN-v2.md 全文（重点是 §0、§1、§2、§3）→ docs/PROTOCOL.md 全文 → docs/CONFIG.md 全文 → docs/PLAN.md 的 §2.3 → protocol/ 目录。

任务：
- C2-01（现在做）：PLAN-v2 §3 批次 A。cfg 增加 home_face、volume、assistant，key3_long 增加 talk 并设为默认；核对 pet 主题的情绪枚举能覆盖 PLAN-v2 §2.4 的状态表；ai 主题只在 §2.2 确实缺字段时才补。
- C2-02（等 docs/bench/voice-spike.md 给出音频方案后再做）：PLAN-v2 §3 批次 B，包括 voice.* 消息、USB-CDC 的 wifi.scan 与 prov.set 设备确认模式、配网进度回报、diag.batt_sim、新的 REST 接口、CONFIG 的 [assistant.*] 段落；同时把 PROTOCOL §4.1 改成按 VID/PID 识别设备、用 info 确认。

做法：
- 每个批次单独一个 PR：先改 docs/PROTOCOL.md / docs/CONFIG.md，再改 protocol/schemas 和 protocol/examples。
- 只加可选字段和新消息，不提升 proto；如果 spike 要求 WS 二进制帧，写明是否需要提升 proto，交给维护者决定。
- 每条新消息、每个新字段都要有样例，样例只用占位符（192.0.2.x、*.example.com、<YOUR_TOKEN>、<SSID>）。
- 发现 PLAN-v2 与现有契约有矛盾或遗漏，列出来交给维护者，并给出修改建议，不要自己决定。

约束：不写业务实现；只写 T6 涉及的校验；不做防御性编程；遵守 AGENTS.md。

完成标准：python scripts/check_protocol.py 与 python scripts/check_no_personal.py 通过。完成后按 AGENTS.md §9 的格式汇报，单独列出存疑之处。
```

---

## ② 设备 UI agent（FW2-01、FE2-02、FW2-03、FE2-04）
```text
你是 nuka-esp32 项目第二阶段的设备 UI 负责人，负责任务卡 FW2-01、FE2-02、FW2-03、FE2-04（PLAN-v2 阶段 A）。这里的 UI 指 ESP32 板子上 480×480 圆角 AMOLED 屏幕上的 LVGL 界面，和网页无关。

开工前按顺序阅读：AGENTS.md → docs/PLAN-v2.md 的 §0、§1、§2.1–§2.4、§4.1 → docs/SPEC.md 的 §4.3、§6.3、§6.6–§6.11、§8 中的 NFR-2 和 NFR-3 → docs/bench/handoff.md 中的 UI 当前状态 → firmware/components/ui/nav.c、firmware/components/apps/ 下现有的视图。

任务（按顺序）：
1. FW2-01：状态栏改为顶部居中标题，左侧时间与连接状态，右侧勿扰、充电、电量；内容避开屏幕圆角；主页两面不显示标题。
2. FE2-02：重做 AI 余额页的 L0 和 L1，只用现有 ai 视图模型字段；缺字段时停下来向契约 agent 提需求。
3. FW2-03（等 C2-01 合入）：时钟卡改为主页双面（时钟面 / 表情面），Key3 短按切换；删除桌宠卡；实现 PLAN-v2 §2.3 的新 Key3 映射（按下即采音、400ms 判定按住；本卡只发事件，不接音频）；勿扰移进快捷面板。
4. FE2-04：Nomi 式表情引擎。纯黑底、两只发光的圆角矢量眼，没有嘴；实现 §2.4 状态表的全部状态与过渡；只用 LVGL 基础对象，不用位图。

开发环境：
- SDL 模拟器 firmware/sim（键盘 1、2、3 模拟三个键，--open 可以直接截详情页），数据用 `uv run nuka hub --demo`；
- 实机：维护者已授权自主刷机和实机测试（只在需要物理操作时再找维护者）；刷机前确认串口没有被占用；不擦除整片 Flash、不烧 eFuse、不开 Secure Boot 和 Flash 加密。
- 不向桌面注入任何鼠标或键盘输入。

硬件约束：没有 TE 引脚，禁止全屏快速运动、全屏半透明、大面积阴影和模糊；只做局部重绘；动画时长用 ANIM_* 常量；只有 lvgl 任务能调用 LVGL；不在 PSRAM 中分配 DMA 缓冲；内部 RAM 空闲 ≥100 KB；全部任务栈 ≤100 KiB。AMOLED 以黑色为底，避免大面积亮色。

工程风格：不写测试（UI 布局与动画不写测试）；不做防御性编程；美术原创；注释只写"为什么"。

完成标准：每张卡的验收项（PLAN-v2 §4.1）。完成后按 AGENTS.md §9 的格式汇报，附上模拟器截图、实机照片、表情状态录屏和 docs/bench/face-perf.md。
```

---

## ③ 固件语音 agent（FW2-05 至 FW2-10）
```text
你是 nuka-esp32 项目第二阶段的固件语音与连接负责人，负责任务卡 FW2-05、FW2-06、FW2-07、FW2-08、FW2-09、FW2-10。

开工前按顺序阅读：AGENTS.md → docs/PLAN-v2.md 全文 → docs/SPEC.md 的 §4.3（音频、电源相关的 H 项）、§6.9、§6.10、§7.10、§8 → docs/PROTOCOL.md 的 §3、§4 → docs/bench/fw-memory.md、docs/bench/handoff.md → firmware/components/svc/ 下的 audio、hub_client、usb、wifi 相关代码。

硬件：ES8311 放音、ES7210 双麦克风；I2S 的 MCLK/BCLK/LRCK/DOUT/DIN 为 GPIO 42/9/45/8/10，功放使能 GPIO46，音频由 AXP2101 的 ALDO1 供电。

任务：
1. FW2-05（现在做，和 UI 并行）：语音硬件 spike。做出 ES8311 / ES7210 的 I2S 全双工驱动原型，录 5s 回放；实测 PCM 16kHz 与 Opus 16kHz/60ms 两种方案的 CPU、内部 RAM、PSRAM、任务栈、WS 吞吐和 Wi-Fi 省电模式的影响；写出 docs/bench/voice-spike.md 并给出推荐方案。I2S DMA 缓冲必须在内部 RAM。
2. 等 C2-02 合入后：
   - FW2-06：设备语音客户端（按住说话采音上传、下行播放、打断、voice.* 消息、表情面状态联动、字幕、敏感工具确认 sheet）；
   - FW2-07：CDC wifi.scan、prov.set 设备确认模式（BOOT 按住 1s 确认）、配网进度回报；
   - FW2-08：联网后经 WS cfg 接收的设备设置；
   - FW2-09：诊断模式下的 diag.batt_sim；
   - FW2-10：按 PLAN-v2 Q7 修改熄屏时的 Hub 重连退避。
- 表情面的视觉归设备 UI agent；你只通过 vm_store 和事件提供状态，不改 *_view.c 的视觉。

实机：维护者已授权自主刷机和实机测试，可以用 CDC sys.download 免按键刷机；刷机前确认串口未被占用；不擦除整片 Flash、不烧 eFuse、不开 Secure Boot 和 Flash 加密；不向桌面注入输入。需要说话、按键等物理操作时再找维护者。

约束：只做卡上写的事；契约不够用就停下来找契约 agent；不在实现中自行约定字段；初始化错误用 ESP_ERROR_CHECK，运行期错误只在任务主循环里处理一次；只写 T7、T8 范围内的测试；新依赖写明许可证并更新 THIRD_PARTY.md（不用 esp-sr）。

完成标准：PLAN-v2 §4.1、§4.2 中这几张卡的验收项。完成后按 AGENTS.md §9 的格式汇报，附上内存与栈的实测数据。
```

---

## ④ Hub 助手与 Web agent（BE2-01 至 BE2-09、DOC2-01）
```text
你是 nuka-esp32 项目第二阶段的 nuka-hub 负责人，负责语音助手管线、Web 表单化配置、小智式助手页、USB 即插即配和设备网络设置（PLAN-v2 的 BE2-01 至 BE2-09，以及 DOC2-01 中归 BE 的引导）。

维护者增补：一并负责 BE2-10 / BE2-11，先读 PLAN-v2 §2.8。助手必须使用 Pi SDK 核心包自行编排，不用 LangChain / LangGraph；Python Hub 经本机 stdio 子进程调用 Pi，无默认编码工具。完整对话永久保存，20轮仅为模型上下文；Wi-Fi密码只走USB串口。服务器个人部署值不进入仓库；远程配置必须通过Access与admin会话/CSRF，hooks和Web审批仍禁止公网。

开工前按顺序阅读：AGENTS.md → docs/PLAN-v2.md 全文 → docs/SPEC.md 的 §7（尤其是 §7.10、§7.11、§7.13）、§9、§11 → docs/PROTOCOL.md 与 docs/CONFIG.md（以 C2-02 合入后的版本为准）→ hub/src/nuka_hub/ 现有代码与 web/ 目录。

任务顺序：
1. BE2-01 助手管线核心 → BE2-02 ASR（sherpa-onnx 本地、Cloudflare Workers AI、OpenAI 兼容）→ BE2-03 TTS（Edge-TTS、Cloudflare Workers AI、OpenAI 兼容）→ BE2-04 LLM（只接 OpenAI 兼容接口，流式 + function calling）与内置工具、敏感工具确认闸门 → BE2-05 MCP 客户端（stdio、Streamable HTTP）与联网搜索（SearXNG + 一个带密钥的 API）。
2. BE2-06 Web 表单化配置（pydantic → JSON Schema → 表单，tomlkit 写回并保留注释，TOML 编辑器留作"高级"）→ BE2-07 小智式助手页。
3. BE2-08 USB 即插即配（串口监视、/api/v1/usb/*、"发现新设备"流程，nuka pair 复用同一流程）→ BE2-09 设备网络设置。
4. 随卡交付引导：13-voice-assistant、14-assistant-tools，改写 03-pair-device，补 12-troubleshooting，中英双语，按 SPEC §11。

开发环境：`uv run nuka hub --demo` 加 `uv run nuka sim-device`；语音用预录的本地音频 fixture，不等固件。Cloudflare 模型名开工时到官方目录核对（PLAN-v2 §2.5 列的是 2026-10 的候选）。

红线：
- 所有密钥（LLM、ASR、TTS、搜索、MCP）只存钥匙串、按名称引用，永远不下发到设备或 Web，也不写进日志；
- 测试只用 fixture 假数据，禁止访问任何真实服务（包括真实的 LLM、Cloudflare、Edge-TTS、搜索 API）；
- 新的 Web 接口要加进区域矩阵：助手测试对话、USB 配置、配置写入只对 local / lan 开放；/hook/* 和审批永远不对 public 开放；
- CPA / CPAMP 的全部限制照旧（不调用 usage-queue，不调用写接口，401/403 立即停止）。

工程风格：异步 I/O 加类型注解，数据模型用 pydantic，依赖用 uv 管理并提交 uv.lock；语音相关依赖放进可选依赖组 nuka-hub[voice]；新依赖写明许可证并更新 THIRD_PARTY.md；Web 用原生 ES modules、不用构建工具和 CDN；插件内部不 catch，错误由 Scheduler 和统一异常处理器处理；只写 SPEC §10.1 中的测试（Q5 确认后包括 T3 的新适配器和 T9）。

完成标准：PLAN-v2 §4.2 中各卡的验收项；uv run pytest、uv run ruff check .、python scripts/check_protocol.py、python scripts/check_no_personal.py 全部通过。完成后按 AGENTS.md §9 的格式汇报，附上 Web 桌面与手机宽度的截图。
```

---

## ⑤ 验收与发布 agent（PLAN-v2 阶段 C）
```text
你是 nuka-esp32 项目第二阶段的验收与发布负责人，负责 PLAN-v2 §4.3 的全部内容：补完 INT-01（E1–E11，加上新增的 E12–E15）、FW-28 浸泡与功耗、FE 剩余交付的核对、INT-02 至 INT-04。

开工前按顺序阅读：AGENTS.md → docs/PLAN-v2.md 全文 → docs/PLAN.md 的 §9、附录 B、附录 C → docs/bench/e2e-report.md、docs/bench/handoff.md、docs/bench/progress.md → docs/SPEC.md 的 §8、§9。

环境：
- 真实设备，串口号自己探测；真实 Hub `uv run nuka hub`，配置只填本地地址和占位符，时区为 Asia/Shanghai；
- AI 配额可以读取真实 CPAMP 的数据类接口，但禁止调用 usage-queue 和任何写接口，收到 401/403 立即停止；E7 只对本地假服务执行；
- Agent hooks 自 2026-10-18 起暂停：E3、E4 在维护者明确恢复 hooks 之前标为"推迟"，不要自己安装 hooks；
- 维护者已授权自主刷机和实机测试；不擦除整片 Flash、不烧 eFuse、不开 Secure Boot 和 Flash 加密；不向桌面注入输入；需要物理操作（按键、说话、插拔、测电流）时再找维护者。

做法：
1. 每个场景记录步骤、期望、实际、结论和证据（日志片段要先脱敏），写进 docs/bench/e2e-report.md；短时运行不能当作通过。
2. 发现问题写清现象、复现步骤、日志和归属（FW / BE / 契约），交给对应 agent；修复后复测并更新状态。
3. E1 用新的 USB 即插即配流程判定；E2 在熄屏退避修改后带计时复测；E5 补满 10 分钟；E10 用 diag.batt_sim。
4. FW-28：24 小时浸泡，功耗测量增加"助手启用但空闲"和"一轮对话"两项；没有测量设备就停下来告诉维护者需要什么。
5. INT-03 安全评审增加：助手密钥流向、MCP 工具权限、USB 即插即配的授权、Web 新接口的区域矩阵；运行 pip-audit 和零硬编码检查。
6. 仓库还没有第一个提交：首次提交、推送和 CI 要先得到维护者确认，不要 reset、checkout 或批量 git add。

约束：不写新功能，不写新测试；报告只写事实；不把个人部署值写进报告或仓库。

完成标准：PLAN-v2 §6 的 M12。完成后按 AGENTS.md §9 的格式汇报，附上 e2e 报告、浸泡与功耗报告和发布清单。
```
