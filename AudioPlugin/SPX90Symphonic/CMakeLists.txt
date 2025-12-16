# SPX90 Symphonic Effect Plugin
# A faithful emulation of the Yamaha SPX90 Symphonic algorithm

cmake_minimum_required(VERSION 3.24)

project(SPX90Symphonic VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)

# Get JUCE from parent scope (should be included by parent CMakeLists.txt)
# If building standalone, uncomment the CPM section below

juce_add_plugin(${PROJECT_NAME}
    COMPANY_NAME probonopd
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    JUCE_VST3_CAN_REPLACE_VST2 FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Tap1
    PLUGIN_CODE Spx9
    FORMATS Standalone VST3
    PRODUCT_NAME "SPX90Symphonic"
    VST3_AUTO_MANIFEST FALSE
)

set_target_properties(${PROJECT_NAME}_VST3 PROPERTIES OUTPUT_NAME "SPX90Symphonic")

target_sources(${PROJECT_NAME} PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

juce_add_binary_data(SPX90SymphonicBinaryData SOURCES dummy.txt)

target_compile_definitions(${PROJECT_NAME} PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_audio_processors
    juce::juce_audio_utils
    juce::juce_core
    juce::juce_data_structures
    juce::juce_dsp
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
    SPX90SymphonicBinaryData
)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "==============================="
    COMMAND ${CMAKE_COMMAND} -E echo "SPX90 Symphonic build complete!"
    COMMAND ${CMAKE_COMMAND} -E echo "VST3 output: $<TARGET_FILE_DIR:${PROJECT_NAME}>/$<TARGET_FILE_NAME:${PROJECT_NAME}>.vst3"
    COMMAND ${CMAKE_COMMAND} -E echo "==============================="
)
