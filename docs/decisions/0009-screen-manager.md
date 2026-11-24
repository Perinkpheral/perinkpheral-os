# 0009: A screen manager integrated with the scene stack

Date: 2026-11-24

Status: deferred until after version 1

## Context

Applications with several screens use `sdk/lib/app_nav` (version 1): a plain
struct of screens and views kept in the application's session, button routing
through `app_nav_button()`, and drawing by a `switch` on `nav.screen` in
`app_on_draw()`. Perinkpheral OS keeps its own navigation stack of scenes
(`firmware/components/ui/wm.c`): BACK steps down, a long BACK empties it,
BACK+UP/DOWN switches siblings, and sessions follow the scenes.

The two are separate: the application's screens and views are invisible to the
stack, each application writes the same dispatch, and screen state is restored
by every application from its own session.

## Decision

For version 1, `app_nav` with a `switch` in the application is sufficient. After
version 1, design a screen manager: screens and views declared by the
application (handlers per screen rather than one `switch`), and agreed with the
OS stack, so that one model covers scenes, screens and views - what BACK, long
BACK, sibling switching, notifications and session restoration do at each
level.

## Evidence

The owner's decision of 2026-11-24, after reviewing how an application writes
its screens with `app_nav`.

## Consequences

- No change before the release; `app_nav` stays the recommended helper and
  remains optional.
- The manager will need a design that keeps ABI 3 frozen: additive calls with
  `HOST_SINCE_` stamps, or a library-only change if the stack needs no new host
  support.

## Reopen when

Version 1 is released.
