# 网页表单、文字助手与永久记录

适用场景：在浏览器里配置 Hub 集成、测试 Pi 文字助手、上传参考文件和管理记录。先按 [Windows](02-install-hub-windows.zh-CN.md)、[macOS](02-install-hub-macos.zh-CN.md) 或 [Linux](02-install-hub-linux.zh-CN.md) 引导安装 Hub，准备 admin 令牌。助手还需要 [Pi 运行时与模型配置](13-voice-assistant.zh-CN.md)。远程网页需先完成 [Cloudflare Access 与服务器部署](15-server-deployment.zh-CN.md)。

当前文字功能可独立使用。设备语音、音色试听和物理确认通道仍取决于音频契约及固件；没有确认通道时，新增记忆返回 409，敏感工具不会执行。

## 三平台启动与验证

Windows PowerShell、macOS Terminal、Linux shell 使用相同命令：

```sh
nuka hub
nuka doctor
```

浏览器打开 `http://127.0.0.1:8790`，用 admin 令牌登录。`nuka hub --demo` 提供合成监控数据和固定演示设备，文字助手仍按配置使用模型；它不会替代上游模型服务。

1. 进入“配置”，展开对应集成。数组可逐条添加、删除；字典可填写键名和类型。普通字段不需要编辑 TOML。时间间隔表单使用秒。
2. 密钥字段只填引用名称，例如 `assistant.llm`；在页面下方“密钥”表单写入实际值。值不会回显，也不会返回给设备。
3. 保存时，错误显示在对应表单组下。有效修改保留原文件的注释和顺序，并先创建 `.bak.<时间戳>` 备份。另一处保存过配置会返回 409，需要重新加载。
4. `assistant`、服务器、告警、AI、CLI 助手、天气和通知按已有热重载规则生效。监听地址、端口和安全设置需要重启 Hub；其他字段的生效方式见 [CONFIG](../CONFIG.md)。
5. “高级 TOML”可编辑原文件，使用相同校验、备份和并发保护。

## 设置文字助手

进入“助手 → 人设与模型”，填写模型基础地址、模型名、密钥名称、上下文容量和人设，然后启用助手并保存。以下是同一配置的占位符示例，无密钥的本地模型可将 `key_secret` 留空：

```toml
[assistant]
enabled = true
name = "Nuka"
system_prompt = "You are Nuka. Reply briefly."
lang = "zh-CN"
max_output_tokens = 1024

[assistant.llm]
base_url = "https://llm.example.com/v1"
model = "<MODEL>"
key_secret = "assistant.llm"
temperature = 0.7
context_window = 32768

[assistant.history]
enabled = true
context_turns = 20

[assistant.memory]
enabled = true

[assistant.uploads]
max_bytes = 67108864
```

`context_window` 应按实际模型容量修改。Hub 在发送前按保守字节估算选择最近的完整轮，并预留输出预算；不同模型不共享固定容量。

选择本 Hub 已注册的设备，发送文字，确认回复逐段出现。点“停止”会取消这一轮及未完成的工具；“新对话”创建独立上下文。没有设备时先完成配对。

## 文件、记录和记忆

- 上传默认最多 64 MiB，使用服务器生成的文件 ID。勾选最多 8 个文件附在下一条消息；助手只可读取这一轮明确附加的文件。文件不会自动解包或执行，下载始终作为附件。Vault 备份只走 USB CLI，网页拒绝上传。
- 完整对话保存在 Hub 数据目录的 SQLite 中，不自动过期；最近 20 轮仅限制模型上下文。页面支持按会话过滤、游标翻页和 JSONL 导出。
- 删除会话会先取消活动轮，再删除消息和来源记忆；附件需单独删除。
- 记忆是显式保存的事实，按设备隔离，不会后台自动抽取。关闭记忆不删已有记录；删除后不会从旧消息回填。模型请求写入或删除记忆必须经过设备确认。
- 关闭 `assistant.history.enabled` 后不写入新对话，已存记录仍可查看、导出和删除。此时临时上下文在 Hub 重启后消失。
- 备份和离线恢复流程见 [服务器部署](15-server-deployment.zh-CN.md)。不要直接复制正在写入的 SQLite 文件。

## 回滚与卸载

在表单中关闭助手即可停止新对话，已存文件和记录保持可管理。需要回滚配置时，停 Hub，把对应 `.bak.<时间戳>` 复制回 `config.toml`，再启动。删除不再使用的密钥引用后，可在“密钥”中删除其实际值。删除记录、记忆和文件是显式操作，先导出需要保留的内容。

## 排错

| 现象 | 诊断与处理 |
|---|---|
| 网页打不开 | `nuka doctor` 的 `hub_http` 应显示 `ok: Hub health reachable`；若为 `degraded`，先启动 Hub 并检查监听端口。 |
| 公网看不到配置或助手 | 检查 `remote_admin`、Access 的域名和 aud、有效 Access JWT、Hub admin 会话。仅带 Bearer 不允许远程管理；修改安全设置后重启。Hooks 与审批始终不开放公网。 |
| 保存提示内容已改变 | 409 并发保护。先重新加载再修改，避免覆盖另一处保存的内容。 |
| 对话返回 503 | 检查助手是否启用、Node 是否至少 22.19、Pi 依赖是否安装；再核对模型地址、模型名和密钥引用。`doctor` 的 HTTP 检查通过不代表模型已连通。 |
| 记忆保存返回 409 | 当前没有可用的设备确认通道。等待对应固件与契约接线；不要绕过确认。 |
| 上传返回 400/413 | 检查文件名、Vault 类型和大小上限。上传不接受路径名，分块请求同样累计计算大小。 |
