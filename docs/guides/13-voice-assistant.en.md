# Voice assistant

Use Windows, macOS, or Linux with Python 3.11+, uv, Node 22.19+, and a paired device. Run the following commands from a source checkout; see [build from source](11-build-from-source.en.md). Package installation does not download recognition models or contact any model service. Device voice integration follows the audio contract in [CONFIG](../CONFIG.md) and [PROTOCOL](../PROTOCOL.md); installing adapters alone does not complete hardware acceptance.

## Install and verify

Windows PowerShell:

```powershell
uv sync --project hub --locked --extra voice
npm --prefix hub/src/nuka_hub/assistant/pi ci --ignore-scripts --no-audit --no-fund
node --version
uv run --project hub --extra voice python -c "import sherpa_onnx, edge_tts, mcp; print('voice dependencies available')"
Get-Command ffmpeg
```

macOS:

```sh
uv sync --project hub --locked --extra voice
npm --prefix hub/src/nuka_hub/assistant/pi ci --ignore-scripts --no-audit --no-fund
node --version
uv run --project hub --extra voice python -c 'import sherpa_onnx, edge_tts, mcp; print("voice dependencies available")'
command -v ffmpeg
```

Linux:

```sh
uv sync --project hub --locked --extra voice
npm --prefix hub/src/nuka_hub/assistant/pi ci --ignore-scripts --no-audit --no-fund
node --version
uv run --project hub --extra voice python -c 'import sherpa_onnx, edge_tts, mcp; print("voice dependencies available")'
command -v ffmpeg
```

Expect Node 22.19 or later and `voice dependencies available`. TTS needs an external [FFmpeg](https://ffmpeg.org/download.html) executable on PATH to decode provider audio and resample it to mono 16-bit PCM. Edge delivers 24 kHz MP3; Cloudflare MeloTTS delivers MP3 bytes or JSON base64; the OpenAI-compatible adapter requests WAV. Audio stays in memory and decoding is cancelled when the turn stops. The final device rate and framing are defined by the frozen audio contract.

Install FFmpeg using your platform's package manager or a build linked from its official download page. On macOS, `brew install ffmpeg`; on Ubuntu, `sudo apt-get install ffmpeg`. On Windows, extract a suitable build and add its `bin` directory to PATH. Run `ffmpeg -version`, `ffmpeg -L` and `ffmpeg -buildconf` on all platforms. Choose an LGPL build or a GPLv3 build; exclude GPL-2.0-only or `--enable-nonfree` builds. The Hub does not redistribute FFmpeg. Restart a background Hub service after changing its PATH.

## Model and credentials

Back up your private `config.toml`, then configure the Pi language model:

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

On Windows PowerShell, macOS and Linux, run `uv run --project hub --extra voice nuka secret set assistant.llm`; the prompt stores the key in the operating system keyring. API keys are never sent to the device or returned by the Web UI. For a local endpoint that needs no key, use `key_secret = ""`. Set the actual model context capacity; 20 turns is only the input window. Full conversation archives remain until deleted, and long-term memories are separate. See [server deployment](15-server-deployment.en.md).

Run `uv run --project hub --extra voice nuka doctor`, then `uv run --project hub --extra voice nuka hub`. A running Hub should report `hub_http ok` with detail `Hub health reachable`. Log into the local administrator page and send a short text question on the assistant page. A response verifies the configured LLM, separately from microphone, ASR and TTS.

## Choose speech providers

| Provider | Selection and limits |
|---|---|
| Local sherpa-onnx Whisper | CPU recognition without sending audio off the Hub. Use a multilingual model for Chinese, not an `.en` model. Model files are supplied separately under MIT; no SenseVoice weights are downloaded or bundled. |
| Cloudflare ASR | `@cf/openai/whisper-large-v3-turbo` accepts a base64 WAV and language `zh`. Enter your own account-specific Workers AI base URL and keyring reference in the voice configuration. |
| OpenAI-compatible ASR | Configure the provider base URL, model, language and keyring reference. It must support multipart `POST /audio/transcriptions` and JSON `text` output. |
| Edge TTS | Choose an explicit voice such as `zh-CN-XiaoxiaoNeural` for Chinese, and a speed. This community client uses an unofficial Microsoft service, can stop working and has no availability promise. Switch to either configured paid-compatible or Cloudflare TTS if it fails. |
| Cloudflare TTS | `@cf/myshell-ai/melotts`, explicit `zh`, supports Chinese / English mixing in the upstream model. Do a manual availability check before relying on it; model presence in the catalog does not prove successful synthesis for your account. |
| OpenAI-compatible TTS | The provider must support `POST /audio/speech` with model, voice, speed and WAV response. Model names and voice names belong to that provider. |

As of 2026-10-24, the [Cloudflare catalog](https://developers.cloudflare.com/workers-ai/models/) lists Whisper turbo and [MeloTTS](https://developers.cloudflare.com/workers-ai/models/melotts/). [Whisper supports Chinese](https://github.com/openai/whisper/blob/main/whisper/tokenizer.py); [MeloTTS documents Chinese and English mixing](https://github.com/myshell-ai/MeloTTS). Deepgram added [Nova-3 Mandarin support](https://developers.deepgram.com/changelog/2026/3/31), but the Cloudflare-hosted variant still needs its own live language check and is not selected by this adapter. Aura-2-en and Aura-2-es are English and Spanish choices, not Chinese voices.

Cloudflare bills in neurons, with a shared daily free allowance. Its current [pricing](https://developers.cloudflare.com/workers-ai/platform/pricing/) lists Whisper turbo at 46.63 neurons / input audio minute and MeloTTS at 18.63 neurons / output audio minute; Aura-2 costs 2727.27 neurons / 1000 input characters. These units differ, so compare estimated usage rather than treating the rates as the same measure. Confirm prices before enabling a paid plan.

## Cache the local Whisper model

Create a private model cache under the configured Hub data directory, outside the checkout. Download the multilingual `sherpa-onnx-whisper-tiny.tar.bz2` from the [official ASR releases](https://github.com/k2-fsa/sherpa-onnx/releases/tag/asr-models), following the [sherpa Whisper example](https://github.com/k2-fsa/sherpa-onnx/blob/master/python-api-examples/offline-whisper-decode-files.py). Never use the similarly named English-only archive with the `.en` suffix for Chinese.

Windows PowerShell:

```powershell
$modelCache = uv run --project hub --extra voice python -c "from nuka_hub.core.config import config_path,load_config,data_path; print(data_path(load_config(config_path()))/'models')"
New-Item -ItemType Directory -Force -Path $modelCache
curl.exe --fail --location --output "$modelCache/whisper-tiny.tar.bz2" https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-whisper-tiny.tar.bz2
tar -xjf "$modelCache/whisper-tiny.tar.bz2" -C $modelCache
Get-ChildItem "$modelCache/sherpa-onnx-whisper-tiny"
```

macOS:

```sh
model_cache=$(uv run --project hub --extra voice python -c 'from nuka_hub.core.config import config_path,load_config,data_path; print(data_path(load_config(config_path()))/"models")')
mkdir -p "$model_cache"
curl --fail --location --output "$model_cache/whisper-tiny.tar.bz2" https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-whisper-tiny.tar.bz2
tar -xjf "$model_cache/whisper-tiny.tar.bz2" -C "$model_cache"
ls "$model_cache/sherpa-onnx-whisper-tiny"
```

Linux:

```sh
model_cache=$(uv run --project hub --extra voice python -c 'from nuka_hub.core.config import config_path,load_config,data_path; print(data_path(load_config(config_path()))/"models")')
mkdir -p "$model_cache"
curl --fail --location --output "$model_cache/whisper-tiny.tar.bz2" https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-whisper-tiny.tar.bz2
tar -xjf "$model_cache/whisper-tiny.tar.bz2" -C "$model_cache"
ls "$model_cache/sherpa-onnx-whisper-tiny"
```

Verify `tiny-encoder.int8.onnx`, `tiny-decoder.int8.onnx` and `tiny-tokens.txt` exist before choosing their paths in the voice configuration. Loading occurs once on first transcription; the files persist across Hub restarts. Recognition runs in a worker thread. A cancelled turn discards its eventual text; no TTS or tool call follows it.

## Verify, roll back and troubleshoot

For offline development, run the following on each platform. HTTP, Edge chunks and local recognition results are synthetic; no model or search service is contacted:

```sh
uv run --project hub --locked --extra voice pytest hub/tests/test_t3_voice.py
```

Expect eight passing fixture tests. These checks verify provider envelopes, request forms and normalization into the internal interface. They do not measure acoustic quality, real model uptime or end-to-end latency. During manual hardware acceptance, record release-to-first-audio time, ASR provider, model, network conditions and first-sentence TTS time. The target is 2.5 s online or 4 s with local ASR; report measured results without substituting fixture timings.

To roll back, restore the configuration backup or disable `assistant.enabled`, then restart Hub. Run `uv sync --project hub --locked` to remove optional voice packages from the source environment. Remove only the separately downloaded model cache when no other application uses it. Preserve the history database and keyring unless you explicitly intend to delete them. Remove a stored key with `nuka secret rm assistant.llm` in the same environment.

| Symptom / doctor output | Check |
|---|---|
| `hub_http degraded`, `Hub not reachable; start nuka hub and check hub.port` | Start the Hub with the intended config and verify its port. |
| `hub_http ok`, text replies fail with a Pi availability error | Check Node version and run the locked npm install above. Doctor reachability is not a model availability test. |
| `hub_http ok`, local ASR cannot load | Check `voice` extras, all three Whisper files, service account permissions and multilingual model choice. Doctor currently has no ASR model probe. |
| `hub_http ok`, TTS decoding fails / executable missing | Verify `ffmpeg -version` using the service's PATH; providers return encoded audio, not device-ready PCM. |
| `hub_http ok`, Edge or Cloudflare returns 403 / 500 | Check service availability and account permissions, stop repeated manual attempts and choose an alternative provider. No adapter retries automatically. |

The doctor results above describe Hub connectivity. They do not assert that a model has been contacted or that device recording/playback has passed.
