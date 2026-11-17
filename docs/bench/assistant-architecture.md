# Pi 助手与永久存储设计

2026-10-24。范围：PLAN-v2 D30–D32、C2-03、BE2-10、BE2-11。维护者要求使用 Pi SDK，自行编排助手；本报告中的参考项目仅用于比较设计，不作为产品依赖。

## 取舍与来源

| 参考 | 可借鉴的边界 | nuka 的做法 |
|---|---|---|
| [Pi agent-core](https://github.com/earendil-works/pi/tree/main/packages/agent) | 流式 agent 循环、显式工具列表、可中止任务 | 仅安装 agent-core / pi-ai，Python Hub 经本机 stdio 调用，不载入编码工具或个人扩展 |
| [Pi 会话格式](https://github.com/earendil-works/pi/blob/main/packages/coding-agent/docs/session.md) | 持久事件与模型上下文分开；会话条目有稳定标识 | SQLite 消息为事实记录，SDK 原生消息保存在内部 payload；JSONL 为导出格式，不直接把整份档案发给模型 |
| [Letta 的上下文层次](https://docs.letta.com/v1-sdk/concepts/memory/context-hierarchy) | 当前上下文与外部长期数据有不同生命周期 | 最近20轮是工作上下文，完整记录永久保存；长期记忆独立按设备隔离 |
| [Mem0 的写入与检索](https://docs.mem0.ai/core-concepts/how-it-works) | 提取的事实与逐字记录不同；检索必须有身份范围 | 永久对话与长期事实分表；先只保存用户明确要求记住的内容，来源可追溯，不做后台自动抽取 |

Pi 官方仓库已从原地址迁移到 earendil-works/pi。本次核对 npm 的 agent-core / pi-ai 均为1.4.0，MIT，要求 Node≥22.19。准确直接与传递依赖、完整性哈希以随代码提交的 package-lock 为准；部署从官方 npm registry 安装且禁用 lifecycle scripts。

## 运行与权限

Python Hub 保留 REST / WS、区域判定、会话认证、CSRF、密钥读取、语音与数据存储。每轮对话启动一个 Pi 子进程，输入只有当前模型配置、从钥匙串读取的临时密钥、有限上下文与注册的工具。命令行和环境中不放密钥；子进程没有独立网络监听端口。

Pi 产生工具调用后，把名称和参数交还 Hub。Hub 对照已启用工具的 schema 校验，敏感调用等待设备确认，拒绝、超时、断开或取消不得执行。MCP 的 readonly 标注只能影响默认展示，不能授予写权限。Pi 没有读取配置、终端、文件系统或 Vault 的默认工具。

取消时关闭本轮子进程、取消待确认工具和下行播放；错误只返回稳定错误类型，不回显上游响应体或凭据。模型请求不自动重试，不跟随跨源重定向，避免同一敏感动作被隐式重复。

## 数据与恢复

- 永久消息保存设备、会话、消息ID、UTC时间、角色和内容，工具调用与结果保留关联；稳定ID与事务避免重复写入。重启读取既有会话，不依赖内存还在。
- 模型上下文按完整轮次裁剪，不能留下没有请求的工具结果。20轮不是保留期限，容量同时受模型配置的 context_window 与 max_output_tokens 约束。
- 长期记忆带来源消息。每轮最多检索8条、合计8000字符；作为不可信资料处理，不能成为 system 指令或改变权限。
- 关闭记录只停止新写入；清空才删除既有数据。删除会话同时删除其消息、工具记录及以这些消息为来源的记忆。附件可跨会话使用，另行显式删除。
- 附件以 Hub 生成的 opaque ID 存在数据目录，原文件名只作显示；流式限制64MiB（可配置），不得使用请求中的路径读写，也不自动执行或内联渲染上传内容。原始录音默认只在内存中流转。
- SQLite 使用 WAL 与在线 backup API 创建一致快照；恢复需停服、核验数据库完整性与附件清单，不能直接复制运行中的 sqlite 文件。备份文件与源数据库均不进仓库。

## 服务器与 USB

服务器 nuka 服务只绑定回环端口，由 Cloudflare Tunnel 转发，沿用 [Cloudflare 官方隧道流程](https://developers.cloudflare.com/tunnel/get-started/)。admin 令牌只在创建时显示，服务器仅保存哈希；Web 使用 HttpOnly / SameSite=Strict cookie，写请求携带 CSRF。公网配置遵循 C2-03 的显式开关与 Access 身份校验，不把回环代理流量错误归为 local。`/hook/*` 与 Web 审批继续对 public 返回404。

服务器没有用户电脑上的 USB。首次配网仍在插着设备的本机完成，写入服务器 wss 地址和设备专用令牌；后续设置与文件管理使用服务器网页。Wi-Fi密码更新始终走串口，Vault 不进入服务器。

## 验收边界

设计已确定不代表上线完成。C2-03 需先冻结 schema / 样例，BE2-10 的重启、取消、导出、删除、备份恢复及附件越界均需记录实际验证；BE2-11 的 Access、隧道与公网区域矩阵需在部署后实测。没有物理设备确认时，不记敏感工具验收通过。
