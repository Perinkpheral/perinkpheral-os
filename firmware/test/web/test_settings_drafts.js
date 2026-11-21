// SPDX-License-Identifier: MIT
// Settings reloads and time-mode changes must not combine unrelated drafts.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const source = fs.readFileSync(path.join(__dirname, "../../main/web/js/settings.js"), "utf8");
const section = (from, to) => source.slice(source.indexOf(from), source.indexOf(to, source.indexOf(from)));

async function test() {
  const elements = new Map(), requests = [];
  let now = Date.UTC(2026, 10, 21, 12), clockFails = false;
  class Clock extends Date {
    constructor(...args) { super(...(args.length ? args : [now])); }
    static now() { return now; }
  }
  const byId = id => {
    if (!elements.has(id)) elements.set(id, {
      value: "", options: [], attributes: {}, listeners: {},
      setAttribute(name, value) { this.attributes[name] = value; },
      getAttribute(name) { return this.attributes[name]; },
      append(option) { this.options.push(option); },
      replaceChildren(...options) { this.options = options; },
      addEventListener(name, listener) { this.listeners[name] = listener; },
    });
    return elements.get(id);
  };
  const response = { wifi_ssid: "Network A", language: "en", time_auto: true };
  const context = vm.createContext({
    settings: {}, Date: Clock, byId, make: (_tag, values) => values,
    ask: async (url, options) => {
      if (!options) return response;
      requests.push({ url, values: options });
      if (url === "/api/clock" && clockFails) throw Error("Storage error.");
      if (url === "/api/settings") Object.assign(response, options);
    },
    post: values => values, reading: (_what, read) => read(), drawSettingsState() {},
    ZONE_NOT_SET: "unset", setLanguage() {}, showZone() {},
    showWifiNetworks(_networks, selected) { byId("wifi-networks").value = selected; },
    drawDefaultApp() {}, drawStatus() {}, drawTiles() {}, loadStatus() {}, t: text => text,
    chosenZone: () => ({ tz: "UTC0", tz_city: "" }),
    busy: async (_button, run) => { try { await run(); return true; } catch { return false; } },
    send: async (button, url, options) => { await context.ask(url, options); return true; },
  });
  vm.runInContext(section("async function loadSettings()", "function drawDefaultApp()"), context);
  vm.runInContext(section("function saveSettings(", "/* ---- the time zone:"), context);
  vm.runInContext(section("function localNow()", "/* ---- the network:"), context);
  const submit = id => byId(id).listeners.submit({ preventDefault() {}, submitter: {} });
  await context.loadSettings();
  byId("ssid").value = "Network B";
  byId("password").value = "password-b";
  await submit("general");
  assert.equal(byId("ssid").value, "Network A");
  assert.equal(byId("wifi-networks").value, "Network A");
  assert.equal(byId("password").value, "", "reloading the SSID clears its draft password");

  const openingTime = byId("clock-value").value;
  now += 2 * 60 * 60 * 1000;
  byId("time_auto").listeners.click();
  assert.notEqual(byId("clock-value").value, openingTime);
  assert.equal(byId("clock-value").value, context.localNow());
  await submit("time-zone");
  const clockRequests = () => requests.filter(request => request.url === "/api/clock");
  assert.equal(clockRequests().length, 1, "disabling network time saves the time shown");
  assert.equal(clockRequests()[0].values.unix, Math.round(new Date(byId("clock-value").value).getTime() / 1000));

  byId("clock-value").value = "2026-11-21T15:30";
  byId("clock-value").listeners.input();
  clockFails = true;
  await submit("time-zone");
  clockFails = false;
  await submit("time-zone");
  assert.equal(clockRequests().length, 3, "a failed manual save can be retried");
  assert.equal(clockRequests()[2].values.unix, Math.round(new Date("2026-11-21T15:30").getTime() / 1000));
  await submit("time-zone");
  assert.equal(clockRequests().length, 3, "saving the zone again does not reset the clock");

  byId("clock-value").listeners.input();
  byId("time_auto").listeners.click();
  await submit("time-zone");
  byId("time_auto").listeners.click();
  await submit("time-zone");
  assert.equal(clockRequests().length, 4, "switching back to manual saves the newly shown time");
  assert.equal(clockRequests()[3].values.unix, Math.round(new Date(context.localNow()).getTime() / 1000));
  console.log("Settings drafts: Wi-Fi passwords and explicit manual clock edits passed.");
}
module.exports = test;
if (require.main === module) test().catch(error => { console.error(error); process.exitCode = 1; });
