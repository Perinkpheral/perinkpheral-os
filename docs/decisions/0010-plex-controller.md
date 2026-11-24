# 0010: A Plex player controller

Date 2026-11-24

Status accepted. Implementation started on 2026-11-24.

## Context

The owner wants to see playback and control supported players from the device
over the local network. Initial macOS targets are Plex for Mac 1.115.0 and
Plexamp 4.13.2. The Plex Media Server can be on the local network or reachable
at a user-supplied HTTPS address.

## Decision

Now Playing shows title, artist, album, progress and time on one screen under
the OS header. If the server becomes unavailable, the screen marks the last
known playback as stale.

The browser companion supports Plex sign-in and discovery. A user can preserve
a custom HTTPS server address through sign-in. Manual setup accepts a server
address and its scoped access key independently of cloud sign-in. The device
stores credentials for the configured endpoints. Proxy cookies stay in the
browser.

The screen shows only controls offered by a verified player. UP selects the
previous track, OK toggles play and pause, and DOWN selects the next track.
Long UP and DOWN seek by 10 seconds when the player's timeline supports
seeking. The device checks the player, server, media and current timeline
before sending a command. Plex for Mac is a metadata target until its control
capabilities are qualified. Plexamp is the first direct-control target.

Long OK opens the player picker. UP and DOWN highlight a player, OK selects it
and BACK cancels. The device remembers the choice and verifies the new
receiver before enabling its controls. Selecting a player does not transfer
playback or issue a playback command.

The original plan deferred the build to the week of November 30. The owner
approved immediate implementation on November 24.

## Qualification

The prototype uses both device fonts and languages. The Mac Plexamp timeline
reports play and pause, previous and next, and seeking on the tested music
session. A direct host test confirmed a playback transition and restored the
previous state and offset. Plex for Mac's direct timeline returned HTTP 404
in that test. The production application still needs qualification on the
device, including seeking, player selection, credential handling and the
interpreter arena.
