cmake_minimum_required(VERSION 3.10)

project(Synth_Dexed LANGUAGES CXX)

# Ensure C++17 is used
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Check the compiler and set specific flags if necessary
if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    # Add MSVC-specific flags
    add_compile_options(/std:c++17)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # Add GCC-specific flags
    add_compile_options(-std=c++17)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # Add Clang-specific flags
    add_compile_options(-std=c++17)
endif()

# Optimize for performance and low latency
if (CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    add_compile_options(/O2 /fp:fast /arch:AVX2 /Ob2 /DNDEBUG)
    # Optionally: add_compile_options(/GS-)
elseif (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options(-O3 -ffast-math -march=native -funroll-loops -fomit-frame-pointer -DNDEBUG)
    # Optionally: add_link_options(-flto)
endif()

# Gather all source files except main.cpp and main_*.cpp
file(GLOB SYNTH_DEXED_SRC
    "src/*.cpp"
    "src/native/midi_socket_server.cpp"
)
list(REMOVE_ITEM SYNTH_DEXED_SRC
    "${CMAKE_SOURCE_DIR}/src/native/main.cpp"
    "${CMAKE_SOURCE_DIR}/src/native/main_win.cpp"
    "${CMAKE_SOURCE_DIR}/src/native/main_mac.cpp"
    "${CMAKE_SOURCE_DIR}/src/native/main_linux.cpp"
)

# Exclude dexed_pybind.cpp for standalone builds
if (NOT TARGET_PYTHON_BINDINGS)
    list(REMOVE_ITEM SYNTH_DEXED_SRC "${CMAKE_SOURCE_DIR}/src/dexed_pybind.cpp")
endif()

# Add the correct platform-specific file(s)
if (WIN32)
    set(PLATFORM_SOURCES src/native/main_win.cpp)
elseif(APPLE)
    set(PLATFORM_SOURCES src/native/main_mac.cpp)
else()
    set(PLATFORM_SOURCES src/native/main_linux.cpp)
endif()

# Create the executable with main.cpp as the entry point
add_executable(Synth_Dexed ${SYNTH_DEXED_SRC} src/native/main.cpp src/native/main_common.cpp ${PLATFORM_SOURCES})

target_include_directories(Synth_Dexed PRIVATE src src/native)

# Link to Windows multimedia library for MIDI support
if (WIN32)
    target_link_libraries(Synth_Dexed winmm)
endif()

# Link to ALSA and pthread on Linux
if (UNIX AND NOT APPLE)
    target_link_libraries(Synth_Dexed asound pthread)
endif()

# Link to CoreAudio, AudioToolbox, AudioUnit, CoreMIDI, and CoreFoundation on macOS
if(APPLE)
    find_library(COREAUDIO_LIBRARY CoreAudio)
    find_library(AUDIOTOOLBOX_LIBRARY AudioToolbox)
    find_library(AUDIOUNIT_LIBRARY AudioUnit)
    find_library(COREMIDI_LIBRARY CoreMIDI)
    find_library(COREFOUNDATION_LIBRARY CoreFoundation)
    target_link_libraries(Synth_Dexed
        ${COREAUDIO_LIBRARY}
        ${AUDIOTOOLBOX_LIBRARY}
        ${AUDIOUNIT_LIBRARY}
        ${COREMIDI_LIBRARY}
        ${COREFOUNDATION_LIBRARY}
    )
endif()

# Optionally, add other Windows-specific settings here

# Usage:
#   mkdir build && cd build
#   cmake .. -G "Visual Studio 16 2019"   # or your preferred generator
#   cmake --build .

# Add a target for Python bindings
if (BUILD_PYTHON_BINDINGS)
    add_library(dexed_pybind MODULE src/dexed_pybind.cpp)
    target_link_libraries(dexed_pybind PRIVATE pybind11::module)
endif()

# Add a DLL target for Dexed
add_library(dexed SHARED
    src/dexed.cpp
    src/dx7note.cpp
    src/EngineMkI.cpp
    src/EngineMsfa.cpp
    src/EngineOpl.cpp
    src/env.cpp
    src/exp2.cpp
    src/fm_core.cpp
    src/fm_op_kernel.cpp
    src/freqlut.cpp
    src/lfo.cpp
    src/pitchenv.cpp
    src/porta.cpp
    src/sin.cpp
    src/native/performance_loader.cpp
    src/native/StereoDexed.cpp
)
target_compile_definitions(dexed PRIVATE SYNTH_DEXED_EXPORTS)
target_include_directories(dexed PUBLIC ${CMAKE_SOURCE_DIR}/src)
