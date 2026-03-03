# SpyGlass

`SpyGlass` is an SDR++ plugin that adds a focused side-panel spectrum and waterfall to SDR++.

## Screenshots

FLEX / narrow digital example:

![SpyGlass FLEX view](assets/flex.png)

FM broadcast example:

![SpyGlass FM radio view](assets/fm-radio.png)

## Features

Current behavior:

- Tracks the active SDR++ VFO, with `Radio` as the default fallback.
- Uses an independent hidden tap VFO and its own FFT pipeline, so it does not depend on the main waterfall zoom.
- Sets SpyGlass span to `3 x` the tracked VFO bandwidth.
- Draws focus-bandwidth edge markers and a center marker on both the spectrum and waterfall.
- Mirrors the main SDR++ display color map and scale behavior.
- Keeps waterfall history aligned across retunes using per-row frequency mapping.
- Left-clicking the SpyGlass spectrum or waterfall retunes the tracked VFO.

## Install

Copy `spyglass.dll` into your SDR++ `modules` folder and restart SDR++.

Windows example:

- `D:\SDR\sdrpp_windows_x64\modules\spyglass.dll`

## Build

This project builds against the SDR++ source tree for headers while linking against the installed SDR++ runtime DLLs on Windows.

This workspace uses:

```powershell
D:\Codex\sdrpp-spyglass\build-spyglass.bat
```

Expected result:

- `D:\Codex\sdrpp-spyglass\build\spyglass.dll`

## Notes

- This version is currently targeted at the local Windows SDR++ build used during development.
- The repo in this workspace includes local shim headers for `fftw3` and `volk` because the installed SDR++ package is runtime-only.
- Compatibility with other SDR++ builds is not guaranteed without rebuilding against a matching version.
