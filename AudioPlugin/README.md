# JUCE

This directory contains a VST3 plugin and a standalone application using `FMRack`, built using the JUCE framework.

## Installation

To install the VST3 plugin, copy it to the appropriate directory:

* Windows: `C:/Program Files/Common Files/VST3`
* macOS: `~/Library/Audio/Plug-Ins/VST3`
* Linux: `~/.vst3`

## Developer Notes

For a seamless development experience with JUCE, follow these guidelines:

1. Even before starting with JUCE, have a standalone binary of the synthesizer ready.
   This is not only useful for embedded devices (hardware synthesizers), but also for debugging and development.
   Most crucially, this ensures a clear separation between the audio engine and the JUCE specific code.
2. Never bother to use Projucer to create a new project. It is a waste of time.
3. Always use CMake to build the project.
4. Let CMake handle downloading and installing JUCE.
5. To get started, you really only need 4 files from the JUCE examples:
   - `AudioProcessor.h`
   - `AudioProcessor.cpp`
   - `PluginEditor.h`
   - `PluginEditor.cpp`

https://github.com/TheAudioProgrammer/JuceAudioPluginTemplate/ was a big help to get started.
This should be how JUCE projects are created by default.