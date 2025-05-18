cmake_minimum_required(VERSION 3.10)

project(Synth_Dexed LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Gather all source files
file(GLOB SYNTH_DEXED_SRC
    "src/*.cpp"
)

# Create the executable
add_executable(Synth_Dexed ${SYNTH_DEXED_SRC} src/main.cpp)

target_include_directories(Synth_Dexed PRIVATE src)

# Link to Windows multimedia library for MIDI support
if (WIN32)
    target_link_libraries(Synth_Dexed winmm)
endif()

# Link to ALSA and pthread on Linux
if (UNIX AND NOT APPLE)
    target_link_libraries(Synth_Dexed asound pthread)
endif()

# Optionally, add other Windows-specific settings here

# Usage:
#   mkdir build && cd build
#   cmake .. -G "Visual Studio 16 2019"   # or your preferred generator
#   cmake --build .
