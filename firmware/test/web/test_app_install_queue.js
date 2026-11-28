// SPDX-License-Identifier: MIT
// Real queue, request/retry and button code with deferred HTTP responses.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const read = name => fs.readFileSync(path.join(__dirname, "../../main/web/js", name), "utf8");
const apps = read("apps.js"), core = read("core.js");
const flush = async () => { for (let step = 0; step < 30; step++) await Promise.resolve(); };
function harness() {
  const requests = [], messages = [], events = {}, buttons = new Map();
  let active = 0, maximumActive = 0, reloads = 0, confirms = true;
  function button(name) {
    if (!buttons.has(name)) buttons.set(name, {
      textContent: name === "all" ? "Update all" : "Update", disabled: false, style: {}, offsetWidth: 80,
      classList: { add() {}, remove() {} }, listeners: {},
      addEventListener(event, callback) { this.listeners[event] = callback; },
    });
    return buttons.get(name);
  }
  const offers = ["air", "clock", "weather"].map(name => ({ source: 0, generation: 0,
    app: { name, title: name, version: "1.2.0", files: [] } }));
  const context = vm.createContext({
    offered: offers, catalogsRead: true,
    installed: offers.map(offer => ({ name: offer.app.name, version: "1.0.0" })),
    document: { getElementById: id => button(id === "update-all" ? "all" : id) },
    window: { addEventListener: (event, callback) => { events[event] = callback; } },
    localStorage: { getItem: () => "en" },
    compatibilityProblem: () => "", offerSays: offer => offer.app.title,
    confirmed: async () => confirms,
    updates: () => offers,
    drawTiles() {}, loadApps: async () => { reloads++; },
    setTimeout: callback => callback(),
    fetch: (url, options) => new Promise((resolve, reject) => {
      active++; maximumActive = Math.max(active, maximumActive);
      requests.push({ url, options,
        reply(status = 200, message = "Saved.") {
          active--; resolve({ ok: status === 200, status,
            json: async () => status === 200 ? { ok: message } : { error: message } });
        },
        lose() { active--; reject(new Error("Failed to fetch")); },
      });
    }),
  });
  vm.runInContext(read("i18n.js").split("const SAID")[0], context);
  vm.runInContext(core.slice(0, core.indexOf("const CONFIRM_ON_DEVICE")), context);
  vm.runInContext(read("compatibility.js"), context);
  context.toast = (text, kind, stays) => {
    const message = { text, kind, stays, dismissed: false }; messages.push(message);
    return () => { message.dismissed = true; };
  };
  vm.runInContext(apps.slice(apps.indexOf("const appInstallQueue"), apps.indexOf("/* An icon is")), context);
  vm.runInContext(apps.slice(apps.indexOf("let offeredSources"), apps.indexOf("/* One reading at a time")), context);
  Object.assign(context, { catalogAddress: line => line, catalogsReading: null, loadCatalogs: async () => {} });
  const sourcesStart = apps.indexOf('byId("sources").addEventListener');
  vm.runInContext(apps.slice(sourcesStart, apps.indexOf('byId("refresh-catalogs").addEventListener', sourcesStart)), context);
  button("source-rows").values = () => ["https://example.test/b"];
  const bulk = apps.indexOf('byId("update-all").addEventListener');
  vm.runInContext(apps.slice(bulk, apps.indexOf('byId("auto-update-apps").addEventListener', bulk)), context);
  return { context, requests, messages, events, offers, button,
    start: index => context.install(button(String(index)), offers[index]),
    all: () => button("all").listeners.click(),
    get maximumActive() { return maximumActive; }, get reloads() { return reloads; },
    refusePermission() { confirms = false; },
    saveSources: () => button("sources").listeners.submit({ preventDefault() {}, submitter: button("save") }),
  };
}

async function testQueue() {
  let test = harness();
  const first = test.start(0);
  assert.equal(test.requests.length, 1);
  assert.equal(test.messages[0].text, "Updating air…", "start notice precedes the HTTP response");
  const second = test.start(1);
  assert.equal(test.messages.at(-1).text, "clock queued.");
  assert.equal(test.button("1").disabled, true);
  assert.equal(test.button("1").textContent, "Queued");
  assert.strictEqual(test.start(1), second, "duplicate queued clicks share completion");
  assert.strictEqual(test.start(0), first, "duplicate active clicks do not reinstall");
  const batch = test.all();
  assert.equal(test.requests.length, 1, "bulk joins the active individual queue");
  const redrawn = { disabled: false };
  test.context.markPendingInstall(redrawn, test.offers[1]);
  assert.equal(redrawn.disabled, true); assert.equal(redrawn.textContent, "Queued");
  test.requests[0].reply(); await flush();
  assert.equal(test.requests.length, 2);
  assert(test.requests[1].url.endsWith("app=clock"));
  test.requests[1].reply(403, "Not confirmed."); await flush();
  assert.equal(test.requests.length, 3, "one refused update does not stop the batch");
  assert(test.requests[2].url.endsWith("app=weather"));
  test.requests[2].reply();
  assert.deepEqual(await Promise.all([first, second]), [true, false]);
  await batch;
  assert.equal(test.maximumActive, 1);
  assert.equal(test.reloads, 2);
  assert.equal(test.button("all").disabled, false);
  assert(test.messages.some(message => message.text === "clock: Not confirmed." && message.kind === "error"));
  assert.deepEqual(test.messages.filter(message => message.text.includes("updated to")).map(message => message.text),
    ["air updated to 1.2.0.", "weather updated to 1.2.0."]);
  assert(test.messages.filter(message => message.stays).every(message => message.dismissed));

  test = harness();
  const bulkFirst = test.all();
  const overlap = test.start(2);
  for (let index = 0; index < 3; index++) { test.requests[index].reply(); await flush(); }
  await bulkFirst; assert.equal(await overlap, true);
  assert.equal(test.requests.length, 3); assert.equal(test.maximumActive, 1);

  test = harness();
  const uncertain = test.start(0), following = test.start(1);
  test.requests[0].lose(); await flush();
  assert.equal(test.requests.length, 2);
  assert(test.requests[1].url.endsWith("app=clock"), "lost response is not replayed");
  test.requests[1].reply();
  assert.equal(await uncertain, false); assert.equal(await following, true);
  assert(!test.messages.some(message => message.text === "air updated to 1.2.0."));

  test = harness();
  test.offers[0].app.system = true;
  test.refusePermission();
  const refused = test.start(0), permitted = test.start(1);
  await flush();
  assert.equal(await refused, false); assert.equal(test.requests.length, 1);
  assert(test.requests[0].url.endsWith("app=clock"));
  test.requests[0].reply(); await permitted;

  test = harness();
  test.offers[0].app.trusts = ["Example authority"];
  const approved = test.start(0);
  await flush();
  assert.equal(JSON.parse(test.requests[0].options.body).approved_authorities, 1);
  test.requests[0].reply(); assert.equal(await approved, true);

  test = harness();
  test.offers[0].app.trusts = ["Example authority"];
  test.refusePermission();
  assert.equal(await test.start(0), false);
  assert.equal(test.requests.length, 0, "refused authority consent sends no install");

  test = harness();
  const noAuthorities = test.start(0);
  assert.equal(JSON.parse(test.requests[0].options.body).approved_authorities, 0);
  test.requests[0].reply(); await noAuthorities;

  test = harness();
  const inFlight = test.start(0), abandoned = test.start(1);
  const leaving = { preventDefault() { this.prevented = true; } };
  test.events.beforeunload(leaving);
  assert(leaving.prevented && leaving.returnValue === "");
  test.events.pagehide();
  assert.equal(await abandoned, false);
  test.requests[0].reply(); await inFlight;
  assert.equal(test.requests.length, 1, "navigation drops only unsent work");
  assert.equal(test.reloads, 0, "hidden page does not start follow-up requests");
  assert.equal(await test.start(2), false);
  test.events.pageshow({ persisted: true }); await flush();
  assert.equal(test.reloads, 1, "BFCache return refreshes data without replaying POST");
  assert.equal(test.requests.length, 1);
  const resumed = test.start(2);
  assert.equal(test.requests.length, 2, "BFCache return accepts a new user action");
  test.requests[1].reply(); assert.equal(await resumed, true);
  const idle = { preventDefault() { throw new Error("idle page should not warn"); } };
  test.events.beforeunload(idle);
  test = harness();
  const active = test.start(0), staleQueued = test.start(1);
  test.context.invalidateCatalogs();
  assert.equal(await staleQueued, false, "a source change cancels unsent installs");
  assert.equal(await test.start(2), false, "an old tile cannot enqueue another install");
  assert.equal(test.button("1").disabled, false);
  test.requests[0].reply(); await active;
  assert.equal(test.requests.length, 1);

  test = harness();
  let consent;
  test.context.confirmed = () => new Promise(resolve => { consent = resolve; });
  test.offers[0].app.system = true;
  const waiting = test.start(0);
  test.context.invalidateCatalogs();
  consent(true);
  assert.equal(await waiting, false, "permission for an old offer cannot start an install");
  assert.equal(test.requests.length, 0);
  test = harness();
  const installing = test.start(0), queued = test.start(1);
  const saving = test.saveSources();
  await flush();
  const waitingNotice = test.messages.find(message => message.text === "Waiting for app installation.");
  assert(waitingNotice && waitingNotice.stays && waitingNotice.kind.includes("passive"));
  assert.equal(waitingNotice.dismissed, false);
  assert.equal(await queued, false);
  assert.equal(test.requests.length, 1, "source indices stay in place while a sent install may retry");
  test.requests[0].reply(503, "Device busy."); await flush();
  assert.equal(waitingNotice.dismissed, false, "the explanation stays during install retries");
  assert.equal(test.requests.length, 2);
  assert(test.requests[1].url.startsWith("/api/apps/install"));
  test.requests[1].reply(); await installing; await flush();
  assert.equal(test.requests.length, 3);
  assert.equal(test.requests[2].url, "/api/sources", "sources change only after the in-flight install finishes");
  assert.equal(waitingNotice.dismissed, true, "the explanation ends before the sources POST");
  test.requests[2].reply(); await saving;
  assert.equal(await test.start(1), false);
  test = harness();
  const idleSave = test.saveSources(); await flush();
  assert(!test.messages.some(message => message.text === "Waiting for app installation."), "idle saves do not claim to wait");
  test.requests[0].reply(); await idleSave;
  await testCatalogPrecedence();
  await testConfirmationQueue();
  console.log("App install queue: sequential individual/bulk, dedupe, errors, permission cancellation and BFCache passed.");
}

async function testCatalogPrecedence() {
  for (const previouslyRead of [false, true]) {
    for (const failedSource of [0, 1]) {
      const test = harness(), reads = [];
      const requestApp = test.context.askForApp;
      const active = previouslyRead ? test.start(0) : null;
      const queued = previouslyRead ? test.start(1) : null;
      Object.assign(test.context, {
        catalogsRead: previouslyRead, TextEncoder, MANIFEST_AUTHOR_MAX: 63,
        sources: ["https://official.test", "https://added.test", "https://last.test"],
        loadSources: async () => {}, loadRemoved() {},
        askForApp: async url => {
          const source = Number(url.split("=")[1]);
          reads.push(source);
          if (source === failedSource) throw new Error("Failed to fetch");
          return { apps: test.offers.map(offer => ({ ...offer.app, version: "9.0.0" })) };
        },
      });
      vm.runInContext(apps.slice(apps.indexOf("function catalogAuthorIsAllowed("), apps.indexOf("let offeredSources")) +
        apps.slice(apps.indexOf("/* One reading at a time"), apps.indexOf("/* ---- sources ---- */")), test.context);
      vm.runInContext("offeredSources = sources.slice()", test.context);
      const previous = test.context.offered;
      const previousSources = vm.runInContext("offeredSources", test.context);
      const previousGeneration = vm.runInContext("catalogGeneration", test.context);
      await test.context.loadCatalogs();
      assert.deepEqual(reads, Array.from({ length: failedSource + 1 }, (_, index) => index));
      assert.equal(vm.runInContext("catalogGeneration", test.context), previousGeneration + 1,
        "every failed read invalidates previous handlers");
      if (failedSource === 0) {
        assert.strictEqual(test.context.offered, previous, "first-source failure keeps previous offers visible");
        assert.strictEqual(vm.runInContext("offeredSources", test.context), previousSources,
          "kept offers retain their source list");
        assert.equal(test.context.offered.length, 3);
        assert.equal(test.context.catalogsRead, false, "kept offers cannot be installed or updated");
      } else {
        assert.notStrictEqual(test.context.offered, previous, "a partial read publishes only sources with established precedence");
        assert.equal(test.context.offered.length, 3);
        assert(test.context.offered.every(offer => offer.source < failedSource));
        assert.equal(test.context.catalogsRead, true);
      }
      assert.equal(await test.start(2), false, "old individual offers cannot install");
      if (queued) assert.equal(await queued, false, "failure cancels queued installs");
      const batch = test.all();
      if (active) { test.requests[0].reply(); await active; }
      await batch;
      assert.equal(test.requests.length, previouslyRead ? 1 : 0, "Update all cannot use old offers after failure");
      assert(test.messages.some(message => message.text.includes("Source unavailable")));
      if (failedSource > 0) {
        test.context.askForApp = requestApp;
        const install = test.context.install(test.button("safe"), test.context.offered[0]);
        await flush();
        assert.equal(test.requests.length, previouslyRead ? 2 : 1);
        test.requests.at(-1).reply();
        assert.equal(await install, true, "earlier sources remain installable after a later failure");
        test.context.updates = () => test.context.offered.slice(1);
        const updates = test.all();
        await flush();
        test.requests.at(-1).reply(); await flush();
        test.requests.at(-1).reply(); await updates;
      }
      test.context.askForApp = async url => ({ apps: Number(url.split("=")[1]) === 0 ?
        [{ ...test.offers[0].app, version: "2.0.0" }] : [{ ...test.offers[0].app, version: "9.0.0" }] });
      await test.context.loadCatalogs();
      assert.equal(test.context.catalogsRead, true);
      assert.equal(test.context.offered[0].source, 0);
      assert.equal(test.context.offered[0].app.version, "2.0.0", "successful refresh restores first-source precedence");
      assert.equal(test.context.isCurrentOffer(test.offers[0]), false, "old click handlers remain invalid");
    }
  }
}

async function testConfirmationQueue() {
  const test = harness();
  const dialog = test.button("confirm-dialog"), listeners = [];
  dialog.addEventListener = (event, callback, options) => {
    assert.equal(event, "close"); assert.equal(options.once, true); listeners.push(callback);
  };
  dialog.showModal = () => { assert(!dialog.open); dialog.open = true; };
  const close = value => {
    dialog.returnValue = value; dialog.open = false;
    for (const callback of listeners.splice(0)) callback();
  };
  vm.runInContext(core.slice(core.indexOf("let confirmationQueue"), core.indexOf("/* After a restart")), test.context);
  test.offers[1].app.trusts = ["Private CA"];
  const downloading = test.start(0), waiting = test.start(1);
  let removed = false;
  const removal = test.context.confirmed("Remove C?", "Remove").then(accepted => { removed = accepted; });
  await flush();
  assert.equal(test.button("confirm-text").textContent, "Remove C?");
  test.requests[0].reply(); await downloading; await flush();
  assert.equal(test.button("confirm-text").textContent, "Remove C?", "the install must not overwrite removal");
  assert.equal(listeners.length, 1);
  close("no"); await removal; await flush();
  assert.equal(removed, false);
  assert.match(test.button("confirm-text").textContent, /trusts its own certificate authority/);
  assert.equal(test.button("confirm-yes").textContent, "Install");
  assert.equal(dialog.returnValue, "no", "each question starts cancelled");
  close("yes"); await flush();
  assert.equal(removed, false, "accepting the authority warning cannot also remove C");
  assert.equal(test.requests.length, 2);
  test.requests[1].reply(); await waiting;

  const accepted = test.context.confirmed("First?", "Remove", "First notes");
  const cancelled = test.context.confirmed("Second?", "Reset");
  await flush();
  assert.equal(test.button("confirm-notes").textContent, "First notes");
  assert.equal(test.button("confirm-notes").hidden, false);
  close("yes"); assert.equal(await accepted, true); await flush();
  assert.equal(test.button("confirm-text").textContent, "Second?");
  assert.equal(test.button("confirm-notes").hidden, true);
  close("no"); assert.equal(await cancelled, false);
  assert.equal(listeners.length, 0);
}
module.exports = testQueue;
if (require.main === module) testQueue().catch(error => { console.error(error); process.exitCode = 1; });
