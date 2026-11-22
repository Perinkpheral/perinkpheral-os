# Manual acceptance, 2026-11-22

## Candidate

| Item | Value |
|---|---|
| Firmware release | 0.1.82 |
| Firmware image ID | dd06f7ebeb7daa49d31489f4ba72e1e1aae484f1d4c99a707bf0c95d9787de7d |
| Catalog commit | 48f85d7 (catalog release 0.1.43) |
| Applications installed, with versions | air 0.1.10, calendar 0.4.20, clock 0.1.10, donut 0.1.3, ha 0.3.21, iss 0.1.14, keenetic 0.4.4, life 0.1.5, metals 0.1.5, pomodoro 0.1.5, rates 0.1.9, status 0.2.3, stocks 0.2.13, stopwatch 0.1.15, sun 0.1.11, timer 0.3.15, weather 0.2.18 |
| Font, language, power profile | Departure Mono, Russian, Auto |
| Date, who | 2026-11-22, the owner with Claude |

## 1. Update and identity

| # | Result |
|---|---|
| 1.1 | ok |
| 1.2 | ok - installed through recovery with the development tool; confirmed on the device; restarted into 0.1.82 |
| 1.3 | ok - firmware 0.1.82, image_id as above, on_probation false |
| 1.4 | ok |
| 1.5 | ok - every application already at the catalog's version (automatic updates); ISS and Life open |
| 1.6 | ok - GitHub activity is not installed |

## 2. Buttons and navigation

| # | Result |
|---|---|
| 2.1 | ok |
| 2.2 | ok |
| 2.3 | ok |
| 2.4 | ok |
| 2.5 | ok |
| 2.6 | ok |
| 2.7 | ok |
| 2.8 | ok |
| 2.9 | ok |
| 2.10 | ok |
| 2.11 | ok |
| 2.12 | ok |

## 3. Air quality and the redrawn icons

| # | Result |
|---|---|
| 3.1 | ok |
| 3.2 | ok |
| 3.3 | ok |
| 3.4 | ok |
| 3.5 | ok |
| 3.6 | re-check - the device still had the Cyrillic pack from before the Jersey polish (pack not updated; reinstalled 2026-11-22). Seen with the old pack: Jersey: the stem of Ы stands too far from its Ь; no caption touches a number or the rule |

## 4. Session and background delivery

| # | Result |
|---|---|
| 4.1 | ok |
| 4.2 | ok |
| 4.3 | ok |
| 4.4 | ok |
| 4.5 | ok |
| 4.6 | ok |

## 5. Every application, once

| Application | Result |
|---|---|
| air | ok |
| calendar | re-check - Jersey Ь in "воскресенье" (the device had the old Cyrillic pack) |
| clock | ok |
| donut | ok |
| ha | ok |
| iss | ok |
| keenetic | ok |
| kit | skip - not installed |
| life | ok |
| metals | ok |
| pomodoro | re-check - Jersey Б in "Работа" (the device had the old Cyrillic pack) |
| rates | ok |
| status | ok |
| stocks | ok |
| stopwatch | ok |
| sun | ok |
| timer | ok |
| weather | ok |

## 6. Device page

| # | Result |
|---|---|
| 6.1 | ok - the owner asks that installed applications sort to the bottom of the Catalog |
| 6.2 | ok |
| 6.3 | ok |
| 6.4 | ok |
| 6.5 | ok; the toggle must say the agreed "Automatic" ("Автоматически"), as on the device, strictly on one line - fixed on the branch |
| 6.6 | ok |
| 6.7 | ok |

## 7. Setup and recovery

| # | Result |
|---|---|
| 7.1 | ok |
| 7.2 | ok |
| 7.3 | ok - by Connect to Wi-Fi on the shelf device; phone and seconds not written down |
| 7.4 | ok |
| 7.5 | ok |
| 7.6 | ok |
| 7.7 | fail on its first step, the rest ok (menu, the eight open, Clock removed and installed again) - a fresh install from the flasher lands in recovery and needs BACK; the owner: "a user will never guess there is something more to do; it must be: press flash, wait, get a working screen" |

## 8. Alarms, notifications, power

| # | Result |
|---|---|
| 8.1 | ok |
| 8.2 | ok |
| 8.3 | ok |

## Findings and what was done

- 3.6, Jersey's Cyrillic: the device had kept the pack the owner tested on 2026-11-16 while the catalog's had been redrawn since; the owner rejected the redrawn letters. The catalog's pack goes back to the tested one, with Ы and ы (stem closer), ь (larger counter) and Jersey 25's б redrawn, accepted on the device on 2026-11-22 (firmware 0.1.83, catalog after it).
- 6.1: the Catalog lists installed applications last (0.1.83).
- 6.5: the time toggle says Automatic, on one line (0.1.83).
- 7.7: a fresh install started recovery and needed BACK; from 0.1.83 the flasher's fresh install starts main by itself. Tried on the board with 0.1.82's files and the new journal: the setup screen with no press, the image confirmed after 20 seconds. To be passed again through https://perinkpheral.dev/flash after the release.
- Seen twice after a USB session with esptool (a flash read, then the whole flash written back) and a reset over USB: the device ran, but its Wi-Fi did not start - no attempt in the router's log, no access point - until another reset; Forget network and joining again did not help. Not reproduced yet; to be investigated on a debug build after the release.
- Sun and Moon gained optional notifications before sunrise and sunset, at the owner's word (catalog).
