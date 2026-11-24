# 0010: A Plex player controller

Date: 2026-11-24

Status: deferred; design accepted, build planned for the week of 2026-11-30

## Context

The owner uses Plexamp on several players and wants the device to control
playback on the local network, without Bluetooth.

## Decision

The design is accepted: one screen under the OS header with the title, artist,
album, progress and time, centred; a footer of the kit's actions - UP previous,
OK play or pause, DOWN next; long UP and DOWN seek by 10 seconds (one screen,
so long presses are the application's); local network only; sign-in through a
companion page; Plex internals hidden from the user; when the server is out of
reach the last known playback stays on screen, marked as such.

The build is deferred to the week after this decision.

## Open questions

- Which Plexamp platforms and versions must work.
- A player that supports only some controls: show what it supports, or treat
  it as unsupported.
- Update pace: 15 or 30 seconds by default.

## Evidence

The owner's decisions of 2026-11-23 and 2026-11-24; a merged design and a
rendered prototype of every state in both fonts and languages.

## Reopen when

The week of 2026-11-30; real sign-in, receiver compatibility and seeking are
verified against a real server before the build is accepted.
