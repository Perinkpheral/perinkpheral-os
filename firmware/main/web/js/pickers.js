/* SPDX-License-Identifier: MIT
 * The page's own choosers, in ink on paper where the browser would open its system's menus: a
 * list's choices, and a date with its time. The fields stay what they are - a select, an input of
 * date and time - and keep the values; what opens from them is drawn here, and a choice is told
 * to the field's listeners as the browser's own would be. On a touch screen the phone's own
 * chooser stays: that is what people expect there. */
"use strict";

let chooser = null; /* what is open: { box, field, cleanups } */
let pointerKind = "mouse";

function closeChooser(refocus) {
  if (!chooser) return;
  const { box, field } = chooser;
  const { cleanups } = chooser;
  chooser = null;
  cleanups.forEach(cleanup => cleanup());
  box.remove();
  if (refocus) field.focus({ preventScroll: true });
}

/* Under its field, or above it when there is more room there; inside the window. A list scrolls
 * in the room there is; a calendar scrolls only in a window lower than itself. */
function positionChooser(box, field) {
  const at = field.getBoundingClientRect();
  const below = window.innerHeight - at.bottom - 8, above = at.top - 8;
  const isList = box.classList.contains("listbox");
  box.style.maxHeight = "";
  const up = below < (isList ? Math.min(box.scrollHeight, 240) : box.offsetHeight) && above > below;
  box.style.minWidth = at.width + "px";
  box.style.maxHeight = (isList ? Math.max(120, Math.min(320, up ? above : below)) : window.innerHeight - 16) + "px";
  box.style.left = Math.max(8, Math.min(at.left, window.innerWidth - box.offsetWidth - 8)) + "px";
  const top = up ? at.top - box.offsetHeight : at.bottom;
  box.style.top = Math.max(8, Math.min(top, window.innerHeight - box.offsetHeight - 8)) + "px";
}

function placeChooser(box, field) {
  (field.closest("dialog") || document.body).append(box);
  positionChooser(box, field);
  chooser = { box, field, cleanups: [] };
  /* Focus that leaves the box and its field closes it: Tab, or a toggle pressed from the keyboard.
   * Asked a moment later: a calendar that redraws its days moves focus to the new ones. */
  const left = () => setTimeout(() => {
    const now = document.activeElement;
    if (chooser && chooser.box === box && !box.contains(now) && now !== field && document.hasFocus())
      closeChooser(false);
  });
  box.addEventListener("focusout", left);
  field.addEventListener("focusout", left);
  chooser.cleanups.push(() => field.removeEventListener("focusout", left));
}

function tell(field) {
  field.dispatchEvent(new Event("input", { bubbles: true }));
  field.dispatchEvent(new Event("change", { bubbles: true }));
}

/* ---- a list ---- */

function openList(select) {
  closeChooser(false);
  const box = make("div", { className: "chooser listbox", tabIndex: -1 });
  box.setAttribute("role", "listbox");
  if (select.labels && select.labels[0]) box.setAttribute("aria-label", select.labels[0].textContent);
  const items = [];
  const add = option => {
    if (option.hidden) return;
    /* The option's text is already in the page's language. */
    const item = make("div", { className: "option", textContent: option.textContent, translate: false,
                               id: "choice-" + items.length });
    item.setAttribute("role", "option");
    item.setAttribute("aria-selected", String(option.selected));
    if (option.disabled) item.setAttribute("aria-disabled", "true");
    item.option = option;
    items.push(item);
    box.append(item);
  };
  for (const child of select.children) {
    if (child.tagName !== "OPTGROUP") add(child);
    else {
      box.append(make("div", { className: "group", textContent: child.label, translate: false }));
      [...child.children].forEach(add);
    }
  }
  if (!items.length) return;

  let active = Math.max(0, items.findIndex(item => item.option.selected));
  const show = index => {
    items[active].classList.remove("active");
    active = index;
    items[active].classList.add("active");
    box.setAttribute("aria-activedescendant", items[active].id);
    items[active].scrollIntoView({ block: "nearest" });
  };
  const step = (from, by, count = 1) => {
    let found = from;
    for (let i = from + by; i >= 0 && i < items.length && count > 0; i += by)
      if (!items[i].option.disabled) { found = i; count--; }
    return found;
  };
  const choose = item => {
    if (item.option.disabled) return;
    /* The list was redrawn while open - networks found meanwhile: nothing stale is chosen. */
    const current = select.contains(item.option);
    const changed = current && !item.option.selected;
    if (changed) item.option.selected = true;
    closeChooser(true);
    if (changed) tell(select);
  };
  box.addEventListener("pointermove", event => {
    const item = event.target.closest(".option");
    if (item && item !== items[active]) show(items.indexOf(item));
  });
  box.addEventListener("click", event => {
    const item = event.target.closest(".option");
    if (item) choose(item);
  });
  let typed = "", typedAt = 0;
  box.addEventListener("keydown", event => {
    const page = Math.max(1, Math.floor(box.clientHeight / items[active].offsetHeight) - 1);
    const moves = { ArrowDown: () => step(active, 1), ArrowUp: () => step(active, -1),
                    Home: () => step(-1, 1), End: () => step(items.length, -1),
                    PageDown: () => step(active, 1, page), PageUp: () => step(active, -1, page) };
    if (moves[event.key]) {
      event.preventDefault();
      show(moves[event.key]());
    } else if (event.key === "Enter" || (event.key === " " && !typed)) {
      event.preventDefault();
      choose(items[active]);
    } else if (event.key === "Escape") {
      event.preventDefault(); /* a dialog under the list stays open */
      closeChooser(true);
    } else if (event.key === "Tab") {
      closeChooser(true);
    } else if (event.key.length === 1 && !event.ctrlKey && !event.metaKey && !event.altKey) {
      const now = Date.now();
      typed = (now - typedAt > 700 ? "" : typed) + event.key.toLowerCase();
      typedAt = now;
      const found = items.findIndex(item => !item.option.disabled &&
                                            item.textContent.toLowerCase().startsWith(typed));
      if (found >= 0) show(found);
    }
  });
  placeChooser(box, select);
  show(active);
  box.focus({ preventScroll: true });
  /* Choices that change while open - networks found meanwhile - are shown as they now are. */
  const watch = new MutationObserver(() => {
    watch.disconnect();
    if (chooser && chooser.box === box) openList(select);
  });
  watch.observe(select, { childList: true, subtree: true, characterData: true });
  chooser.cleanups.push(() => watch.disconnect());
}

/* ---- a date and its time ---- */

const two = number => String(number).padStart(2, "0");

/* The field's value, "YYYY-MM-DDTHH:MM", as numbers; an empty field is now. */
function readMoment(input) {
  const match = /^(\d{4})-(\d\d)-(\d\d)T(\d\d):(\d\d)/.exec(input.value);
  if (match) return match.slice(1).map(Number);
  const now = new Date();
  return [now.getFullYear(), now.getMonth() + 1, now.getDate(), now.getHours(), now.getMinutes()];
}

function openCalendar(input, focusInside) {
  closeChooser(false);
  let [year, month, day, hour, minute] = readMoment(input);
  let shownYear = year, shownMonth = month - 1, focused = day;
  const box = make("div", { className: "chooser calendar", tabIndex: -1 });
  box.setAttribute("role", "dialog");
  if (input.labels && input.labels[0]) box.setAttribute("aria-label", input.labels[0].textContent);
  const title = make("span", { className: "title", translate: false });
  const back = make("button", { type: "button", className: "step", textContent: "‹", title: t("Previous month") });
  const ahead = make("button", { type: "button", className: "step", textContent: "›", title: t("Next month") });
  const days = make("div", { className: "days" });
  const hours = make("input", { className: "part", inputMode: "numeric", maxLength: 2, value: two(hour),
                                autocomplete: "off", ariaLabel: t("Hours") });
  const minutes = make("input", { className: "part", inputMode: "numeric", maxLength: 2, value: two(minute),
                                  autocomplete: "off", ariaLabel: t("Minutes") });
  const done = make("button", { type: "button", className: "quiet", textContent: t("Done") });
  box.append(make("div", { className: "month" }), days, make("div", { className: "time" }));
  box.firstChild.append(back, title, ahead);
  box.lastChild.append(make("span", { textContent: t("Time") }), hours, make("span", { textContent: ":" }), minutes, done);

  let keeping = false;
  const keep = () => {
    if (input.disabled) return closeChooser(false);
    keeping = true;
    input.value = String(year).padStart(4, "0") + "-" + two(month) + "-" + two(day) + "T" + two(hour) + ":" + two(minute);
    tell(input);
    keeping = false;
  };
  /* Typed into the field itself, the date is the field's: the calendar gives way. */
  const typedInField = () => {
    if (!keeping && chooser && chooser.box === box) closeChooser(false);
  };
  const language = pageLanguage === "en" ? "en-GB" : pageLanguage;
  /* Weeks start on Monday, as the device's calendar does. 1 January 2024 was a Monday. */
  const weekdays = [...Array(7)].map((_, at) =>
    new Intl.DateTimeFormat(language, { weekday: "short" }).format(new Date(2024, 0, 1 + at)));
  const draw = focusDay => {
    const named = new Intl.DateTimeFormat(language, { month: "long", year: "numeric" }).format(new Date(shownYear, shownMonth, 1));
    title.textContent = named.charAt(0).toUpperCase() + named.slice(1);
    const count = new Date(shownYear, shownMonth + 1, 0).getDate();
    focused = Math.min(focused, count);
    const today = new Date();
    const cells = weekdays.map(name => make("span", { className: "weekday", textContent: name, translate: false }));
    for (let gap = (new Date(shownYear, shownMonth, 1).getDay() + 6) % 7; gap > 0; gap--) cells.push(make("span"));
    for (let date = 1; date <= count; date++) {
      const cell = make("button", { type: "button", className: "day", textContent: String(date), tabIndex: date === focused ? 0 : -1 });
      cell.date = date;
      if (shownYear === year && shownMonth === month - 1 && date === day) cell.setAttribute("aria-pressed", "true");
      if (shownYear === today.getFullYear() && shownMonth === today.getMonth() && date === today.getDate())
        cell.classList.add("today");
      cells.push(cell);
    }
    days.replaceChildren(...cells);
    if (chooser && chooser.box === box) positionChooser(box, input); /* a month of six weeks is taller */
    if (focusDay) days.querySelector(".day[tabindex='0']").focus({ preventScroll: true });
  };
  const turn = (by, focusDay) => {
    const at = new Date(shownYear, shownMonth + by, 1);
    shownYear = at.getFullYear();
    shownMonth = at.getMonth();
    draw(focusDay);
  };
  back.addEventListener("click", () => turn(-1));
  ahead.addEventListener("click", () => turn(1));
  days.addEventListener("click", event => {
    if (!event.target.classList.contains("day")) return;
    [year, month, day, focused] = [shownYear, shownMonth + 1, event.target.date, event.target.date];
    keep();
    draw(true);
  });
  days.addEventListener("keydown", event => {
    const moves = { ArrowLeft: -1, ArrowRight: 1, ArrowUp: -7, ArrowDown: 7 };
    if (!(event.key in moves)) return;
    event.preventDefault();
    const at = new Date(shownYear, shownMonth, focused + moves[event.key]);
    focused = at.getDate();
    if (at.getMonth() !== shownMonth) turn(at.getMonth() - shownMonth + 12 * (at.getFullYear() - shownYear), true);
    else draw(true);
  });
  const typedTime = (field, most, apply) => field.addEventListener("input", () => {
    field.value = field.value.replace(/\D/g, "");
    const number = Number(field.value);
    if (field.value !== "" && number <= most) {
      apply(number);
      keep();
    }
  });
  typedTime(hours, 23, number => { hour = number; });
  typedTime(minutes, 59, number => { minute = number; });
  for (const field of [hours, minutes])
    field.addEventListener("blur", () => { field.value = two(field === hours ? hour : minute); });
  done.addEventListener("click", () => closeChooser(true));
  box.addEventListener("keydown", event => {
    if (event.key === "Escape") {
      event.preventDefault();
      closeChooser(true);
    }
  });
  draw(false);
  placeChooser(box, input);
  input.addEventListener("input", typedInField);
  chooser.cleanups.push(() => input.removeEventListener("input", typedInField));
  /* Opened from the keyboard the days take the keys; opened with the mouse the field keeps them, to
   * type the date in as well. */
  if (focusInside) days.querySelector(".day[tabindex='0']").focus({ preventScroll: true });
}

/* ---- where they open ---- */

const isList = field => field && field.tagName === "SELECT" && !field.multiple && field.size <= 1;
const isMoment = field => field && field.tagName === "INPUT" && field.type === "datetime-local";

document.addEventListener("pointerdown", event => {
  pointerKind = event.pointerType || "mouse";
  if (chooser && !chooser.box.contains(event.target) && event.target !== chooser.field) closeChooser(false);
}, true);

document.addEventListener("mousedown", event => {
  const field = event.target.closest && event.target.closest("select, input[type=datetime-local]");
  if (!field || field.disabled || event.button !== 0 || pointerKind === "touch") return;
  if (!isList(field) && !isMoment(field)) return;
  /* The system's menu does not open; the page's own does. */
  event.preventDefault();
  if (chooser && chooser.field === field) return closeChooser(true);
  field.focus({ preventScroll: true });
  if (isList(field)) openList(field);
  else openCalendar(field, false);
}, true);

/* Firefox opens its calendar on the click, not the press. */
document.addEventListener("click", event => {
  if (pointerKind !== "touch" && isMoment(event.target) && !event.target.disabled) event.preventDefault();
}, true);

document.addEventListener("keydown", event => {
  const field = event.target;
  if (chooser && field === chooser.field && event.key === "Escape") {
    event.preventDefault();
    return closeChooser(false);
  }
  if (chooser || event.ctrlKey || event.metaKey) return;
  const opens = event.key === "F4" || (event.altKey && (event.key === "ArrowDown" || event.key === "ArrowUp"));
  if (isList(field) && (opens || ["ArrowDown", "ArrowUp", " ", "Enter"].includes(event.key))) {
    event.preventDefault();
    openList(field);
  } else if (isMoment(field) && opens) {
    event.preventDefault();
    openCalendar(field, true);
  }
}, true);

window.addEventListener("resize", () => closeChooser(false));
document.addEventListener("scroll", event => {
  if (chooser && !chooser.box.contains(event.target)) closeChooser(false);
}, true);
