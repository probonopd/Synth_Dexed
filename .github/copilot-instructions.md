
The code inside AudioPlugin is a JUCE-based audio plugin and standalone application. 

When changing anything in the AudioPlugin directory, we rebuild the VST3 plugin and standalone application with:

```bash
cd AudioPlugin && cmake --build build --config Release && build\FMRack_artefacts\Release\Standalone\FMRack.exe ; cd .. 
```

After each build DON'T forget the `cd ..`

The code outside of `AudioPlugin` must not depend on JUCE. The `AudioPlugin` directory is the only place where JUCE is used, and it should be kept separate from the rest of the codebase.
