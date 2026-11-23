// SPDX-License-Identifier: MIT
// The real glyph install action reports a memory refusal and leaves its button usable.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const read = name => fs.readFileSync(path.join(__dirname, "../../main/web/js", name), "utf8");

module.exports = async function () {
  const messages = [], requests = [], elements = new Map();
  const element = () => ({
    children: [], listeners: {}, style: {}, dataset: {}, offsetWidth: 90,
    classList: { add() {}, remove() {} },
    append(...children) { this.children.push(...children); },
    replaceChildren(...children) { this.children = children; },
    addEventListener(event, handler) { this.listeners[event] = handler; },
  });
  const context = vm.createContext({
    document: {
      createElement: element,
      getElementById: id => {
        if (!elements.has(id)) elements.set(id, element());
        return elements.get(id);
      },
    },
    t: text => text, isOffline: () => false, settings: {}, pageLanguage: "en",
    sources: ["https://example.test/catalog"],
    fetch: async (url, options) => {
      requests.push({ url, options });
      if (options?.method === "POST") {
        assert.equal(url, "/api/glyphs");
        return { ok: false, status: 503, json: async () => ({ error: "Not enough memory." }) };
      }
      if (url.endsWith(".glyphs")) return { arrayBuffer: async () => new ArrayBuffer(32 * 1024) };
      return { ok: true, json: async () => url === "/api/glyphs" ? { packs: [] }
        : [{ name: "cyrillic", title: "Cyrillic", languages: "Russian", file: "cyrillic.glyphs" }] };
    },
  });
  const core = read("core.js"), settings = read("settings.js");
  vm.runInContext(core.slice(0, core.indexOf("const CONFIRM_ON_DEVICE")), context);
  vm.runInContext('loadState.sources = "ready";', context);
  context.toast = (text, kind) => messages.push({ text, kind });
  vm.runInContext(settings.slice(settings.indexOf("async function loadGlyphs()")), context);
  await context.loadGlyphs();
  const button = elements.get("glyph-rows").children[0].children[1];
  await button.listeners.click();
  assert.deepEqual(messages, [{ text: "Not enough memory.", kind: "error" }]);
  assert.equal(button.disabled, false);
  assert.equal(button.textContent, "Install");
  assert.equal(requests.filter(request => request.options?.method === "POST").length, 1);

  const device = read("device.js");
  vm.runInContext(device.slice(0, device.indexOf("/* Facts as a list")), context);
  vm.runInContext("settings = {}; status = { app_area: 147456, app_area_available: false };", context);
  const facts = context.statusFacts().flatMap(([, rows]) => rows);
  assert.equal(facts.find(([label]) => label === "App area")[1], "Unavailable");
  console.log("Glyph memory refusal and application-area status: ok");
};
