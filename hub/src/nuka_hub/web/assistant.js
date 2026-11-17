// SPDX-License-Identifier: AGPL-3.0-or-later
import {schemaForm} from "/forms.js";

function el(tag, text = "", className = "") { const node = document.createElement(tag); node.textContent = text; node.className = className; return node; }
function button(text, operation) { const value = el("button", text); value.type = "button"; value.onclick = operation; return value; }

export function assistantPage({api, t:translate, show, csrf}) {
  const labels = new Map();
  const t = (value) => { const output = translate(value); labels.set(output, value); return output; };
  const root = document.querySelector("#assistant-root");
  let controller, session = "", config, mtime, editor, cursors = {}, selected = new Set();
  const device = el("select"), sessionLabel = el("output"), status = el("p");
  const deviceLabel = el("label", t("Device / 设备")); deviceLabel.append(device);
  const toolbar = el("div", "", "assistant-toolbar");
  toolbar.append(deviceLabel, button(t("New conversation / 新对话"), () => { stop(); session = ""; sessionLabel.textContent = ""; transcript.replaceChildren(); }), sessionLabel);
  root.append(toolbar, status);
  const settings = el("details", "", "card"); settings.append(el("summary", t("Persona and model / 人设与模型")));
  const settingsForm = el("form", "", "settings-form"), settingsFields = el("div"); const save = el("button", t("Save assistant / 保存助手")); settingsForm.append(settingsFields, save); settings.append(settingsForm); root.append(settings);
  const chat = el("div", "", "card"); chat.append(el("h2", t("Text conversation / 文字对话")));
  const transcript = el("div", "", "transcript"); transcript.setAttribute("aria-live", "polite"); transcript.setAttribute("role", "log");
  const chatForm = el("form", "", "chat-form"), text = el("textarea"); text.rows = 3; text.maxLength = 16384; text.required = true; text.setAttribute("aria-label", t("Message / 消息"));
  const send = el("button", t("Send / 发送")), stopButton = button(t("Stop / 停止"), stop); stopButton.disabled = true;
  chatForm.append(text, send, stopButton); chat.append(transcript, chatForm); root.append(chat);
  const fileCard = el("div", "", "card"); fileCard.append(el("h2", t("Files / 文件")), el("p", t("Select up to 8 files to attach to the next message. Files stay on the server until deleted. / 最多选择 8 个文件附加到下一条消息。文件保存在服务器上，直到显式删除。")));
  const fileInput = el("input"); fileInput.type = "file"; fileInput.setAttribute("aria-label", t("Upload file / 上传文件"));
  const fileList = el("div"), fileMore = button(t("Load more / 加载更多"), () => run(() => loadFiles(true)));
  fileCard.append(fileInput, button(t("Upload / 上传"), () => run(upload)), fileList, fileMore); root.append(fileCard);
  const historyCard = el("div", "", "card"); historyCard.append(el("h2", t("Permanent archive / 永久记录")));
  const historySession = el("input"); historySession.placeholder = t("Session ID (empty means all) / 会话 ID（留空为全部）"); historySession.setAttribute("aria-label", t("Session filter / 会话筛选"));
  const historyList = el("div"), historyMore = button(t("Older messages / 更早的消息"), () => run(() => loadHistory(true)));
  const exportLink = el("a", t("Export JSONL / 导出 JSONL")); exportLink.download = "nuka-history.jsonl";
  historyCard.append(historySession, button(t("Load archive / 查看记录"), () => run(() => loadHistory())), exportLink, historyList, historyMore); root.append(historyCard);
  const memoryCard = el("div", "", "card"); memoryCard.append(el("h2", t("Saved memories / 已保存的记忆")), el("p", t("Saving from the assistant requires approval on the device. Deleted memories are never rebuilt from old messages. / 助手写入记忆需要设备确认；删除后不会从旧消息自动重建。")));
  const memoryQuery = el("input"); memoryQuery.placeholder = t("Search saved facts / 搜索已保存的事实"); memoryQuery.setAttribute("aria-label", t("Search memories / 搜索记忆"));
  const memoryList = el("div"), memoryMore = button(t("Load more / 加载更多"), () => run(() => loadMemories(true)));
  const memoryForm = el("form"), memoryText = el("input"); memoryText.required = true; memoryText.maxLength = 2000; memoryText.setAttribute("aria-label", t("Fact to remember / 需要记住的事实"));
  memoryForm.append(memoryText, el("button", t("Request device approval / 请求设备确认")));
  memoryCard.append(memoryQuery, button(t("Search / 搜索"), () => run(() => loadMemories())), memoryList, memoryMore, memoryForm); root.append(memoryCard);

  function query(extra = {}) { return new URLSearchParams({dev_id:device.value, ...extra}).toString(); }
  function stop() { controller?.abort(); }
  async function run(operation) { try { await operation(); } catch (error) { show(error.message); } }
  async function check(response) { if (!response.ok) { const value = await response.json(); throw new Error(value.error.message); } return response; }
  function message(role, content) { const row = el("div", "", `chat-message ${role}`); row.append(el("small", role === "user" ? t("You / 你") : role === "assistant" ? config.assistant.name : t("Tool / 工具"))); const body = el("div", content); row.append(body); transcript.append(row); transcript.scrollTop = transcript.scrollHeight; return body; }
  async function loadFiles(more = false) {
    if (!device.value) return; const data = await api(`assistant/files?${query(more && cursors.files ? {before:cursors.files} : {})}`);
    if (!more) { fileList.replaceChildren(); selected.clear(); }
    for (const item of data.items) {
      const row = el("div", "", "resource-row"), label = el("label"), checkbox = el("input"); checkbox.type = "checkbox";
      checkbox.onchange = () => { if (checkbox.checked && selected.size >= 8) { checkbox.checked = false; show(t("Attach at most 8 files / 最多附加 8 个文件")); return; } checkbox.checked ? selected.add(item.id) : selected.delete(item.id); };
      label.append(checkbox, document.createTextNode(`${item.name} · ${(item.size / 1024).toFixed(1)} KiB`));
      const download = el("a", t("Download / 下载")); download.href = `/api/v1/assistant/files/${item.id}/download?${query()}`; download.download = item.name;
      row.append(label, download, button(t("Delete / 删除"), () => run(async () => { if (!confirm(t("Delete this file permanently? / 永久删除这个文件？"))) return; await api(`assistant/files/${item.id}?${query()}`, "DELETE"); selected.delete(item.id); row.remove(); }))); fileList.append(row);
    }
    cursors.files = data.next_before; fileMore.hidden = !data.next_before;
  }
  async function upload() {
    if (!fileInput.files.length || !device.value) return;
    const file = fileInput.files[0]; await check(await fetch(`/api/v1/assistant/files?${query()}`, {method:"POST", credentials:"same-origin", headers:{"Content-Type":"application/octet-stream", "X-Filename":encodeURIComponent(file.name), "X-CSRF-Token":csrf()}, body:file})); fileInput.value = ""; await loadFiles(); show(t("Uploaded / 已上传"));
  }
  async function loadHistory(more = false) {
    if (!device.value) return; const filter = historySession.value.trim(), params = filter ? {session_id:filter} : {};
    const data = await api(`assistant/history?${query({...params, ...(more && cursors.history ? {before:cursors.history} : {})})}`);
    if (!more) historyList.replaceChildren(); exportLink.href = `/api/v1/assistant/history/export?${query(params)}`;
    for (const item of data.items) {
      const row = el("article", "", "archive-row"); row.append(el("small", `${item.role} · ${item.created_at} · ${item.session_id}`), el("p", item.text));
      row.append(button(t("Continue session / 继续会话"), () => { stop(); session = item.session_id; sessionLabel.textContent = session; historySession.value = session; text.focus(); }), button(t("Delete session / 删除会话"), () => run(async () => { if (!confirm(t("Permanently delete this session and its sourced memories? Attachments remain. / 永久删除该会话及其来源记忆？附件单独保留。"))) return; if (session === item.session_id) { stop(); session = ""; sessionLabel.textContent = ""; } await api(`assistant/history?${query({session_id:item.session_id})}`, "DELETE"); await Promise.all([loadHistory(), loadMemories()]); })));
      historyList.append(row);
    }
    cursors.history = data.next_before; historyMore.hidden = !data.next_before;
  }
  async function loadMemories(more = false) {
    if (!device.value) return; const data = await api(`assistant/memories?${query({q:memoryQuery.value, ...(more && cursors.memories ? {before:cursors.memories} : {})})}`);
    if (!more) memoryList.replaceChildren();
    for (const item of data.items) { const row = el("div", "", "resource-row"); row.append(el("p", item.text), button(t("Delete / 删除"), () => run(async () => { if (!confirm(t("Delete this memory permanently? / 永久删除这条记忆？"))) return; await api(`assistant/memories/${item.id}?${query()}`, "DELETE"); row.remove(); }))); memoryList.append(row); }
    cursors.memories = data.next_before; memoryMore.hidden = !data.next_before;
  }
  async function chatTurn() {
    const content = text.value; controller = new AbortController(); send.disabled = true; stopButton.disabled = false; message("user", content); text.value = ""; const output = message("assistant", "");
    try {
      const response = await check(await fetch("/api/v1/assistant/chat", {method:"POST", credentials:"same-origin", signal:controller.signal, headers:{"Content-Type":"application/json", "X-CSRF-Token":csrf()}, body:JSON.stringify({dev_id:device.value, text:content, ...(session ? {session_id:session} : {}), file_ids:[...selected]})}));
      const reader = response.body.getReader(), decoder = new TextDecoder(); let buffer = "";
      while (true) {
        const part = await reader.read(); buffer += decoder.decode(part.value, {stream:!part.done}).replace(/\r\n/g, "\n"); let boundary;
        while ((boundary = buffer.indexOf("\n\n")) !== -1) {
          const frame = buffer.slice(0, boundary); buffer = buffer.slice(boundary + 2); const lines = frame.split("\n"), event = lines.find((line) => line.startsWith("event:"))?.slice(6).trim(); const data = JSON.parse(lines.filter((line) => line.startsWith("data:")).map((line) => line.slice(5).trim()).join("\n"));
          if (event === "start") { session = data.session_id; sessionLabel.textContent = session; }
          else if (event === "delta") output.textContent += data.text;
          else if (event === "tool") message("tool", `${data.name} · ${data.state}${data.text ? "\n" + data.text : ""}`);
          else if (event === "error") throw new Error(data.message);
          transcript.scrollTop = transcript.scrollHeight;
        }
        if (part.done) break;
      }
    } catch (error) { if (error.name === "AbortError") output.append(document.createTextNode("\n" + t("Stopped / 已停止"))); else throw error; }
    finally { controller = null; send.disabled = !config.assistant.enabled || !device.value; stopButton.disabled = true; await loadHistory(); }
  }
  chatForm.onsubmit = (event) => { event.preventDefault(); run(chatTurn); };
  memoryForm.onsubmit = (event) => { event.preventDefault(); run(async () => { await api("assistant/memories", "POST", {dev_id:device.value, text:memoryText.value}); memoryText.value = ""; await loadMemories(); }); };
  settingsForm.onsubmit = (event) => { event.preventDefault(); run(async () => { try { config.assistant = editor.read(); await api("config", "PUT", {config, mtime}); await loadSettings(); show(t("Saved / 已完成")); } catch (error) { editor.errors(error.details); throw error; } }); };
  device.onchange = () => run(async () => { stop(); session = ""; sessionLabel.textContent = ""; transcript.replaceChildren(); cursors = {}; await Promise.all([loadHistory(), loadFiles(), loadMemories()]); });
  async function loadSettings() {
    const [schema, value, raw, state] = await Promise.all([api("config/schema"), api("config"), api("config/raw"), api("assistant/status")]); config = value; mtime = raw.mtime;
    editor = schemaForm(schema, config.assistant, t, "Assistant"); settingsFields.replaceChildren(editor.element);
    status.textContent = t(!state.enabled ? "Configure the model and enable the assistant to start chatting. / 配置模型并启用助手后即可对话。" : state.history_enabled ? "Assistant enabled · full archive on server / 助手已启用 · 完整记录保存在服务器" : "Assistant enabled · new conversations are not archived / 助手已启用 · 新对话不写入记录"); send.disabled = !state.enabled;
  }
  async function load() {
    await loadSettings(); const devices = (await api("devices")).items, previous = device.value; device.replaceChildren();
    for (const item of devices) { const option = el("option", item.name || item.dev_id); option.value = item.dev_id; device.append(option); }
    if (devices.some((item) => item.dev_id === previous)) device.value = previous;
    if (!devices.length) { status.textContent = t("Pair a device before starting a conversation. / 请先配对设备再开始对话。"); send.disabled = true; }
    await Promise.all([loadHistory(), loadFiles(), loadMemories()]);
  }
  function relabel() {
    const textNodes = document.createTreeWalker(root, NodeFilter.SHOW_TEXT);
    while (textNodes.nextNode()) { const node = textNodes.currentNode; if (labels.has(node.textContent)) node.textContent = t(labels.get(node.textContent)); }
    for (const node of root.querySelectorAll("[placeholder],[aria-label]")) for (const attribute of ["placeholder", "aria-label"]) { const value = node.getAttribute(attribute); if (labels.has(value)) node.setAttribute(attribute, t(labels.get(value))); }
  }
  return {load, stop, relabel};
}
