/* SPDX-License-Identifier: MIT */
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "tinytest.h"

typedef enum { HEALTHY, OPEN_DIR, READ_DIR, CLOSE_DIR, FILE_STAT, SEEK_FILE, READ_FILE, CLOSE_FILE } fault_t;
static fault_t fault;
static DIR *fault_opendir(const char *path)
{
    if (fault == OPEN_DIR) { errno = EIO; return NULL; }
    return opendir(path);
}
static struct dirent *fault_readdir(DIR *directory)
{
    if (fault == READ_DIR) { errno = EIO; return NULL; }
    return readdir(directory);
}
static int fault_closedir(DIR *directory)
{
    int result = closedir(directory);
    return fault == CLOSE_DIR ? -1 : result;
}
static int fault_stat(const char *path, struct stat *found)
{
    if (fault == FILE_STAT && strstr(path, "cache") != NULL) { errno = EIO; return -1; }
    return stat(path, found);
}
static int fault_fseek(FILE *file, long offset, int origin)
{
    if (fault == SEEK_FILE) { errno = EIO; return -1; }
    return fseek(file, offset, origin);
}
static size_t fault_fread(void *bytes, size_t size, size_t count, FILE *file)
{
    return fread(bytes, size, fault == READ_FILE && count > 1 ? count / 2 : count, file);
}
static int fault_ferror(FILE *file) { return fault == READ_FILE ? 1 : ferror(file); }
static int fault_fclose(FILE *file)
{
    int result = fclose(file);
    return fault == CLOSE_FILE ? EOF : result;
}
#define opendir fault_opendir
#define readdir fault_readdir
#define closedir fault_closedir
#define stat(path, found) fault_stat(path, found)
#define fseek fault_fseek
#define fread fault_fread
#define ferror fault_ferror
#define fclose fault_fclose
#include "../../components/appstore/app_store.c"
#undef opendir
#undef readdir
#undef closedir
#undef stat
#undef fseek
#undef fread
#undef ferror
#undef fclose

int main(void)
{
    char root[] = "/tmp/perink-store-fault-XXXXXX";
    CHECK(mkdtemp(root) != NULL);
    char folder[128], path[128];
    snprintf(folder, sizeof folder, "%s/app", root); CHECK(mkdir(folder, 0700) == 0);
    snprintf(path, sizeof path, "%s/app/manifest.json", root);
    FILE *file = fopen(path, "wb"); CHECK(file != NULL);
    fputs("{\"name\":\"app\",\"title\":\"App\",\"abi\":3}", file); CHECK(fclose(file) == 0);
    CHECK(app_store_write_file(root, "app", "cache", 0, "abcdef", 6) == APP_WRITE_OK);
    long total = 99;
    CHECK(app_store_files_bytes(root, "app", &total) && total == MANIFEST_FILE_BLOCK_BYTES);
    for (fault = OPEN_DIR; fault <= FILE_STAT; fault++) {
        total = 99;
        CHECK(!app_store_files_bytes(root, "app", &total) && total == 99);
        CHECK(app_store_write_file(root, "app", "other", 0, "x", 1) == APP_WRITE_FAILED);
    }
    fault = FILE_STAT;
    CHECK(app_store_file_size(root, "app", "cache") == APP_FILE_FAILED);
    CHECK(app_store_write_file(root, "app", "cache", 0, "123456", 6) == APP_WRITE_FAILED);
    CHECK(app_store_write_file(root, "app", "cache", 0, NULL, 0) == APP_WRITE_FAILED);
    fault = HEALTHY;
    char bytes[8] = {0};
    CHECK(app_store_file_size(root, "app", "absent") == APP_FILE_NOT_FOUND);
    CHECK(app_store_read_file(root, "app", "absent", 0, bytes, sizeof bytes) == APP_FILE_NOT_FOUND);
    CHECK(app_store_read_file(root, "app", "cache", 4, bytes, sizeof bytes) == 2);
    CHECK(app_store_read_file(root, "app", "cache", 100, bytes, sizeof bytes) == 0);
    for (fault = SEEK_FILE; fault <= CLOSE_FILE; fault++) {
        memset(bytes, 0, sizeof bytes);
        CHECK(app_store_read_file(root, "app", "cache", 0, bytes, 6) == APP_FILE_FAILED);
        if (fault == READ_FILE) CHECK(memcmp(bytes, "abc", 3) == 0 && bytes[3] == 0);
    }
    fault = HEALTHY;
    CHECK(app_store_read_file(root, "app", "cache", 0, bytes, 6) == 6 && memcmp(bytes, "abcdef", 6) == 0);
    CHECK(app_store_remove(root, "app")); CHECK(rmdir(root) == 0);
    REPORT();
}
