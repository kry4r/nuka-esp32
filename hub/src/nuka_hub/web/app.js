// SPDX-License-Identifier: AGPL-3.0-or-later
import {schemaForm} from "/forms.js";
import {assistantPage} from "/assistant.js";
let csrf = "", current = "status", mtime = "", signedIn = false, me;
let configEditor, configSchema;
const $ = (selector) => document.querySelector(selector);
const staticLabels = [];
const texts = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT);
while (texts.nextNode()) { const node = texts.currentNode; if (node.textContent.includes(" / ")) staticLabels.push([node, node.textContent]); }
const t = (value) => { const parts = value.split(" / "); return parts.length === 2 ? parts[$("#language").value === "zh-CN" ? 1 : 0] : value; };
const show = (text) => { $("#message").textContent = text; };
function displayValue(key, value) {
  if (value === undefined) return "—";
  if (key === "cpu") return value.pct === undefined ? "—" : `${value.pct.toFixed(1)}%`;
  if (key === "mem") return value.used_b === undefined || value.total_b === undefined ? "—" : `${(value.used_b / 1073741824).toFixed(1)} / ${(value.total_b / 1073741824).toFixed(1)} GiB`;
  if (key === "alerts") return String(value.length);
  if (key === "windows") return value.map((window) => `${window.label || window.kind}: ${window.used_pct.toFixed(1)}%`).join(" · ");
  return typeof value === "object" ? JSON.stringify(value) : String(value);
}
async function api(path, method = "GET", body) {
  const response = await fetch(`/api/v1/${path}`, {method, credentials: "same-origin", headers: {"Content-Type": "application/json", "X-CSRF-Token": csrf}, body: body === undefined ? undefined : JSON.stringify(body)});
  const data = await response.json();
  if (!response.ok) {
    const details = data.error.details?.map((d) => `${$("#language").value === "zh-CN" ? "第" : "Line "}${d.line}: ${d.msg}`).join("\n");
    const messages = {unauthorized:"请使用管理员令牌登录", forbidden:"当前请求没有权限", not_found:"此页面或操作不可用", conflict:"内容已改变，请重新加载", rate_limited:"操作过于频繁，请稍后再试"};
    const error = new Error(details || ($("#language").value === "zh-CN" ? messages[data.error.code] || data.error.message : data.error.message));
    error.details = data.error.details; error.status = response.status; throw error;
  }
  return data;
}
function table(title, rows, keys, actions = []) {
  const card = document.createElement("div"); card.className = "card";
  const heading = document.createElement("h2"); heading.textContent = t(title); card.append(heading);
  if (!rows.length) { const empty = document.createElement("p"); empty.textContent = t("No data / 暂无数据"); card.append(empty); return card; }
  const grid = document.createElement("table"), head = grid.createTHead().insertRow();
  const names = {name:"Name / 名称", type:"Type / 类型", next_try:"Next attempt / 下次尝试", online:"Online / 在线", cpu:"CPU", mem:"Memory / 内存", cpu_pct:"CPU %", alerts:"Alerts / 告警", label:"Label / 标签", status:"Status / 状态", windows:"Quota windows / 配额窗口", source:"Source / 来源", project:"Project / 项目", state:"State / 状态", fw:"Firmware / 固件", health:"Health / 健康", detail:"Detail / 详情", doc:"Guide / 引导", last_seen:"Last seen / 最后在线", created:"Created / 创建时间", last_used:"Last used / 最近使用", scope:"Scope / 作用域", tool:"Tool / 工具", summary:"Summary / 摘要", expires:"Expires / 过期时间", ts:"Time / 时间", level:"Level / 级别", title:"Title / 标题", body:"Body / 内容"};
  for (const key of [...keys, ...(actions.length ? ["Actions / 操作"] : [])]) { const th = document.createElement("th"); th.textContent = t(names[key] || key); head.append(th); }
  const body = grid.createTBody();
  for (const row of rows) {
    const tr = body.insertRow();
    const states = {true:"是", false:"否", ok:"正常", degraded:"部分可用", error:"错误", disabled:"未启用", auth_failed:"认证失败，已熔断", backoff:"退避等待", idle:"空闲", thinking:"思考中", tool:"执行工具", waiting_permission:"等待审批", done:"已完成", playing:"正在播放", paused:"已暂停", stopped:"已停止", exhausted:"已耗尽", reauth:"需要重新登录", limited:"受限"};
    for (const key of keys) {
      const cell = tr.insertCell(), value = row[key];
      if (key === "doc" && value !== undefined) { const link = document.createElement("a"); link.href = document.querySelector("footer a").href + `/blob/main/docs/guides/${value.replace(/^guides\//, "")}.${$("#language").value}.md`; link.textContent = t("Setup guide / 安装引导"); cell.append(link); }
      else cell.textContent = $("#language").value === "zh-CN" && ["online", "state", "status", "health"].includes(key) ? states[String(value)] || displayValue(key, value) : displayValue(key, value);
    }
    if (actions.length) {
      const cell = tr.insertCell();
      for (const [label, action, visible] of actions) { if (visible && !visible(row)) continue; const button = document.createElement("button"); button.textContent = t(label); button.onclick = () => act(() => action(row)); cell.append(button); }
    }
  }
  card.append(grid); return card;
}
async function act(operation) { try { await operation(); show(t("Saved / 已完成")); await refresh(); } catch (error) { show(error.message); } }
function mount(selector, child) { $(selector).replaceChildren(child); }
async function refresh() {
  if (!signedIn) return;
  const state = await api("state");
  if (current === "status") {
    const cards = [table("Hosts / 主机", state.hosts, ["name", "online", "cpu", "mem"]), table("Servers / 服务器", state.servers, ["name", "online", "cpu_pct", "alerts"]), table("AI", state.ai.accounts, ["label", "status", "windows"]), table("AI providers / AI 提供者", state.ai.providers, ["name", "type", "health", "next_try"], [["Retry / 重试", (row) => api(`ai/providers/${encodeURIComponent(row.name)}/retry`, "POST", {}), (row) => row.health === "auth_failed" && me.zone !== "public"]]), table("CLI sessions / CLI 会话", state.agents.sessions, ["source", "project", "state"]), table("Devices / 设备", state.devices, ["name", "fw", "online"]), table("Plugins / 插件", state.plugins, ["name", "health", "detail", "doc"])];
    $("#status-tables").replaceChildren(...cards);
  } else if (current === "devices") {
    mount("#device-list", table("Devices / 设备", state.devices, ["dev_id", "name", "fw", "online", "last_seen"], [["Rename / 改名", async (row) => { const name = prompt("Name / 名称", row.name); if (name) await api(`devices/${row.dev_id}`, "PATCH", {name}); }], ["Delete / 删除", (row) => api(`devices/${row.dev_id}`, "DELETE")]]));
    if (me.zone !== "public") mount("#pair-list", table("Pending / 待配对", (await api("pair/pending")).items, ["dev_id", "fw", "expires"]));
  } else if (current === "tokens") mount("#token-list", table("Tokens / 令牌", (await api("tokens")).items, ["name", "scope", "created", "last_used"], [["Revoke / 撤销", (row) => api(`tokens/${row.id}`, "DELETE")]]));
  else if (current === "notifications") mount("#notification-list", table("Recent / 最近通知", state.notify, ["ts", "level", "title", "body"]));
  else if (current === "approve") mount("#approval-list", table("Pending / 待审批", state.agents.pending, ["host", "project", "tool", "summary", "expires"], [["Allow / 允许", (row) => api(`perm/${row.rid}`, "POST", {behavior:"allow"})], ["Deny / 拒绝", (row) => api(`perm/${row.rid}`, "POST", {behavior:"deny"})]]));
  else if (current === "config") mount("#secret-list", table("Names only / 仅名称", (await api("secrets")).items, ["name", "source"], [["Delete / 删除", (row) => api(`secrets/${encodeURIComponent(row.name)}`, "DELETE")]]));
}
async function rawConfig() { const value = await api("config/raw"); $("#toml").value = value.toml; mtime = value.mtime; }
async function configuration() { const [schema, value] = await Promise.all([api("config/schema"), api("config"), rawConfig()]); configSchema = schema; configEditor = schemaForm(schema, value, t); $("#config-fields").replaceChildren(configEditor.element); }
const assistant = assistantPage({api, t, show, csrf:() => csrf});
async function enter() {
  me = await api("me"); csrf = me.csrf; signedIn = true;
  $("#login").hidden = true; $("#manager").hidden = false; $("#logout").hidden = false;
  document.querySelectorAll(".sensitive").forEach((el) => { el.hidden = me.zone === "public"; });
  let config;
  try { config = await api("config"); } catch (error) { if (error.status !== 404) throw error; }
  document.querySelectorAll(".remote-manage").forEach((el) => { el.hidden = !config; });
  $("#approval-nav").hidden = me.zone === "public" || !config?.security.web_approve_permissions;
  $("#language").value = me.lang; translate(); await refresh();
}
function translate() { const chinese = $("#language").value === "zh-CN"; document.documentElement.lang = chinese ? "zh-CN" : "en"; for (const [node, original] of staticLabels) node.textContent = t(original); document.querySelectorAll("[data-en]").forEach((el) => { el.textContent = chinese ? el.dataset.zh : el.dataset.en; }); assistant.relabel(); if (configEditor) { configEditor = schemaForm(configSchema, configEditor.read(), t); $("#config-fields").replaceChildren(configEditor.element); } }
$("#language").onchange = () => { translate(); refresh().catch((error) => show(error.message)); };
$("#login-form").onsubmit = (event) => { event.preventDefault(); act(async () => { const value = await api("login", "POST", {token: $("#admin-token").value}); csrf = value.csrf; $("#admin-token").value = ""; await enter(); }); };
$("#logout").onclick = () => act(async () => { await api("logout", "POST", {}); location.reload(); });
document.querySelectorAll("[data-page]").forEach((button) => { button.onclick = () => act(async () => { current = button.dataset.page; document.querySelectorAll(".page").forEach((page) => { page.hidden = page.id !== current; }); if (current === "config") await configuration(); if (current === "assistant") await assistant.load(); }); });
function form(selector, operation) { $(selector).onsubmit = (event) => { event.preventDefault(); act(async () => { const values = Object.fromEntries(new FormData(event.target)); await operation(values); }); }; }
form("#pair-form", (data) => api("pair/confirm", "POST", data));
form("#ota-form", (data) => api("ota/offer", "POST", {ver: data.ver, kind: data.kind, dev_ids: data.ids.split(",").map((v) => v.trim())}));
form("#config-form", async () => { await api("config/raw", "PUT", {toml: $("#toml").value, mtime}); await configuration(); });
form("#structured-config-form", async () => { try { await api("config", "PUT", {config:configEditor.read(), mtime}); await configuration(); } catch (error) { configEditor.errors(error.details); throw error; } });
$("#reload-fields").onclick = () => act(configuration);
$("#reload-config").onclick = () => act(rawConfig);
form("#secret-form", async (data) => { await api(`secrets/${encodeURIComponent(data.name)}`, "PUT", {value: data.value}); $("#secret-form").reset(); });
form("#token-form", async (data) => { const value = await api("tokens", "POST", data); $("#new-token").textContent = `${t("Shown once / 仅显示一次")}: ${value.token}`; });
form("#notify-form", (data) => api("notify", "POST", {...data, source:"web"}));
setInterval(() => { refresh().catch((error) => show(error.message)); }, 5000);
translate();
enter().catch(() => {});
