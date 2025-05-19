cmake_minimum_required(VERSION 3.10)

project(Synth_Dexed LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Gather all source files except main.cpp and main_*.cpp
file(GLOB SYNTH_DEXED_SRC
    "src/*.cpp"
)
list(REMOVE_ITEM SYNTH_DEXED_SRC
    "${CMAKE_SOURCE_DIR}/src/main.cpp"
    "${CMAKE_SOURCE_DIR}/src/main_win.cpp"
    "${CMAKE_SOURCE_DIR}/src/main_mac.cpp"
    "${CMAKE_SOURCE_DIR}/src/main_linux.cpp"
)

# Exclude dexed_pybind.cpp for standalone builds
if (NOT TARGET_PYTHON_BINDINGS)
    list(REMOVE_ITEM SYNTH_DEXED_SRC "${CMAKE_SOURCE_DIR}/src/dexed_pybind.cpp")
endif()

# Add the correct main file per platform
if (WIN32)
    set(PLATFORM_MAIN src/main_win.cpp)
elseif(APPLE)
    set(PLATFORM_MAIN src/main_mac.cpp)
else()
    set(PLATFORM_MAIN src/main_linux.cpp)
endif()

# Create the executable
add_executable(Synth_Dexed ${SYNTH_DEXED_SRC} ${PLATFORM_MAIN})

target_include_directories(Synth_Dexed PRIVATE src)

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
