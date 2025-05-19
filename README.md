# Synth_Dexed

Synth_Dexed is a port of the [Dexed sound engine](https://github.com/asb2m10/dexed) as library for the Teensy-3.5/3.6/4.x with an audio shield, and for Windows, Linux, and macOS (WIP; see below). Dexed is heavily based on https://github.com/google/music-synthesizer-for-android. Synth_Dexed is SysEx compatible with a famous 6-OP FM synth and is used in [MicroDexed](https://codeberg.org/dcoredump/MicroDexed) and [MiniDexed](https://github.com/probonopd/MiniDexed).

## Mirrored branches

Mirrored from https://codeberg.org/dcoredump/Synth_Dexed.

See the `master` and `dev` branches.

## Native branch [![Teensy](https://github.com/probonopd/Synth_Dexed/actions/workflows/teensy.yml/badge.svg?branch=native)](https://github.com/probonopd/Synth_Dexed/actions/workflows/teensy.yml) [![Build Executables](https://github.com/probonopd/Synth_Dexed/actions/workflows/build.yml/badge.svg?branch=native)](https://github.com/probonopd/Synth_Dexed/actions/workflows/build.yml)


The branch `native` is original to this repository, in the hope that it can eventually be merged upstream. It contains the following:

- [x] Synth_Dexed port to Windows (standalone executable)
- [ ] Synth_Dexed port to Linux (standalone executable)
- [ ] Synth_Dexed port to macOS (standalone executable)
- [x] Synth_Dexed Python bindings for Windows
- [ ] Synth_Dexed Python bindings for Linux
- [ ] Synth_Dexed Python bindings for macOS
- [x] Unit tests for Python bindings
- [x] Automated builds on GitHub Actions for Teensy 3.6, Teensy 4.0, and Teensy 4.1
- [x] Automated builds on GitHub Actions for Windows
- [ ] Automated builds on GitHub Actions for Linux
- [ ] Automated builds on GitHub Actions for macOS
