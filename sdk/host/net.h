/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * The network: answers to polls and requests as pieces or picked values,
 * polls run and paced, requests from code.
 */
#pragma once

#include "base.h"

/* One piece of the answer to a poll or to http_send(). Perinkpheral OS does
 * not hold a complete response in memory.
 *
 * request:     poll number in the manifest, or the tag of http_send().
 * data:        the piece, in the buffer returned by app_data_buffer().
 *              Valid until the handler returns.
 * flags:       HOST_DATA_FIRST on the first piece, HOST_DATA_LAST on the
 *              last one. A short answer is one piece with both flags.
 *              HOST_DATA_FAILED: a call without data; the request was
 *              not made or failed, possibly after earlier pieces. Only
 *              LAST makes an answer whole; FAILED discards all preceding
 *              pieces. Piece boundaries are unspecified, even within a
 *              character or a document marker.
 * http_status: HTTP status, delivered with the first piece. With
 *              HOST_DATA_FAILED: a HOST_FAILED_* code.
 *
 * Delivery can stop without HOST_DATA_LAST and without HOST_DATA_FAILED:
 * a receiving instance, on or off the screen, can be unloaded by a button
 * press, screen switch, application-list reload or delivery to another application.
 * Foreground unload calls app_on_blur() followed by app_on_terminate();
 * background unload calls app_on_terminate() only. Neither callback runs
 * after the instance has faulted. Saving the application's settings also
 * stops a poll stream; the instance may stay loaded, with no call to end
 * the stream. An automatic poll is requested again, subject to queued work and
 * server cooldowns; a manual poll needs another poll_run().
 * The next HOST_DATA_FIRST starts a new answer.
 * Perinkpheral OS repeats an interrupted automatic GET or HEAD poll after one
 * minute, or after its interval when that is shorter; other poll methods keep
 * their normal pace. An
 * interrupted answer to http_send() is dropped, and the application
 * asks again if it still needs it. Discard partially read data.
 *
 * A poll with "each" in the manifest is one request for every item of a
 * list setting: answer_item() tells which item the answer is for.
 *
 * Called on and off the screen. Off the screen the application is loaded
 * for the delivery and unloaded after it; a poll that cannot start at
 * all - no network, no memory - is told only to an application on the
 * screen. Save the result to the store.
 * Call screen_request() to request the screen. */
#define HOST_DATA_FIRST     1
#define HOST_DATA_LAST      2
#define HOST_DATA_FAILED    4
#define HOST_DATA_CHUNK_MAX 1024
/* Failure codes, delivered in place of the HTTP status: in app_on_data()
 * with HOST_DATA_FAILED, in app_on_picked(), and in app_on_document(). */
#define HOST_FAILED_NO_NETWORK  (-1) /* no network connection */
#define HOST_FAILED_NO_ANSWER   (-2) /* no answer from the server, or an incomplete answer */
#define HOST_FAILED_NOT_READY   (-3) /* preparation failed: missing/invalid template input
                                        or expansion limit, or missing document buffer;
                                        stored empty values are valid */
#define HOST_FAILED_NOT_ALLOWED (-4) /* destination outside the current manifest "hosts" list */
#define HOST_FAILED_TIMEOUT     (-5) /* timeout after the server was reached */
#define HOST_FAILED_NOT_TRUSTED (-6) /* HTTPS certificate not trusted: self-signed, expired,
                                        or issued for another name */
#define HOST_FAILED_TOO_LARGE   (-7) /* answer larger than HOST_ANSWER_MAX, or larger than
                                        HOST_ANSWER_PIECES_MAX when streamed, or the
                                        registered document-buffer capacity */
#define HOST_FAILED_NO_MEMORY   (-8) /* insufficient device memory for the request */
/* Maximum answer size: with value extraction ("pick"), and streamed. */
#define HOST_ANSWER_MAX        (256 * 1024)
#define HOST_ANSWER_PIECES_MAX (4 * 1024 * 1024)
APP_EXPORT(app_on_data, void app_on_data(int32_t request, const uint8_t *data, int32_t length,
                                         int32_t flags, int32_t http_status));

/* A poll with nonempty "pick" selects JSON and app_on_picked(), regardless
 * of Content-Type. Explicit raw/XML formats cannot be combined with pick.
 * "pick" contains JSON paths such as "current.temperature_2m" or
 * "daily.weather_code[0]". The application receives one call instead of pieces.
 * The complete body must be one valid JSON value: matching containers (up to
 * 64 levels), required separators, valid literals and escapes, and strings
 * without unescaped control characters or invalid UTF-8. A 2xx response with
 * an invalid body gives HOST_FAILED_NO_ANSWER. Other HTTP statuses, including
 * 401, 404 and 503, are preserved; an invalid body supplies no values.
 * Applications must still validate the schema and expected scalar types.
 * HEAD supplies no values and preserves its HTTP status.
 *
 * request:     poll number in the manifest.
 * http_status: HTTP status, or a HOST_FAILED_* code.
 *
 * answer_value() is valid inside the handler. It copies the value of path
 * number `index` as text without a terminator and returns its length.
 * Numbers, true, false and null are copied as written; strings are copied
 * without quotes. Escaped quotes, backslashes and slashes are decoded.
 * Unicode escapes become UTF-8; surrogate escapes combine only when adjacent in the same string,
 * including across input chunks; unpaired surrogates are discarded.
 * Short control escapes become spaces. Extracted strings are
 * NUL-terminated internally, so an embedded NUL ends the exposed value.
 * Only ASCII property names are supported reliably; non-ASCII key
 * characters become "?" and can match a path containing "?". Property names
 * containing path delimiters, NUL or control characters are unaddressable;
 * they cannot impersonate nested properties or array indices.
 * A value is at most HOST_VALUE_MAX bytes; a longer one
 * is cut between characters.
 * Returns HOST_ERR_NOT_FOUND if the answer does not contain the path,
 * HOST_ERR_NO_SPACE if the buffer is too small.
 *
 * A list path has "[*]" in place of one index - "close[*]",
 * "items[*].title" - and gives matching scalars in source order.
 * answer_count() returns the number of elements, answer_element() copies one
 * the way answer_value() copies a value; answer_value() has none for it. Any
 * other path has one element, its value, or none. A null scalar is kept
 * as "null". Missing members and non-scalar values are skipped. Overlapping
 * paths give a scalar to the first matching path only. Parallel lists cannot
 * safely be zipped unless the source guarantees corresponding scalars for
 * every record and the paths do not overlap. The elements of
 * one answer take at most HOST_LIST_BYTES as text, each its length plus
 * 2 bytes; elements beyond that are dropped and the log says so.
 * sdk/lib/app_values.h converts values to numbers and text. */
#define HOST_VALUE_MAX  63
#define HOST_LIST_BYTES 8192
APP_EXPORT(app_on_picked, void app_on_picked(int32_t request, int32_t http_status));
HOST_IMPORT(answer_value, int32_t answer_value(int32_t index, char *text, int32_t capacity));
HOST_IMPORT(answer_count, int32_t answer_count(int32_t index));
HOST_IMPORT(answer_element,
            int32_t answer_element(int32_t index, int32_t element, char *text, int32_t capacity));

/* The item of the list setting the answer being delivered is for: its
 * number in the list, from 0, for a poll with "each"; -1 for any other
 * answer. Valid inside app_on_data(), app_on_picked(), and app_on_document(),
 * for the current answer. */
HOST_IMPORT(answer_item, int32_t answer_item(void));

/* Retry-After accepts a complete unsigned decimal integer of seconds or an
 * IMF-fixdate (for example "Fri, 27 Nov 2026 12:00:00 GMT"), with optional
 * surrounding spaces/tabs. Dates use one captured wall clock per response.
 * An unknown clock, expired date, absent or invalid value yields 0; positive
 * overflow saturates at INT32_MAX. Decimal seconds do not require a clock.
 * Perinkpheral OS defers all of the application's manifest polls by a positive
 * delay, or 60 seconds for HTTP 429 without one, and ends active list rounds.
 * Cooldowns use uptime and are unaffected by later wall-clock changes.
 * http_send responses never defer manifest polls or other raw requests;
 * applications implement their own raw-request cooldowns from this metadata.
 *
 * Response metadata. Future fields may be appended beyond its existing size
 * with alignment and the existing field types, offsets and sizes preserved.
 * Valid inside app_on_data(), app_on_picked(), and app_on_document(),
 * for the current answer. The same for every piece of one answer.
 * A header that the server did not send is an empty string. A header
 * longer than its field is cut. A terminal HOST_DATA_FAILED callback may
 * have no metadata: status is the failure code, length is -1 and header
 * strings are empty. Picked 2xx responses that fail the checks described above
 * are reported as HOST_FAILED_NO_ANSWER even when an HTTP response arrived;
 * non-2xx responses preserve their HTTP status. */
typedef struct host_answer_info {
    int32_t status;            /* HTTP status, or a HOST_FAILED_* code */
    int32_t length;            /* Content-Length; -1 if absent or outside 0..INT32_MAX */
    int32_t retry_after;       /* normalized Retry-After delay in seconds; see above */
    char    content_type[48];  /* "application/json; charset=utf-8" */
    char    etag[64];          /* for If-None-Match */
    char    last_modified[32]; /* for If-Modified-Since */
    char    location[128];     /* target of a redirect that was not followed */
    char    link[192];         /* Link header: address of the next page */
} host_answer_info_t;
HOST_LAYOUT(host_answer_info_t, 476);
/* Copies the first min(capacity, sizeof(host_answer_info_t)) bytes of the
 * Perinkpheral OS's struct, including zeroed absent fields. Returns bytes written,
 * HOST_ERR_ARGUMENT for a negative capacity or invalid destination, or
 * HOST_ERR_NOT_FOUND outside app_on_data/app_on_picked/app_on_document. Bytes beyond Perinkpheral OS's
 * struct are untouched; zero the destination first when accepting a
 * shorter struct. Capacity 0 writes nothing and returns 0 in a handler. */
HOST_IMPORT(answer_info, int32_t answer_info(host_answer_info_t *response, int32_t capacity));

/* Copies occurrence (from 0) of a response header, without a terminating NUL.
 * Valid inside app_on_data(), app_on_picked(), and app_on_document(),
 * for the current answer. Names are case-insensitive HTTP tokens of 1..63
 * bytes. Repeated fields, including
 * Set-Cookie, are separate values in arrival order, never comma-joined.
 * Returns the value length, HOST_ERR_NOT_FOUND if absent/outside a handler,
 * HOST_ERR_ARGUMENT for invalid arguments, or HOST_ERR_NO_SPACE if the whole
 * value does not fit. On error the output is unchanged; capacity 0 accepts
 * an empty value. Perinkpheral OS keeps headers in 1024 bytes, including names
 * and two NUL separators per field. If the complete set does not fit, every
 * answer_header call for that response returns HOST_ERR_NO_SPACE, including
 * fields received before overflow. answer_info is independent of this limit.
 * Only the final response after redirects is exposed; no cookies are stored
 * or sent automatically. Save required session values with kv_set only when
 * they change: background instances are unloaded between answers. Bind cached
 * credentials to their server/account and clear them when settings change.
 * app-check mocks supply response headers as string values or arrays of
 * repeated values in "headers". Its stand-in uses the firmware storage and
 * fixed metadata parser; no transport decoding or automatic headers are
 * simulated (sdk/check/README.md). */
HOST_IMPORT(answer_header,
            int32_t answer_header(const char *name, int32_t name_length, int32_t occurrence,
                                  char *value, int32_t capacity));

/* Returns the buffer for one piece of data: HOST_DATA_CHUNK_MAX bytes of
 * application memory. Provided by sdk/lib; applications do not define it. */
APP_EXPORT(app_data_buffer, uint8_t *app_data_buffer(void));

/* Packaged trust files contain exactly one complete CA certificate each, in
 * PEM or DER. PEM allows only ASCII spaces, tabs and line breaks outside its
 * CERTIFICATE block; DER allows no extra bytes. Additional or partial
 * certificates and other material are invalid. Companion URLs use HTTPS with
 * an ASCII DNS host or IPv4 address and an optional decimal port 1..65535,
 * without userinfo or backslashes. */

/* ---- Network ---- */

/* Manifest JSON, including unknown metadata, permits at most 16 nested containers,
 * signed 64-bit integer parts and no exponent notation. Typed optional fields are
 * omitted when absent; present nulls are invalid. Integer fields, including
 * "every", require whole values; zero-only decimal fractions are whole.
 * A textual interval is 1..6 decimal digits, optional ASCII spaces, then an
 * optional unit: s/sec/second/seconds, m/min/minute/minutes, h/hour/hours or
 * d/day/days. The entire text must match. A number alone is seconds; poll
 * intervals must be within MANIFEST_POLL_EVERY_MIN_S..MANIFEST_POLL_EVERY_MAX_S.
 *
 * Manifest polls accept "method" (the HOST_HTTP_* names, GET by default)
 * and "body" (at most 1024 bytes before and after expansion). A body is
 * refused with GET or HEAD. {{key}} values in a body are stored text,
 * including line feeds; a carriage return fails preparation. The developer
 * supplies escaping and Content-Type in headers. Headers and body expand
 * together immediately before sending; "each" substitutes its item in both.
 * "pick" works with every method; HEAD has no values. Only GET follows redirects. */

/* A poll with "manual": true starts only through poll_run(), never at startup,
 * on settings changes or on a schedule. "every" is optional and, when supplied,
 * validated but unused. A requested round retains transport-failure retries on
 * reconnection, queueing, pick/each and server cooldowns. A healthy or never
 * requested manual poll is not started on reconnection. */

/* Network availability is reported in the answer: a request made without
 * a network fails with HOST_FAILED_NO_NETWORK. The radio can be off
 * between requests. */

/* Requests poll number `poll` through the shared queue without waiting for
 * a response. Repeated pending requests are coalesced. Perinkpheral OS performs
 * one request at a time. Calls within a second of the last accepted
 * poll_run() are delayed until that second has elapsed; this does not
 * guarantee a one-second gap between request starts. Active server
 * Retry-After delays also defer manifest polls requested by this call. */
HOST_IMPORT(poll_run, int32_t poll_run(int32_t poll));

/* Queues a request from code.
 *
 * Destination: only hosts listed under "hosts" in the manifest. Host names
 *              are compared case-insensitively. A literal entry without a
 *              port permits ports 80 and 443; an explicit port must match.
 *              A url/link setting entry requires its configured host and
 *              effective port. URLs must use http:// or https:// without
 *              user information before the host.
 * Queue:       Perinkpheral OS performs one request at a time for the whole
 *              device. An application has at most HOST_HTTP_PENDING_MAX
 *              requests waiting; with more to ask, it queues the next
 *              ones as answers arrive. The applications take turns, and
 *              queued requests alternate with scheduled polls. A queued
 *              request can fail before transmission. Completion time depends
 *              on network availability, queueing and server response time.
 * Answer:      delivered through app_on_data() in pieces, with `tag` as
 *              the request number, on or off the screen. The instance
 *              that receives the answer may not be the one that made the
 *              request: one that left the screen meanwhile is loaded anew.
 * tag:         a number chosen by the application, HOST_REQUEST_FIRST to
 *              HOST_REQUEST_LAST, distinct from poll numbers:
 *
 *                  enum { REQUEST_TOKEN = HOST_REQUEST_FIRST, REQUEST_EVENTS };
 *
 *              Tags span 16..INT32_MAX. For a changing generation, test the
 *              bound before adding: tag = tag < HOST_REQUEST_LAST ? tag + 1
 *              : HOST_REQUEST_FIRST. Avoid reusing tags while old answers
 *              could still arrive. No global uniqueness is guaranteed.
 *
 * headers:     lines "Name: value" separated by '\n', at most four. The
 *              Perinkpheral OS sends "User-Agent: perinkpheral"; a User-Agent header
 *              given here replaces it. The same for polls. Each name is
 *              at most 31 bytes and each raw value at most 255 bytes.
 *              URL and header input must be printable ASCII. Headers use
 *              LF only; CRLF returns HOST_ERR_ARGUMENT. The body is a byte
 *              buffer and is not subject to this text restriction. GET and
 *              HEAD require body_length == 0; nonempty bodies return
 *              HOST_ERR_ARGUMENT before queuing.
 * {{key}}      reads the application's KV store, including keys not
 *              declared as settings. An absent declared setting uses its
 *              manifest default. URL values are percent encoded (except
 *              a leading url/link setting); list values use commas.
 *              URL substitution and the initial host check happen before
 *              queuing: an unset URL value returns HOST_ERR_ARGUMENT with
 *              no callback. The expanded destination stays queued. At send
 *              time it is authorized again and headers are expanded from
 *              the same current KV snapshot. After a settings change a
 *              queued request whose destination is no longer allowed fails
 *              with HOST_FAILED_NOT_ALLOWED; it is never silently retargeted.
 *              Missing/invalid header values give HOST_FAILED_NOT_READY.
 *              Once prepared, a request keeps that snapshot even if settings
 *              change during transmission. Body bytes are copied at queue time.
 *              Expanded URLs fit in 511 bytes; expanded header values fit
 *              in 255 bytes. Expansion failure gives HOST_FAILED_NOT_READY,
 *              except for the initial URL check.
 *
 * Redirects: a GET is followed up to three times; the application
 * receives the last answer only. Redirects to the same name with the same
 * port, or with both ports in {80, 443}, bypass the allowlist check.
 * Other destinations must be in "hosts" or fail with HOST_FAILED_NOT_ALLOWED.
 * Application headers are dropped for all remaining hops on a different host
 * or a transition from HTTPS to HTTP, even after an earlier upgrade. A request
 * with any other method is sent once; its 3xx status is delivered as the
 * answer. GET polls follow redirects to any host; polls of another method
 * are sent once too.
 *
 * A JSON answer can be parsed with sdk/lib/json_pick.h: feed every piece,
 * read the values after the last one.
 *
 * Returns HOST_OK; HOST_ERR_ARGUMENT for a URL outside the manifest hosts
 * or malformed arguments; HOST_ERR_NO_SPACE if HOST_HTTP_PENDING_MAX of the
 * application's requests are waiting, or the device has no memory for
 * another. All queued requests share a 16 KB budget, including queue
 * records, URLs, headers and bodies. The per-application ceiling reserves
 * no slots; HOST_ERR_NO_SPACE can occur before sixteen requests wait. */
#define HOST_HTTP_GET    0
#define HOST_HTTP_POST   1
#define HOST_HTTP_PUT    2
#define HOST_HTTP_DELETE 3
#define HOST_HTTP_PATCH    4
#define HOST_HTTP_HEAD     5 /* the answer is one piece without data; use answer_info() */
#define HOST_HTTP_PROPFIND 6 /* CalDAV */
#define HOST_HTTP_REPORT   7 /* CalDAV */
#define HOST_HTTP_METHOD_LAST HOST_HTTP_REPORT
#define HOST_HTTP_HEADERS_MAX 1023
#define HOST_HTTP_BODY_MAX    512
#define HOST_HTTP_PENDING_MAX 16
#define HOST_REQUEST_FIRST    16
#define HOST_REQUEST_LAST     INT32_MAX
HOST_IMPORT(http_send,
            int32_t http_send(int32_t tag, int32_t method, const char *url, int32_t url_length,
                                 const char *headers, int32_t headers_length,
                                 const void *body, int32_t body_length));

/* Overrides the interval of poll number `poll`, in seconds, kept across
 * restarts and updates. Positive values must be 10..86400; anything else
 * except 0 returns HOST_ERR_ARGUMENT, as does an unknown or manual poll. 0 forgets
 * the stored code override and resumes the manifest's interval or its
 * interval setting. A code override takes precedence over that setting.
 * A changed resolved interval, before the power-saving floor, restarts the
 * deadline from now; setting the same resolved interval does not. Changing
 * 10 s to 20 s resets it even when both run at the 300 s floor. Returns
 * HOST_OK, or HOST_ERR_FAILED if the override could not be kept or forgotten. */
HOST_IMPORT(poll_set_interval, int32_t poll_set_interval(int32_t poll, int32_t seconds));

/* Response representation. Nonempty pick selects JSON. Otherwise, without
 * a manifest format the final media type selects JSON or XML; unknown and
 * absent types stay raw. */
#define HOST_FORMAT_RAW  0
#define HOST_FORMAT_JSON 1
#define HOST_FORMAT_XML  2
#define HOST_DOCUMENT_MAX 12288

/* Register application-owned storage for structured responses, normally in
 * app_on_load. Returns HOST_OK, or HOST_ERR_ARGUMENT for an invalid range,
 * capacity, overlap, answer callback, or active document collection. A failed
 * registration leaves the previous registration unchanged. Successful
 * registration replaces the previous buffer, which may then be reused.
 * Keep the registered storage valid until replacement or instance unload;
 * registration is lost at unload and must be restored in app_on_load.
 * The buffer must be
 * disjoint from app_data_buffer; capacity is 1..HOST_DOCUMENT_MAX.
 * While receiving, leave this buffer untouched until app_on_document.
 * Registration is refused inside an answer callback or while a structured
 * document is being collected. JSON supports 64 nested containers.
 * Structured XML uses the host/xml.h subset, with up to 64 attributes per
 * element. Duplicate attribute names within an element, or unknown entities
 * anywhere, invalidate the document. */
HOST_IMPORT(document_buffer_set, int32_t document_buffer_set(uint8_t *buffer, int32_t capacity));

/* Opt-in notification for recognized formats, normally after the final raw piece.
 * When no app_on_data handler consumes the response, the result may arrive
 * before the final piece. A non-2xx HTTP status is delivered after the headers
 * without reading the body. Buffer capacity errors and a missing document
 * buffer (HOST_FAILED_NOT_READY) are detected when a body piece reaches the
 * document reader; that piece may already be the final one. A transport failure
 * before that delivery can be reported instead. After an early result, the
 * remaining body is not read and no second document result is delivered.
 * A live raw-piece consumer still receives the stream through its final piece,
 * followed by the document result.
 * A successful 2xx body is complete and validated, in the registered buffer,
 * with the given length (no NUL). HEAD, 204 and 205 deliver the HTTP status
 * with zero bytes. It remains usable until the next answer
 * or unload, or until a successful buffer replacement. answer_item(),
 * answer_info() and answer_header() describe this response inside the callback,
 * including for a document-only consumer. The callback status includes document
 * validation/buffer errors. answer_info().status retains the transport status:
 * for example, malformed JSON can give callback HOST_FAILED_NO_ANSWER with
 * metadata status 200. Transport failures use HOST_FAILED_* in both places;
 * terminal failures may lack headers. Non-2xx statuses and validation, size or transport errors have
 * zero length. An interrupted instance has no later completion. Raw MIME
 * and JSON pick polls continue through their own handlers. */
APP_EXPORT(app_on_document, void app_on_document(int32_t request, int32_t format,
                                                 int32_t status, int32_t length));
