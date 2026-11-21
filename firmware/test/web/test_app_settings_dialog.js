// SPDX-License-Identifier: MIT
// A delayed save must preserve other forms without leaving stale settings open.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const read = name => fs.readFileSync(path.join(__dirname, "../../main/web/js", name), "utf8");
const apps = read("apps.js"), core = read("core.js");
const flush = async () => { for (let step = 0; step < 30; step++) await Promise.resolve(); };

async function testDialog() {
  const nodes = new Map(), requests = [], pauses = [];
  let reloads = 0, finishReload;
  const context = vm.createContext({
    document: { getElementById(id) {
      if (!nodes.has(id)) nodes.set(id, { listeners: {}, children: [],
        addEventListener(event, handler) { this.listeners[event] = handler; },
        replaceChildren(...children) { this.children = children; },
        querySelectorAll(selector) {
          if (selector.includes("place")) return this.children.filter(input => input.dataset.type === "place");
          if (selector === "[data-key]") return this.children.filter(input => input.dataset.key);
          return this.children;
        },
        showModal() { this.open = true; }, close() { this.open = false; },
      });
      return nodes.get(id);
    } },
    t: text => text, show() {}, settingFields: app => app.fields,
    loadApps: () => { reloads++; return new Promise(resolve => { finishReload = resolve; }); },
    setTimeout: callback => pauses.push(callback),
    fetch: (url, options) => new Promise(resolve => requests.push({ url, options,
      reply: ok => resolve({ ok, status: ok ? 200 : 400,
        json: async () => ok ? { ok: "Saved." } : { error: "Invalid value." } }),
    })),
  });
  vm.runInContext(core.slice(0, core.indexOf("const CONFIRM_ON_DEVICE")), context);
  context.toast = () => {};
  vm.runInContext(apps.slice(apps.indexOf("let appInDialog"), apps.indexOf("/* ---- the companion")), context);
  vm.runInContext(apps.slice(apps.indexOf('byId("app-settings").addEventListener')), context);
  const dialog = context.document.getElementById("app-dialog");
  const input = value => ({ value, disabled: false, dataset: { key: "text", type: "text" } });
  const first = { name: "first", fields: [input("first edit")] };
  const second = { name: "second", fields: [input("newer edit")] };
  const save = { textContent: "Save", disabled: false, style: {}, offsetWidth: 80,
    classList: { add() {}, remove() {} } };
  const submit = () => context.document.getElementById("app-settings").listeners.submit({ preventDefault() {},
    submitter: save,
  });

  const choice = { value: "metric", disabled: false, dataset: { key: "units", type: "choice" } };
  const addRow = { disabled: false, dataset: {} };
  const unavailable = { disabled: true, dataset: {} };
  const locked = { name: "locked", fields: [input("saved edit"), choice, addRow, unavailable] };
  context.openAppSettings(locked);
  const pending = submit();
  const requestCount = requests.length;
  const assertLocked = async phase => {
    assert.equal(save.disabled, true, "Save stays disabled during " + phase);
    assert(locked.fields.every(control => control.disabled), "fields stay disabled during " + phase);
    await submit();
    assert.equal(requests.length, requestCount, "a second submit cannot POST during " + phase);
  };
  await assertLocked("the request");
  requests.at(-1).reply(true); await flush();
  await assertLocked("the success pause");
  pauses.shift()(); await flush();
  assert.equal(dialog.open, true, "the form waits for the delayed app list reload");
  await assertLocked("the app list reload");
  finishReload(); await pending;
  assert.equal(dialog.open, false, "the saved form closes after the reload");
  assert.equal(save.disabled, false, "Save is restored for the next form");
  assert(locked.fields.slice(0, -1).every(control => !control.disabled));
  assert.equal(unavailable.disabled, true, "an already disabled control stays disabled");

  for (const phase of ["request", "success pause", "reload"]) for (const newer of [second, first]) {
    context.openAppSettings(first);
    const saved = submit();
    const request = requests.at(-1);
    assert(request.url.endsWith("app=first"));
    assert.equal(JSON.parse(request.options.body).text, "first edit");
    const reopen = () => { dialog.close(); context.openAppSettings(newer); };
    if (phase === "request") reopen();
    request.reply(true); await flush();
    assert.equal(pauses.length, 1, "the real busy() success delay is still pending");
    if (phase === "success pause") reopen();
    pauses.shift()();
    await flush();
    if (phase === "reload") reopen();
    finishReload();
    await saved;
    assert.equal(dialog.open, newer !== first,
      "a same-app form has stale values; a different app's edits must stay open during " + phase);
    assert.equal(context.document.getElementById("app-fields").children[0].value, newer.fields[0].value);
  }
  context.openAppSettings(first);
  let saved = submit();
  requests.at(-1).reply(true); await flush(); pauses.shift()(); await flush(); finishReload(); await saved;
  assert.equal(dialog.open, false, "a save still closes its own unchanged form");
  context.openAppSettings(first);
  saved = submit(); requests.at(-1).reply(false); await saved;
  assert.equal(dialog.open, true, "a refused save keeps the form open");
  assert.equal(save.disabled, false, "a refused save allows another save");
  assert.equal(first.fields[0].disabled, false, "a refused save allows correcting the input");
  assert.equal(reloads, 8);
  context.loadApps = async () => { throw new Error("Device unreachable."); };
  for (const newer of [first, second]) {
    context.openAppSettings(first);
    saved = submit();
    requests.at(-1).reply(true); await flush();
    dialog.close(); context.openAppSettings(newer);
    pauses.shift()();
    await assert.rejects(saved, /Device unreachable/);
    assert.equal(dialog.open, newer !== first, "a failed reload must not leave the saved app's stale form open");
    assert.equal(save.disabled, false, "a failed reload restores Save for the next form");
    assert.equal(first.fields[0].disabled, false, "a failed reload releases the old inputs");
  }
  console.log("App settings dialog: save locks through reload, stale forms close, other edits survive, refusals unlock.");
}
module.exports = testDialog;
if (require.main === module) testDialog().catch(error => { console.error(error); process.exitCode = 1; });
