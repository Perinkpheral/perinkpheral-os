// SPDX-License-Identifier: MIT
// Page wording and device replies use the real dictionary and text-node translator.
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");
const root = path.resolve(__dirname, "../../..");
const read = name => fs.readFileSync(path.join(root, name), "utf8");
const script = name => process.env.PERINKPHERAL_TEST_PAGE
  ? fs.readFileSync(process.env.PERINKPHERAL_TEST_PAGE, "utf8") : read("firmware/main/web/js/" + name);

function languageContext(language, values = {}) {
  const context = vm.createContext({
    URL, Node: { TEXT_NODE: 3, ELEMENT_NODE: 1 },
    localStorage: { getItem: () => language }, ...values,
  });
  const source = script("i18n.js");
  vm.runInContext(source.slice(source.indexOf("const RUSSIAN"), source.indexOf('if (pageLanguage !== "en")')), context);
  return context;
}

function displayed(context, text) {
  const node = { nodeType: 3, nodeValue: text, parentElement: { closest: () => null } };
  context.translate(node);
  return node.nodeValue;
}

function testPolls() {
  const source = script("apps.js");
  for (const language of ["en", "ru"]) {
    const context = languageContext(language);
    vm.runInContext(source.slice(source.indexOf("const pace ="), source.indexOf("const appDescription")), context);
    const app = { name: "sample", settings: [{ key: "interval", options: ["daily"], labels: ["Daily"] }],
      values: { interval: "daily" } };
    const updates = polls => displayed(context, context.appFacts({ ...app,
      polls: polls.map(poll => ({ url: "https://example.test/data", ...poll })),
    }).find(([name]) => name === "Updates")[1]);
    const manual = language === "ru" ? "По запросу" : "On request";
    const minute = language === "ru" ? "Каждые 1 мин" : "Every 1 min";
    assert.equal(updates([]), manual);
    assert.equal(updates([{ manual: true }]), manual);
    assert.equal(updates([{ manual: true, every: 60 }]), manual, "manual polls ignore every");
    assert.equal(updates([{ manual: true, every: "{{interval}}" }]), manual);
    assert.equal(updates([{ every: 60 }]), minute);
    assert.equal(updates([{ manual: false, every: 60 }]), minute);
    assert.equal(updates([{ manual: true }, { every: 60 }]), manual + ", " + minute);
    assert.equal(updates([{ every: 60 }, { manual: true, every: 60 }, { manual: true }]), minute + ", " + manual);
    assert.equal(updates([{ every: 3600 }, { every: 20 }, { every: "{{interval}}" }]),
      language === "ru" ? "Каждые 1 ч, Каждые 20 с, Каждые Daily" : "Every 1 h, Every 20 s, Every Daily");
    // An application's own data is shown as it is, even when it spells a word of the dictionary.
    const facts = context.appFacts({ name: "stored", version: "restarting", hosts: ["https://saved.example"],
      trusts: ["Saved."], companion: "https://setup.example/page", polls: [] });
    for (const label of ["Name", "Version", "Connects to", "Own certificate authorities", "Set up page"]) {
      const fact = facts.find(([name]) => name === label);
      assert.equal(fact[2], true, label + " is shown verbatim");
    }
    const none = context.appFacts({ name: "x", polls: [] });
    assert.equal(none.find(([name]) => name === "Connects to")[1], "Nothing");
    assert.notEqual(none.find(([name]) => name === "Connects to")[2], true, "a page word stays translatable");
    assert.notEqual(none.find(([name]) => name === "Own certificate authorities")[2], true);
  }
}

async function testReset() {
  const source = script("device.js");
  const warning = "Factory reset? Device settings and saved application values will be erased. Installed applications and their files will remain.";
  const russian = "Сбросить к заводским настройкам? Настройки устройства и сохранённые значения приложений будут стёрты. Установленные приложения и их файлы останутся.";
  for (const language of ["en", "ru"]) {
    let click, accepted = false;
    const questions = [], requests = [], notices = [], button = {};
    const context = languageContext(language, {
      byId: id => { assert.equal(id, "reset"); return { addEventListener: (_event, handler) => { click = handler; } }; },
      confirmed: async (text, action) => { questions.push({ text, action }); return accepted; },
      toast: text => notices.push(text), CONFIRM_ON_DEVICE: "Confirm on the device.",
      send: async (...request) => requests.push(request),
    });
    vm.runInContext(source.slice(source.indexOf('byId("reset").addEventListener'), source.indexOf("/* What a section does")), context);
    await click({ target: button });
    assert.deepEqual(requests, [], "cancelling the reset sends no request");
    assert.equal(questions[0].text, warning);
    assert.equal(displayed(context, questions[0].text), language === "ru" ? russian : warning);
    assert.equal(displayed(context, questions[0].action), language === "ru" ? "Сбросить к заводским настройкам" : "Factory reset");
    accepted = true;
    await click({ target: button });
    assert.equal(requests.length, 1);
    assert.strictEqual(requests[0][0], button);
    assert.equal(requests[0][1], "/api/reset");
    assert.equal(requests[0][2].method, "POST");
    assert.deepEqual(notices, ["Confirm on the device."]);
  }
}

function testDeviceAnswers() {
  // Include the services whose errors are forwarded by main and recovery routes.
  const sources = [
    "firmware/main/web_server.c", "firmware/main/web_apps.c", "firmware/main/web_screen.c",
    "firmware/main/devtools.c", "firmware/main/app_install.c", "firmware/main/ota.c",
    "firmware/components/appconf/appconf.c", "firmware/components/net/ota_stream.c",
    "recovery/main/main.c", "recovery/main/ota.c",
  ];
  const english = languageContext("en"), russian = languageContext("ru");
  const answers = new Set();
  for (const name of sources) {
    const source = read(name).replace(/"(?:[^"\\]|\\.)*"|\/\*[\s\S]*?\*\/|\/\/[^\n]*/g,
      text => text.startsWith('"') ? text : "");
    const strings = text => [...text.matchAll(/"(?:[^"\\]|\\.)*"/g)].map(match => match[0].slice(1, -1));
    const literals = strings(source);
    const messages = literals.filter(text => /^[A-Z]/.test(text) && /[.!]$/.test(text));
    for (const literal of literals.filter(text => text.startsWith('{\\"') && text.endsWith("}"))) {
      const reply = JSON.parse(JSON.parse('"' + literal + '"'));
      for (const field of ["error", "ok", "message"]) if (reply[field]) messages.push(reply[field]);
    }
    for (const call of source.matchAll(/\b(?:refuse|web_send_text|httpd_resp_send_err)\([^;]*;/g))
      messages.push(...strings(call[0]).filter(text => !/^\d{3} /.test(text) &&
        (/ /.test(text) || ["pressed", "opened", "stored", "restarting"].includes(text))));
    for (const message of messages) {
      // Compiler assertions and build diagnostics are not web responses.
      if (message.startsWith("Updates over the network rely on the bootloader")) continue;
      let part = 0;
      const template = message.replace(/%[dsu]/g, () => "{" + part++ + "}");
      const values = template.startsWith("Invalid value:") ? ["token"] : ["7", "31", "255"];
      const answer = template.replace(/\{(\d)\}/g, (_match, index) => values[index]);
      const translated = displayed(russian, answer);
      assert.notEqual(translated, answer, name + ": missing Russian for " + template);
      assert.match(translated, /[А-Яа-яЁё]/, name + ": Russian reply " + template);
      assert.equal(displayed(english, answer), answer, "English reply stays unchanged");
      answers.add(template);
    }
  }
  assert(answers.size >= 70, "audit includes settings, application and recovery replies");
  assert.equal(displayed(russian, "Recovery image unavailable. Restore it over USB."),
    "Образ recovery недоступен. Восстановите его по USB.");
  assert.equal(displayed(russian, "Invalid value: token."), "Неверное значение: token.");
  assert.equal(displayed(russian, "Source unavailable (HTTP -1)."), "Источник недоступен (HTTP -1).");
  assert.equal(displayed(russian, "Device busy. Retrying"), "Устройство занято. Повторяем попытку");
  assert.equal(displayed(russian, "Device busy. Try again shortly."), "Устройство пока занято. Попробуйте чуть позже.");
  assert.equal(displayed(russian, "Weather"), "Weather", "application titles are not dictionary entries");
}

async function test() {
  testPolls();
  await testReset();
  testDeviceAnswers();
  console.log("Page wording: manual polls, reset warning and device-answer translations passed.");
}

module.exports = test;
if (require.main === module) test().catch(error => { console.error(error); process.exitCode = 1; });
