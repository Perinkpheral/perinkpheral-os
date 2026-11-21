// SPDX-License-Identifier: MIT
// Settings reloads and time-mode changes must not combine unrelated drafts.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const source = fs.readFileSync(path.join(__dirname, "../../main/web/js/settings.js"), "utf8");
const section = (from, to) => source.slice(source.indexOf(from), source.indexOf(to, source.indexOf(from)));

async function test() {
  const elements = new Map(), requests = [], intervals = [];
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
    setInterval: (callback, delay) => { assert.equal(delay, 1000); intervals.push(callback); },
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

  const clockRequests = () => requests.filter(request => request.url === "/api/clock");
  await submit("time-zone");
  assert.equal(clockRequests().length, 0, "network time saves never set the clock");

  response.time_auto = false;
  await context.loadSettings();
  now += 61 * 1000;
  intervals[0]();
  assert.equal(byId("clock-value").value, context.localNow(), "an untouched manual field stays current");
  now += 31 * 1000;
  await submit("time-zone");
  assert.equal(clockRequests().length, 1, "opening in manual mode and saving sets the clock without typing");
  assert.equal(clockRequests()[0].values.unix, Math.round(now / 1000), "Save takes browser time at submit");

  byId("time_auto").listeners.click();
  await submit("time-zone");
  assert.equal(clockRequests().length, 1);
  const openingTime = byId("clock-value").value;
  now += 2 * 60 * 60 * 1000;
  byId("time_auto").listeners.click();
  assert.notEqual(byId("clock-value").value, openingTime);
  assert.equal(byId("clock-value").value, context.localNow());
  now += 40 * 60 * 1000;
  intervals[0]();
  assert.equal(byId("clock-value").value, context.localNow());
  now += 17 * 1000;
  await submit("time-zone");
  assert.equal(clockRequests().length, 2, "disabling network time saves even without typing");
  assert.equal(clockRequests()[1].values.unix, Math.round(now / 1000), "a delayed Save does not use the toggle's time");

  byId("clock-value").value = "2026-11-21T15:30";
  byId("clock-value").listeners.input();
  now += 40 * 60 * 1000;
  intervals[0]();
  assert.equal(byId("clock-value").value, "2026-11-21T15:30", "typing stops the live field refresh");
  clockFails = true;
  await submit("time-zone");
  clockFails = false;
  await submit("time-zone");
  assert.equal(clockRequests().length, 4, "a failed manual save can be retried");
  assert.equal(clockRequests()[3].values.unix, Math.round(new Date("2026-11-21T15:30").getTime() / 1000));
  assert.equal(byId("clock-value").value, context.localNow(), "a successful save resumes the live field");
  now += 60 * 1000;
  await submit("time-zone");
  assert.equal(clockRequests().length, 5, "a later manual zone save sets the clock too");
  assert.equal(clockRequests()[4].values.unix, Math.round(now / 1000));

  byId("clock-value").value = "2026-11-21T15:30";
  byId("clock-value").listeners.input();
  byId("time_auto").listeners.click();
  await submit("time-zone");
  assert.equal(clockRequests().length, 5, "network time ignores a discarded manual draft");
  byId("time_auto").listeners.click();
  await submit("time-zone");
  assert.equal(clockRequests().length, 6, "switching back to manual saves the current browser time");
  assert.equal(clockRequests()[5].values.unix, Math.round(now / 1000));

  byId("clock-value").value = "";
  byId("clock-value").listeners.input();
  const beforeInvalid = requests.length;
  await submit("time-zone");
  assert.equal(requests.length, beforeInvalid, "an invalid typed time cannot report a successful save");
  console.log("Settings drafts: Wi-Fi passwords, live browser time and manual clock saves passed.");
}
module.exports = test;
if (require.main === module) test().catch(error => { console.error(error); process.exitCode = 1; });
