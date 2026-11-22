// SPDX-License-Identifier: MIT
// A delayed app list may open only the dialog still named by the current route.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const read = name => fs.readFileSync(process.env.PERINKPHERAL_TEST_PAGE || path.join(__dirname, "../../main/web/js", name), "utf8");
const core = read("core.js"), apps = read("apps.js");

async function scenario(start, destination, navigate, expected) {
  const opened = [], nodes = new Map();
  const byId = id => {
    if (!nodes.has(id)) nodes.set(id, {
      id, children: [], classList: { contains: name => name === "pane", toggle() {} },
      showModal: () => opened.push("catalogs"),
    });
    return nodes.get(id);
  };
  let finishApps;
  const context = vm.createContext({
    location: { hash: start }, CSS: { escape: value => value }, byId,
    document: {
      querySelector: selector => /^section\[data-section="(apps|settings|device|catalog)"\]$/.test(selector),
      querySelectorAll: () => [],
    },
    installed: [], scrollTo() {}, drawTiles() {}, drawDefaultApp() {},
    openAppSettings: app => opened.push(app.name), openAppInfo: app => opened.push(app.name + ":info"),
    ask: url => {
      assert.equal(url, "/api/apps");
      return new Promise(resolve => { finishApps = resolve; });
    },
    reading: (_what, read) => read(),
  });
  vm.runInContext(core.slice(core.indexOf("const sectionShown"), core.indexOf('for (const tab of byId("tabs").children)\n')), context);
  vm.runInContext(apps.slice(apps.indexOf("async function loadApps("), apps.indexOf("async function loadRemoved(")), context);
  context.showSection();
  const loading = context.loadApps(false);
  context.location.hash = destination;
  if (navigate) {
    context.showSection();
    if (!destination.startsWith("#apps:"))
      assert.equal(vm.runInContext("wantedInside", context), null, "navigation clears a deferred dialog");
  }
  finishApps([{ name: "weather", settings: [{}] }, { name: "clock", settings: [{}] }]);
  await loading;
  assert.deepEqual(opened, expected, start + " followed by " + destination);
  assert.equal(vm.runInContext("wantedInside", context), null, "completed or abandoned navigation is consumed");
  context.ask = async () => context.installed;
  await context.loadApps(false);
  assert.deepEqual(opened, expected, "a later refresh must not reopen a consumed dialog");
}

async function test() {
  for (const destination of ["#settings", "#settings:general", "#settings:time", "#device", "#catalog", "#apps", "#settings:weather"])
    for (const navigate of [true, false])
      await scenario("#apps:weather", destination, navigate, []);
  await scenario("#apps:weather", "#apps:weather", true, ["weather"]);
  await scenario("#apps:weather:info", "#apps:weather:info", true, ["weather:info"]);
  await scenario("#apps:weather", "#apps:clock", true, ["clock"]);
  await scenario("#apps:weather", "#apps:catalogs", true, ["catalogs"]);
  console.log("App navigation: deferred settings and info follow the current route and open once.");
}
module.exports = test;
if (require.main === module) test().catch(error => { console.error(error); process.exitCode = 1; });
