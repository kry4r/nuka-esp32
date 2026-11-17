// SPDX-License-Identifier: AGPL-3.0-or-later
import { once } from "node:events";
import { Agent } from "@earendil-works/pi-agent-core";
import { streamSimple } from "@earendil-works/pi-ai/api/openai-completions";

const MAX_LINE_BYTES = 1024 * 1024;
let agent;
let started = false;
const pendingTools = new Map();

async function send(value) {
  const line = JSON.stringify(value) + "\n";
  if (Buffer.byteLength(line) > MAX_LINE_BYTES) throw new Error("frame_limit");
  if (!process.stdout.write(line)) await once(process.stdout, "drain");
}

function tool(spec) {
  return {
    ...spec,
    label: spec.name,
    executionMode: "sequential",
    execute: async (toolCallId, args, signal) => {
      signal.throwIfAborted();
      const result = new Promise((resolve, reject) => {
        const cancel = () => {
          pendingTools.delete(toolCallId);
          reject(signal.reason);
        };
        signal.addEventListener("abort", cancel, { once: true });
        pendingTools.set(toolCallId, (value) => {
          signal.removeEventListener("abort", cancel);
          pendingTools.delete(toolCallId);
          resolve(value);
        });
      });
      await send({ type: "tool", call_id: toolCallId, name: spec.name, arguments: args });
      return await result;
    },
  };
}

async function run(request) {
  const config = request.config;
  const model = {
    id: config.model,
    name: config.model,
    provider: "nuka",
    api: "openai-completions",
    baseUrl: config.base_url,
    reasoning: false,
    input: ["text"],
    cost: { input: 0, output: 0, cacheRead: 0, cacheWrite: 0 },
    contextWindow: config.context_window,
    maxTokens: config.max_output_tokens,
    compat: {
      supportsStore: false,
      supportsDeveloperRole: false,
      maxTokensField: "max_tokens",
    },
  };
  const baseOrigin = new URL(config.base_url).origin;
  const providerFetch = async (input, init) => {
    const outgoing = new Request(input, { ...init, redirect: "error" });
    if (new URL(outgoing.url).origin !== baseOrigin) throw new Error("origin_changed");
    // Pi's adapter requires a nonempty key even for keyless local servers.
    if (!request.api_key) outgoing.headers.delete("authorization");
    return await fetch(outgoing);
  };
  agent = new Agent({
    initialState: {
      systemPrompt: request.system_prompt,
      model,
      messages: request.messages,
      tools: request.tools.map(tool),
    },
    toolExecution: "sequential",
    identity: { attribution: null },
    streamFn: (selected, context, options) => streamSimple(selected, context, {
      ...options,
      apiKey: request.api_key || "keyless",
      fetch: providerFetch,
      maxRetries: 0,
      timeoutMs: 60000,
      temperature: config.temperature,
      maxTokens: config.max_output_tokens,
    }),
  });
  agent.subscribe(async (event) => {
    if (event.type === "message_update" && event.assistantMessageEvent.type === "text_delta") {
      await send({ type: "delta", text: event.assistantMessageEvent.delta });
    } else if (event.type === "message_end") {
      const message = event.message;
      if (message.role === "system") return;
      if (message.role === "assistant" && ["error", "aborted"].includes(message.stopReason)) return;
      const { diagnostics, errorMessage, ...persisted } = message;
      await send({ type: "message", message: persisted });
    }
  });
  await agent.prompt(request.text);
  if (agent.state.errorMessage) {
    await send({ type: "error", code: "provider_error" });
  } else {
    await send({ type: "done" });
  }
}

function receive(message) {
  if (message.type === "start") {
    if (started) throw new Error("already_started");
    started = true;
    void run(message).catch(async () => {
      await send({ type: "error", code: "engine_error" });
      process.exitCode = 1;
      process.stdin.destroy();
    });
  } else if (message.type === "tool_result") {
    const deliver = pendingTools.get(message.call_id);
    if (!deliver) throw new Error("unknown_tool_call");
    deliver({
      content: [{ type: "text", text: message.text }],
      details: {},
      isError: message.is_error,
    });
  } else if (message.type === "abort") {
    agent?.abort();
  } else {
    throw new Error("unknown_command");
  }
}

try {
  let buffer = Buffer.alloc(0);
  for await (const chunk of process.stdin) {
    buffer = Buffer.concat([buffer, chunk]);
    let end;
    while ((end = buffer.indexOf(10)) !== -1) {
      if (end > MAX_LINE_BYTES) throw new Error("frame_limit");
      const line = buffer.subarray(0, end).toString("utf8");
      buffer = buffer.subarray(end + 1);
      receive(JSON.parse(line));
    }
    if (buffer.length > MAX_LINE_BYTES) throw new Error("frame_limit");
  }
  agent?.abort();
  await agent?.waitForIdle();
} catch {
  agent?.abort();
  await send({ type: "error", code: "invalid_frame" });
  process.exitCode = 1;
}
