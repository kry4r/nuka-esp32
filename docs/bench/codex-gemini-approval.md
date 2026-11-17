# Codex / Gemini 审批能力调研

日期：2026-10-18。能力范围先按官方文档核对，随后完成真实 Codex CLI 的状态 hook 验证。

| CLI | 官方文档中的能力 | v1 处理 |
|---|---|---|
| Codex | hooks 包括 PermissionRequest，支持 allow/deny；定义需要通过 /hooks 审阅与信任 | 按 SPEC §7.7 保持状态同步，安装 SessionStart/PreToolUse/PostToolUse/Stop/SessionEnd，不新增审批来源 |
| Gemini | BeforeTool 可阻止工具；Notification 用于观察。它与 Claude 原生 HTTP PermissionRequest 的等待/返回语义不同 | 按 SPEC 保持状态同步，不把工具拦截误当作同一种审批契约 |

来源：[Codex hooks](https://learn.chatgpt.com/docs/hooks)、[Gemini hook reference](https://geminicli.com/docs/hooks/reference/)、[Claude hook reference](https://code.claude.com/docs/en/hooks)。

真实 Codex 验证：本机 CLI 0.163.0，在临时 Git 工作目录中通过 `nuka hooks install codex --home <TEMP_DIR>` 安装五个事件，使用进程环境中的专用 hook 令牌。维护者在 `/hooks` 中审阅并授信，然后让 CLI 用 shell 读取仅含占位文本的 `example.txt`。

| 事件 | Hub 实际状态 | 结论 |
|---|---|---|
| SessionStart | idle | 通过 |
| PreToolUse | tool | 通过 |
| PostToolUse | thinking | 通过 |
| Stop | done | 通过 |
| SessionEnd | 会话从列表移除 | 通过 |

证据来自真实 Hub 的 `/api/v1/agents` 观测，按同一匿名会话顺序记录；没有伪造输入事件。该轮 Hub 运行 demo 模式，证明真实 CLI 到 Hub 的状态转发；设备显示仍待 USB 和配网恢复后的 INT-01 验收，不能算作 E3/E4 通过。

退出后执行卸载，临时 hook 文件恢复为原先不存在的状态，并撤销专用令牌。用户原有 Codex 配置未改动。原始观测仅保存在 git 忽略的构建目录，报告不包含真实路径、主机名或令牌。

建议：后续版本若需支持 Codex/Gemini 设备审批，先冻结各自事件、等待时限、终端接管和响应格式，再修改契约。当前实现没有启用该建议。
