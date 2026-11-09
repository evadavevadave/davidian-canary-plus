# Davidian Canary Plus

A Davidian LV2 circuit model of an op-amp gain stage driving a diode clipper. Mono, with a native GUI, for Windows, macOS and Linux. The name, panel and lettering are original, with no branding from any commercial pedal.

## Download

Ready-made bundles are on the [Releases page](../../releases/latest):

| Download | For |
|---|---|
| `canary-plus-<version>-windows-x64.zip` | Windows 10 and 11, 64-bit |
| `canary-plus-<version>-macos-universal.zip` | macOS 10.13 or later, Intel and Apple Silicon |
| `canary-plus-<version>-linux-x86_64.zip` | Linux on 64-bit Intel and AMD (glibc 2.35 or later) |
| `canary-plus-<version>-linux-arm64.zip` | Linux on 64-bit ARM (glibc 2.35 or later) |

Each zip holds one `canary-plus.lv2` folder with these files:

- The plugin library
- The GUI library
- The two `.ttl` files
- `LICENSE.txt`
- `AI-DISCLOSURE.txt`

GitHub Actions builds every release from its tagged source and runs all the tests below on each platform first. This repository is the source code for those builds, as the GPL requires. If you pass a zip on, pass on a link to this repository with it.

## Build and test

Any platform, with CMake 3.16 or later and a C compiler:

```
cmake -B out -DCMAKE_BUILD_TYPE=Release
cmake --build out --config Release
ctest --test-dir out -C Release --output-on-failure
```

The bundle lands in `out/lv2/canary-plus.lv2`.

- **Linux:** the GUI needs the X11 development files (`libx11-dev` on Debian and Ubuntu). Without them, the build leaves the GUI out. The GUI test needs a display, so on a machine without one run `ctest` under `xvfb-run -s "-screen 0 1280x1024x24"`.
- **macOS:** the GUI is Objective-C, so it needs Xcode or the Command Line Tools. Add `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` for one build that runs on both Intel and Apple Silicon.
- **lilv check:** add `-DLILV_CHECK=ON` to also check the bundle with the lilv LV2 parser, which most LV2 hosts use. It needs lilv installed: `liblilv-0-0` on Debian and Ubuntu, `brew install lilv` on macOS, or Ardour 9 on Windows.

The tests are:

- `test_dsp` loads the plugin library and checks its sound, latency, bypass, robustness and CPU use.
- `ui_check` hosts the GUI in a window, drives it with mouse events, and compares what the window shows with a reference render.
- `ui_render` draws the panel to an image without opening a window.

On Windows, `build.bat` builds the same bundle without CMake, using Visual Studio's C++ tools. It puts the bundle in `build\` and the test programs in `build-test\`:

```
build.bat
build-test\test_dsp.exe build\canary-plus.lv2
build-test\ui_check.exe build\canary-plus.lv2
```

## Install

Copy the whole `canary-plus.lv2` folder, from a zip or from a build, into one of the standard LV2 folders:

- Windows: `%APPDATA%\LV2` (current user) or `%COMMONPROGRAMFILES%\LV2` (all users)
- Linux: `~/.lv2` (current user) or `/usr/local/lib/lv2` (all users)
- macOS: `~/Library/Audio/Plug-Ins/LV2`

If your host doesn't scan that folder, add it to the host's LV2 search path or set the `LV2_PATH` environment variable. Close the host before copying and restart it afterwards. Replace any older copy instead of keeping two: when two bundles have the same URI, the loader uses one and ignores the other.

Look for **Davidian Canary Plus** in your host's plugin list. The plugin URI is `urn:davidian:canary-plus`.

On Windows, `install.bat` copies the bundle to `%APPDATA%\LV2` for you.

**macOS:** the libraries aren't signed with an Apple Developer ID, so macOS blocks them when they come from a downloaded zip. After copying, clear the download flag once in Terminal:

```
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/LV2/canary-plus.lv2
```

## GUI

![GUI](docs/gui.png)

The GUI uses the Davidian house style (`src/ui/style.h`, `font.h` and `canvas.h`). It has no dependencies beyond the operating system: it draws with its own software renderer and stroke font, then shows the result in a native window. That's Win32 on Windows (`ui:WindowsUI`), Cocoa on macOS (`ui:CocoaUI`) and X11 on Linux (`ui:X11UI`). Each window layer is automatically tested on its own platform. So far only the Windows GUI has been tried by hand in a host (Ardour 9).

- Drag a knob up or down to turn it. Hold Shift for fine control. Double-click resets it, and the mouse wheel works too.
- Click a diode row to select it, or use the mouse wheel over the list.
- Click the footswitch to engage or bypass. Its red LED shows the state. It drives the host bypass port (`enabled`).
- The amber CLIP lamp shows how hard the diodes are clipping, from the plugin's `clip` output port. It stays dark for a clean signal, even with germanium diodes.
- The readout shows the last control you touched. Distortion also shows the gain stage's mid-band gain, and Output shows its level in dB.

The panel name, lettering and layout are original, with none of the original pedal's branding or artwork.

## Controls

| Control | Range | Default | Notes |
|---|---|---|---|
| Distortion | 0–1 | 0.5 | 1M pot; sets gain (6–46.6 dB) and low cut together |
| Output | 0–1 | 0.8 | 10k volume pot |
| Diodes | 5 choices | 1N270 | The schematic's alternatives |
| Volts at 0 dBFS | 0.1–10 | 1.0 | Voltage a full-scale digital signal represents. Higher drives the circuit harder and comes out quieter; lower does the reverse |
| Enabled | on/off | on | Crossfaded bypass, delayed to match the effect |
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
| `src/plugin.c` | LV2 entry points, controls, bypass |
| `src/denormal.h` | Flush-to-zero for x86 and ARM processors |
| `src/ui/panel.h` | GUI layout, drawing and mouse handling |
| `src/ui/ui_lv2.c` | LV2 GUI entry points and the Win32, Cocoa and X11 window layers |
| `src/ui/style.h`, `font.h`, `canvas.h` | Davidian house style: palette, stroke font, renderer |
| `bundle/` | LV2 metadata, `AI-DISCLOSURE.txt` and `LICENSE.txt` |
| `LICENSE` | GPL-3.0 licence text |
| `src/version.rc`, `src/ui/version.rc` | Windows file details for the DLLs, including the AI disclosure |
| `CMakeLists.txt` | Build for all platforms |
| `build.bat`, `install.bat` | Windows build and install without CMake |
| `.github/workflows/build.yml` | CI: builds and tests on every platform, publishes releases from version tags |
| `test/platform.h` | Library loading and timing for the tests on each platform |
| `test/test_dsp.c` | Tests that load the built plugin library |
| `test/ui_check.c`, `test/ui_render.c` | GUI tests |
| `test/lilv_check.c` | Checks the metadata with the lilv LV2 parser |

## Values not from the schematic

These are estimates to tune by ear or against a real pedal:

- Diode `Is` and `n` (`src/clipper.h`)
- 741 gain-bandwidth, slew rate and output swing (`src/gain_stage.h`)
- Pot taper (`TAPER_BASE` in `src/ports.h`)
- The 5 Hz output DC blocker (`src/plugin.c`), which stands in for the next device's input capacitor

## AI disclosure

This plugin was written with AI assistance. Claude (Anthropic) generated the DSP code, GUI, tests, build scripts and documentation. The author directed the work and tested it by ear in Ardour. Every build carries this disclosure:

- `AI-DISCLOSURE.txt` in the bundle folder
- The plugin description in `canary-plus.ttl`, which hosts may show
- On Windows, the Comments field of both DLLs (right-click > Properties > Details)
- The message the CMake build prints, and each release's notes

## Licensing

This project is free software under the **GNU General Public License, version 3 or later** (GPL-3.0-or-later). The full text is in `LICENSE`, and the bundle carries a copy as `LICENSE.txt`.

- **Where it's declared:** each source file starts with an SPDX line. The plugin's `.ttl` declares the licence with `doap:license`, and the Comments field of both DLLs names it too.
- **If you share the bundle:** include `LICENSE.txt` and make the matching source code available as well. A link to the source, or a zip of this folder next to the bundle, is enough.
- **No copyright notices:** the files carry no copyright lines, at the author's choice. The GPL only asks for them as a recommendation. The licence text itself keeps the Free Software Foundation's line, because the GPL allows only verbatim copies of itself.
- **No third-party code is included.** `src/lv2_abi.h` declares the LV2 interface by hand from the public specification, so the ISC-licensed LV2 headers aren't copied. The libraries link only the operating system's own libraries: the Windows system DLLs and the statically linked Microsoft C runtime, which Visual Studio's licence lets you redistribute in a compiled program; Cocoa on macOS; and the C library and libX11 on Linux.
- **Test tools:** `test/lilv_check.c` loads the lilv library (ISC licence) while it runs. It's a local test tool and isn't part of the bundle.
- **Reference images:** `schematic.gif` and `doubler_layout.png` are other people's drawings, kept here for reference only. Neither is part of the bundle, so don't redistribute them with it.
- **AI-generated code:** in some places, including the US, material generated by AI without enough human creative input may not be protected by copyright. Where that applies, the GPL covers the parts that are protected, and anyone can use the rest anyway.
