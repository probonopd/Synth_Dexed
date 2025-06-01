We never edit code in `src/`, except for `src/FMRack`, which contains the source code for the command line tool.

When changing anything in the `src/FMRack` directory, we rebuild the command line tool with:

```bash
cmake --build build --config Release
```

When changing anything in the AudioPlugin directory, we rebuild the VST3 plugin and standalone application with:

```bash
( cd AudioPlugin && cmake --build build --config Release && build\FMRack_artefacts\Release\Standalone\FMRack.exe )
```

The code outside of `AudioPlugin` must not depend on JUCE. The `AudioPlugin` directory is the only place where JUCE is used, and it should be kept separate from the rest of the codebase.

When we encounter build issues including compiler warnings, we iterate until it builds successfully without any errors or warnings.

We never want "Would you like me to proceed and implementWould you like me to proceed and implement this feature?" prompts in the code. Instead, we want the code to be complete and functional without any further user interaction.