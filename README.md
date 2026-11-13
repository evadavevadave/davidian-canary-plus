# Davidian Canary Plus

A circuit-modelled distortion pedal plugin (LV2, VST3 and Audio Unit), modelled on a classic 1970s op-amp distortion pedal: a 741 gain stage whose Distortion pot sets gain and low cut together, driving a pair of clipping diodes, with a choice of the diodes the schematic offers.

**A close model, not an exact copy.** It is a simulation of the circuit from its component values, not a sampled or measured pedal, and some details are reasoned estimates (listed under [Estimates](#estimates)). It has not been compared side by side with a built unit, so expect it to sound close to the hardware rather than identical.

![The GUI](docs/gui.png)

A mono effect; the VST3 and Audio Unit versions also run stereo as two independent circuits. The same GUI in every format. GPL-3.0-or-later.

| Format | Windows | macOS | Linux |
|---|---|---|---|
| LV2 | yes | yes | yes |
| VST3 | yes | | |
| Audio Unit (AUv2) | | yes | |

## Download

Ready-made builds are on the [Releases page](https://github.com/evadavevadave/davidian-canary-plus/releases). Each zip holds one plugin bundle:

| Download | Contains | For |
|---|---|---|
| `…-windows-x64-vst3.zip` | VST3 | Windows 10/11, 64-bit hosts |
| `…-windows-x64-lv2.zip` | LV2 | Windows 10/11, 64-bit hosts |
| `…-macos-universal-au.zip` | Audio Unit | macOS 11 or later, Apple silicon and Intel |
| `…-macos-universal-lv2.zip` | LV2 | macOS 10.13 or later, Apple silicon and Intel |
| `…-linux-x86_64-lv2.zip` | LV2 | 64-bit Intel/AMD Linux (glibc 2.35 or later) |
| `…-linux-arm64-lv2.zip` | LV2 | 64-bit ARM Linux (glibc 2.35 or later) |

Unzip, copy the plugin folder for your host's format into its plugin folder, and restart your host (or rescan plugins):

| Format | Copy | Into |
|---|---|---|
| VST3 (Windows) | `canary-plus.vst3` | `C:\Program Files\Common Files\VST3` |
| Audio Unit (macOS) | `canary-plus.component` | `~/Library/Audio/Plug-Ins/Components` (or `/Library/Audio/Plug-Ins/Components` for all users) |
| LV2 (Windows) | `canary-plus.lv2` | `%APPDATA%\LV2` (create it if it doesn't exist; `install.bat` does this for you) |
| LV2 (macOS) | `canary-plus.lv2` | `~/Library/Audio/Plug-Ins/LV2` |
| LV2 (Linux) | `canary-plus.lv2` | `~/.lv2` |

In the host it is listed as **Davidian Canary Plus** (LV2), **Canary Plus** by **Davidian** among the distortion effects (VST3), or **Davidian: Canary Plus** (Audio Unit). Replace any older copy instead of keeping two. The LV2 URI is `urn:davidian:canary-plus`.

The macOS build isn't notarized, so macOS may block it after download. Run these once in Terminal after copying:

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/canary-plus.component
```

```bash
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/LV2/canary-plus.lv2
```

Logic Pro and GarageBand only show an Audio Unit once macOS has registered it. If it doesn't appear, log out and back in, or restart the Mac. If Logic lists it as failed, reset and rescan it in Logic's Plug-in Manager.

## Controls

| Panel | Control | Circuit part |
|---|---|---|
| Gain | Distortion | The 1M pot: sets the 741's gain (6 to 46.6 dB) and its low cut together. Default 0.5 |
| Clipper Diodes | 1N270 germanium, 1N34 germanium, 1N4148 silicon, red LED, 1N34 array | The schematic's alternatives for the clipping pair. Default 1N270 |
| Level | Output | The 10k volume pot. Default 0.8 |
| Calibrate | Input V | Volts at the input jack for a 0 dBFS sample (0.1 to 10, default 1.0). Higher drives the circuit harder and comes out quieter; lower does the reverse |
| Monitor | Clip | Not on the pedal: an amber lamp showing how hard the diodes are clipping, with a 150 ms release. It stays dark for a clean signal, even with germanium diodes |
| Footswitch | Engage | Plugin bypass, crossfaded and delayed to match the effect; its red LED shows the state. In LV2 it drives the host bypass port (`enabled`); in VST3 it is the host's bypass switch (on when the pedal is off); in an Audio Unit it is the Engage parameter |

The readout shows the last control you touched; Distortion also shows the gain stage's mid-band gain, and Output its level in dB. VST3 hosts show the knobs as 0 to 10, like the panel; LV2 and Audio Unit hosts show 0 to 1.

Drag a knob up or down, hold Shift to fine-tune, use the mouse wheel, and double-click to reset. Click a diode row to select it, or use the mouse wheel over the list. VST3 and Audio Unit hosts get each knob drag as a single edit, so automation recording works.

## How it is modelled

The circuit runs in schematic order, with its part values:

- **Input coupling** (`src/input_hp.h`): the input capacitor and resistor.
- **Gain stage** (`src/gain_stage.h`): the 741 non-inverting stage, with its gain-bandwidth, slew rate and output swing.
- **Clipper** (`src/clipper.h`): the output coupling and the diode pair, solved with the Shockley diode equation for the selected diodes.
- **Oversampling** (`src/oversampler.h`): 8x at 44.1/48 kHz, 4x up to 96 kHz, 2x above. The latency is 32 samples at every sample rate, and the plugin reports it to the host.

### Checked by the tests

`test_dsp` loads the built plugin library and checks its sound, latency, bypass, robustness and CPU use. `vst3_check` and `au_check` check that the VST3 and Audio Unit sound the same in mono and stereo.

### Estimates

These values are not on the schematic, so they are reasoned estimates:

- Diode `Is` and `n` (`src/clipper.h`).
- 741 gain-bandwidth, slew rate and output swing (`src/gain_stage.h`).
- Pot taper (`TAPER_BASE` in `src/ports.h`).
- The 5 Hz output DC blocker (`src/plugin.c`), which stands in for the next device's input capacitor.

## Build

The plugin and GUI are plain C (Objective-C for the macOS GUI) with no dependencies beyond the platform's own libraries: Win32 on Windows, Cocoa on macOS, Xlib on Linux. The VST3 and Audio Unit versions need their SDKs, which CMake downloads while configuring.

| Source | What it is |
|---|---|
| `src/input_hp.h`, `src/gain_stage.h`, `src/clipper.h`, `src/oversampler.h` | The circuit model, stage by stage |
| `src/plugin.c` | LV2 entry points, controls and bypass. The VST3 and Audio Unit run this same code |
| `src/ports.h`, `src/params.h` | Port numbers and knob curves, and the parameters as VST3 and Audio Unit hosts see them |
| `src/denormal.h` | Flush-to-zero for x86 and ARM processors |
| `src/ui/ui_lv2.c` | LV2 GUI entry points |
| `src/vst3/` | VST3 version: plugin, editor, build file, Windows file details |
| `src/au/` | Audio Unit version: unit, Cocoa view, `Info.plist` template, build file |
| `src/ui/panel.h`, `src/ui/window.h`, `src/ui/editor.h`, `editor.c` | The panel, the window layer under it (Win32, Cocoa, X11) and the embeddable editor for VST3 and Audio Unit |
| `src/ui/style.h`, `font.h`, `canvas.h` | Davidian house style: palette, stroke font, renderer |
| `src/version.rc`, `src/ui/version.rc` | Windows file details for the LV2 DLLs, including the AI disclosure |
| `bundle/` | LV2 metadata, `AI-DISCLOSURE.txt` and `LICENSE.txt` |

### Every platform (CMake)

Needs CMake and a C compiler. On Linux, also the X11 headers (`libx11-dev` on Debian/Ubuntu); without them, the build leaves the GUI out. On macOS, Xcode or the Command Line Tools; add `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` for one build that runs on both Intel and Apple silicon.

```bash
cmake -B out -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build out --config Release
```

The LV2 bundle lands in `out/lv2/canary-plus.lv2`. On Windows and macOS the build also makes that platform's own format, unless you turn it off:

- **VST3 (Windows):** `out/vst3/canary-plus.vst3`. It needs CMake 3.25 or later and a C++17 compiler, and downloads Steinberg's VST3 SDK while configuring. Add `-DVST3=OFF` to skip it.
- **Audio Unit (macOS):** `out/au/canary-plus.component`. It needs Xcode 16 or later (for C++23), and downloads Apple's AudioUnitSDK while configuring. Add `-DAU=OFF` to skip it.
- **SDKs you already have:** to build from them instead of downloading, add `-DFETCHCONTENT_SOURCE_DIR_VST3SDK=<path>` or `-DFETCHCONTENT_SOURCE_DIR_AUDIOUNITSDK=<path>`.

### Windows, LV2 only (Visual Studio)

`build.bat` builds the LV2 bundle without CMake, using Visual Studio's C++ tools. It puts the bundle in `build\` and the test programs in `build-test\`.

```bash
build.bat
```

## Tests

With CMake:

```bash
ctest --test-dir out -C Release --output-on-failure
```

With `build.bat` on Windows:

```bash
build-test\test_dsp.exe build\canary-plus.lv2
```

```bash
build-test\ui_check.exe build\canary-plus.lv2
```

- `test_dsp` loads the LV2 plugin library and checks its sound, latency, bypass, robustness and CPU use.
- `ui_check` hosts the LV2 GUI in a window, drives it with mouse events, and compares what the window shows with a reference render. On Linux it needs a display; without one, run `ctest` under `xvfb-run -s "-screen 0 1280x1024x24"`.
- `ui_render` draws the panel to an image without opening a window.
- `lilv_check` checks the bundle with the lilv LV2 parser, which most LV2 hosts use. Add `-DLILV_CHECK=ON` to build it; it needs lilv installed (`liblilv-0-0` on Debian and Ubuntu, `brew install lilv` on macOS, or Ardour 9 on Windows).
- `vst3_validator` (Windows) runs Steinberg's VST3 validator on the bundle.
- `vst3_check` (Windows) and `au_check` (macOS) load the plugin as a host would. They check the sound in mono and stereo, the latency, bypass and saved settings. They also open the editor in a window, click and drag its controls, and check that it follows changes made by the host.

CI also runs Apple's `auval` on the installed Audio Unit, on Apple silicon and on Intel (under Rosetta).

GitHub Actions builds every format and runs all of these on each platform for every push (see `.github/workflows/build.yml`). Pushing a version tag publishes a release built from that tagged source.

## Use of AI

Made with generative AI (Claude Code), directed and reviewed by Davidian. Claude (Anthropic) generated the DSP code, GUI, plugin wrappers, tests, build scripts and documentation. The author directed the work and tested it by ear in Ardour. Only the Windows LV2 GUI has been tried by hand in a host so far; the other window layers are tested automatically on their own platforms. Every build carries this disclosure:

- `AI-DISCLOSURE.txt` in every bundle.
- The plugin description in `canary-plus.ttl`, which LV2 hosts may show.
- On Windows, the Comments field of the LV2 DLLs and the VST3 (right-click > Properties > Details).
- The message the CMake build prints, and each release's notes.

## References

- Circuit: the published schematic kept for reference in `schematic.gif` (not part of any bundle).
- Diode model: the Shockley diode equation, with `Is` and `n` estimated for each diode type.

## Licence

GPL-3.0-or-later. The full text is in [LICENSE](LICENSE), and every bundle carries a copy as `LICENSE.txt`. This repository is the source code for the release builds, as the GPL requires: if you pass a zip on, pass on a link to this repository with it.

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
