# Instructions

We never edit code in `src/`, except for `src/FMRack`, which contains the source code for the command line tool.

When changing a .cpp file, don't forget to also changes the corresponding .h file if needed, and to adjust the CMakeLists.txt file if you add a new source file.
When adding a new class, we always create both a .cpp and a .h file. The .h file should contain the class declaration, and the .cpp file should contain the class implementation.

When changing anything in the `src/FMRack` directory, we rebuild the command line tool with:

```bash
cmake --build build --config Release
```


When we encounter build issues including compiler warnings, we iterate until it builds successfully without any errors or warnings.

We never want "Would you like me to proceed and implement this feature?" or similar prompts. Instead, we want the code to be complete and functional without any further user interaction. Never talk about which changes need to be made in the code, just edit the codebase directly to implement the requested feature or fix the issue.

We always use threading to ensure that the user interface remains responsive. We never block the main thread with long-running operations. If a task takes more than 100ms, it should be run in a separate thread.

The job is ONLY complete AFTER a successful build and test run, ensuring that the code is functional and does not introduce any new issues.

For the Voice Editor: Hint: You can use C:\Users\User\Development\MiniDexed_Service_Utility\src as a reference, especially C:\Users\User\Development\MiniDexed_Service_Utility\src\voice_editor_panel.py. Port that code to AudioPlugin/Source/VoiceEditorPanel.cpp and AudioPlugin/Source/VoiceEditorPanel.h.

## JUCE

The code inside AudioPlugin is a JUCE-based audio plugin and standalone application. 

When changing anything in the AudioPlugin directory, we rebuild the VST3 plugin and standalone application with:

```bash
cd AudioPlugin && cmake --build build --config Release && build\FMRack_artefacts\Release\Standalone\FMRack.exe ; cd .. 
```

After each build DON'T forget the `cd ..`

The code outside of `AudioPlugin` must not depend on JUCE. The `AudioPlugin` directory is the only place where JUCE is used, and it should be kept separate from the rest of the codebase.


Use the API documentation at https://docs.juce.com/master/index.html

### Logging and comments

Use logging and comments like this:

```cpp
    
    // Create the VoiceEditorPanel lazily when first requested
    if (!voiceEditorPanel) {
        std::cout << "[PluginEditor] Creating VoiceEditorPanel lazily" << std::endl;
        voiceEditorPanel = std::make_unique<VoiceEditorPanel>();
        std::cout << "[PluginEditor] VoiceEditorPanel created" << std::endl;
    }
```

### Peristence

In JUCE, there are several standard ways to save or persist user settings and application state:

1. **PropertiesFile (ApplicationProperties):**
   - JUCE provides the `juce::PropertiesFile` class, often accessed via `juce::ApplicationProperties`, for storing user settings, preferences, and last-used values.
   - This system stores key-value pairs in a file (XML or INI format) in a user-specific location (e.g., AppData on Windows).
   - Typical use: remembering window positions, last opened files, user preferences, etc.
   - Example: `appProperties.getUserSettings()->setValue("lastVoiceDir", dir.getFullPathName());`

2. **ValueTree State (getStateInformation/setStateInformation):**
   - For plugins, JUCE uses a `juce::ValueTree` to serialize/deserialize plugin state.
   - The state is saved/restored by the host (DAW) using `getStateInformation` and `setStateInformation`.
   - This is for plugin parameters, not general app settings.

3. **Manual File I/O:**
   - You can use JUCE’s file classes (`juce::File`, `juce::FileOutputStream`, etc.) to read/write custom files (e.g., .ini, .json, .xml).
   - This is used for things like saving patches, exporting/importing data, or custom config files.

**Summary:**  
- Use `PropertiesFile` for user preferences and last-used paths.
- Use plugin state (ValueTree) for plugin parameters.
- Use manual file I/O for custom data formats, such as MiniDexed Performance files in .ini format.

### DO

- **DO use `std::vector` for dynamic arrays**: It automatically manages memory and resizes as needed.
- **DO use `std::string` for character data**: It simplifies string manipulation and avoids manual memory management.
- **DO utilize member functions like `at()`**: This provides bounds checking and safer access to elements.
- **DO prefer STL containers**: They reduce the complexity of memory management and improve code safety.
- **DO take advantage of automatic memory management**: This helps prevent memory leaks and dangling pointers.

### DON'T

- **DON'T use raw pointers for dynamic memory**: This increases the risk of memory leaks and requires manual management.
- **DON'T perform pointer arithmetic**: Use the subscript operator or member functions for accessing elements in containers.
- **DON'T forget to deallocate memory when using raw pointers**: This can lead to memory leaks if not handled properly.
- **DON'T use fixed-size arrays when flexibility is needed**: Opt for `std::vector` to handle varying sizes efficiently.
- **DON'T ignore the benefits of STL containers**: They provide safer and more efficient alternatives to traditional data structures.