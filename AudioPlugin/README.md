# JUCE

## Developer Notes

For a seamless development experience with JUCE, follow these guidelines:

1. Never bother to use Projucer to create a new project. It is a waste of time.
2. Always use CMake to build the project.
3. Let CMake handle downloading and installing JUCE.
4. To get started, you really only need 4 files from the JUCE examples:
   - `AudioProcessor.h`
   - `AudioProcessor.cpp`
   - `PluginEditor.h`
   - `PluginEditor.cpp`

https://github.com/TheAudioProgrammer/JuceAudioPluginTemplate/ was a big help to get started.
This should be how JUCE projects are created by default.