# nuka proto=1 契约

规范为 [PROTOCOL](../docs/PROTOCOL.md)（含 §9 字段规范表）和 [CONFIG](../docs/CONFIG.md)。当前为首次本地冻结、待远程 PR 评审，矛盾与解决决定见 [DECISIONS.md](DECISIONS.md)。Vault 备份 KDF 由本机 USB CLI 执行，设备负责 AES-GCM；assets 单包≤3 MiB，在原 6 MiB 分区的两个 bank 间更新。这里只包含契约、数据和 T6 工具，不包含 Hub 或固件的业务实现。

```sh
uv sync --project scripts --locked
uv run --project scripts python scripts/check_protocol.py
```

激活 `scripts/.venv` 后也可直接执行 `python scripts/check_protocol.py`。工具离线加载所有 JSON Schema 2020-12 与本地引用，校验每份样例、所有已定义消息的样例覆盖、10 个 VM 消息类型的 full / min 样例，以及满字段样例是否遗漏已定义字段。Unix 时间、百分比、枚举、字符串长度、列表上限和 8192 字节帧上限都参与检查。百分比使用 Decimal 校验，避免 0.1 的浮点误差。

`schemas/ws-device.json`、`ws-agent.json`、`cdc.json` 用 `t` 判别 `oneOf`；公共信封在 `envelope.json`。消息体和资源的 `$defs` 供其他轨道直接引用。JSON 数据样例不加 SPDX 注释，以保留真实的帧内容；schema 则在 `$comment` 中注明许可证。

`examples/device/vm.<topic>.full.json` 包含全部已定义字段；`min.json` 只含所选状态的必选字段。详情消息也分别提供两个样例。最少字段不意味着零值填充：缺测指标省略，真实的零计数保留。空的天气 VM 只含 `rev`，表示目前没有天气数据；是否启用天气由 welcome.features 表示。

C2-01 批次 A 增补 `cfg.home_face`、`volume`、`assistant`，并允许 `key3_long.action=talk`；`cfg.full.json` 展开所有设置（含宏动作的 slot），`cfg.min.json` 展示空的局部更新，`cfg.talk.json` 展示按住说话。schema 的默认值仅说明新设备的初始设置，校验器不把省略的字段补进消息。AI 账号增加可选的 `breaker_open`，只有提供方的真实熔断状态可驱动该显示，不改变账号 status。`pet` 仍是主页表情面的数据主题，第二阶段不再提供独立的 `pet` 卡；语音状态的契约等待硬件 spike 后的 C2-02。

C2-03 独立定义非音频助手：`schemas/assistant-config.json` 校验可选配置，`schemas/rest/assistant-*.json` 校验文字对话、SSE 事件、永久记录、记忆与附件。`examples/rest/config.assistant.json` 展开新配置；`examples/http/assistant.http` 给出每条新 HTTP 请求；SSE 样例用 `{event,data}` 表示线上的 event / data 两行，不把 JSON 包装发到线上。`assistant-message.export.jsonl` 展示导出中的工具请求 / 结果配对，T6 同时扫描 JSON 与 JSONL。模型输出预算小于总上下文预算、设备归属、取消与权限闸门由配置 / HTTP 运行边界校验，标准 JSON Schema 不做跨字段算术、数据库查找或执行授权。

检查捕获的 demo 帧（每行一份完整 JSON，不能混入日志）：

```sh
uv run --project scripts python scripts/check_protocol.py --transport device demo-frames.jsonl
uv run --project scripts python scripts/check_protocol.py --stdin --transport device
uv run --project scripts python scripts/check_protocol.py --schema rest/host.json host.json
```

HUB-07 完成后，BE 负责把 demo 实际输出的 JSONL 送入同一检查器并接入 Hub CI。检查器发现错误时返回非零，并输出文件 / 行号与 `$.d.accounts[0]...` 这样的字段路径，不回显帧值。CDC 样例保存为带 LF 的单行 JSON；WS / REST 样例可以缩进排版。

schema 允许接收方忽略未知字段；T6 对未知消息类型报错，以避免发送方拼错 `t`。运行时仍必须按 PROTOCOL 忽略未知消息。跨消息的请求关联、rev 单调性、base64 解码后的 JPEG 尺寸和编码、密文认证、导入块顺序由各自输入边界处理，JSON Schema 不执行这些操作。Vault 的 `key_b64` / `salt_b64` 样例是公开固定字节，不是真实密钥；分块样例中的 `b64` 仅验证消息形状，不是一份可恢复的密码库。封面内联样例是原创纯色 160×160 baseline JPEG。

修改流程：先提出原因与具体字段改法，再单独修改 PROTOCOL / CONFIG、schema 和样例；维护者批准合入后，各实现轨道更新代码。不兼容变更提升 proto；增加可选字段或消息类型等兼容变更不提升。提交前执行：

```sh
uv run --project scripts python scripts/check_protocol.py
python scripts/check_no_personal.py
python scripts/check_license.py
python firmware/tools/gen_theme.py --check
```
