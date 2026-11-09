# Davidian Canary Plus

A Davidian circuit model of an op-amp gain stage driving a diode clipper, with a native GUI. It comes in three plugin formats that share the same circuit model and panel:

- **LV2** for Windows, macOS and Linux (mono)
- **VST3** for Windows (mono, or stereo as two independent circuits)
- **Audio Unit** for macOS (mono, or stereo as two independent circuits)

The name, panel and lettering are original, with no branding from any commercial pedal.

## Download

Ready-made bundles are on the [Releases page](../../releases/latest):

| Download | Format | For |
|---|---|---|
| `canary-plus-<version>-windows-x64-vst3.zip` | VST3 | Windows 10 and 11, 64-bit |
| `canary-plus-<version>-windows-x64-lv2.zip` | LV2 | Windows 10 and 11, 64-bit |
| `canary-plus-<version>-macos-universal-au.zip` | Audio Unit | macOS 11 or later, Intel and Apple Silicon |
| `canary-plus-<version>-macos-universal-lv2.zip` | LV2 | macOS 10.13 or later, Intel and Apple Silicon |
| `canary-plus-<version>-linux-x86_64-lv2.zip` | LV2 | Linux on 64-bit Intel and AMD (glibc 2.35 or later) |
| `canary-plus-<version>-linux-arm64-lv2.zip` | LV2 | Linux on 64-bit ARM (glibc 2.35 or later) |

Each zip holds one plugin bundle:

- `canary-plus.lv2`: the plugin and GUI libraries, the two `.ttl` files, `LICENSE.txt` and `AI-DISCLOSURE.txt`
- `canary-plus.vst3`: the plugin in `Contents/x86_64-win`. `Contents/Resources` holds the licences, `AI-DISCLOSURE.txt` and `moduleinfo.json`
- `canary-plus.component`: the plugin in `Contents/MacOS`. `Contents/Resources` holds the licences and `AI-DISCLOSURE.txt`

GitHub Actions builds every release from its tagged source and runs all the tests below on each platform first. This repository is the source code for those builds, as the GPL requires. If you pass a zip on, pass on a link to this repository with it.

## Build and test

Any platform, with CMake and a C compiler:

```
cmake -B out -DCMAKE_BUILD_TYPE=Release
cmake --build out --config Release
ctest --test-dir out -C Release --output-on-failure
```

The LV2 bundle lands in `out/lv2/canary-plus.lv2`. On Windows and macOS the build also makes that platform's own format, unless you turn it off:

- **VST3 (Windows):** `out/vst3/canary-plus.vst3`. It needs CMake 3.25 or later and a C++17 compiler, and downloads Steinberg's VST3 SDK while configuring. Add `-DVST3=OFF` to skip it.
- **Audio Unit (macOS):** `out/au/canary-plus.component`. It needs Xcode 16 or later (for C++23), and downloads Apple's AudioUnitSDK while configuring. Add `-DAU=OFF` to skip it.
- **SDKs you already have:** to build from them instead of downloading, add `-DFETCHCONTENT_SOURCE_DIR_VST3SDK=<path>` or `-DFETCHCONTENT_SOURCE_DIR_AUDIOUNITSDK=<path>`.

Platform notes:

- **Linux:** the GUI needs the X11 development files (`libx11-dev` on Debian and Ubuntu). Without them, the build leaves the GUI out. The GUI test needs a display, so on a machine without one run `ctest` under `xvfb-run -s "-screen 0 1280x1024x24"`.
- **macOS:** the GUI is Objective-C, so it needs Xcode or the Command Line Tools. Add `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` for one build that runs on both Intel and Apple Silicon.
- **lilv check:** add `-DLILV_CHECK=ON` to also check the bundle with the lilv LV2 parser, which most LV2 hosts use. It needs lilv installed: `liblilv-0-0` on Debian and Ubuntu, `brew install lilv` on macOS, or Ardour 9 on Windows.

The tests are:

- `test_dsp` loads the LV2 plugin library and checks its sound, latency, bypass, robustness and CPU use.
- `ui_check` hosts the LV2 GUI in a window, drives it with mouse events, and compares what the window shows with a reference render.
- `ui_render` draws the panel to an image without opening a window.
- `vst3_validator` (Windows) runs Steinberg's VST3 validator on the bundle.
- `vst3_check` (Windows) and `au_check` (macOS) load the plugin as a host would. They check the sound in mono and stereo, the latency, bypass and saved settings. They also open the editor in a window, click and drag its controls, and check that it follows changes made by the host.
- CI also runs Apple's `auval` on the installed Audio Unit, on Apple Silicon and on Intel (under Rosetta).

On Windows, `build.bat` builds the LV2 bundle without CMake, using Visual Studio's C++ tools. It puts the bundle in `build\` and the test programs in `build-test\`:

```
build.bat
build-test\test_dsp.exe build\canary-plus.lv2
build-test\ui_check.exe build\canary-plus.lv2
```

## Install

Close the host before copying and restart it afterwards. Replace any older copy instead of keeping two.

**VST3 (Windows):** copy the whole `canary-plus.vst3` folder into `C:\Program Files\Common Files\VST3`. Look for **Canary Plus** by **Davidian** among your host's distortion effects.

**Audio Unit (macOS):** copy `canary-plus.component` into `~/Library/Audio/Plug-Ins/Components` (current user) or `/Library/Audio/Plug-Ins/Components` (all users). It appears as **Davidian: Canary Plus**. The bundle isn't signed with an Apple Developer ID, so macOS blocks it when it comes from a downloaded zip. After copying, clear the download flag once in Terminal:

```
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/canary-plus.component
```

Logic Pro checks new Audio Units when it starts. If it lists the plugin as failed, reset and rescan it in Logic's Plug-in Manager.

**LV2:** copy the whole `canary-plus.lv2` folder into one of the standard LV2 folders:

- Windows: `%APPDATA%\LV2` (current user) or `%COMMONPROGRAMFILES%\LV2` (all users)
- Linux: `~/.lv2` (current user) or `/usr/local/lib/lv2` (all users)
- macOS: `~/Library/Audio/Plug-Ins/LV2`

If your host doesn't scan that folder, add it to the host's LV2 search path or set the `LV2_PATH` environment variable. When two LV2 bundles have the same URI, the loader uses one and ignores the other. Look for **Davidian Canary Plus** in your host's plugin list. The plugin URI is `urn:davidian:canary-plus`.

On Windows, `install.bat` copies the LV2 bundle to `%APPDATA%\LV2` for you. On macOS, clear the download flag for the LV2 bundle too:

```
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/LV2/canary-plus.lv2
```

## GUI

![GUI](docs/gui.png)

The GUI uses the Davidian house style (`src/ui/style.h`, `font.h` and `canvas.h`). It has no dependencies beyond the operating system: it draws with its own software renderer and stroke font, then shows the result in a native window. That's Win32 on Windows, Cocoa on macOS and X11 on Linux, the same in every plugin format. Each window layer is automatically tested on its own platform. So far only the Windows LV2 GUI has been tried by hand in a host (Ardour 9).

- Drag a knob up or down to turn it. Hold Shift for fine control. Double-click resets it, and the mouse wheel works too.
- Click a diode row to select it, or use the mouse wheel over the list.
- Click the footswitch to engage or bypass. Its red LED shows the state. In LV2 it drives the host bypass port (`enabled`), and in VST3 the host's bypass parameter.
- The amber CLIP lamp shows how hard the diodes are clipping. It stays dark for a clean signal, even with germanium diodes.
- The readout shows the last control you touched. Distortion also shows the gain stage's mid-band gain, and Output shows its level in dB.
- VST3 and Audio Unit hosts get each knob drag as a single edit, so automation recording works.

The panel name, lettering and layout are original, with none of the original pedal's branding or artwork.

## Controls

| Control | Range | Default | Notes |
|---|---|---|---|
| Distortion | 0–1 | 0.5 | 1M pot; sets gain (6–46.6 dB) and low cut together. VST3 hosts show it as 0–10, like the knob |
| Output | 0–1 | 0.8 | 10k volume pot. VST3 hosts show it as 0–10 |
| Diodes | 5 choices | 1N270 | The schematic's alternatives |
| Volts at 0 dBFS | 0.1–10 | 1.0 | Voltage a full-scale digital signal represents. Higher drives the circuit harder and comes out quieter; lower does the reverse |
| Enabled (LV2), Engage (AU), Bypass (VST3) | on/off | engaged | Crossfaded bypass, delayed to match the effect. VST3's Bypass is on when the pedal is off |
| Clip (output) | 0–1 | | Diode clipping amount for the GUI lamp, with a 150 ms release |

Latency is 32 samples, reported to the host, at every sample rate.

## Code layout

| File | Contents |
|---|---|
| `src/ports.h` | Port numbers and knob curves, shared by the plugin and GUI |
| `src/input_hp.h` | Stage 1: input coupling |
| `src/gain_stage.h` | Stage 2: 741 gain stage |
| `src/clipper.h` | Stages 3–4: output coupling, diodes, diode values, clip amount |
| `src/oversampler.h` | Oversampling filters |
| `src/plugin.c` | LV2 entry points, controls, bypass. The VST3 and Audio Unit run this same code |
| `src/denormal.h` | Flush-to-zero for x86 and ARM processors |
| `src/params.h` | The parameters as VST3 and Audio Unit hosts see them |
| `src/ui/panel.h` | GUI layout, drawing and mouse handling |
| `src/ui/window.h` | Win32, Cocoa and X11 window layers, shared by all three formats |
| `src/ui/ui_lv2.c` | LV2 GUI entry points |
| `src/ui/editor.h`, `editor.c` | The panel as an embeddable editor, for VST3 and Audio Unit |
| `src/ui/style.h`, `font.h`, `canvas.h` | Davidian house style: palette, stroke font, renderer |
| `src/vst3/` | VST3 version: plugin, editor, build file, Windows file details |
| `src/au/` | Audio Unit version: unit, Cocoa view, `Info.plist` template, build file |
| `bundle/` | LV2 metadata, `AI-DISCLOSURE.txt` and `LICENSE.txt` |
| `LICENSE` | GPL-3.0 licence text |
| `src/version.rc`, `src/ui/version.rc` | Windows file details for the LV2 DLLs, including the AI disclosure |
| `CMakeLists.txt` | Build for all platforms |
| `build.bat`, `install.bat` | Windows LV2 build and install without CMake |
| `.github/workflows/build.yml` | CI: builds and tests on every platform, publishes releases from version tags |
| `test/platform.h` | Library loading and timing for the tests on each platform |
| `test/test_dsp.c` | Tests that load the built LV2 plugin library |
| `test/ui_check.c`, `test/ui_render.c` | GUI tests |
| `test/lilv_check.c` | Checks the metadata with the lilv LV2 parser |
| `test/vst3_check.cpp`, `test/au_check.m` | Host-style tests of the VST3 and Audio Unit |

## Values not from the schematic

These are estimates to tune by ear or against a real pedal:

- Diode `Is` and `n` (`src/clipper.h`)
- 741 gain-bandwidth, slew rate and output swing (`src/gain_stage.h`)
- Pot taper (`TAPER_BASE` in `src/ports.h`)
- The 5 Hz output DC blocker (`src/plugin.c`), which stands in for the next device's input capacitor

## AI disclosure

This plugin was written with AI assistance. Claude (Anthropic) generated the DSP code, GUI, plugin wrappers, tests, build scripts and documentation. The author directed the work and tested it by ear in Ardour. Every build carries this disclosure:

- `AI-DISCLOSURE.txt` in every bundle
- The plugin description in `canary-plus.ttl`, which LV2 hosts may show
- On Windows, the Comments field of the LV2 DLLs and the VST3 (right-click > Properties > Details)
- The message the CMake build prints, and each release's notes

## Licensing

This project is free software under the **GNU General Public License, version 3 or later** (GPL-3.0-or-later). The full text is in `LICENSE`, and every bundle carries a copy as `LICENSE.txt`.

- **Where it's declared:** each source file starts with an SPDX line. The plugin's `.ttl` declares the licence with `doap:license`, and the Comments field of the Windows binaries names it too.
- **If you share a bundle:** include its licence files and make the matching source code available as well. A link to the source, or a zip of this folder next to the bundle, is enough.
- **No copyright notices:** this project's files carry no copyright lines, at the author's choice. The GPL only asks for them as a recommendation. The licence text itself keeps the Free Software Foundation's line, because the GPL allows only verbatim copies of itself. The third-party licence files below keep their authors' notices, as their licences require.
- **LV2:** contains no third-party code. `src/lv2_abi.h` declares the LV2 interface by hand from the public specification, so the ISC-licensed LV2 headers aren't copied.
- **VST3:** includes code from Steinberg's VST3 SDK, under the MIT licence. The bundle carries that licence as `VST3-SDK-LICENSE.txt`. The SDK isn't stored in this repository; the build downloads it. VST is a trademark of Steinberg Media Technologies GmbH.
- **Audio Unit:** includes code from Apple's AudioUnitSDK, under the Apache License 2.0. The bundle carries that licence as `AudioUnitSDK-LICENSE.txt`. The SDK isn't stored in this repository; the build downloads it. Both licences are compatible with the GPL version 3.
- **System libraries:** the binaries otherwise link only the operating system's own libraries: the Windows system DLLs and the statically linked Microsoft C and C++ runtime, which Visual Studio's licence lets you redistribute in a compiled program; Cocoa and the audio frameworks on macOS; and the C library and libX11 on Linux.
- **Test tools:** `test/lilv_check.c` loads the lilv library (ISC licence) while it runs, and the VST3 tests use the SDK's validator and hosting code. They're local test tools and aren't part of any bundle.
- **Reference images:** `schematic.gif` and `doubler_layout.png` are other people's drawings, kept here for reference only. Neither is part of a bundle, so don't redistribute them with one.
- **AI-generated code:** in some places, including the US, material generated by AI without enough human creative input may not be protected by copyright. Where that applies, the GPL covers the parts that are protected, and anyone can use the rest anyway.
