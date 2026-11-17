// SPDX-License-Identifier: AGPL-3.0-or-later
const names = {
  hub:"Hub", mdns:"Local discovery / 本地发现", security:"Security / 安全", collectors:"Collectors / 主机采集", servers:"Servers / 服务器", alerts:"Alerts / 告警", ai:"AI accounts / AI 账户", agents:"CLI assistants / CLI 助手", media:"Media / 媒体", weather:"Weather / 天气", notify:"Notifications / 通知", devices:"Device defaults / 设备默认设置", ota:"Updates / 更新", store:"Monitoring storage / 监控存储", web:"Web", agent:"Agent mode / Agent 模式", assistant:"Assistant / 助手",
  enabled:"Enabled / 启用", name:"Name / 名称", host:"Listen address / 监听地址", port:"Port / 端口", public_url:"Public URL / 对外地址", lang:"Language / 语言", timezone:"Timezone / 时区", data_dir:"Data directory / 数据目录", log_level:"Log level / 日志级别", url:"URL", base_url:"API base URL / API 基础地址", type:"Type / 类型", model:"Model / 模型", key_secret:"Secret name / 密钥名称", auth_secret:"Authentication secret name / 认证密钥名称", system_prompt:"Persona / 人设", llm:"Language model / 语言模型", temperature:"Temperature / 温度", context_window:"Model context tokens / 模型上下文 Token 数", max_output_tokens:"Maximum output tokens / 最大输出 Token 数", history:"Conversation archive / 对话记录", context_turns:"Recent context turns / 最近上下文轮数", memory:"Long term memory / 长期记忆", uploads:"Uploads / 上传", max_bytes:"Maximum file bytes / 文件字节上限",
  interval:"Interval (seconds) / 间隔（秒）", timeout:"Timeout (seconds) / 超时（秒）", remote_admin:"Remote administration / 远程管理", cf_access:"Cloudflare Access", team_domain:"Access team domain / Access 团队域名", aud:"Application audience / 应用受众", require_for_ws:"Require Access for WebSocket / WebSocket 需要 Access", trusted_proxies:"Trusted proxy networks / 受信任代理网段", lan_cidrs:"LAN networks / 局域网网段", public_detect_headers:"Public detection headers / 公网识别请求头", web_approve_permissions:"Web CLI approvals / 网页 CLI 审批", allow_lan_hooks:"Allow LAN hooks / 允许局域网 Hooks", session_days:"Session days / 会话天数",
  source:"Sources / 数据源", provider:"Providers / 提供者", options:"Adapter options / 适配器选项", headers:"HTTP headers / HTTP 请求头", accounts:"Account labels / 账户标签", label:"Label / 标签", hidden:"Hidden / 隐藏", map:"Sensor mappings / 传感器映射", field:"Target field / 目标字段", sensor_id:"Sensor ID / 传感器 ID", name_regex:"Name pattern / 名称表达式", priority:"Priority / 优先级", merge:"Merge / 合并", aliases:"Aliases / 别名", by:"Match by / 匹配方式", rule:"Rules / 规则", when:"Condition / 条件", for:"Sustained seconds / 持续秒数", level:"Level / 级别", title:"Title / 标题", builtin:"Built in rules / 内置规则", repeat:"Repeat (seconds) / 重复间隔（秒）", city:"City / 城市", lat:"Latitude / 纬度", lon:"Longitude / 经度", units:"Units / 单位", quiet_hours:"Quiet hours / 免打扰时段", keep:"Retained notifications / 通知保留条数", allow_config_edit:"Allow configuration editing / 允许编辑配置", default_lang:"Default language / 默认语言", home_face:"Home face / 主页样式", volume:"Volume / 音量", key3_long:"Key3 hold / Key3 长按", action:"Action / 操作", slot:"Slot / 槽位", macro:"Macros / 宏", value:"Value / 值", kind:"Kind / 类型", key3_double_card:"Key3 double press card / Key3 双击卡片", key3_clock_style:"Clock style shortcut / 时钟样式快捷键", push_overrides:"Push intervals / 推送间隔", t_dim:"Dim timeout (seconds) / 暗屏超时（秒）", t_off:"Screen off timeout (seconds) / 熄屏超时（秒）", dir:"Directory / 目录", auto_offer:"Automatically offer updates / 自动推送更新", path:"Executable / 可执行程序", user:"User / 用户", password_secret:"Password secret name / 密码密钥名称", host_name:"Host label / 主机名称", enabled_sources:"Enabled sources / 启用的数据源", permission_timeout:"Approval timeout (seconds) / 审批超时（秒）", collect_last_message:"Collect last reply / 采集最后回复", show_project:"Project display / 项目显示", idle_after:"Idle timeout (seconds) / 空闲超时（秒）", purge_after:"Purge timeout (seconds) / 清理超时（秒）",
};
const hints = {
  "assistant.history":"Full records have no automatic expiry. Context turns limit only what is sent to the model. / 完整记录不会自动过期；上下文轮数只限制发给模型的内容。",
  "assistant.memory":"Only explicitly saved facts are used. Turning this off does not delete records. / 仅使用显式保存的事实；关闭不会删除已有记录。",
  "assistant.llm.context_window":"Set this to the actual model capacity. Input uses a conservative byte estimate and reserves the output budget. / 按实际模型容量填写；输入采用保守字节估算并预留输出预算。",
  "security.remote_admin":"Requires verified Cloudflare Access, an administrator session and CSRF for writes. Hooks and approvals stay local. / 需要验证 Cloudflare Access、管理员会话及写入 CSRF；Hooks 和审批仅限本地。",
  "devices.assistant":"Device voice needs the audio firmware; text chat can be configured independently. / 设备语音需要对应固件；文字对话可单独配置。",
};
function node(tag, text, className) { const value = document.createElement(tag); if (text) value.textContent = text; if (className) value.className = className; return value; }

export function schemaForm(root, initial, t, prefix = "") {
  const fields = new Map();
  function resolve(schema) {
    if (schema.$ref) return {...resolve(root.$defs[schema.$ref.split("/").pop()]), ...schema};
    return schema;
  }
  function fresh(schema) {
    schema = resolve(schema);
    if (schema.default !== undefined && schema.default !== null) return structuredClone(schema.default);
    if (schema.anyOf) return fresh(schema.anyOf.find((part) => part.type !== "null"));
    if (schema.enum) return schema.enum[0];
    if (schema.type === "object") return Object.fromEntries(Object.entries(schema.properties || {}).map(([key, part]) => [key, fresh(part)]));
    if (schema.type === "array") return [];
    if (schema.type === "boolean") return false;
    if (["integer", "number"].includes(schema.type)) return schema.minimum ?? (schema.exclusiveMinimum !== undefined ? schema.exclusiveMinimum + 1 : 0);
    return "";
  }
  function field(raw, value, path, label) {
    let schema = resolve(raw);
    const nullable = schema.anyOf?.some((part) => part.type === "null");
    if (schema.anyOf) schema = resolve(schema.anyOf.find((part) => part.type !== "null"));
    const wrap = node("div", "", "schema-field"); wrap.dataset.path = path;
    const message = node("small", "", "field-error"); fields.set(path, {wrap, message});
    let read;
    if (schema.type === "object" || schema.properties) {
      const group = node("details", "", "schema-group"); group.open = !!prefix;
      group.append(node("summary", t(label))); const body = node("div", "", "schema-grid"); group.append(body); wrap.append(group);
      if (hints[path]) body.append(node("p", t(hints[path]), "field-help"));
      if (schema.properties) {
        const children = Object.entries(schema.properties).map(([key, part]) => {
          const child = field(part, value?.[key], path ? `${path}.${key}` : key, names[key] || part.title || key);
          body.append(child.element); return [key, child];
        });
        read = () => Object.fromEntries(children.map(([key, child]) => [key, child.read()]).filter(([, child]) => child !== undefined));
      } else {
        const rows = node("div", "", "schema-rows"); body.append(rows); const entries = [];
        function add(key, item) {
          const row = node("div", "", "schema-entry"); const keyInput = node("input"); keyInput.value = key; keyInput.placeholder = t("Key / 键名"); keyInput.setAttribute("aria-label", t("Key / 键名"));
          const child = field(typeof schema.additionalProperties === "object" ? schema.additionalProperties : {}, item, `${path}.${key}`, t("Value / 值"));
          const remove = node("button", t("Remove / 移除")); remove.type = "button"; const entry = {keyInput, child, row};
          remove.onclick = () => { entries.splice(entries.indexOf(entry), 1); row.remove(); }; row.append(keyInput, child.element, remove); rows.append(row); entries.push(entry);
        }
        for (const [key, item] of Object.entries(value || {})) add(key, item);
        const addButton = node("button", t("Add entry / 添加条目")); addButton.type = "button"; addButton.onclick = () => add("", fresh(typeof schema.additionalProperties === "object" ? schema.additionalProperties : {})); body.append(addButton);
        read = () => { const output = {}; for (const entry of entries) { const key = entry.keyInput.value.trim(); if (!key || Object.hasOwn(output, key)) { entry.keyInput.setCustomValidity(t("Use a unique nonempty key / 请输入非空且唯一的键名")); entry.keyInput.reportValidity(); throw new Error(t("Check dictionary keys / 请检查字典键名")); } entry.keyInput.setCustomValidity(""); output[key] = entry.child.read(); } return output; };
      }
    } else if (schema.type === "array") {
      wrap.append(node("strong", t(label))); const rows = node("div", "", "schema-rows"); wrap.append(rows); const entries = [];
      function add(item) {
        const row = node("div", "", "schema-entry"), child = field(schema.items || {}, item, `${path}.${entries.length}`, t("Item / 条目"));
        const remove = node("button", t("Remove / 移除")); remove.type = "button"; const entry = {row, child}; remove.onclick = () => { entries.splice(entries.indexOf(entry), 1); row.remove(); };
        row.append(child.element, remove); rows.append(row); entries.push(entry);
      }
      for (const item of value || []) add(item);
      const addButton = node("button", t("Add item / 添加条目")); addButton.type = "button"; addButton.onclick = () => add(fresh(schema.items || {})); wrap.append(addButton);
      read = () => entries.map((entry) => entry.child.read());
    } else if (!schema.type && !schema.enum) {
      const type = node("select"), host = node("div");
      for (const name of ["string", "number", "boolean", "object", "array"]) { const option = node("option", name); option.value = name; type.append(option); }
      type.value = Array.isArray(value) ? "array" : value && typeof value === "object" ? "object" : typeof value === "number" ? "number" : typeof value === "boolean" ? "boolean" : "string";
      let child; const draw = (current) => { const part = {type: type.value}; if (part.type === "array") part.items = {}; child = field(part, current, path, label); host.replaceChildren(child.element); };
      draw(value); type.onchange = () => draw(fresh({type:type.value})); wrap.append(type, host); read = () => child.read();
    } else {
      const labelNode = node("label", t(label)); let input;
      if (schema.enum) { input = node("select"); for (const choice of schema.enum) { const option = node("option", String(choice) || t("Automatic / 自动")); option.value = String(choice); input.append(option); } input.value = value ?? fresh(schema); }
      else if (schema.type === "boolean") { input = node("input"); input.type = "checkbox"; input.checked = value ?? fresh(schema); }
      else { input = node(path.endsWith("system_prompt") ? "textarea" : "input"); input.value = value ?? (nullable ? "" : fresh(schema));
        if (["number", "integer"].includes(schema.type)) { input.type = "number"; input.step = schema.type === "integer" ? "1" : "any"; if (schema.minimum !== undefined) input.min = schema.minimum; if (schema.maximum !== undefined) input.max = schema.maximum; }
        if (schema.maxLength !== undefined) input.maxLength = schema.maxLength;
        if (schema.minLength !== undefined) input.minLength = schema.minLength;
        if (input.tagName === "TEXTAREA") input.rows = 5;
      }
      input.dataset.path = path; if (path.endsWith("_secret")) input.placeholder = t("Secret name, not its value / 填密钥名称，值在下方单独写入"); labelNode.append(input); wrap.append(labelNode);
      if (hints[path]) wrap.append(node("small", t(hints[path]), "field-help"));
      read = () => input.type === "checkbox" ? input.checked : input.value === "" && nullable ? undefined : ["number", "integer"].includes(schema.type) ? Number(input.value) : input.value;
    }
    wrap.append(message); return {element:wrap, read};
  }
  const editor = field(prefix ? {$ref:`#/$defs/${prefix}`} : root, initial, prefix ? prefix.toLowerCase() : "", prefix ? names[prefix.toLowerCase()] || prefix : "Configuration / 配置");
  editor.element.querySelector(":scope > details").open = true;
  return {...editor, errors(details = []) { for (const {wrap, message} of fields.values()) { wrap.classList.remove("invalid"); message.textContent = ""; }
    for (const detail of details) { const key = detail.msg.split(":")[0].replace(/^body\.config\./, ""); const target = fields.get(key) || fields.get(key.split(".").slice(0, -1).join(".")); if (target) { target.wrap.classList.add("invalid"); target.message.textContent = detail.msg; let parent = target.wrap.parentElement; while (parent) { if (parent.tagName === "DETAILS") parent.open = true; parent = parent.parentElement; } } }
  }};
}
