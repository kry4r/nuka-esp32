# 语音助手

适用于 Windows、macOS 和 Linux；需要 Python 3.11+、uv、Node 22.19+ 和已配对的设备。以下命令从源码仓库根目录运行，前置安装见[源码构建](11-build-from-source.zh-CN.md)。安装依赖不会下载识别模型，也不会请求任何模型服务。设备语音的启用以 [CONFIG](../CONFIG.md) 和 [PROTOCOL](../PROTOCOL.md) 冻结的音频契约为准；仅装好适配器不代表实机验收通过。

## 安装与验证

Windows PowerShell：

```powershell
uv sync --project hub --locked --extra voice
npm --prefix hub/src/nuka_hub/assistant/pi ci --ignore-scripts --no-audit --no-fund
node --version
uv run --project hub --extra voice python -c "import sherpa_onnx, edge_tts, mcp; print('voice dependencies available')"
Get-Command ffmpeg
```

macOS：

```sh
uv sync --project hub --locked --extra voice
npm --prefix hub/src/nuka_hub/assistant/pi ci --ignore-scripts --no-audit --no-fund
node --version
uv run --project hub --extra voice python -c 'import sherpa_onnx, edge_tts, mcp; print("voice dependencies available")'
command -v ffmpeg
```

Linux：

```sh
uv sync --project hub --locked --extra voice
npm --prefix hub/src/nuka_hub/assistant/pi ci --ignore-scripts --no-audit --no-fund
node --version
uv run --project hub --extra voice python -c 'import sherpa_onnx, edge_tts, mcp; print("voice dependencies available")'
command -v ffmpeg
```

应看到 Node 22.19 或更新版本，以及 `voice dependencies available`。TTS 还需要 PATH 中的外部 [FFmpeg](https://ffmpeg.org/download.html)，用于解码并重采样为单声道、16bit PCM。Edge 返回 24kHz MP3；Cloudflare MeloTTS 返回 MP3 字节或 JSON 中的 base64；OpenAI 兼容适配器请求 WAV。音频只在内存中流转，打断会终止解码进程。设备最终采样率和分帧由音频契约规定。

使用系统包管理器，或 FFmpeg 官方下载页链接的构建安装 FFmpeg。macOS 可运行 `brew install ffmpeg`，Ubuntu 可运行 `sudo apt-get install ffmpeg`；Windows 解压合适的构建并把 `bin` 加到 PATH。三平台都运行 `ffmpeg -version`、`ffmpeg -L` 和 `ffmpeg -buildconf`：选择 LGPL 构建或 GPLv3 构建，不使用 GPL-2.0-only 或带 `--enable-nonfree` 的构建。Hub 不分发 FFmpeg。修改 PATH 后重启后台 Hub 服务。

## 模型与密钥

先备份私有 `config.toml`，再配置 Pi 使用的语言模型：

```toml
[assistant]
enabled = true
name = "Nuka"
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
```

Windows PowerShell、macOS 和 Linux 都运行 `uv run --project hub --extra voice nuka secret set assistant.llm`，在提示中输入密钥，保存到系统钥匙串。API 密钥不会发到设备，也不会由 Web 读回。无需密钥的本地接口使用 `key_secret = ""`。按实际模型容量设置上下文预算；20 轮只控制模型输入，完整对话永久保留到主动删除，长期记忆单独管理。服务器配置见[部署引导](15-server-deployment.zh-CN.md)。

运行 `uv run --project hub --extra voice nuka doctor`，再运行 `uv run --project hub --extra voice nuka hub`。Hub 启动后 doctor 应报告 `hub_http ok`，detail 为 `Hub health reachable`。在本地管理网页登录，进入助手页发送一句文字问题；有回答表示 LLM 已配置，不能据此判断麦克风、ASR 或 TTS 已通过。

## 选择语音提供方

| 提供方 | 选择与限制 |
|---|---|
| sherpa-onnx 本地 Whisper | 使用 CPU，录音不离开 Hub。中文选多语言模型，不能选 `.en`。Whisper 权重单独下载，许可证为 MIT；不自动下载或打包 SenseVoice 权重。 |
| Cloudflare ASR | `@cf/openai/whisper-large-v3-turbo`，输入 base64 WAV，语言填 `zh`。在语音配置中填自己的账号级 Workers AI base URL 和钥匙串名称。 |
| OpenAI 兼容 ASR | 配置 base URL、模型、语言和密钥名。上游需支持 multipart `POST /audio/transcriptions`，以 JSON `text` 返回文本。 |
| Edge TTS | 明确选择音色，例如中文的 `zh-CN-XiaoxiaoNeural`，并设置语速。社区客户端使用非官方微软接口，可能失效，没有可用性承诺；失败时切换已配置的 Cloudflare 或 OpenAI 兼容 TTS。 |
| Cloudflare TTS | `@cf/myshell-ai/melotts`，语言明确填 `zh`。上游模型支持中英混合；依赖它之前先手动确认当前账号能成功合成，目录中存在不等于服务可用。 |
| OpenAI 兼容 TTS | 上游需支持 `POST /audio/speech` 的模型、音色、语速和 WAV 返回。模型与音色名称由提供方决定。 |

2026-10-24 核对的 [Cloudflare 模型目录](https://developers.cloudflare.com/workers-ai/models/)仍有 Whisper turbo 和 [MeloTTS](https://developers.cloudflare.com/workers-ai/models/melotts/)。[Whisper 支持中文](https://github.com/openai/whisper/blob/main/whisper/tokenizer.py)，[MeloTTS 上游说明支持中英混合](https://github.com/myshell-ai/MeloTTS)。Deepgram 已在 [Nova-3 更新](https://developers.deepgram.com/changelog/2026/3/31)中加入普通话，但 Cloudflare 托管版本还需要单独实测，本适配器不选择 Nova-3。Aura-2-en / Aura-2-es 分别用于英语和西语，不能当作中文音色。

Cloudflare 按神经元计费，每日免费额度由该账号下的请求共享。当前[价格页](https://developers.cloudflare.com/workers-ai/platform/pricing/)列出 Whisper turbo 每分钟输入录音 46.63 神经元、MeloTTS 每分钟输出音频 18.63 神经元；Aura-2 每 1000 个输入字符 2727.27 神经元。计价单位不同，应先估算用量再比较，启用付费套餐前重新核对价格。

## 本地 Whisper 模型缓存

模型缓存在配置的 Hub 数据目录下，位于仓库之外。从[官方 ASR releases](https://github.com/k2-fsa/sherpa-onnx/releases/tag/asr-models)下载多语言 `sherpa-onnx-whisper-tiny.tar.bz2`，格式参照 [sherpa Whisper 官方示例](https://github.com/k2-fsa/sherpa-onnx/blob/master/python-api-examples/offline-whisper-decode-files.py)。中文不要下载名称相近、带 `.en` 后缀的纯英语模型。

Windows PowerShell：

```powershell
$modelCache = uv run --project hub --extra voice python -c "from nuka_hub.core.config import config_path,load_config,data_path; print(data_path(load_config(config_path()))/'models')"
New-Item -ItemType Directory -Force -Path $modelCache
curl.exe --fail --location --output "$modelCache/whisper-tiny.tar.bz2" https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-whisper-tiny.tar.bz2
tar -xjf "$modelCache/whisper-tiny.tar.bz2" -C $modelCache
Get-ChildItem "$modelCache/sherpa-onnx-whisper-tiny"
```

macOS：

```sh
model_cache=$(uv run --project hub --extra voice python -c 'from nuka_hub.core.config import config_path,load_config,data_path; print(data_path(load_config(config_path()))/"models")')
mkdir -p "$model_cache"
curl --fail --location --output "$model_cache/whisper-tiny.tar.bz2" https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-whisper-tiny.tar.bz2
tar -xjf "$model_cache/whisper-tiny.tar.bz2" -C "$model_cache"
ls "$model_cache/sherpa-onnx-whisper-tiny"
```

Linux：

```sh
model_cache=$(uv run --project hub --extra voice python -c 'from nuka_hub.core.config import config_path,load_config,data_path; print(data_path(load_config(config_path()))/"models")')
mkdir -p "$model_cache"
curl --fail --location --output "$model_cache/whisper-tiny.tar.bz2" https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-whisper-tiny.tar.bz2
tar -xjf "$model_cache/whisper-tiny.tar.bz2" -C "$model_cache"
ls "$model_cache/sherpa-onnx-whisper-tiny"
```

应看到 `tiny-encoder.int8.onnx`、`tiny-decoder.int8.onnx` 和 `tiny-tokens.txt`，随后在语音配置中选择这些文件。首次识别加载一次，Hub 重启后复用磁盘文件。识别在线程中运行；打断会立即丢弃最终文本，不会继续 TTS 或工具调用。

## 验证、回滚与排错

三平台离线开发均运行下列命令。HTTP、Edge 音频块与本地识别结果都来自假数据，不请求任何模型或搜索服务：

```sh
uv run --project hub --locked --extra voice pytest hub/tests/test_t3_voice.py
```

应有 8 个 fixture 测试通过。这些检查验证上游数据形状、请求形式和内部接口，不能证明音质、模型实际可用性或端到端延迟。手工验收时记录松开按键到首音频的时间、ASR 提供方、模型、网络条件与首句 TTS 时间；目标是在线 2.5s、本地 ASR 4s，如实填写实测值，不用假数据时延替代。

回滚时恢复配置备份，或关闭 `assistant.enabled` 后重启。源码环境运行 `uv sync --project hub --locked` 可移除语音可选包。确认没有其他程序使用后，才删除单独下载的模型缓存。除非明确要清空，否则保留对话数据库和钥匙串；需要删除模型密钥时在同一环境运行 `nuka secret rm assistant.llm`。

| 现象 / doctor 输出 | 排查 |
|---|---|
| `hub_http degraded`、`Hub not reachable; start nuka hub and check hub.port` | 使用预期配置启动 Hub，检查端口。 |
| `hub_http ok`，但文字对话提示 Pi 不可用 | 检查 Node 版本，执行前面的锁定 npm 安装。doctor 的可达性不是模型可用性检测。 |
| `hub_http ok`，本地 ASR 加载失败 | 检查 voice 可选依赖、三个 Whisper 文件、服务账号权限和多语言版本。doctor 暂无 ASR 模型探测项。 |
| `hub_http ok`，TTS 解码失败或找不到程序 | 在服务的 PATH 下运行 `ffmpeg -version`；提供方返回的压缩音频不能直接作为设备 PCM。 |
| `hub_http ok`，Edge / Cloudflare 返回 403 / 500 | 检查上游服务与账号权限，停止重复手动尝试，切换提供方。适配器不会自动重试。 |

上述 doctor 结果只说明 Hub 连接情况，不表示已请求模型，也不表示实机录音与回放已经通过。
