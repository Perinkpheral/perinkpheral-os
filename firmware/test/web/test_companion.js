// SPDX-License-Identifier: MIT
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const root = path.resolve(__dirname, "../../..");
const example = fs.readFileSync(path.join(root, "sdk/companion/example.html"), "utf8");
const nodes = new Map();
function byId(id) {
  if (!nodes.has(id)) nodes.set(id, { value: "", addEventListener(type, handler) { this[type] = handler; } });
  return nodes.get(id);
}
const messages = [];
const opener = { postMessage: (value, origin) => messages.push({ value, origin }) };
let receive;
vm.runInNewContext(example.match(/<script>([\s\S]*?)<\/script>/)[1], {
  URL, window: { opener, addEventListener: (_type, handler) => { receive = handler; } },
  document: { getElementById: byId },
});
const submit = id => byId(id).submit({ preventDefault() {} });
const message = (origin, source = opener) => receive({ source, origin, data: { perinkpheral: "app", title: "Accepted" } });
message("https://attacker.test");
submit("form");
assert.equal(messages.length, 0);
byId("origin").value = "http://192.168.1.10";
submit("connect");
assert.equal(messages.length, 1);
assert.equal(messages[0].origin, "http://192.168.1.10");
message("https://attacker.test");
message("http://192.168.1.10", {});
submit("form");
assert.equal(messages.length, 1);
message("http://192.168.1.10");
byId("token").value = "secret";
submit("form");
assert.equal(messages[1].value.values.token, "secret");
assert.equal(messages[1].origin, "http://192.168.1.10");
byId("origin").value = "https://attacker.test";
submit("connect");
assert.equal(messages.length, 2);

const apps = fs.readFileSync(process.env.PERINKPHERAL_TEST_PAGE || path.join(root, "firmware/main/web/js/apps.js"), "utf8");
const script = apps.slice(apps.indexOf("let companion = null;"), apps.indexOf("function fillFromCompanion"));
for (const companion of ["https://?x", "bad", "http://example.test", "https://user@example.test", "https://example.test:65536"]) {
  let opened = false, error = false;
  vm.runInNewContext(script, { URL, byId, appInDialog: { companion }, window: { open() { opened = true; } },
    toast() { error = true; } });
  byId("app-companion").click();
  assert.equal(opened, false, companion);
  assert.equal(error, true, companion);
}
console.log("Companion: pinned independent origin, message binding and malformed URL fallback passed");
