idf_component_register(
    SRCS
        "main.cpp"
        "esp32_audio.cpp"
        "esp32_midi.cpp"
        "esp32_usb_audio.cpp"
        "esp32_wifi.cpp"
        "esp32_applemidi.cpp"
        "esp32_captive_portal.cpp"
        "esp32_storage.cpp"
        "esp32_config.cpp"
        "esp32_led.cpp"
        "esp32_button.cpp"
        "AudioEffectSymphonic.cpp"
        "dexed_raw.cpp"
        "atomic_compat.c"
    INCLUDE_DIRS
        "."
        "${CMAKE_CURRENT_SOURCE_DIR}/../../src"
    REQUIRES
        driver
        esp_wifi
        esp_event
        nvs_flash
        spiffs
        esp_timer
        esp_netif
        lwip
        esp_psram
        usb
        esp_driver_rmt
        mdns
        esp_http_server
)

# Add the Dexed engine source files
set(DEXED_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../src")

target_sources(${COMPONENT_LIB} PRIVATE
    # Core Dexed engine
    "${DEXED_SRC_DIR}/dexed.cpp"
    "${DEXED_SRC_DIR}/dx7note.cpp"
    "${DEXED_SRC_DIR}/EngineMkI.cpp"
    "${DEXED_SRC_DIR}/EngineMsfa.cpp"
    "${DEXED_SRC_DIR}/EngineOpl.cpp"
    "${DEXED_SRC_DIR}/env.cpp"
    "${DEXED_SRC_DIR}/exp2.cpp"
    "${DEXED_SRC_DIR}/fm_core.cpp"
    "${DEXED_SRC_DIR}/fm_op_kernel.cpp"
    "${DEXED_SRC_DIR}/freqlut.cpp"
    "${DEXED_SRC_DIR}/lfo.cpp"
    "${DEXED_SRC_DIR}/pitchenv.cpp"
    "${DEXED_SRC_DIR}/porta.cpp"
    "${DEXED_SRC_DIR}/sin.cpp"
)

# Compile definitions for ESP32 target (applied to ALL sources in this component)
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    ESP32_TARGET=1
    SYNTH_DEXED_STATIC=1
)

# Compiler flags for performance
target_compile_options(${COMPONENT_LIB} PRIVATE
    -O2
    -ffast-math
    -fno-rtti
)

# C++17 is required for std::filesystem (used in Rack.cpp)
# ESP-IDF v5.4 ships GCC 14.2 where stdc++fs is merged into libstdc++,
# so no separate link library is needed.

# Upstream Dexed/FMRack sources need __linux__ defined so the platform detection
# in dexed.cpp selects the POSIX code path (gettimeofday-based millis()).
# We apply this ONLY to upstream sources, NOT to ESP32 platform sources,
# to avoid confusing ESP-IDF headers that also check __linux__.
set_source_files_properties(
    "${DEXED_SRC_DIR}/dexed.cpp"
    "${DEXED_SRC_DIR}/dx7note.cpp"
    "${DEXED_SRC_DIR}/EngineMkI.cpp"
    "${DEXED_SRC_DIR}/EngineMsfa.cpp"
    "${DEXED_SRC_DIR}/EngineOpl.cpp"
    "${DEXED_SRC_DIR}/env.cpp"
    "${DEXED_SRC_DIR}/exp2.cpp"
    "${DEXED_SRC_DIR}/fm_core.cpp"
    "${DEXED_SRC_DIR}/fm_op_kernel.cpp"
    "${DEXED_SRC_DIR}/freqlut.cpp"
    "${DEXED_SRC_DIR}/lfo.cpp"
    "${DEXED_SRC_DIR}/pitchenv.cpp"
    "${DEXED_SRC_DIR}/porta.cpp"
    "${DEXED_SRC_DIR}/sin.cpp"
    PROPERTIES COMPILE_FLAGS "-D__linux__=1 -Wno-unused-parameter -Wno-sign-compare -Wno-maybe-uninitialized"
)
