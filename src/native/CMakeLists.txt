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

# Gather all source files except main.cpp and main_*.cpp
file(GLOB SYNTH_DEXED_SRC
    "../*.cpp"
)
list(REMOVE_ITEM SYNTH_DEXED_SRC
    "${CMAKE_CURRENT_LIST_DIR}/main.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/main_win.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/main_mac.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/main_linux.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/main_common.cpp"
)

# Exclude dexed_pybind.cpp for standalone builds
if (NOT TARGET_PYTHON_BINDINGS)
    list(REMOVE_ITEM SYNTH_DEXED_SRC "${CMAKE_CURRENT_LIST_DIR}/../dexed_pybind.cpp")
endif()

# Add the C API wrapper source file
list(APPEND SYNTH_DEXED_SRC "${CMAKE_CURRENT_LIST_DIR}/synth_dexed_c_api.cpp")
# Add platform and shared code for DLL
list(APPEND SYNTH_DEXED_SRC "${CMAKE_CURRENT_LIST_DIR}/main_common.cpp")

# Only add main_win.cpp for Windows builds
if (WIN32)
    list(APPEND SYNTH_DEXED_SRC "${CMAKE_CURRENT_LIST_DIR}/main_win.cpp")
endif()

# Add the TCP MIDI socket server source file
list(APPEND SYNTH_DEXED_SRC "${CMAKE_CURRENT_LIST_DIR}/midi_socket_server.cpp")

# Always build Synth_Dexed as a shared library
add_library(Synth_Dexed SHARED ${SYNTH_DEXED_SRC})

# Add the correct platform-specific file(s)
if (WIN32)
    set(PLATFORM_SOURCES main_win.cpp)
elseif(APPLE)
    set(PLATFORM_SOURCES main_mac.cpp)
else()
    set(PLATFORM_SOURCES main_linux.cpp)
endif()

# Create the executable with main.cpp as the entry point, linking to the Synth_Dexed library
add_executable(Synth_Dexed_exe ../main.cpp main_common.cpp midi_socket_server.cpp ${PLATFORM_SOURCES})
target_link_libraries(Synth_Dexed_exe PRIVATE Synth_Dexed)

if (WIN32)
    target_link_libraries(Synth_Dexed PRIVATE winmm)
    target_link_libraries(Synth_Dexed_exe PRIVATE winmm)
endif()

target_include_directories(Synth_Dexed PRIVATE ../ ../native)
target_include_directories(Synth_Dexed_exe PRIVATE ../ ../native)

# Link to ALSA and pthread on Linux
if (UNIX AND NOT APPLE)
    target_link_libraries(Synth_Dexed_exe PRIVATE asound pthread)
endif()

# Link to CoreAudio, AudioToolbox, AudioUnit, CoreMIDI, and CoreFoundation on macOS
if(APPLE)
    find_library(COREAUDIO_LIBRARY CoreAudio)
    find_library(AUDIOTOOLBOX_LIBRARY AudioToolbox)
    find_library(AUDIOUNIT_LIBRARY AudioUnit)
    find_library(COREMIDI_LIBRARY CoreMIDI)
    find_library(COREFOUNDATION_LIBRARY CoreFoundation)
    target_link_libraries(Synth_Dexed_exe PRIVATE
        ${COREAUDIO_LIBRARY}
        ${AUDIOTOOLBOX_LIBRARY}
        ${AUDIOUNIT_LIBRARY}
        ${COREMIDI_LIBRARY}
        ${COREFOUNDATION_LIBRARY}
    )
endif()

# Set output directories for DLL, LIB, and EXE to ensure MSVC finds the import library
set_target_properties(Synth_Dexed PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
)
set_target_properties(Synth_Dexed_exe PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
)

# On macOS, ensure the shared library has the correct install_name for runtime linking
if(APPLE)
    set_target_properties(Synth_Dexed PROPERTIES
        INSTALL_NAME_DIR "@rpath"
        BUILD_WITH_INSTALL_RPATH TRUE
        MACOSX_RPATH TRUE
    )
endif()

# Add a post-build command to strip symbols from the shared library and executable (if not on Windows)
if (UNIX AND NOT APPLE)
    add_custom_command(TARGET Synth_Dexed POST_BUILD
        COMMAND ${CMAKE_STRIP} $<TARGET_FILE:Synth_Dexed>
        COMMENT "Stripping symbols from Synth_Dexed.so"
    )
    add_custom_command(TARGET Synth_Dexed_exe POST_BUILD
        COMMAND ${CMAKE_STRIP} $<TARGET_FILE:Synth_Dexed_exe>
        COMMENT "Stripping symbols from Synth_Dexed_exe"
    )
endif()

# On Windows, copy the DLL next to the .exe after build for easy running
if (WIN32)
    add_custom_command(TARGET Synth_Dexed_exe POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:Synth_Dexed>
            $<TARGET_FILE_DIR:Synth_Dexed_exe>
        COMMENT "Copying Synth_Dexed.dll next to Synth_Dexed_exe.exe"
    )
endif()

# Optionally, add other Windows-specific settings here

# Usage:
#   cd src/native
#   mkdir build && cd build
#   cmake .. -G "Visual Studio 16 2019"   # or your preferred generator
#   cmake --build .
#   # Executable will be Synth_Dexed_exe

# Add a target for Python bindings
if (BUILD_PYTHON_BINDINGS)
    add_library(dexed_pybind MODULE src/dexed_pybind.cpp)
    target_link_libraries(dexed_pybind PRIVATE pybind11::module)
endif()

target_compile_definitions(Synth_Dexed PRIVATE SYNTH_DEXED_EXPORTS)
