/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 GrakovNe
 */
#include <sys/stat.h>
#include <unistd.h>
#include "tinytest.h"
#include "mbedtls/sha256.h"
#include "../../main/storage.h"

static char test_root[sizeof "/tmp/perink-launcher-XXXXXX"];
#undef STORAGE_APPS_ROOT
#define STORAGE_APPS_ROOT test_root

/* Force a digest-prefix collision while exercising the real package hash path. */
static bool collide;
static int finish_hash(mbedtls_sha256_context *hash, unsigned char digest[32])
{
    int result = mbedtls_sha256_finish(hash, digest);
    if (collide) {
        memset(digest, 0, 4);
        digest[3] = 7;
    }
    return result;
}
#define mbedtls_sha256_finish finish_hash
#define asm __asm__
#include "../../main/launcher.c"
#undef asm
#undef mbedtls_sha256_finish

#define SYSTEM_BYTES(name) \
    const uint8_t system_##name##_wasm_start[1] = { 0 }; \
    const uint8_t system_##name##_wasm_end[1] = { 0 }; \
    const char system_##name##_manifest_start[] = "{}"; \
    const uint8_t system_##name##_strings_ru_start[1] = { 0 }; \
    const uint8_t system_##name##_strings_ru_end[1] = { 0 }; \
    const char system_##name##_manifest_ru_start[] = "{}"
SYSTEM_BYTES(setup);
SYSTEM_BYTES(settings);
SYSTEM_BYTES(about);

static int scenes_created, scenes_destroyed, scenes_dropped;
static scene_t scenes[2];

void vTaskDelay(unsigned ticks) { (void)ticks; }
bool storage_is_mounted(void) { return false; }
void app_code_adopt(const char *root) { (void)root; }
const uint8_t *app_code_find(const char *root, const char *name, size_t *size)
{
    (void)root; (void)name; (void)size;
    return NULL;
}
const app_services_t *app_host_services(void) { return NULL; }
static const app_manifest_t *borrowed[2];
static app_manifest_t expected[2];
scene_t *app_scene_borrow(const app_manifest_t *manifest, const char *root,
                          const uint8_t *code, size_t size, const app_services_t *services)
{
    (void)root; (void)code; (void)size; (void)services;
    CHECK(manifest->id != 0);
    CHECK(scenes_created < 2);
    borrowed[scenes_created] = manifest;
    expected[scenes_created] = *manifest;
    return &scenes[scenes_created++];
}
void app_scene_destroy(scene_t *scene)
{
    size_t index = (size_t)(scene - scenes);
    CHECK(borrowed[index] == &s_installed[index]);
    CHECK(!memcmp(borrowed[index], &expected[index], sizeof expected[index]));
    scenes_destroyed++;
}
static void drop_scene(scene_t *scene, void *context)
{
    (void)scene; (void)context;
    scenes_dropped++;
}
static const char *language = "en";
void settings_read(appconf_t *copy)
{
    memset(copy, 0, sizeof *copy);
    strcpy(copy->language, language);
}
void settings_load_menu_order(char *order, size_t size) { (void)size; order[0] = '\0'; }
esp_err_t settings_save_menu_order(const char *order) { (void)order; return ESP_OK; }
void app_kv_forget_host_record(const char *name) { (void)name; }
void app_kv_erase(const char *name) { (void)name; }
bool app_install_app_is_busy(const char *name) { (void)name; return false; }

static void write_file(const char *name, const char *file, const char *text)
{
    char path[APP_STORE_PATH_MAX];
    snprintf(path, sizeof path, "%s/%s/%s", test_root, name, file);
    FILE *output = fopen(path, "wb");
    CHECK(output != NULL);
    if (output != NULL) {
        CHECK(fwrite(text, 1, strlen(text), output) == strlen(text));
        CHECK(fclose(output) == 0);
    }
}

int main(void)
{
    strcpy(test_root, "/tmp/perink-launcher-XXXXXX");
    CHECK(mkdtemp(test_root) != NULL);
    static const char ALPHA[] = "{\"name\":\"alpha\",\"title\":\"Alpha\",\"abi\":3}";
    static const char BETA[] = "{\"name\":\"beta\",\"title\":\"Beta\",\"abi\":3}";
    const char *names[] = { "alpha", "beta" };
    const char *manifests[] = { ALPHA, BETA };
    for (int i = 0; i < 2; i++) {
        char folder[APP_STORE_PATH_MAX];
        snprintf(folder, sizeof folder, "%s/%s", test_root, names[i]);
        CHECK(mkdir(folder, 0700) == 0);
        write_file(names[i], "manifest.json", manifests[i]);
        write_file(names[i], "app.wasm", "module");
    }
    read_installed();
    CHECK(s_installed_count == 2 && scenes_created == 2);
    CHECK(s_installed[0].id != s_installed[1].id);
    write_file("alpha", "app.wasm.new", "module");
    CHECK(launcher_incoming_id_is_unique("alpha", ALPHA));
    CHECK(!launcher_incoming_id_is_unique("missing", ALPHA));

    /* A language reload reorders the slots, and a replaced module gets a new ID. */
    uint32_t old_id = launcher_find("alpha")->id;
    write_file("alpha", "manifest.ru", "{\"title\":\"Zulu\"}");
    write_file("alpha", "app.wasm", "replacement");
    language = "ru";
    scenes_created = 0;
    launcher_reload(drop_scene, NULL);
    CHECK(s_installed_count == 2 && scenes_created == 2);
    CHECK(!strcmp(borrowed[0]->name, "beta") && !strcmp(borrowed[1]->title, "Zulu"));
    CHECK(borrowed[1] == launcher_find("alpha") && borrowed[1]->id != old_id);

    collide = true;
    launcher_reload(drop_scene, NULL);
    CHECK(s_installed_count == 0 && scenes_created == 2);
    CHECK(scenes_destroyed == 4 && scenes_dropped == 4);
    CHECK(launcher_find_id(7) == NULL);
    CHECK(launcher_app_scene("alpha") == NULL && launcher_app_scene("beta") == NULL);
    CHECK(!launcher_incoming_id_is_unique("alpha", ALPHA));
    CHECK(app_store_remove(test_root, "beta"));
    scenes_created = 0;
    launcher_reload(drop_scene, NULL);
    CHECK(s_installed_count == 1 && scenes_created == 1);
    CHECK(launcher_find_id(7) != NULL);
    CHECK(launcher_incoming_id_is_unique("alpha", ALPHA));
    char folder[APP_STORE_PATH_MAX];
    snprintf(folder, sizeof folder, "%s/beta", test_root);
    CHECK(mkdir(folder, 0700) == 0);
    write_file("beta", "app.wasm.new", "module");
    CHECK(!launcher_incoming_id_is_unique("beta", BETA));
    CHECK(app_store_remove(test_root, "alpha"));
    CHECK(app_store_remove(test_root, "beta"));
    CHECK(rmdir(test_root) == 0);
    REPORT();
}
