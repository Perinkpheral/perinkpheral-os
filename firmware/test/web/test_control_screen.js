// SPDX-License-Identifier: MIT
// Screen headers and delayed bodies can complete in different orders.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const source = fs.readFileSync(path.join(__dirname, "../../main/web/js/device.js"), "utf8");
const deferred = () => {
  let resolve, reject;
  const promise = new Promise((done, fail) => { resolve = done; reject = fail; });
  return { promise, resolve, reject };
};
const flush = async () => { for (let step = 0; step < 10; step++) await Promise.resolve(); };

async function test() {
  const requests = [], revoked = [];
  const image = { src: "" };
  let timers = 0;
  const context = vm.createContext({
    status: { dev_tools: true }, currentSection: () => "control", document: { hidden: false },
    clearTimeout() {}, setTimeout() { timers++; return timers; }, byId: () => image,
    URL: { createObjectURL: blob => blob, revokeObjectURL: url => revoked.push(url) },
    fetch: () => {
      const answer = deferred(), body = deferred();
      requests.push({ answer, body, headers(number) {
        answer.resolve({ ok: true, headers: { get: () => number }, blob: () => body.promise });
      } });
      return answer.promise;
    },
  });
  vm.runInContext(source.slice(source.indexOf("const LONG_PRESS_MS"), source.indexOf("async function press(")), context);
  const slow = context.lookAtScreen();
  requests[0].headers("1"); await flush();
  const fast = context.lookAtScreen();
  requests[1].headers("2"); requests[1].body.resolve("frame-2"); await fast;
  requests[0].body.resolve("frame-1"); await slow;
  assert.equal(image.src, "frame-2", "a superseded body cannot overwrite the newer image");
  assert.equal(timers, 1, "the superseded request cannot create a second poll loop");
  const same = context.lookAtScreen(); requests[2].headers("2"); await same;
  assert.equal(image.src, "frame-2");

  const broken = context.lookAtScreen(); requests[3].headers("3"); await flush();
  requests[3].body.reject(Error("Interrupted body")); await broken;
  assert.equal(image.src, "frame-2");
  const retry = context.lookAtScreen(); requests[4].headers("3"); requests[4].body.resolve("frame-3"); await retry;
  assert.equal(image.src, "frame-3", "a body failure must not commit its frame number");
  assert.deepEqual(revoked, ["frame-2"]);

  const leaving = context.lookAtScreen(); requests[5].headers("4"); await flush();
  context.document.hidden = true;
  await context.lookAtScreen();
  requests[5].body.resolve("frame-4"); await leaving;
  assert.equal(image.src, "frame-3", "hiding the page invalidates an unfinished screen read");
  console.log("Control screen: overlapping reads and interrupted bodies passed.");
}
module.exports = test;
if (require.main === module) test().catch(error => { console.error(error); process.exitCode = 1; });
