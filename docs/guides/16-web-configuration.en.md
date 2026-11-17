# Web forms, text assistant and permanent archive

Use this guide to configure Hub integrations, try Pi text conversations, upload reference files and manage saved records. Install the Hub using the [Windows](02-install-hub-windows.en.md), [macOS](02-install-hub-macos.en.md) or [Linux](02-install-hub-linux.en.md) guide and prepare an admin token. Chat also needs the [Pi runtime and model configuration](13-voice-assistant.en.md). Remote management requires [Cloudflare Access and server deployment](15-server-deployment.en.md).

Text chat works independently. Device voice, voice previews and physical confirmation still depend on the audio contract and firmware. Without a confirmation transport, memory creation returns 409 and sensitive tools do not execute.

## Start and verify on all three platforms

Run the same commands in Windows PowerShell, macOS Terminal or a Linux shell:

```sh
nuka hub
nuka doctor
```

Open `http://127.0.0.1:8790` and sign in with an admin token. `nuka hub --demo` provides synthetic monitoring data and a fixed example device; text chat still uses the configured model service.

1. Open Config and expand an integration. Add or remove array rows and edit dictionary keys and value types. Ordinary settings need no TOML editing. Duration inputs use seconds.
2. Secret fields contain reference names, such as `assistant.llm`. Set the actual value using the separate Secrets form. Values are never returned to the browser or device.
3. Validation errors appear beside the relevant form group. A valid save preserves existing comments and order and first creates a `.bak.<timestamp>` file. Concurrent edits return 409 and require reloading.
4. Assistant, servers, alerts, AI, CLI assistant, weather and notification changes follow the existing reload rules. Listen addresses, ports and security settings require restarting the Hub; see [CONFIG](../CONFIG.md) for other fields.
5. Advanced TOML edits the original file using the same validation, backup and concurrency checks.

## Configure text chat

Open Assistant → Persona and model. Enter the API base URL, model name, secret reference, actual context capacity and persona, then enable and save. The equivalent placeholder configuration is:

```toml
[assistant]
enabled = true
name = "Nuka"
system_prompt = "You are Nuka. Reply briefly."
lang = "en"
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

For a local model requiring no authentication, leave `key_secret` empty. Set `context_window` to the actual model capacity. Before sending, the Hub chooses recent complete turns using a conservative byte estimate and reserves the output budget; model capacities vary.

Select a device registered at this Hub and send a message. Confirm the answer appears incrementally. Stop cancels the turn and unfinished tools. New conversation starts separate context. Pair a device first if the list is empty.

## Files, records and memories

- Uploads default to 64 MiB and receive opaque server IDs. Select up to eight files to attach to the next message. The assistant can read only the files explicitly attached to that turn. Files are never automatically unpacked or executed; downloads always use attachment disposition. Vault backups use the USB CLI and are rejected by the Web uploader.
- Complete conversations remain in SQLite under the Hub data directory without automatic expiry. Twenty turns limits model context only. Filter by session, load older pages and export JSONL.
- Deleting a session cancels its active turn before removing messages and memories sourced from them. Delete attachments separately.
- Memories contain explicitly saved facts, isolated by device. No background extraction runs. Disabling memory retains saved records; deletion never reconstructs facts from old messages. Model initiated memory writes and deletions require physical device approval.
- Disabling `assistant.history.enabled` stops new archive writes. Existing records remain readable, exportable and deletable. Temporary context then disappears when the Hub restarts.
- Follow [server deployment](15-server-deployment.en.md) for backups and offline recovery. Do not copy a live SQLite database file directly.

## Roll back or remove

Disable the assistant to stop new chats while keeping archive controls available. To restore configuration, stop the Hub, copy the relevant `.bak.<timestamp>` over `config.toml` and restart. Remove unused secret references, then delete their values in Secrets. Export anything you need before explicitly deleting conversations, memories or files.

## Troubleshooting

| Symptom | Diagnosis and action |
|---|---|
| Web page is unreachable | `nuka doctor` should report `hub_http`, `ok`, `Hub health reachable`. If degraded, start the Hub and check the listen port. |
| Remote Config or Assistant is hidden | Check `remote_admin`, Access team and aud, verified Access JWT and the Hub admin session. Bearer alone cannot manage remotely. Restart after security changes. Hooks and approvals remain unavailable publicly. |
| Save reports changed content | 409 protects concurrent edits. Reload before editing again. |
| Chat returns 503 | Check assistant enabled state, Node >=22.19 and installed Pi dependencies, then model URL/name and secret reference. A healthy doctor HTTP check does not prove model connectivity. |
| Memory creation returns 409 | No device confirmation transport is available yet. Wait for the matching firmware and contract integration. |
| Upload returns 400/413 | Check filename, Vault type and maximum size. Names cannot contain paths; chunked requests are measured cumulatively too. |
