/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 *
 * What an application keeps: the key-value store and its files.
 */
#pragma once

#include "base.h"

/* ---- Store ---- */

/* Key-value store for small persistent values. Each application has a
 * private store. Writing wears the flash memory: write when data has
 * changed, for example in app_on_terminate(), not on every timer tick.
 * Removal normally keeps the store for reinstall. Perinkpheral OS may reclaim
 * removed applications' stores when the partition is full. Removal with
 * forget erases the store immediately. */
#define HOST_KV_KEY_MAX   15  /* 1..15 characters from '!' to '~' */
#define HOST_KV_VALUE_MAX 1024
#define HOST_KV_TOTAL_MAX 2048 /* all values of one application, its settings among them, as
                                  stored: 64 bytes for a value plus its length rounded up to a multiple of 32 bytes;
                                  guaranteed to every installed application */

/* Reads a value into `value`. Returns its length. A setting of the manifest
 * with nothing stored reads as its "default", a list's items with line
 * breaks between them. Omit a manifest default when absent; explicit null is
 * invalid. Choice options are nonempty printable ASCII values; translated
 * labels may use supported Unicode characters. URL/link defaults need HTTP(S),
 * a nonempty host, no userinfo and an optional decimal port 1..65535. URL defaults have no path/query/fragment;
 * link defaults may include them and contain no spaces. Companion pages receive
 * effective non-secret, non-link values too: defaults for absent settings,
 * preserved stored empty values, and list defaults joined with line feeds. */
HOST_IMPORT(kv_get, int32_t kv_get(const char *key, int32_t key_length, void *value,
                                   int32_t capacity));

/* Writes a value. Length 0 deletes the key. Keys declared as "url" or
 * "link" in the manifest belong to the user: kv_set() returns
 * HOST_ERR_ARGUMENT for them, including deletion. Other keys need not be
 * declared in the manifest. kv_get() copies bytes without a terminator;
 * a missing key returns HOST_ERR_NOT_FOUND; a storage read failure returns
 * HOST_ERR_FAILED. Insufficient capacity returns
 * HOST_ERR_NO_SPACE without copying; capacity 0 can read an empty value. */
HOST_IMPORT(kv_set, int32_t kv_set(const char *key, int32_t key_length, const void *value,
                                   int32_t length));

/* ---- Files ---- */

/* Files hold data that does not fit in a store value: a picture, a table,
 * a cache. An application can read the files of its package (the `files`
 * folder at build time) and, with "writes_files": true in its manifest,
 * the files it has written.
 * Limits, counted as stored on the device, every file in whole 4 KB
 * blocks: HOST_FILES_MAX for the files of a package; HOST_FILES_WRITER_MAX
 * for all files of an application that writes, its package's included.
 * The room of an application that writes is guaranteed: the device keeps
 * it free for it from the moment it is installed.
 * Writing wears the flash memory, like kv_set().
 * File name: 1-23 characters of a-z, 0-9, '.', '-' and '_'; the first
 * character is not a dot. Names ending in .new are reserved.
 * file_write() also refuses app.wasm, every name beginning "manifest.",
 * and files listed in the manifest's "certificates" array.
 * A file supplied by an update replaces an existing file of the same
 * name, including one written by the application. Generated strings.xx and
 * manifest.xx language tables absent from a replacement package are removed;
 * other files are retained. An application without writes_files may therefore
 * retain more than HOST_FILES_MAX across updates. Builder, installation, update
 * and runtime count the same data files: translations and certificates count; app.wasm and
 * manifest.json do not. Manifest storage is admitted separately. An incomplete
 * quota census fails with HOST_ERR_FAILED. */
#define HOST_FILE_NAME_MAX    23
#define HOST_FILES_MAX        (64 * 1024)
#define HOST_FILES_WRITER_MAX (32 * 1024)

/* Returns the file size, HOST_ERR_NOT_FOUND only for absence, or
 * HOST_ERR_FAILED for a storage failure. */
HOST_IMPORT(file_size, int32_t file_size(const char *name, int32_t name_length));

/* Reads up to `capacity` bytes starting at `offset`. Returns the number
 * of bytes read: fewer at a clean end of file, 0 past it. Absence returns
 * HOST_ERR_NOT_FOUND. Seek, read and close failures return HOST_ERR_FAILED,
 * including after a partial copy; the destination may then have changed. */
HOST_IMPORT(file_read, int32_t file_read(const char *name, int32_t name_length, int32_t offset,
                                         void *bytes, int32_t capacity));

/* Writes `length` bytes at `offset`. The offset must be inside the file
 * or at its end: a file is extended piece by piece, and existing data can
 * be overwritten. Length 0 at offset 0 deletes the file.
 * Only for an application whose manifest says "writes_files": true; the
 * build and Perinkpheral OS refuse a module that imports this without it.
 * Returns HOST_ERR_NO_SPACE if the files of the application would exceed
 * HOST_FILES_WRITER_MAX, or HOST_ERR_FAILED when storage or quota accounting
 * fails. A failed write may already have changed part or all of the file. */
HOST_IMPORT(file_write, int32_t file_write(const char *name, int32_t name_length, int32_t offset,
                                           const void *bytes, int32_t length));
