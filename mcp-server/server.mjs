#!/usr/bin/env node
// Water Lilies MCP Server
// Reads device config from ~/.monet/devices.json, proxies to device HTTP API.

import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { z } from "zod";
import { readFileSync } from "fs";
import { homedir } from "os";
import { join } from "path";

function loadConfig() {
  const configPath = join(homedir(), ".monet", "devices.json");
  try {
    return JSON.parse(readFileSync(configPath, "utf-8"));
  } catch {
    return { devices: [] };
  }
}

function getDevice(name) {
  const config = loadConfig();
  if (name) return config.devices.find((d) => d.name === name);
  return config.devices[0];
}

async function deviceFetch(device, path, options = {}) {
  const url = `http://${device.ip}${path}`;
  const headers = { ...options.headers };
  if (device.token && options.method === "POST") {
    headers["Authorization"] = `Bearer ${device.token}`;
  }
  const res = await fetch(url, { ...options, headers, signal: AbortSignal.timeout(3000) });
  return res;
}

const server = new McpServer({
  name: "water-lilies",
  version: "0.1.0",
});

// --- Resources ---

server.resource("info", "water-lilies://info", async (uri) => {
  const device = getDevice();
  if (!device?.ip) return { contents: [{ uri: uri.href, mimeType: "text/plain", text: "No device configured. Run /monet-setup first." }] };
  const res = await deviceFetch(device, "/info");
  const text = await res.text();
  return { contents: [{ uri: uri.href, mimeType: "text/plain", text }] };
});

server.resource("status", "water-lilies://status", async (uri) => {
  const device = getDevice();
  if (!device?.ip) return { contents: [{ uri: uri.href, mimeType: "application/json", text: JSON.stringify({ error: "No device configured" }) }] };
  const res = await deviceFetch(device, "/status");
  const text = await res.text();
  return { contents: [{ uri: uri.href, mimeType: "application/json", text }] };
});

// --- Tools ---

server.tool(
  "display",
  "Draw shapes and text on the Water Lilies OLED screen. Send an array of drawing commands.",
  {
    draw: z.array(z.object({
      type: z.enum(["text", "rect", "bar", "circle", "line", "pixel"]),
      x: z.number().optional(),
      y: z.number().optional(),
      x2: z.number().optional(),
      y2: z.number().optional(),
      w: z.number().optional(),
      h: z.number().optional(),
      r: z.number().optional(),
      pct: z.number().optional(),
      text: z.string().optional(),
      size: z.number().optional(),
      align: z.enum(["left", "right"]).optional(),
      fill: z.boolean().optional(),
    })).describe("Array of drawing commands"),
  },
  async ({ draw }) => {
    const device = getDevice();
    if (!device?.ip) return { content: [{ type: "text", text: "No device configured. Run /monet-setup first." }] };
    const res = await deviceFetch(device, "/display", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ draw }),
    });
    const text = await res.text();
    return { content: [{ type: "text", text }] };
  }
);

server.tool(
  "clear",
  "Clear the Water Lilies OLED screen",
  {},
  async () => {
    const device = getDevice();
    if (!device?.ip) return { content: [{ type: "text", text: "No device configured." }] };
    const res = await deviceFetch(device, "/clear", { method: "POST" });
    const text = await res.text();
    return { content: [{ type: "text", text }] };
  }
);

server.tool(
  "status",
  "Get Water Lilies device status (screen size, IP, uptime, free heap)",
  {},
  async () => {
    const device = getDevice();
    if (!device?.ip) return { content: [{ type: "text", text: "No device configured." }] };
    const res = await deviceFetch(device, "/status");
    const data = await res.json();
    return { content: [{ type: "text", text: JSON.stringify(data, null, 2) }] };
  }
);

// --- Start ---

const transport = new StdioServerTransport();
await server.connect(transport);
