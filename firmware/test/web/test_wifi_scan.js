// SPDX-License-Identifier: MIT
// Scanning occupies the radio and must not be reachable through a browser image GET.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const read = name => fs.readFileSync(path.join(__dirname, "../../main", name), "utf8");

async function test() {
  const routes = [...read("web_server.c").matchAll(
    /\{\s*"\/api\/wifi\/scan"\s*,\s*(HTTP_\w+)\s*,\s*(\w+)\s*,\s*(\w+)\s*,\s*(\w+)\s*,\s*(\w+)\s*\}/g)];
  assert.deepEqual(routes.map(route => route.slice(1)), [
    ["HTTP_POST", "handle_wifi_scan", "MEMORY_FOR_A_PAGE", "WEB_OWN_PAGE", "WEB_WAITS"],
  ], "only POST reaches scanning, with the existing origin guard and admission checks");

  const elements = new Map(), requests = [], messages = [];
  function element(properties = {}) {
    return { value: "", dataset: {}, children: [], listeners: {}, style: {},
      classList: { add() {}, remove() {} }, textContent: "Scan", offsetWidth: 80,
      addEventListener(name, listener) { this.listeners[name] = listener; },
      replaceChildren(...children) { this.children = children; },
      get selectedOptions() { return this.children.filter(child => child.value === this.value); },
      ...properties };
  }
  const byId = id => {
    if (!elements.has(id)) elements.set(id, element());
    return elements.get(id);
  };
  let networks = [{ ssid: "Paper", rssi: -50, secured: true }], failure = false;
  const context = vm.createContext({
    document: { getElementById: byId, createElement: () => element() },
    t: text => text, setTimeout: callback => callback(),
    fetch: async (url, options) => {
      requests.push({ url, options });
      assert.equal(url, "/api/wifi/scan");
      assert.equal(options?.method, "POST", "the Scan button must use the guarded POST route");
      assert.deepEqual(JSON.parse(options.body), {});
      return { ok: !failure, status: failure ? 503 : 200,
        json: async () => failure ? { error: "Wi-Fi is busy. Try again shortly." } : { networks } };
    },
  });
  const core = read("web/js/core.js");
  vm.runInContext(core.slice(0, core.indexOf("const CONFIRM_ON_DEVICE")), context);
  context.toast = (text, kind) => messages.push({ text, kind });
  const settings = read("web/js/settings.js");
  vm.runInContext(settings.slice(settings.indexOf("function chooseWifiNetwork()"),
    settings.indexOf('byId("wifi").addEventListener')), context);
  const button = byId("scan-networks");
  const click = () => button.listeners.click({ target: button });
  assert.equal(await click(), true);
  assert.equal(byId("wifi-networks").children[1].dataset.ssid, "Paper");
  assert.equal(button.disabled, false);
  networks = [];
  assert.equal(await click(), true);
  assert.equal(messages.at(-1).text, "No networks found.");
  failure = true;
  assert.equal(await click(), false);
  assert.deepEqual(messages.at(-1), { text: "Wi-Fi is busy. Try again shortly.", kind: "error" });
  assert.equal(button.disabled, false, "a refused scan can be tried again");
  assert.equal(requests.length, 3);
  console.log("Wi-Fi scan: POST-only route, Scan button, empty results and retry after refusal passed.");
}

module.exports = test;
if (require.main === module) test().catch(error => { console.error(error); process.exitCode = 1; });
