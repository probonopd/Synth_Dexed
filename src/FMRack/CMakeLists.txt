cmake_minimum_required(VERSION 3.15)
project(FMRack)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Include the parent Dexed source directory
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/..)

# Source files - include all modular class files
set(FMRACK_SOURCES
    main.cpp
    Rack.cpp
    Performance.cpp
    Reverb.cpp
    Module.cpp
)

set(FMRACK_HEADERS
    Rack.h
    Performance.h
    Reverb.h
    Module.h
)

# Add Dexed source files that are needed
set(DEXED_SOURCES
    ../dexed.cpp
    ../dx7note.cpp
    ../EngineMkI.cpp
    ../EngineMsfa.cpp
    ../EngineOpl.cpp
    ../env.cpp
    ../exp2.cpp
    ../fm_core.cpp
    ../fm_op_kernel.cpp
    ../freqlut.cpp
    ../lfo.cpp
    ../pitchenv.cpp
    ../porta.cpp
    ../sin.cpp
)

# Create the executable
add_executable(FMRack ${FMRACK_SOURCES} ${FMRACK_HEADERS} ${DEXED_SOURCES})

# Platform-specific configurations
if(WIN32)
    target_link_libraries(FMRack winmm)
    target_compile_definitions(FMRack PRIVATE _WIN32_WINNT=0x0601)
elseif(APPLE)
    find_library(COREAUDIO_FRAMEWORK CoreAudio)
    find_library(COREMIDI_FRAMEWORK CoreMIDI)
    find_library(AUDIOUNIT_FRAMEWORK AudioUnit)
    find_library(COREFDN_FRAMEWORK CoreFoundation)
    find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox)
    target_link_libraries(FMRack ${COREAUDIO_FRAMEWORK} ${COREMIDI_FRAMEWORK} ${AUDIOUNIT_FRAMEWORK} ${COREFDN_FRAMEWORK} ${AUDIOTOOLBOX_FRAMEWORK})
elseif(UNIX)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ALSA REQUIRED alsa)
    target_link_libraries(FMRack ${ALSA_LIBRARIES} pthread)
    target_include_directories(FMRack PRIVATE ${ALSA_INCLUDE_DIRS})
endif()

# Compiler-specific options
if(MSVC)
    target_compile_options(FMRack PRIVATE /W4)
    # Disable specific warnings for Windows API
    target_compile_options(FMRack PRIVATE /wd4996)
else()
    target_compile_options(FMRack PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Debug/Release configurations
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(FMRack PRIVATE DEBUG)
else()
    target_compile_definitions(FMRack PRIVATE NDEBUG)
    if(NOT MSVC)
        target_compile_options(FMRack PRIVATE -O3)
    endif()
endif()

# Set output directory
set_target_properties(FMRack PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Add math library for sin/cos functions
if(UNIX AND NOT APPLE)
    target_link_libraries(FMRack m)
endif()
