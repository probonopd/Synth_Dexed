# CMake build for Dexed VST3 plugin
cmake_minimum_required(VERSION 3.15)
project(DexedVST3 VERSION 1.0.0)

set(VST3_SDK_PATH "${CMAKE_CURRENT_LIST_DIR}/vst3sdk")
add_subdirectory(${VST3_SDK_PATH} ${CMAKE_CURRENT_BINARY_DIR}/vst3sdk_build EXCLUDE_FROM_ALL)

set(DEXED_SRC
    ${CMAKE_CURRENT_LIST_DIR}/../../../src/dexed.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../../src/dx7note.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../../src/EngineMkI.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../../src/EngineMsfa.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../../src/EngineOpl.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../../src/env.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../../src/exp2.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../../src/fm_core.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../../src/fm_op_kernel.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../../src/freqlut.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../../src/lfo.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../../src/pitchenv.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../../src/porta.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../../src/sin.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../../../src/synth_dexed.cpp
)

set(VST3_PLUGIN_SRC
    ${CMAKE_CURRENT_LIST_DIR}/dexed_vst3.cpp
)

# Debug: Print source file variables
message(STATUS "DEXED_SRC: ${DEXED_SRC}")
message(STATUS "VST3_PLUGIN_SRC: ${VST3_PLUGIN_SRC}")

smtg_add_vst3plugin(DexedVST3
    ${DEXED_SRC}
    ${VST3_PLUGIN_SRC}
)

target_include_directories(DexedVST3 PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../../src
    ${VST3_SDK_PATH}
)

target_compile_features(DexedVST3 PRIVATE cxx_std_17)

# Link against VST3 SDK library, similar to SDK examples
target_link_libraries(DexedVST3 PRIVATE sdk)
