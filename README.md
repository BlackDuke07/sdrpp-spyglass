# Spy Glass

`Spy Glass` is an SDR++ plugin that adds a focused side-panel spectrum and waterfall to SDR++.

## Screenshots

FLEX / narrow digital example:

![Spy Glass FLEX view](assets/flex.png)

FM broadcast example:

![Spy Glass FM radio view](assets/fm-radio.png)

## Features

Current behavior:

- Tracks the active SDR++ VFO, with `Radio` as the default fallback.
- Uses an independent hidden tap VFO and its own FFT pipeline, so it does not depend on the main waterfall zoom.
- Sets Spy Glass span to `3 x` the tracked VFO bandwidth.
- Draws focus-bandwidth edge markers and a center marker on both the spectrum and waterfall.
- Mirrors the main SDR++ display color map and scale behavior.
- Keeps waterfall history aligned across retunes using per-row frequency mapping.
- Left-clicking the Spy Glass spectrum or waterfall retunes the tracked VFO.

## Install

Copy `spy_glass.dll` into your SDR++ `modules` folder and restart SDR++.

Typical Windows example:

- `sdrpp_windows_x64\modules\spy_glass.dll`

## Activate In SDR++

After copying the DLL into the `modules` folder:

1. Start or restart SDR++.
2. Open the left-side menu.
3. Look for `Spy Glass` in the module list.

If it does not appear automatically:

1. Open `Module Manager`.
2. Add a new module instance.
3. Set `Name` to `Spy Glass`.
4. Set `Type` to `spy_glass`.
5. Enable the module.

Once loaded:

- Spy Glass follows the active SDR++ VFO.
- Clicking inside the Spy Glass spectrum or waterfall retunes that VFO.
- The displayed Spy Glass span is `3 x` the active VFO bandwidth.

## Build

This project is currently built against an SDR++ source checkout for headers while linking against the installed SDR++ runtime on Windows.

At a high level, the build process is:

```powershell
1. Generate import libraries for the SDR++ runtime DLLs you are targeting
2. Build `spy_glass.dll` against the matching SDR++ source tree
3. Copy the resulting DLL into your SDR++ `modules` folder
```

Expected result:

- `spy_glass.dll`

## Notes

- Tested with the precompiled `sdrpp_windows_x64` package from the SDR++ `v1.2.1 nightly` release by `AlexandreRouma/SDRPlusPlus`.
- This version is currently targeted at Windows SDR++ builds similar to that environment.
- The repo includes local shim headers for `fftw3` and `volk` because many SDR++ Windows packages are runtime-only.
- Compatibility with other SDR++ builds is not guaranteed without rebuilding against a matching version.
