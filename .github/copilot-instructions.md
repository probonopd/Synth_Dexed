We never edit code in `src/`, except for `src/FMRack`, which contains the source code for the command line tool.

When changing anything in the `src/FMRack` directory, we rebuild the command line tool with:

```bash
cmake --build build --config Release
```

When changing anything in the AudioPlugin directory, we rebuild the VST3 plugin and standalone application with:

```bash
cd AudioPlugin && cmake --build build --config Release && build\FMRack_artefacts\Release\Standalone\FMRack.exe ; cd .. 
```

After each build DON'T forget the `cd ..`

The code outside of `AudioPlugin` must not depend on JUCE. The `AudioPlugin` directory is the only place where JUCE is used, and it should be kept separate from the rest of the codebase.

When we encounter build issues including compiler warnings, we iterate until it builds successfully without any errors or warnings.

We never want "Would you like me to proceed and implement this feature?" or similar prompts. Instead, we want the code to be complete and functional without any further user interaction. Never talk about which changes need to be made in the code, just edit the codebase directly to implement the requested feature or fix the issue.

We always use threading to ensure that the user interface remains responsive. We never block the main thread with long-running operations. If a task takes more than 100ms, it should be run in a separate thread.

The job is ONLY complete AFTER a successful build and test run, ensuring that the code is functional and does not introduce any new issues.

For the Voice Editor: Hint: You can use C:\Users\User\Development\MiniDexed_Service_Utility\src as a reference, especially C:\Users\User\Development\MiniDexed_Service_Utility\src\voice_editor_panel.py. Port that code to AudioPlugin/Source/VoiceEditorPanel.cpp and AudioPlugin/Source/VoiceEditorPanel.h.