# Based on https://github.com/TheAudioProgrammer/JuceAudioPluginTemplate/blob/main/CMakeLists.txt

cmake_minimum_required(VERSION 3.24)

project(FMRack VERSION 0.0.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_XCODE_GENERATE_SCHEME OFF)
set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# We're going to use CPM as our package manager to bring in JUCE
# Check to see if we have CPM installed already.  Bring it in if we don't.
set(CPM_DOWNLOAD_VERSION 0.34.0)
set(CPM_DOWNLOAD_LOCATION "${CMAKE_BINARY_DIR}/cmake/CPM_${CPM_DOWNLOAD_VERSION}.cmake")

if (NOT EXISTS ${CPM_DOWNLOAD_LOCATION})
    message(STATUS "Downloading CPM.cmake to ${CPM_DOWNLOAD_LOCATION}")
    file(DOWNLOAD https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake ${CPM_DOWNLOAD_LOCATION})
endif ()

include(${CPM_DOWNLOAD_LOCATION})

# Bring in JUCE locally
CPMAddPackage(
    NAME juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    # GIT TAG origin/master
    GIT_TAG 8.0.7
)

juce_add_plugin(${PROJECT_NAME}
    COMPANY_NAME probonopd
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    JUCE_VST3_CAN_REPLACE_VST2 FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    PLUGIN_MANUFACTURER_CODE Tap1
    PLUGIN_CODE Reg0
    FORMATS Standalone VST3
    PRODUCT_NAME "${PROJECT_NAME}"
    STANDALONE_BUFFER_SIZE 512
)

# Ensure the VST3 output is named FMRack.vst3
set_target_properties(${PROJECT_NAME}_VST3 PROPERTIES OUTPUT_NAME "FMRack")

set(SourceFiles
    Source/PluginEditor.cpp
    Source/PluginProcessor.cpp
    Source/RackAccordionComponent.cpp
    Source/FileBrowserDialog.cpp
    Source/VoiceEditorPanel.cpp
    Source/VoiceEditorWindow.cpp
    Source/EnvelopeDisplay.cpp
    Source/KeyboardScalingDisplay.cpp
    ../src/dexed.cpp
    ../src/dx7note.cpp
    ../src/EngineMkI.cpp
    ../src/EngineMsfa.cpp
    ../src/EngineOpl.cpp
    ../src/env.cpp
    ../src/exp2.cpp
    ../src/fm_core.cpp
    ../src/fm_op_kernel.cpp
    ../src/freqlut.cpp
    ../src/lfo.cpp
    ../src/pitchenv.cpp
    ../src/porta.cpp
    ../src/sin.cpp
    ../src/FMRack/Module.cpp
    ../src/FMRack/Performance.cpp
    ../src/FMRack/Rack.cpp
    ../src/FMRack/VoiceData.cpp
    ../src/FMRack/AudioEffectPlateReverb.cpp
    ../src/FMRack/FileRenderer.cpp
    ../src/FMRack/UdpServer.cpp
)

target_include_directories(${PROJECT_NAME} PRIVATE ../src ../src/FMRack)
target_sources(${PROJECT_NAME} PRIVATE ${SourceFiles})

target_compile_definitions(${PROJECT_NAME} PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    SYNTH_DEXED_STATIC
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    juce::juce_analytics
    juce::juce_audio_basics
    juce::juce_audio_devices
    juce::juce_core
    juce::juce_data_structures
    juce::juce_graphics
    juce::juce_gui_basics
    juce::juce_gui_extra
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
    BinaryData
)

# Add all SVGs in Images/ as resources for JUCE BinaryData
file(GLOB ALGORITHM_SVGS "${CMAKE_CURRENT_SOURCE_DIR}/Images/algorithm-*.svg")

juce_add_binary_data(BinaryData SOURCES ${ALGORITHM_SVGS})

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "==============================="
    COMMAND ${CMAKE_COMMAND} -E echo "Build complete!"
    COMMAND ${CMAKE_COMMAND} -E echo "VST3 output: $<TARGET_FILE_DIR:${PROJECT_NAME}>/$<TARGET_FILE_NAME:${PROJECT_NAME}>.vst3"
    COMMAND ${CMAKE_COMMAND} -E echo ""
    COMMAND ${CMAKE_COMMAND} -E echo "Manual install instructions:"
    COMMAND ${CMAKE_COMMAND} -E echo "  Windows:   C:/Program Files/Common Files/VST3"
    COMMAND ${CMAKE_COMMAND} -E echo "  macOS:     ~/Library/Audio/Plug-Ins/VST3"
    COMMAND ${CMAKE_COMMAND} -E echo "  Linux:     ~/.vst3"
    COMMAND ${CMAKE_COMMAND} -E echo "==============================="
)