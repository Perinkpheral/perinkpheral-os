# Threats

Assets, threats, implemented controls and accepted limitations.
This assessment is based on code review, not adversarial testing.
`CLAUDE.md` defines the project scope; accepted tradeoffs are recorded below.

## Assets

1. **Application credentials**: the Home Assistant integration's screen secret,
   private calendar URLs and other application tokens. Stored unencrypted in `appkv`.
2. **The Wi-Fi password.** In the `nvs` partition, in the clear.
3. **The device itself**: what runs on it, what it shows, whether it still answers.
4. **A foothold in the home network**: the device makes requests from inside it.
5. **What the screen shows**: a calendar, who is at home, what plays.

## Threat actors and capabilities

### An application the user installed

Applications are untrusted. The interpreter and Perinkpheral OS API enforce their isolation.

Controls:

- It runs in the interpreter with memory of its own, at most 64 KB. Every pointer, length and coordinate
  it passes is checked before use (`firmware/components/appsys/app.c`); it cannot read or write Perinkpheral OS's
  memory. Handlers have a cooperative 3000 ms execution budget, checked at interpreter
  yield points, after native imports and on return. Native calls and scheduling delays
  can extend elapsed time.
- Its store is its own (`a.<name>`; a name has no dot), its files are its own folder, and
  a file's name cannot leave it. It cannot reach another application's secrets.
- It has no sockets. Polls start at their manifest URLs; requests made with `http_send`
  are confined to `hosts` at dispatch. GET redirects to the same name with the same port,
  or with both ports in {80, 443}, bypass the allowlist check. These redirects retain
  application headers unless they downgrade from HTTPS to HTTP.
  Other raw-request redirect destinations must be in `hosts`; application headers are dropped there.
  GET polls follow redirects even to other hosts. Other methods deliver the redirect
  response without replaying the request. Host names are compared without case sensitivity.
  URLs containing user information are rejected by host authorization and same-host checks.
- It cannot move the server the user gave it: `kv_set` on a `url` or `link` setting is refused. Queued raw-request destinations are
  authorized again before sending; headers use the same current settings view. Manifest
  polls expand their destinations at dispatch and do not use the `hosts` allowlist.
- The system API - reset, forgetting the network, removing applications - needs
  `"system": true`, which is asked about on the page and on the device's own screen.

Limitations:

- **It can send its own secrets to any host its manifest names, and nobody is shown that
  list before installation.** It reads the token with `kv_get`; `hosts` is its own word; the page says
  nothing about `hosts` before installing, and the catalog's index does not carry them
  (once it is installed, the Info button on its tile does).
  For example, an application can request a token and list both `{{server}}` and
  another host in its manifest. Installation does not warn about that destination.
- It can ask for the screen every 30 seconds, write lines into the log that look like
  Perinkpheral OS's, and wear the flash with writes to its store and files. These operations
  permit interface disruption, misleading log output and flash wear.
- Manifest-authorized destinations may include private-network HTTP services.
  Installation does not display these destinations.

### A catalog, or whoever controls one

Catalog source URLs must use HTTPS. The installer checks each downloaded file against
the size and SHA-256 supplied in the installation request. The browser obtains these
values from the catalog index; automatic updates read the index in firmware. These
checks do not authenticate the publisher. The official source cannot be removed.

Limitations:

- There are no signatures. Whoever can write to a source - its owner, or somebody who took
  the account over - serves whatever they like, and an Update is one click. The official
  source is a GitHub repository: its safety is that account's.
- An update is not shown to differ from what was installed: new hosts, a new request for
  secrets, nothing is pointed out. Only `"system": true` and a certificate authority of its
  own are asked about.

When automatic application updates are enabled, compatible newer versions may be
installed daily without another click. Automatic updates restrict changes to
destination declarations, system access, file-writing privileges and certificate
authorities. They do not authenticate replacement application code.

### Local network peers

Threat actors include network guests, compromised local devices and malicious browser content.

Controls:

- What cannot be taken back is asked about on the device's own screen: a firmware update,
  a factory reset, a change of the Wi-Fi network, installing an application with system
  access. In development builds, injected button events can answer main-firmware
  questions remotely; this boundary requires a release build. Recovery always requires
  physical OK and has no development input route.
- A page of another site cannot use the user's browser against the device, for reading or
  for changing: `Origin` and `Host` are checked, which covers a name rebound to the
  device's address too.
- The page never reads stored secrets or custom links back. Hidden settings are
  a form rule; their values remain available through the API unless secret or link.

Limitations:

- **There is no password on the page.** Anybody who can reach port 80 may install and
  remove applications, add a catalog, change every setting, restart the device or enter
  recovery, read the log and exposed settings values, excluding secrets and stored
  custom links - with a command line; the checks above are against browsers. The owner
  chose this scope on 2026-11-12.
- Entering recovery does not require physical confirmation and interrupts normal
  applications until main is started again. Replacing main still requires a new
  physical OK after recovery has shown the image's version. Main refuses direct
  firmware uploads; recovery cannot write its own image, the bootloader or the
  partition table through its HTTP API. Firmware uploads are not authenticated by a
  publisher signature. Recovery checks image compatibility and integrity, then requires
  physical OK. The displayed version is supplied by the uploaded image and does not
  establish its origin.
- **They can change application credentials and servers.** The settings API does not
  require confirmation. A network peer can disrupt pairing or redirect later requests.
  Changing a url/link destination whose previous stored or default value is nonempty
  first deletes all declared secrets, including supplied replacements. Destinations are
  then written before the other supplied values, including replacement secrets. Writes
  are not transactional: a failed save can leave a supplied secret deleted.
  HA Companion's hidden fields are normally provisioned by its integration.
  A peer can also add a catalog and install an application.
- **They see the screen.** `/api/screen` gives the frame the panel shows, whatever an
  application puts there (events, Home Assistant values), in every build.
- **The page is plain `http`.** A token typed into it crosses the network in the clear.
- In a build with the development tools (`APP_DEV_CONSOLE`, on by default): `/api/dev/flash`
  refuses direct updates (recovery requires physical OK), `/api/dev/kv` writes any key of any
  application, and `/api/dev/button` can answer
  main-firmware confirmation questions. Such a build is for a bench; releases omit it.

### Nearby radio attackers

- With no saved network and Wi-Fi enabled, or after two minutes of failing to join a
  saved network, it puts up an **open** access point. Deliberately disabled Wi-Fi starts
  no access point. Whoever is in range is then "somebody on the same network",
  with everything above. Sustained connection failure can expose the rescue access point
  after two minutes of active connection attempts. Power saving can stop the radio and
  postpone this transition.
  From there, as from any network, what cannot be undone - another network, a firmware,
  a reset - is asked about on the screen; the rest is not.
- Recovery always exposes the open `perink-recovery` access point at `192.168.4.1`,
  including while it is connected to the saved Wi-Fi. Its HTTP surface is limited
  to the upload page, status and firmware upload. It applies its own Host/Origin
  checks and physical confirmation; it has no settings, application or development
  API. It reads saved settings and glyph packs without modifying them.

### Physical access

- Flash encryption and secure boot are not enabled. USB access allows reading stored
  credentials and replacing firmware. The buttons provide access to Settings.
  Physical access is outside the project's accepted protection scope.

### A server the device talks to

Controls: HTTPS is verified against ESP-IDF's root certificate bundle. Picked JSON
responses are parsed incrementally and limited to 256 KiB. Streamed responses are
limited to 4 MiB. Optional JSON/XML document delivery uses an application-owned
buffer of at most 12,288 bytes. Reception stops early when no consumer needs the
remaining body. Text that is not ASCII is drawn from glyph packs, spelled in ASCII
or shown as `?`; it is not executed.

Limitations: plain `http` is allowed, so a token sent over it crosses the network in the
clear. Packaged authority roots are supported for that application only; a setting for
a user-supplied server certificate is not implemented.

The connection-cut timer does not cover TLS setup because ESP-TLS exposes the socket
only after the handshake finishes. A slow handshake can therefore block the poller
and keep the radio active beyond the response deadlines.

### A companion page

A page of the application's author, opened from the device's page. It is given the
application's setting declarations and values except secrets and links, and hands back values the user
then saves. Ordinary hidden settings have no form inputs. Latitude and longitude
settings referenced by a place field have hidden inputs and can be filled by
companion messages. A malicious companion page can propose an attacker-controlled
server; credentials subsequently saved for that destination may be disclosed.

## Accepted protection scope

The accepted scope, set on 2026-11-13, trusts the local network and physical access.
It does not defend against radio jamming or a malicious network peer. The limitations
above describe that scope; they are not a backlog of promised protections.

The sandbox addresses **untrusted applications from third-party catalogs**:

1. Application information is available through Info on an installed application's
   tile. It describes destinations, polling and storage. This information is not
   shown before installation.
2. **Home Assistant** uses an integration-issued secret for a device screen, rather than
   the user's general access token. The integration is in the separate
   `perinkpheral-hacs` repository; this repository does not test an HA server.

Accepted and not to be built: a password or a question for settings and catalogs, a
password on the access point, a secret an application cannot read, HTTPS on the page,
signatures, encrypted flash. The development tools stay in the build while the project is
being developed; a release is built without them.

## Excluded protections

The project provides no backend, accounts or telemetry. Its protection scope trusts
local-network peers and physical access. Application signatures are not implemented.
The device page permits reversible operations without authentication. The builder is
responsible for battery and device safety.
