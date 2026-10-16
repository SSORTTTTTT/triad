# Triad — multiband effects host

Version 0.2.2 ("Triad v2") for Thomas Ross. Split your sound into up to 10 bands and run a chain of as many VST3 or Audio Unit effects as you like on each band, FL Studio mixer style, with a mix knob on every slot and on every band.

**This package is source code.** `Build-Mac.command` compiles the Apple Silicon AU and VST3 and wraps them in a standard macOS installer package (`Installer/Triad-v2-0.2.1-macOS-AppleSilicon.pkg`). DAW validation on your Mac is still required.

## What it does

- **Bands you can add while working.** Triad opens with 3 bands (crossovers at 250 Hz and 2.5 kHz). Press **+ BAND** or double-click anywhere on the frequency display to split a band right there, up to 10 bands. Each band strip has an **×** to remove it; its range joins the neighbouring band.
- **Live spectrum.** The frequency display shows the incoming audio in real time behind the bands, like ShaperBox.
- **Draggable crossovers.** Drag any divider on the display; double-click one to reset it; Tab focuses a divider, arrow keys adjust it, Shift gives finer steps. Each band strip shows its range (start → end); click either edge to type a frequency such as `250` or `2.5k`. Drags reach the DAW as automation gestures.
- **Effect slots per band, as many as you need**, stacked in processing order (Slot 1 runs first), like an FL Studio mixer insert. Each band starts with one slot; **+ ADD SLOT** under the last slot adds another and picks its effect in one go. The list scrolls once it is longer than the band.
  - **Click an empty slot** to choose an effect. The list shows **effects only** (instruments such as Omnisphere are left out), grouped into VST3 and Audio Units, then by manufacturer.
  - **The effect's window opens as soon as it loads.** Click a loaded slot's name to bring it back.
  - **Each slot's knob** is that effect's dry/wet mix, with the percentage shown inside the knob. Double-click a knob for 100%.
  - **The green light** switches the slot on or off (bypassed slots are skipped entirely).
  - **The ▸ arrow (or right-click)** gives Open window, Replace, Move up/down, Bypass and Remove.
- **Band Mix** on each strip blends the dry band with its whole processed chain (0% dry, 100% wet). This is the knob a future LFO tab will drive.
- Hosts **VST3 and Audio Unit** effects (Audio Units on macOS). Triad itself installs as AU and VST3, named **Triad v2**.
- Compensates each effect's latency inside its slot, aligns all bands to the slowest chain, and reports the total to the DAW.
- Saves the band layout, crossovers, every mix knob, every slot's on/off state, plugin identities and hosted plugin states with the DAW project. Projects saved with Triad 0.1 still open.
- Exposes crossovers and band mixes to DAW automation. Slot mixes are saved with the project (with any number of slots they are not fixed DAW parameters). Hosted effects' own parameters are edited in their windows.

## Build the installer on your M3 Mac

1. Extract the whole `Triad` folder from the ZIP.
2. Install Apple's command-line tools if needed: open Terminal and run `xcode-select --install`. (The build script offers this automatically if they are missing.) These are Apple's compiler tools, needed only to build; Apple's installer asks for several GB of free disk space. Triad itself is only a few MB. Once you have the `.pkg`, you can remove the tools with `sudo rm -rf /Library/Developer/CommandLineTools` to get that space back.
3. Open `Build-Mac.command`. It checks the tools, downloads what the build needs, builds the AU and VST3 for Apple Silicon, and creates the installer:
   - **CMake 3.22+**: uses one from Homebrew or `/Applications/CMake.app` if present; otherwise it downloads the official CMake 3.31.9 into `deps/` (checksum-verified) for this build only.
   - **JUCE 8.0.12**: downloaded into `deps/JUCE` at the pinned commit on the first run.
   - An internet connection is needed for the first run. A full log is written to `build-mac.log`.
4. When it finishes, Finder shows `Installer/Triad-v2-0.2.1-macOS-AppleSilicon.pkg`. Quit your DAWs, double-click the package and follow the installer. It installs, for all users:
   - `/Library/Audio/Plug-Ins/Components/Triad v2.component` (AU)
   - `/Library/Audio/Plug-Ins/VST3/Triad v2.vst3` (VST3)

   It shows up in your DAW as **Triad v2**. It replaces an earlier "Triad" (the installer removes `Triad.vst3` / `Triad.component` first, so hosts never see two copies of the same plug-in), and projects made with the earlier Triad open in Triad v2. Quit your DAW completely before installing: macOS keeps an already-loaded plug-in running until the app quits.

   Use **Customize** in the installer to install only one format.

   **Size.** Triad is built to stay small: it links only the JUCE modules it uses (plugin hosting and UI; no audio-device or audio-file-codec code), uses its own compact Linkwitz–Riley crossover instead of the `juce_dsp` module, compiles framework/UI code for size with link-time optimisation while the audio path stays at full `-O3` speed, and dead-strips and symbol-strips the shipped binaries. `cmake -DTRIAD_SIZE_OPTIMISED=OFF` restores a plain `-O3` build.

   After building, the `build-mac` and `deps` folders (roughly 1 GB) can be deleted to free space; they are recreated by the next build.
5. Reopen your DAW and rescan plugins. Load **Triad v2** on a stereo track or mixer insert.

`Install-Mac.command` is still included as an alternative: it copies the built bundles into your *user* plugin folders (`~/Library/Audio/Plug-Ins/...`) without an installer, backing up any previous copy to `~/Library/Application Support/Triad/Backups/`. Use one method or the other so your DAW does not find two copies of Triad.

If macOS refuses to launch a `.command` file or the ZIP extractor loses its executable bit, open Terminal, type `bash `, drag `Build-Mac.command` into the window, and press Return.

The package and plugins are signed ad hoc on the Mac that built them (no Apple Developer ID, no notarization). That is fine on your own Mac. A copy downloaded onto another Mac will be blocked by Gatekeeper until it is allowed under System Settings > Privacy & Security > Open Anyway, or until it is signed with a Developer ID Installer certificate (`productsign`) and notarized.

To uninstall, quit your DAWs and delete the two Triad v2 bundles listed above, then run `sudo pkgutil --forget audio.thomasross.triad.au` and `sudo pkgutil --forget audio.thomasross.triad.vst3` in Terminal.

Your hosted effects must have native Apple Silicon VST3 versions installed. Triad does not bridge Intel-only binaries. Their existing licenses/authorizations still apply.

## Cloud build (GitHub Actions)

If you would rather not install Apple's developer tools, push this folder to a GitHub repository. `.github/workflows/build-mac.yml` runs `Build-Mac.command` on GitHub's Apple Silicon Mac runner, uploads the `.pkg` as the **Triad-macOS-AppleSilicon-pkg** artifact, then installs it on the runner and checks it with `auval`, the integration tests and pluginval. A downloaded package is quarantined by macOS: Control-click it and choose **Open**, or allow it under System Settings > Privacy & Security.

## Use

1. Load **Triad v2** on a stereo track or mixer insert. It starts with 3 bands.
2. Drag the dividers to set the band edges, or click a strip's start/end frequency to type one. Need another band? Double-click the display where you want the split, or press **+ BAND**.
3. Click **Slot 1** in a band and choose an effect. Its window opens straight away. Click **+ ADD SLOT** to add more effects; they run top to bottom. The arrow on a slot moves, bypasses or removes it.
4. Use each slot's knob for that effect's dry/wet, the green light to bypass it, and **Band Mix** for the whole band.
5. Save your Ableton or FL Studio project to keep everything.

The first time you open Triad, it sorts your plug-ins into effects and instruments in the background (the status line shows progress). It never loads a plug-in inside your DAW to do this: Audio Units and most modern VST3s are recognised from their files, and the rest are checked by **Triad Scanner**, a small helper inside Triad's bundle that opens each one in its own process, the way a DAW's plug-in scanner does. If a plug-in crashes or hangs there, only the helper is affected; the plug-in is marked and Triad and your DAW carry on. Plug-ins that could not be checked are listed separately under **Not checked**. Results are cached, so later sessions are instant. Use **Refresh plug-in list** after installing new plug-ins, or **Rescan all plug-ins** to start over.

For a delay or reverb, setting the hosted plugin's own mix to fully wet usually makes Triad's slot knob the easiest way to control the blend.

Suggested first DAW check: an EQ and a compressor chained on the low band, distortion on the mids, a delay on the highs. Try slot mixes and band mixes at 0%, 50% and 100%; add a band and remove one; save, close and reopen; then compare playback and an offline export.

## Build manually

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DTRIAD_BUILD_TESTS=OFF
cmake --build build --config Release --parallel 4
```

On Apple Silicon add `-DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0` to the first command. Intel or universal builds can be requested separately using CMake's architecture setting, but have not been validated here.

For an existing JUCE 8.0.12 checkout, add `-DJUCE_SOURCE_DIR=/absolute/path/to/JUCE`. The fallback download pins commit `29396c22c93392d6738e021b83196283d6e4d850`.

A standalone app is no longer built by default (it pulls in audio-device code a plugin does not need); add `-DTRIAD_BUILD_STANDALONE=ON` for development use.

Linux build dependencies: a C++17 compiler, CMake, Ninja or Make, pkg-config, ALSA, X11/Xext/Xinerama/Xrandr/Xcursor development libraries, FreeType/fontconfig, and OpenGL development libraries. Linux builds are for development/testing, not for installation into Mac DAWs.

The point-based mix LFO is deferred until after testing the multiband host; see `ROADMAP.md`. There is no LFO tab in this version.

## Implementation and limits

- Linkwitz–Riley 24 dB/octave crossovers for 1–10 bands. Each band's low-pass output is followed by all-pass filters at every higher crossover, so the dry bands sum to a flat-magnitude, phase-rotated signal. This is a minimum-phase crossover, not a linear-phase or bit-identical dry bypass.
- Crossover transitions are smoothed over 35 ms, mix transitions over 20 ms. Adding or removing a band restarts the filters with a 10 ms fade-in.
- Bands, crossovers and their parameters are positional (Band 1 is always the lowest). Adding a band in the middle shifts the bands above it up by one, and their mix values move with them; DAW automation lanes stay attached to positions, so re-check automation after changing the band layout.
- Frequencies are kept in order in the DSP. If typed values or automation cross, the higher crossover keeps priority and lower ones are limited 1% below it; the display shows the effective frequencies. The top crossover is limited to 45% of the sample rate.
- The spectrum shows the input (before the bands are split), with a +3 dB/octave tilt so typical music reads level.
- Stereo effects only. No VST2, instruments, sidechain inputs, MIDI routing or mono layouts; hosted effects' parameters are not re-exposed for DAW automation.
- Building the effect list is crash-isolated (see above). Effects you load still run inside the DAW process, as in any host: a plug-in that crashes while playing can still take the DAW down. Loading/replacing plugins and opening some plugin editors can briefly interrupt audio. Do initial compatibility checks in a test project.
- Latency compensation assumes each effect reports its latency correctly. Changes are reconciled on the message thread, with a brief silent interval instead of misaligned audio. Up to 262,144 samples per effect and per band chain; beyond that the effect is bypassed with a status message.
- Projects restored from a background thread are instantiated on the message thread; audio stays silent until restoration completes. Missing plugins keep their identity and state for later recovery, and their slots pass audio through.
- The audio callback never waits on a lock or allocates. The editor reads a snapshot of the slots, so redrawing the screen cannot interrupt audio. Third-party plugin realtime behaviour is outside Triad's control.
- AUv3 effects are created asynchronously (the slot shows "..." while loading).
- macOS binaries are locally signed for personal testing. No Developer ID distribution signing/notarization is included.

## Tests

With `TRIAD_BUILD_TESTS=ON` (the default), CMake builds real VST3 fixtures — an effect that applies stored gain with 73 samples of latency, an instrument, and a "plug-in" that crashes (or hangs) the moment it is loaded — plus two programs. `TriadTests` loads, processes and recalls actual plug-ins rather than mocking the host boundary: crossover reconstruction for 1–10 bands, a 16-effect chain on one band, slot and band mixes, bypass, moving/adding/removing slots, adding/removing bands with effects loaded, Triad 0.1 and 0.2 project migration, the effects-only list (instruments excluded; the crashing and hanging fixtures only ever take down Triad Scanner, never the test process), Audio Unit hosting on macOS, editor windows and UI snapshots. `TriadStress` loads 200 effects into one Triad, makes 3,000 random edits while another thread plays audio through it, runs 24 instances at once, and scans 60 plug-ins of which 20 crash; it reports timings and fails on any crash or NaN/Inf output.

```sh
cmake --build build --config Release --target TriadTests TriadStress TriadScanner TriadCrashFixture \
  TriadTestEffect_VST3 TriadTestSynth_VST3 Triad_VST3 --parallel 3
build/TriadTests_artefacts/Release/TriadTests \
  "$PWD/build/TriadTestEffect_artefacts/Release/VST3/Triad Test Effect.vst3" \
  "$PWD/build/TriadTestSynth_artefacts/Release/VST3/Triad Test Synth.vst3" \
  "$PWD/build/fixtures/Triad Crash Fixture.vst3" \
  "$PWD/build/TriadScanner_artefacts/Release/TriadScanner" \
  "$PWD/Triad-preview.png" \
  "$PWD/build/Triad_artefacts/Release/VST3/Triad v2.vst3"
build/TriadStress_artefacts/Release/TriadStress \
  "$PWD/build/TriadTestEffect_artefacts/Release/VST3/Triad Test Effect.vst3" \
  "$PWD/build/fixtures/Triad Crash Fixture.vst3" \
  "$PWD/build/TriadScanner_artefacts/Release/TriadScanner" stress-results.txt
```

The fixtures are development-only and are not installed. See `VALIDATION.md` for results.

## Source map

- `Source/AudioEngine.h`: N-band crossover, phase compensation, delay lines, spectrum tap, FFT.
- `Source/PluginProcessor.*`: bands, slot chains, mixing, latency, VST3/AU hosting, state and recall.
- `Source/PluginCatalog.*`, `Source/AudioUnitList.*`: the effects-only plug-in list and its cache.
- `Source/CrossoverDisplay.*`: live spectrum, draggable dividers, adding bands, host gestures and keyboard control.
- `Source/BandStrip.*`: FL-style band strips, slot rows and the + ADD SLOT row.
- `Source/PluginCatalog.*`, `Source/Scanner.cpp`: the effects-only list and the Triad Scanner helper.
- `Source/PluginEditor.*`: main window, knob style, effect chooser.
- `Tests/`: VST3 fixtures and integration tests.
- `Build-Mac.command`: Mac dependency check, AU/VST3 build and `.pkg` installer creation.
- `Install-Mac.command`: optional per-user install without the package.
- `.github/workflows/build-mac.yml`: builds, installs and validates the package on GitHub's Apple Silicon Mac.

JUCE and its included SDKs retain their own upstream licenses. JUCE is fetched as a dependency and is not bundled in this source archive. Consult the pinned checkout's license and <https://juce.com/> before distributing a product built with it.
