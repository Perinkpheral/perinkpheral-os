// SPDX-License-Identifier: MIT
// The real glyph action shares install retries, with a scripted device and virtual clock.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const read = name => fs.readFileSync(path.join(__dirname, "../../main/web/js", name), "utf8");

async function run(replies) {
  const messages = [], requests = [], delays = [], elements = new Map();
  let uploads = 0, downloads = 0, reads = 0, button;
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
    setTimeout: (callback, delay) => {
      if (delay !== 1400) {
        delays.push(delay);
        assert.equal(button.disabled, true);
        assert.equal(button.textContent, "Installing");
      }
      callback();
    },
    fetch: async (url, options) => {
      requests.push({ url, options });
      if (options?.method === "POST") {
        assert.equal(url, "/api/glyphs");
        const reply = replies[Math.min(uploads++, replies.length - 1)];
        if (reply instanceof Error) throw reply;
        return { ok: reply[0] === 200, status: reply[0], json: async () =>
          reply[0] === 200 ? { ok: "Installed." } : { error: reply[1] } };
      }
      if (url.endsWith(".glyphs")) {
        downloads++;
        return { arrayBuffer: async () => new ArrayBuffer(31964) };
      }
      if (url === "/api/glyphs") reads++;
      return { ok: true, json: async () => url === "/api/glyphs" ? { packs: [] }
        : [{ name: "cyrillic", title: "Cyrillic", languages: "Russian", file: "cyrillic.glyphs" }] };
    },
  });
  const core = read("core.js"), settings = read("settings.js");
  vm.runInContext(core.slice(0, core.indexOf("const CONFIRM_ON_DEVICE")), context);
  vm.runInContext('loadState.sources = "ready";', context);
  context.toast = (text, kind, stays) => {
    const message = { text, kind, stays, dismissed: false };
    messages.push(message);
    return () => { message.dismissed = true; };
  };
  vm.runInContext(settings.slice(settings.indexOf("async function loadGlyphs()")), context);
  await context.loadGlyphs();
  button = elements.get("glyph-rows").children[0].children[1];
  await button.listeners.click();
  assert.equal(button.disabled, false);
  assert.equal(button.textContent, "Install");
  const posts = requests.filter(request => request.options?.method === "POST");
  assert.equal(downloads, 1);
  assert(posts.every(request => request.options.body === posts[0].options.body));
  assert(messages.filter(message => message.stays).every(message => message.dismissed));
  return { messages, delays, uploads, reads, context };
}

module.exports = async function () {
  for (const reason of ["Not enough memory.", "Device busy."]) {
    const refused = await run([[503, reason]]);
    assert.equal(refused.uploads, 4);
    assert.deepEqual(refused.delays, [2000, 4000, 8000]);
    assert.equal(refused.reads, 1);
    assert.equal(refused.messages.at(-1).text, "Device busy. Try again shortly.");
    assert.equal(refused.messages.at(-1).kind, "error");
    assert(refused.messages.slice(0, -1).every(message => message.text === "Device busy. Retrying"));
    for (let refusals = 1; refusals <= 3; refusals++) {
      const recovered = await run([...Array(refusals).fill([503, reason]), [200]]);
      assert.equal(recovered.uploads, refusals + 1);
      assert.deepEqual(recovered.delays, [2000, 4000, 8000].slice(0, refusals));
      assert.equal(recovered.reads, 2);
      assert(recovered.messages.every(message => message.kind !== "error"));
    }
  }
  for (const reply of [[400, "Invalid pack."], [500, "Storage error."],
    [503, "Unavailable."], new Error("Failed to fetch")]) {
    const stopped = await run([reply]);
    assert.equal(stopped.uploads, 1);
    assert.deepEqual(stopped.delays, []);
    assert.equal(stopped.messages.at(-1).kind, "error");
  }
  const { context } = await run([[200]]);

  const device = read("device.js");
  vm.runInContext(device.slice(0, device.indexOf("/* Facts as a list")), context);
  vm.runInContext("settings = {}; status = { app_area: 147456, app_area_available: false };", context);
  const facts = context.statusFacts().flatMap(([, rows]) => rows);
  assert.equal(facts.find(([label]) => label === "App area")[1], "Unavailable");
  console.log("Glyph memory refusal and application-area status: ok");
};
