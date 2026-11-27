CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -Werror -g -fsanitize=address,undefined
# POSIX beside C11, as the device's newlib has it (strnlen): glibc hides it under -std=c11
CFLAGS  += -D_DEFAULT_SOURCE
COMP    := ../../components
# Units and other headers with no code, shared by every component.
CFLAGS  += -I$(COMP)/board/include
APPCONF := $(COMP)/appconf
GFX     := $(COMP)/gfx
WORDS   := $(COMP)/words
CFLAGS  += -I$(WORDS)/include
INPUT   := $(COMP)/input
EPD     := $(COMP)/epd
UI      := $(COMP)/ui
APPSYS  := $(COMP)/appsys
APPSTORE := $(COMP)/appstore
CFLAGS  += -I$(APPSTORE)/include
DEADLINES := $(COMP)/deadlines
NET     := $(COMP)/net
WASM3   := $(COMP)/wasm3/wasm3
SDK     := ../../../sdk
SDK_LIB := $(SDK)/lib
TOOLS   := ../../../tools/perinkpheral
MKAPP   := $(TOOLS) build
MAIN    := ../../main

MANIFEST_SRCS := $(APPSTORE)/app_manifest.c $(APPSTORE)/json_tree.c $(SDK_LIB)/json_pick.c
MANIFEST_FLAGS := -I$(APPSYS)/include -I$(SDK) -I$(SDK_LIB)

# Whatever draws is rebuilt when the drawing code changes.
GFX_SRCS := $(GFX)/gfx.c $(GFX)/gfx_icons.c $(GFX)/gfx_icon_art.c $(WORDS)/words.c $(GFX)/font_square.c
GFX_DEPS := $(GFX_SRCS) $(wildcard $(GFX)/include/*.h $(GFX)/*.h $(GFX)/fonts/*.h)
# The map service, which the application runtime draws with.
MAP      := $(COMP)/map
MAP_SRCS := $(MAP)/map_geo.c $(MAP)/map_land.c $(MAP)/map_land_data.c $(MAP)/map_draw.c
MAP_DEPS := $(MAP_SRCS) $(wildcard $(MAP)/include/*.h)
APP_SRCS := $(APPSYS)/answer_headers.c $(APPSYS)/xml_query.c $(APPSYS)/document_reader.c $(APPSYS)/json_document.c $(APPSYS)/app.c $(APPSYS)/app_api_draw.c $(APPSYS)/app_api_map.c $(APPSYS)/app_api_ui.c $(APPSYS)/app_api_data.c \
            $(APPSYS)/app_api_time.c $(APPSYS)/app_api_sys.c $(MAP_SRCS)
QR      := $(COMP)/qr
QR_SRCS := $(QR)/qrcodegen/qrcodegen.c $(QR)/wifi_qr.c
QR_INCS := -I$(QR)/include -I$(QR)/qrcodegen
TESTS   := test_sdk_lib test_json_tree test_appconf test_gfx test_btn_core test_refresh_policy test_frame_box test_deadline_table test_scrollbar test_wm test_notice_queue test_scenes test_wifi_plan test_ota test_recovery_ota test_recovery_settings test_log_ring test_setup test_json_pick test_catalog test_app_manifest test_app_store test_polls test_request_queue test_app_room
TESTS   += test_document_reader test_xml_query test_answer_headers test_web_settings_json test_web_app_settings
TESTS   += test_map test_map_land


test_map: test_map.c tinytest.h $(MAP_DEPS) $(GFX_DEPS)
	$(CC) $(CFLAGS) -I$(MAP)/include -I$(GFX)/include -I$(GFX)/fonts -o $@ test_map.c $(MAP_SRCS) $(GFX)/gfx.c $(GFX)/font_square.c -lm

test_map_land: test_map_land.c tinytest.h $(MAP)/map_land.c $(wildcard $(MAP)/include/*.h)
	$(CC) $(CFLAGS) -I$(MAP)/include -o $@ test_map_land.c $(MAP)/map_land.c

# Not a test: how long the views take, and an estimate for the C6 (bench_map.c).
bench_map: bench_map.c bench_map.h $(MAP_DEPS) $(GFX_DEPS)
	$(CC) -std=c11 -O2 -include bench_map.h -I$(MAP)/include -I$(GFX)/include -I$(GFX)/fonts -o $@ bench_map.c $(MAP_SRCS) $(GFX)/gfx.c $(GFX)/font_square.c

test_web_app_settings: test_web_app_settings.c tinytest.h $(MAIN)/web_app_settings.c $(MAIN)/web_app_settings.h $(APPSTORE)/json_tree.c
	$(CC) $(CFLAGS) $(MANIFEST_FLAGS) -I$(MAIN) -o $@ test_web_app_settings.c $(MAIN)/web_app_settings.c $(APPSTORE)/json_tree.c

test_web_settings_json: test_web_settings_json.c tinytest.h $(MAIN)/web_settings_json.c $(MAIN)/web_settings_json.h $(APPCONF)/appconf.c $(APPSTORE)/json_tree.c $(APPSTORE)/catalog.c $(APPSTORE)/include/catalog.h
	$(CC) $(CFLAGS) -I$(MAIN) -I$(APPCONF)/include -o $@ test_web_settings_json.c $(APPCONF)/appconf.c $(APPSTORE)/catalog.c

test_document_reader: test_document_reader.c tinytest.h $(APPSYS)/document_reader.c $(APPSYS)/json_document.c $(APPSYS)/include/document_reader.h $(APPSYS)/xml_query.c $(APPSYS)/answer_headers.c $(APPSYS)/include/response_format.h $(SDK_LIB)/json_pick.c
	$(CC) $(CFLAGS) -I$(SDK) -I$(SDK_LIB) -I$(APPSYS)/include -o $@ test_document_reader.c $(APPSYS)/document_reader.c $(APPSYS)/json_document.c $(APPSYS)/xml_query.c $(APPSYS)/answer_headers.c $(SDK_LIB)/json_pick.c

test_xml_query: test_xml_query.c tinytest.h $(APPSYS)/xml_query.c $(APPSYS)/include/xml_query.h
	$(CC) $(CFLAGS) -I$(APPSYS)/include -I$(SDK) -o $@ test_xml_query.c $(APPSYS)/xml_query.c

test_answer_headers: test_answer_headers.c tinytest.h $(APPSYS)/answer_headers.c $(APPSYS)/include/answer_headers.h
	$(CC) $(CFLAGS) -I$(APPSYS)/include -I$(SDK) -o $@ test_answer_headers.c $(APPSYS)/answer_headers.c

TESTS   += test_recovery_screen
TESTS   += test_ota_probation
TESTS   += test_poll_round test_poller test_wifi_credentials test_memory
TESTS   += test_m3_area test_web_memory
TESTS   += test_standin_text
TESTS   += test_standin_contract
TESTS   += test_app_storage test_app_kv
TESTS   += test_wifi_networks test_wifi_search
TESTS   += test_catalog_stream
TESTS   += test_app_update_policy
TESTS   += test_app_update_schedule
TESTS   += test_app_install test_system_settings
TESTS   += test_wifi_events test_web_connections test_question_press test_launcher
TESTS   += test_poller_net
TESTS   += test_status

test_status: test_status.c tinytest.h $(MAIN)/status.c $(MAIN)/status.h $(NET)/wifi_status.c $(NET)/include/wifi_status.h $(NET)/wifi_plan.c $(wildcard event_stubs/freertos/*.h)
	$(CC) $(CFLAGS) -Ievent_stubs -Iinstall_stubs -Ipersistence_stubs -I$(MAIN) -I$(APPCONF)/include -I$(NET)/include -I$(DEADLINES)/include -o $@ test_status.c $(MAIN)/status.c $(NET)/wifi_status.c $(NET)/wifi_plan.c

test_poller_net: test_poller_net.c tinytest.h $(MAIN)/poller_net.c $(MAIN)/poller_net.h $(MANIFEST_SRCS) $(APPSTORE)/text_template.c $(APPSYS)/answer_headers.c $(wildcard net_stubs/*.h net_stubs/*/*.h)
	$(CC) $(CFLAGS) $(MANIFEST_FLAGS) -Inet_stubs -Ipoller_stubs -Iinstall_stubs -I$(UI)/include -I$(GFX)/include -I$(NET)/include -I$(MAIN) -o $@ test_poller_net.c $(MANIFEST_SRCS) $(APPSTORE)/text_template.c $(APPSYS)/answer_headers.c -lm
TESTS   += test_host_events

# Unused hardware paths of host.c are discarded; the queue, buttons and frame path run here.
comma := ,
HOST_TEST_LDFLAGS := $(if $(filter Darwin,$(shell uname -s)),-Wl$(comma)-dead_strip,-Wl$(comma)--gc-sections)
test_host_events: test_host_events.c tinytest.h $(MAIN)/host.c $(MAIN)/events.c $(MAIN)/question_press.c $(UI)/back_switch.c $(UI)/wm.c $(GFX_DEPS) $(wildcard $(MAIN)/*.h $(UI)/include/*.h event_stubs/*.h event_stubs/freertos/*.h persistence_stubs/*.h)
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections -Ievent_stubs -Ipersistence_stubs $(MANIFEST_FLAGS) -I$(MAIN) -I$(APPCONF)/include -I$(NET)/include -I$(DEADLINES)/include -I$(EPD)/include -I$(UI)/include -I$(GFX)/include -I$(GFX)/fonts -o $@ test_host_events.c $(MAIN)/events.c $(MAIN)/question_press.c $(UI)/back_switch.c $(UI)/wm.c $(GFX_SRCS) $(HOST_TEST_LDFLAGS)

test_question_press: test_question_press.c tinytest.h $(MAIN)/question_press.c $(MAIN)/question_press.h $(UI)/include/scene.h
	$(CC) $(CFLAGS) -I$(MAIN) -I$(UI)/include -I$(GFX)/include -o $@ test_question_press.c $(MAIN)/question_press.c

test_launcher: test_launcher.c tinytest.h $(MAIN)/launcher.c $(MAIN)/launcher.h $(APPSTORE)/app_store.c $(MANIFEST_SRCS) $(MAIN)/menu_order.c $(wildcard install_stubs/*.h install_stubs/*/*.h persistence_stubs/*.h)
	$(CC) $(CFLAGS) $(MANIFEST_FLAGS) -D_DARWIN_C_SOURCE -Iinstall_stubs -Ipersistence_stubs -I$(APPCONF)/include -I$(GFX)/include -I$(UI)/include -I$(MAIN) -o $@ test_launcher.c $(APPSTORE)/app_store.c $(MANIFEST_SRCS) $(MAIN)/menu_order.c -lm

test_wifi_events: test_wifi_events.c tinytest.h $(NET)/include/wifi_events.h $(NET)/include/wifi_plan.h $(NET)/wifi_plan.c
	$(CC) $(CFLAGS) -I$(NET)/include -o $@ test_wifi_events.c $(NET)/wifi_plan.c

test_web_connections: test_web_connections.c tinytest.h $(MAIN)/web_connections.c $(MAIN)/web_connections.h $(wildcard web_stubs/*.h web_stubs/lwip/*.h)
	$(CC) $(CFLAGS) -Iweb_stubs -I$(MAIN) -o $@ test_web_connections.c $(MAIN)/web_connections.c

SETTINGS_APP := ../../system/settings
SETTINGS_HEADERS := build/settings-headers
$(SETTINGS_HEADERS)/strings.h: $(wildcard $(SETTINGS_APP)/strings/*.json) $(SETTINGS_APP)/manifest.json
	$(TOOLS) headers $(SETTINGS_APP) $(SETTINGS_HEADERS)

test_system_settings: test_system_settings.c tinytest.h $(SETTINGS_APP)/settings.c $(SETTINGS_HEADERS)/strings.h $(wildcard $(SDK_LIB)/*.c $(SDK_LIB)/*.h $(SDK)/host/*.h) $(SDK)/host_api.h
	$(CC) $(CFLAGS) -Wno-attributes -I$(SDK) -I$(SDK_LIB) -I$(SETTINGS_HEADERS) -o $@ test_system_settings.c $(SETTINGS_HEADERS)/strings.c

test_app_install: test_app_install.c tinytest.h $(MAIN)/app_install.c $(MAIN)/app_install.h $(MANIFEST_SRCS) $(APPSTORE)/app_update_policy.c $(APPSTORE)/app_room.c $(APPSTORE)/catalog.c $(wildcard install_stubs/*.h install_stubs/*/*.h persistence_stubs/*.h)
	$(CC) $(CFLAGS) $(MANIFEST_FLAGS) -Iinstall_stubs -Ipersistence_stubs -I$(APPCONF)/include -I$(GFX)/include -I$(UI)/include -I$(MAIN) -o $@ test_app_install.c $(MANIFEST_SRCS) $(APPSTORE)/app_update_policy.c $(APPSTORE)/app_room.c $(APPSTORE)/catalog.c -lm

test_app_update_schedule: test_app_update_schedule.c tinytest.h $(APPSTORE)/app_update_schedule.c $(APPSTORE)/include/app_update_schedule.h
	$(CC) $(CFLAGS) -o $@ test_app_update_schedule.c $(APPSTORE)/app_update_schedule.c

.DEFAULT_GOAL := test
PERSISTENCE_STUBS := $(wildcard persistence_stubs/*.h)

test_app_storage: test_app_storage.c tinytest.h $(PERSISTENCE_STUBS) $(MAIN)/app_code.c $(MAIN)/app_code.h $(MAIN)/events.h $(APPSTORE)/app_store.c $(APPSTORE)/include/app_store.h $(APPSTORE)/app_room.c $(APPSTORE)/app_manifest.c $(APPSTORE)/json_tree.c
	$(CC) $(CFLAGS) $(MANIFEST_FLAGS) -D_DARWIN_C_SOURCE -Ipersistence_stubs -o $@ test_app_storage.c $(APPSTORE)/app_room.c $(MANIFEST_SRCS) -lm

test_app_kv: test_app_kv.c tinytest.h $(PERSISTENCE_STUBS) $(MAIN)/app_kv.c $(MAIN)/app_kv.h $(MAIN)/settings.h $(MAIN)/storage.h $(APPSTORE)/app_manifest.c $(APPSTORE)/json_tree.c
	$(CC) $(CFLAGS) $(MANIFEST_FLAGS) -D_DARWIN_C_SOURCE -Ipersistence_stubs -I$(APPCONF)/include -o $@ test_app_kv.c $(MANIFEST_SRCS) -lm

test_poll_round: test_poll_round.c tinytest.h $(APPSTORE)/poll_round.c $(APPSTORE)/include/poll_round.h $(APPSTORE)/poll_schedule.c $(APPSTORE)/include/poll_schedule.h
	$(CC) $(CFLAGS) $(MANIFEST_FLAGS) -o $@ test_poll_round.c $(APPSTORE)/poll_round.c $(APPSTORE)/poll_schedule.c

test_poller: test_poller.c tinytest.h $(MAIN)/poller.c $(MAIN)/poller_net.h $(NET)/wifi_status.c $(NET)/include/wifi_status.h $(APPSTORE)/poll_schedule.c $(APPSTORE)/poll_round.c $(APPSTORE)/request_queue.c $(APPSTORE)/text_template.c $(MANIFEST_SRCS) $(DEADLINES)/deadline_table.c $(NET)/wifi_plan.c $(wildcard poller_stubs/freertos/*.h)
	$(CC) $(CFLAGS) $(MANIFEST_FLAGS) -Ipoller_stubs -Iinstall_stubs -Ipersistence_stubs -Imemory_stubs -I$(APPCONF)/include -I$(GFX)/include -I$(UI)/include -I$(NET)/include -I$(DEADLINES)/include -I$(MAIN) -o $@ test_poller.c $(APPSTORE)/poll_schedule.c $(APPSTORE)/poll_round.c $(APPSTORE)/request_queue.c $(APPSTORE)/text_template.c $(MANIFEST_SRCS) $(DEADLINES)/deadline_table.c $(NET)/wifi_plan.c $(NET)/wifi_status.c -lm

test_wifi_credentials: test_wifi_credentials.c tinytest.h $(NET)/wifi_credentials.c $(NET)/include/wifi_credentials.h
	$(CC) $(CFLAGS) -I$(NET)/include -o $@ test_wifi_credentials.c $(NET)/wifi_credentials.c

test_wifi_search: test_wifi_search.c tinytest.h $(NET)/wifi_search.c $(NET)/include/wifi_search.h
	$(CC) $(CFLAGS) -I$(NET)/include -o $@ test_wifi_search.c $(NET)/wifi_search.c

test_wifi_networks: test_wifi_networks.c tinytest.h $(NET)/wifi_networks.c $(NET)/include/wifi_networks.h
	$(CC) $(CFLAGS) -I$(NET)/include -o $@ test_wifi_networks.c $(NET)/wifi_networks.c

test_memory: test_memory.c tinytest.h $(MAIN)/memory.c $(MAIN)/memory.h $(MAIN)/glyphs.h $(wildcard memory_stubs/*.h memory_stubs/*/*.h)
	$(CC) $(CFLAGS) -DMEMORY_TEST_LOG -pthread -Imemory_stubs -Iinstall_stubs -I$(MAIN) -I$(DEADLINES)/include -I$(APPSYS)/include -I$(UI)/include -I$(GFX)/include -I$(SDK_LIB) -o $@ test_memory.c $(MAIN)/memory.c

test_m3_area: test_m3_area.c tinytest.h $(COMP)/wasm3/m3_area.c $(COMP)/wasm3/m3_area.h area_stubs/multi_heap.h
	$(CC) $(CFLAGS) -Iarea_stubs -I$(COMP)/wasm3 -o $@ test_m3_area.c

# Running a real application needs wasi-sdk to build it.
ifdef WASI_SDK_PATH
TESTS   += test_app
else
$(info WASI_SDK_PATH is not set: skipping test_app)
endif

# The whole way of an answer to an application needs a built application.
ifdef WASI_SDK_PATH
TESTS   += test_delivery
endif

.PHONY: test clean

test: $(TESTS) $(if $(WASI_SDK_PATH),check-abi)
	@for t in $(TESTS); do ./$$t || exit 1; done
	@$(TOOLS) check-languages
	@$(TOOLS) check-layers ../..
	@$(TOOLS) cities --check && echo "cities.h is what cities.json makes"
	@$(MAKE) -C ../../../tools/simulator test

test_json_tree: test_json_tree.c tinytest.h $(APPSTORE)/json_tree.c $(APPSTORE)/include/json_tree.h
	$(CC) $(CFLAGS) -I$(APPSYS)/include -o $@ test_json_tree.c $(APPSTORE)/json_tree.c

test_appconf: test_appconf.c tinytest.h $(APPCONF)/appconf.c $(APPCONF)/include/appconf.h
	$(CC) $(CFLAGS) -I$(APPCONF)/include -o $@ test_appconf.c $(APPCONF)/appconf.c

test_gfx: test_gfx.c tinytest.h $(GFX_DEPS)
	$(CC) $(CFLAGS) -I$(GFX)/include -I$(GFX)/fonts -o $@ test_gfx.c $(GFX)/gfx.c $(GFX)/gfx_icons.c $(GFX)/gfx_icon_art.c $(WORDS)/words.c $(GFX)/font_square.c

test_btn_core: test_btn_core.c tinytest.h $(INPUT)/btn_core.c $(INPUT)/include/btn_core.h
	$(CC) $(CFLAGS) -I$(INPUT)/include -o $@ test_btn_core.c $(INPUT)/btn_core.c

test_deadline_table: test_deadline_table.c tinytest.h $(DEADLINES)/deadline_table.c $(DEADLINES)/include/deadline_table.h
	$(CC) $(CFLAGS) -I$(DEADLINES)/include -o $@ test_deadline_table.c $(DEADLINES)/deadline_table.c

test_scrollbar: test_scrollbar.c tinytest.h $(UI)/scrollbar.c $(UI)/include/scrollbar.h $(GFX_DEPS)
	$(CC) $(CFLAGS) -I$(UI)/include -I$(GFX)/include -I$(GFX)/fonts -o $@ test_scrollbar.c $(UI)/scrollbar.c $(GFX)/gfx.c $(GFX)/font_square.c

test_frame_box: test_frame_box.c tinytest.h $(EPD)/frame_box.c $(EPD)/include/frame_box.h
	$(CC) $(CFLAGS) -I$(EPD)/include -o $@ test_frame_box.c $(EPD)/frame_box.c

test_refresh_policy: test_refresh_policy.c tinytest.h $(EPD)/refresh_policy.c $(EPD)/include/refresh_policy.h
	$(CC) $(CFLAGS) -I$(EPD)/include -o $@ test_refresh_policy.c $(EPD)/refresh_policy.c

test_wm: test_wm.c tinytest.h $(UI)/back_switch.c $(UI)/include/back_switch.h $(UI)/wm.c $(UI)/notice.c $(UI)/kit.c $(UI)/scrollbar.c $(UI)/list.c $(UI)/include/wm.h $(UI)/include/scene.h $(GFX_DEPS)
	$(CC) $(CFLAGS) -I$(UI)/include -I$(GFX)/include -I$(GFX)/fonts -o $@ test_wm.c $(UI)/back_switch.c $(UI)/wm.c $(UI)/notice.c $(UI)/kit.c $(UI)/scrollbar.c $(UI)/list.c $(GFX)/gfx.c $(GFX)/gfx_icons.c $(GFX)/gfx_icon_art.c $(WORDS)/words.c $(GFX)/font_square.c

test_notice_queue: test_notice_queue.c tinytest.h $(UI)/notice_queue.c $(UI)/include/notice_queue.h $(GFX_DEPS)
	$(CC) $(CFLAGS) -I$(UI)/include -I$(GFX)/include -I$(GFX)/fonts -o $@ test_notice_queue.c $(UI)/notice_queue.c $(GFX)/gfx.c $(GFX)/gfx_icons.c $(GFX)/gfx_icon_art.c $(WORDS)/words.c $(GFX)/font_square.c

test_scenes: test_scenes.c tinytest.h $(UI)/kit.c $(MAIN)/header.c $(MAIN)/header.h $(MAIN)/menu_order.c $(MAIN)/menu_order.h $(MAIN)/scene_menu.c $(MAIN)/scene_splash.c $(MAIN)/scene_confirm.c $(MAIN)/scene_notify.c $(MAIN)/scene_working.c $(MAIN)/scene_storage.c $(MAIN)/screens.c $(MAIN)/scenes.h $(MAIN)/screens.h $(UI)/include/scene.h $(GFX_DEPS)
	$(CC) $(CFLAGS) -I$(MAIN) -I$(SDK) -I$(UI)/include -I$(GFX)/include -I$(GFX)/fonts $(QR_INCS) -o $@ test_scenes.c \
		$(MAIN)/header.c $(MAIN)/menu_order.c $(MAIN)/scene_menu.c $(MAIN)/scene_splash.c $(MAIN)/scene_confirm.c $(MAIN)/scene_notify.c $(MAIN)/scene_working.c $(MAIN)/scene_storage.c $(MAIN)/screens.c $(UI)/wm.c $(UI)/notice.c $(UI)/kit.c $(UI)/scrollbar.c $(UI)/list.c $(GFX)/gfx.c $(GFX)/gfx_icons.c $(GFX)/gfx_icon_art.c $(WORDS)/words.c $(GFX)/font_square.c $(QR_SRCS)

STANDIN_SRCS := $(SDK)/check/standin.c $(filter-out $(SDK_LIB)/app_alloc.c $(SDK_LIB)/app_runtime.c,$(wildcard $(SDK_LIB)/*.c)) $(MAP)/map_geo.c $(APPSYS)/answer_headers.c $(APPSYS)/xml_query.c $(APPSYS)/document_reader.c $(APPSYS)/json_document.c
test_standin_text test_standin_contract: %: %.c tinytest.h $(STANDIN_SRCS) $(SDK)/check/standin.h $(wildcard $(SDK)/host/*.h)
	$(CC) $(CFLAGS) -D_DARWIN_C_SOURCE -Wno-attributes '-DHOST_IMPORT(name,...)=__VA_ARGS__' '-DAPP_EXPORT(name,...)=__VA_ARGS__' $(MANIFEST_FLAGS) -I$(SDK)/check -I$(MAP)/include -o $@ $< $(STANDIN_SRCS) -lm

# The interpreter is upstream code: built once, without our warnings, and
# limited to one page of memory exactly as on the device.
# The native stack budget is the device's, scaled to the frames an
# unoptimized build with sanitizers makes.
WASM3_FLAGS := -std=c11 -O1 -w -Dd_m3MaxLinearMemoryPages=1 -Dd_m3MaxNativeStack=262144 \
               -Dd_m3VerboseErrorMessages=0 -Dd_m3LogOutput=0 \
               -Dd_m3HasFloat=0 -Dd_m3HasRefTypes=0 -Dd_m3HasExtendedConst=0 -Dd_m3HasCompactImports=0
WASM3_OBJS  := $(patsubst $(WASM3)/%.c,build/wasm3/%.o,$(wildcard $(WASM3)/*.c))

build/wasm3/%.o: $(WASM3)/%.c
	@mkdir -p build/wasm3
	$(CC) $(WASM3_FLAGS) -c -o $@ $<

build/wasm3/m3_compile.o: $(COMP)/wasm3/m3_budget_compile.c $(wildcard $(WASM3)/*.h) $(WASM3)/m3_compile.c
	@mkdir -p build/wasm3
	$(CC) $(WASM3_FLAGS) -I$(WASM3) -c -o $@ $<

# A file as the bytes of a C array, by POSIX od: xxd is not everywhere.
c_bytes = od -An -v -tx1 $(1) | sed -e 's/\([0-9a-f][0-9a-f]\)/0x\1,/g'

build/probe/app.wasm: apps/probe/probe.c apps/probe/manifest.json $(SDK)/host_api.h $(wildcard $(SDK)/host/*.h) $(wildcard $(SDK)/lib/*) $(TOOLS)
	$(MKAPP) apps/probe --out build/probe

build/settings/app.wasm: $(SETTINGS_APP)/settings.c $(SETTINGS_APP)/manifest.json $(SETTINGS_APP)/cities.h $(wildcard $(SETTINGS_APP)/strings/*.json $(SDK)/host/*.h $(SDK_LIB)/*) $(SDK)/host_api.h $(TOOLS)
	$(MKAPP) $(SETTINGS_APP) --out build/settings

build/probe_wasm.h: build/probe/app.wasm
	(echo "static const unsigned char probe_wasm[] = {"; $(call c_bytes,$<); \
	 echo "};"; echo "static const unsigned int probe_wasm_len = sizeof probe_wasm;") > $@

build/wrongret/app.wasm: apps/wrongret/wrongret.c apps/wrongret/manifest.json $(SDK)/host_api.h $(wildcard $(SDK)/host/*.h) $(TOOLS)
	@mkdir -p build/wrongret
	$(WASI_SDK_PATH)/bin/clang $(subst ;, ,$(shell $(TOOLS) flags compile)) $(subst ;, ,$(shell $(TOOLS) flags link)) apps/wrongret/wrongret.c -o $@

build/wrongret_wasm.h: build/wrongret/app.wasm
	(echo "static const unsigned char wrongret_wasm[] = {"; $(call c_bytes,$<); echo "};") > $@

build/wrongarg/app.wasm: apps/wrongarg/wrongarg.c apps/wrongarg/manifest.json $(SDK)/host_api.h $(wildcard $(SDK)/host/*.h) $(TOOLS)
	@mkdir -p build/wrongarg
	$(WASI_SDK_PATH)/bin/clang $(subst ;, ,$(shell $(TOOLS) flags compile)) $(subst ;, ,$(shell $(TOOLS) flags link)) apps/wrongarg/wrongarg.c -o $@

build/wrongarg_wasm.h: build/wrongarg/app.wasm
	(echo "static const unsigned char wrongarg_wasm[] = {"; $(call c_bytes,$<); echo "};") > $@

build/document/app.wasm: apps/document/document.c $(TOOLS) $(SDK)/host_api.h $(wildcard $(SDK)/host/*.h)
	@mkdir -p build/document
	$(WASI_SDK_PATH)/bin/clang $(subst ;, ,$(shell $(TOOLS) flags compile)) $(subst ;, ,$(shell $(TOOLS) flags link)) -I$(SDK) $< -o $@

build/document_dual/app.wasm: apps/document/document.c $(TOOLS) $(SDK)/host_api.h $(wildcard $(SDK)/host/*.h)
	@mkdir -p build/document_dual
	$(WASI_SDK_PATH)/bin/clang $(subst ;, ,$(shell $(TOOLS) flags compile)) $(subst ;, ,$(shell $(TOOLS) flags link)) -I$(SDK) -DDUAL_HANDLER $< -o $@

build/document_wasm.h: build/document/app.wasm build/document_dual/app.wasm
	(echo "static const unsigned char document_wasm[] = {"; $(call c_bytes,build/document/app.wasm); echo "};"; \
	 echo "static const unsigned char document_dual_wasm[] = {"; $(call c_bytes,build/document_dual/app.wasm); echo "};") > $@

build/budget/app.wasm: apps/budget/budget.c $(TOOLS)
	@mkdir -p build/budget
	$(WASI_SDK_PATH)/bin/clang $(subst ;, ,$(shell $(TOOLS) flags compile)) $(subst ;, ,$(shell $(TOOLS) flags link)) $< -o $@

build/budget_wasm.h: build/budget/app.wasm
	(echo "static const unsigned char budget_wasm[] = {"; $(call c_bytes,$<); echo "};") > $@

# A growable module made outside the builder, with a declared maximum above the runtime limit.
build/memory/app.wasm: apps/memory/memory.c $(SDK)/host_api.h $(wildcard $(SDK)/host/*.h) $(TOOLS)
	@mkdir -p build/memory
	$(WASI_SDK_PATH)/bin/clang --target=wasm32 -nostdlib -Oz -Wall -Wextra -Werror -I$(SDK) -Wl,--no-entry -Wl,--page-size=1 -Wl,-z,stack-size=0 -Wl,--initial-memory=65528 -Wl,--max-memory=65538 $< -o $@

build/memory_wasm.h: build/memory/app.wasm
	(echo "static const unsigned char memory_wasm[] = {"; $(call c_bytes,$<); echo "};") > $@

build/host_imports.inc: $(SDK)/host_api.h $(wildcard $(SDK)/host/*.h)
	$(TOOLS) imports build

build/src/every-call/every_call.c: $(SDK)/host_api.h $(wildcard $(SDK)/host/*.h)
	$(TOOLS) every-call build/src/every-call

build/every_call/app.wasm: build/src/every-call/every_call.c $(TOOLS)
	$(MKAPP) build/src/every-call --out build/every_call

build/every_call_wasm.h: build/every_call/app.wasm
	(echo "static const unsigned char every_call_wasm[] = {"; $(call c_bytes,$<); echo "};") > $@

build/frozen_probe_wasm.h: ../../../sdk/abi/3/probe/app.wasm
	@mkdir -p build
	(echo "static const unsigned char frozen_probe_wasm[] = {"; $(call c_bytes,$<); echo "};") > $@

test_app: test_app.c tinytest.h build/document_wasm.h build/budget_wasm.h build/memory_wasm.h build/frozen_probe_wasm.h build/host_imports.inc build/probe_wasm.h build/every_call_wasm.h build/wrongret_wasm.h build/wrongarg_wasm.h $(APP_SRCS) $(APPSYS)/app_internal.h $(APPSYS)/include/app.h $(GFX)/gfx.c $(GFX)/gfx_icons.c $(GFX)/gfx_icon_art.c $(WORDS)/words.c $(WASM3_OBJS)
	$(CC) $(CFLAGS) -Ibuild -I$(APPSYS)/include -I$(UI)/include -I$(GFX)/include -I$(GFX)/fonts -I$(MAP)/include -I$(WASM3) -I$(SDK) -I$(SDK_LIB) \
		-Dd_m3MaxLinearMemoryPages=1 $(QR_INCS) -o $@ test_app.c $(APP_SRCS) $(SDK_LIB)/json_pick.c $(UI)/list.c $(UI)/notice.c $(UI)/kit.c $(UI)/scrollbar.c $(GFX)/gfx.c $(GFX)/gfx_icons.c $(GFX)/gfx_icon_art.c $(WORDS)/words.c $(GFX)/font_square.c $(QR)/qrcodegen/qrcodegen.c $(WASM3_OBJS)

test_wifi_plan: test_wifi_plan.c tinytest.h $(NET)/wifi_plan.c $(NET)/include/wifi_plan.h
	$(CC) $(CFLAGS) -I$(NET)/include -o $@ test_wifi_plan.c $(NET)/wifi_plan.c

test_setup: test_setup.c tinytest.h $(NET)/captive_dns.c $(NET)/include/captive_dns.h $(NET)/captive_portal.c $(NET)/include/captive_portal.h $(QR_SRCS) $(QR)/include/wifi_qr.h
	$(CC) $(CFLAGS) -I$(NET)/include $(QR_INCS) -o $@ test_setup.c $(NET)/captive_dns.c $(NET)/captive_portal.c $(QR_SRCS)

test_sdk_lib: test_sdk_lib.c tinytest.h $(wildcard $(SDK_LIB)/*.c) $(wildcard $(SDK_LIB)/*.h) $(SDK)/host_api.h $(wildcard $(SDK)/host/*.h)
	$(CC) $(CFLAGS) -I$(SDK) -I$(SDK_LIB) -o $@ test_sdk_lib.c

test_json_pick: test_json_pick.c tinytest.h $(SDK_LIB)/json_pick.c $(SDK_LIB)/json_pick.h
	$(CC) $(CFLAGS) -I$(APPSYS)/include -I$(SDK_LIB) -o $@ test_json_pick.c $(SDK_LIB)/json_pick.c

test_catalog: test_catalog.c tinytest.h $(APPSTORE)/catalog.c $(APPSTORE)/include/catalog.h
	$(CC) $(CFLAGS) -I$(APPSYS)/include -o $@ test_catalog.c $(APPSTORE)/catalog.c

test_ota: test_ota.c tinytest.h ../../../recovery/main/control.h $(NET)/ota_stream.c $(NET)/ota_health.c $(wildcard $(NET)/include/ota_*.h)
	$(CC) $(CFLAGS) -I$(NET)/include -o $@ test_ota.c $(NET)/ota_stream.c $(NET)/ota_health.c

test_ota_probation: test_ota_probation.c tinytest.h $(MAIN)/ota.c $(MAIN)/ota.h $(NET)/ota_health.c $(wildcard $(NET)/include/ota_*.h ota_stubs/*.h ota_stubs/freertos/*.h recovery_stubs/*.h memory_stubs/*.h install_stubs/freertos/*.h)
	$(CC) $(CFLAGS) -Iota_stubs -Irecovery_stubs -Imemory_stubs -I$(MAIN) -I$(NET)/include -I$(APPSYS)/include -I$(UI)/include -I$(GFX)/include -I$(SDK_LIB) -o $@ test_ota_probation.c $(MAIN)/ota.c $(NET)/ota_health.c

test_recovery_ota: test_recovery_ota.c tinytest.h ../../../recovery/main/ota.c ../../../recovery/main/ota.h ../../../recovery/main/control.h $(wildcard recovery_stubs/*.h) $(NET)/ota_stream.c $(NET)/include/ota_stream.h $(NET)/include/ota_handoff.h
	$(CC) $(CFLAGS) -DRECOVERY_LAYOUT=0x1234abcd -Irecovery_stubs -I$(NET)/include -o $@ test_recovery_ota.c ../../../recovery/main/ota.c $(NET)/ota_stream.c

test_recovery_screen: test_recovery_screen.c tinytest.h ../../../recovery/main/screen.c ../../../recovery/main/screen.h ../../../recovery/main/control.h $(GFX_DEPS) $(UI)/notice.c $(UI)/kit.c
	$(CC) $(CFLAGS) -I$(GFX)/include -I$(GFX)/fonts -I$(UI)/include -o $@ test_recovery_screen.c ../../../recovery/main/screen.c $(GFX_SRCS) $(UI)/notice.c $(UI)/kit.c

test_recovery_settings: test_recovery_settings.c tinytest.h ../../../recovery/main/settings.c ../../../recovery/main/settings.h $(wildcard recovery_stubs/*.h) $(APPCONF)/appconf.c $(APPCONF)/include/appconf.h
	$(CC) $(CFLAGS) -Irecovery_stubs -I$(APPCONF)/include -o $@ test_recovery_settings.c ../../../recovery/main/settings.c $(APPCONF)/appconf.c

test_log_ring: test_log_ring.c tinytest.h $(MAIN)/log_ring.c $(MAIN)/log_ring.h
	$(CC) $(CFLAGS) -I$(MAIN) -o $@ test_log_ring.c $(MAIN)/log_ring.c

test_app_manifest: manifest_defaults.json test_app_manifest.c tinytest.h $(MANIFEST_SRCS) $(APPSTORE)/include/app_manifest.h
	$(CC) $(CFLAGS) $(MANIFEST_FLAGS) -o $@ test_app_manifest.c $(MANIFEST_SRCS) -lm

test_app_update_policy: test_app_update_policy.c tinytest.h $(MANIFEST_SRCS) $(APPSTORE)/app_update_policy.c $(APPSTORE)/include/app_update_policy.h
	$(CC) $(CFLAGS) $(MANIFEST_FLAGS) -o $@ test_app_update_policy.c $(APPSTORE)/app_update_policy.c $(MANIFEST_SRCS) -lm

test_app_room: test_app_room.c tinytest.h $(APPSTORE)/app_room.c $(APPSTORE)/include/app_room.h
	$(CC) $(CFLAGS) $(MANIFEST_FLAGS) -o $@ test_app_room.c $(APPSTORE)/app_room.c

test_request_queue: test_request_queue.c tinytest.h $(APPSTORE)/request_queue.c $(APPSTORE)/include/request_queue.h
	$(CC) $(CFLAGS) $(MANIFEST_FLAGS) -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE -o $@ test_request_queue.c $(APPSTORE)/request_queue.c

test_polls: test_polls.c tinytest.h $(MANIFEST_SRCS) $(APPSTORE)/text_template.c $(APPSTORE)/poll_schedule.c $(wildcard $(APPSYS)/include/*.h $(APPSTORE)/include/*.h)
	$(CC) $(CFLAGS) $(MANIFEST_FLAGS) -o $@ test_polls.c $(MANIFEST_SRCS) $(APPSTORE)/text_template.c \
		$(APPSTORE)/poll_schedule.c -lm

# mkdtemp and friends are POSIX, not C11.
test_delivery: test_delivery.c tinytest.h $(MAIN)/host_apps.c build/document/app.wasm $(COMP)/wasm3/m3_area.h build/host_imports.inc build/probe/app.wasm build/settings/app.wasm $(MAIN)/question_press.c $(MAIN)/question_press.h $(wildcard $(APPSYS)/*.c $(APPSYS)/include/*.h $(APPSTORE)/*.c $(APPSTORE)/include/*.h) $(UI)/wm.c $(UI)/notice.c $(UI)/kit.c $(UI)/scrollbar.c $(UI)/list.c $(WASM3_OBJS) $(MANIFEST_SRCS)
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections -Inet_stubs -Ipoller_stubs -Iinstall_stubs -Imemory_stubs -Ipersistence_stubs -I$(APPCONF)/include -I$(NET)/include -I$(DEADLINES)/include -Ibuild -I$(MAIN) -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE $(MANIFEST_FLAGS) -I$(UI)/include -I$(GFX)/include -I$(GFX)/fonts -I$(MAP)/include -I$(WASM3) \
		-Dd_m3MaxLinearMemoryPages=1 -Dd_m3ApplicationArea -I$(COMP)/wasm3 $(QR_INCS) -o $@ test_delivery.c $(MAIN)/host_apps.c $(QR)/qrcodegen/qrcodegen.c $(APP_SRCS) $(APPSYS)/app_scene.c $(APPSYS)/app_delivery.c \
		$(MAIN)/question_press.c $(MANIFEST_SRCS) $(UI)/wm.c $(UI)/notice.c $(UI)/kit.c $(UI)/scrollbar.c $(UI)/list.c $(GFX)/gfx.c $(GFX)/gfx_icons.c $(GFX)/gfx_icon_art.c $(WORDS)/words.c $(GFX)/font_square.c $(WASM3_OBJS) $(HOST_TEST_LDFLAGS) -lm

test_app_store: test_app_store.c tinytest.h $(APPSTORE)/app_store.c $(APPSTORE)/include/app_store.h $(MANIFEST_SRCS)
	$(CC) $(CFLAGS) -D_DEFAULT_SOURCE -D_DARWIN_C_SOURCE $(MANIFEST_FLAGS) -o $@ test_app_store.c \
		$(APPSTORE)/app_store.c $(MANIFEST_SRCS) -lm

clean:
	rm -rf build
	rm -f $(TESTS) bench_map

# Frozen contracts and packages are inputs, never rebuilt by this target.
.PHONY: check-abi
check-abi: build/probe/app.wasm build/wrongarg/app.wasm build/wrongret/app.wasm build/wrongimport/app.wasm
	@$(TOOLS) contract check
	@$(TOOLS) check-wasm build/probe/app.wasm
	@$(TOOLS) check-wasm ../../../sdk/abi/3/probe/app.wasm
	@$(TOOLS) check-wasm ../../../sdk/abi/3/hello/app.wasm
	@if $(TOOLS) check-wasm build/wrongarg/app.wasm > build/wrongarg/check.log 2>&1; then cat build/wrongarg/check.log; exit 1; fi
	@grep -q 'export app_on_timer has wasm type' build/wrongarg/check.log
	@if $(TOOLS) check-wasm build/wrongret/app.wasm > build/wrongret/check.log 2>&1; then cat build/wrongret/check.log; exit 1; fi
	@grep -q 'export app_strings_buffer has wasm type' build/wrongret/check.log
	@if $(TOOLS) check-wasm build/wrongimport/app.wasm > build/wrongimport/check.log 2>&1; then cat build/wrongimport/check.log; exit 1; fi
	@grep -q 'import host.gfx_width has wasm type' build/wrongimport/check.log

build/wrongimport/app.wasm: apps/wrongimport/wrongimport.c $(TOOLS)
	@mkdir -p build/wrongimport
	$(WASI_SDK_PATH)/bin/clang $(subst ;, ,$(shell $(TOOLS) flags compile)) $(subst ;, ,$(shell $(TOOLS) flags link)) $< -o $@

test_catalog_stream: test_catalog_stream.c tinytest.h $(APPSTORE)/catalog_stream.c $(APPSTORE)/include/catalog_stream.h
	$(CC) $(CFLAGS) -I$(SDK)/lib -o $@ test_catalog_stream.c $(APPSTORE)/catalog_stream.c

# Compile the real route tables, dispatcher, status and glyph handler. Discard
# unrelated hardware paths, as test_host_events does for host.c.
WEB_POLLER_SRCS := $(APPSTORE)/poll_schedule.c $(APPSTORE)/poll_round.c $(APPSTORE)/request_queue.c $(APPSTORE)/catalog.c $(MANIFEST_SRCS)
test_web_memory: test_web_memory.c tinytest.h $(MAIN)/web_server.c $(MAIN)/web_apps.c $(MAIN)/poller.c $(MAIN)/memory.c $(WEB_POLLER_SRCS) $(WORDS)/words.c $(COMP)/wasm3/m3_area.h $(wildcard $(MAIN)/*.h route_stubs/*.h route_stubs/*/*.h memory_stubs/*.h install_stubs/*.h install_stubs/*/*.h persistence_stubs/*.h poller_stubs/freertos/*.h)
	$(CC) $(CFLAGS) -O2 -Dasm=__asm__ -pthread -ffunction-sections -fdata-sections -DCONFIG_LWIP_MAX_SOCKETS=12 $(MANIFEST_FLAGS) -Ipoller_stubs -Iroute_stubs -Imemory_stubs -Iinstall_stubs -Ipersistence_stubs -I$(COMP)/wasm3 -I$(APPCONF)/include -I$(DEADLINES)/include -I$(NET)/include -I$(UI)/include -I$(GFX)/include -I$(MAIN) -o $@ test_web_memory.c $(MAIN)/memory.c $(WEB_POLLER_SRCS) $(WORDS)/words.c $(HOST_TEST_LDFLAGS) -lm
