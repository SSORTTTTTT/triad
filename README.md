# Triad — three-band effect host

Version 0.1.1: a working source prototype for Thomas Ross. One stereo VST3 effect per band, two movable crossover frequencies, and an independent 0–100% effect mix on every band.

**This package is source code.** `Build-Mac.command` compiles the Apple Silicon AU and VST3 and wraps them in a standard macOS installer package (`Installer/Triad-0.1.1-macOS-AppleSilicon.pkg`). DAW validation on your Mac is still required.

## What it does

- Splits stereo input into LOW, MID, and HIGH with 24 dB/octave Linkwitz–Riley crossovers.
- Drag either round handle or dashed divider directly on the frequency panel. Double-click a handle to reset it; Tab can focus a handle, arrow keys adjust it, and Shift gives finer steps. Drags are sent to the DAW as automation gestures.
- Every band card shows its start frequency on the left and end frequency on the right. Low includes DC (0 Hz); High extends to Nyquist, half the session sample rate. The logarithmic display begins at 20 Hz.
- Default split points: 250 Hz and 2,500 Hz. Frequency controls span 20 Hz–20 kHz, limited below Nyquist at lower sample rates.
- Hosts an installed VST3 audio effect on each band, with its normal editor accessible using **Open**.
- **Mix 0%:** the dry, frequency-filtered band. **Mix 100%:** the effect's output. Intermediate positions use a linear wet/dry blend, like an effect-slot mix knob. This is not a band volume control.
- Empty slots pass the band through. **Remove** unloads the effect.
- Compensates dry/wet and between-band latency and reports the total to the DAW.
- Saves both crossover settings, all three mix controls, plugin identities, and hosted plugin states with the DAW project.
- Exposes crossovers and mixes to DAW automation. Other effect parameters are edited in each hosted effect's own window.
- Uses native Apple Silicon build settings in the supplied Mac script. Triad can be built as a VST3 or AU on Mac; both wrappers host **VST3** effects internally.

## Build the installer on your M3 Mac

1. Extract the whole `Triad` folder from the ZIP.
2. Install Apple's command-line tools if needed: open Terminal and run `xcode-select --install`. (The build script offers this automatically if they are missing.) These are Apple's compiler tools, needed only to build; Apple's installer asks for several GB of free disk space. Triad itself is only a few MB. Once you have the `.pkg`, you can remove the tools with `sudo rm -rf /Library/Developer/CommandLineTools` to get that space back.
3. Open `Build-Mac.command`. It checks the tools, downloads what the build needs, builds the AU and VST3 for Apple Silicon, and creates the installer:
   - **CMake 3.22+**: uses one from Homebrew or `/Applications/CMake.app` if present; otherwise it downloads the official CMake 3.31.9 into `deps/` (checksum-verified) for this build only.
   - **JUCE 8.0.12**: downloaded into `deps/JUCE` at the pinned commit on the first run.
   - An internet connection is needed for the first run. A full log is written to `build-mac.log`.
4. When it finishes, Finder shows `Installer/Triad-0.1.1-macOS-AppleSilicon.pkg`. Quit your DAWs, double-click the package and follow the installer. It installs, for all users:
   - `/Library/Audio/Plug-Ins/Components/Triad.component` (AU)
   - `/Library/Audio/Plug-Ins/VST3/Triad.vst3` (VST3)

   Use **Customize** in the installer to install only one format. The shipped plugin binaries are dead-stripped and have local symbols removed to keep them small.

   After building, the `build-mac` and `deps` folders (roughly 1 GB) can be deleted to free space; they are recreated by the next build.
5. Restart your DAW and rescan plugins. Load **Triad** on a stereo track or mixer insert.

`Install-Mac.command` is still included as an alternative: it copies the built bundles into your *user* plugin folders (`~/Library/Audio/Plug-Ins/...`) without an installer, backing up any previous copy to `~/Library/Application Support/Triad/Backups/`. Use one method or the other so your DAW does not find two copies of Triad.

If macOS refuses to launch a `.command` file or the ZIP extractor loses its executable bit, open Terminal, type `bash `, drag `Build-Mac.command` into the window, and press Return.

The package and plugins are signed ad hoc on the Mac that built them (no Apple Developer ID, no notarization). That is fine on your own Mac. A copy downloaded onto another Mac will be blocked by Gatekeeper until it is allowed under System Settings > Privacy & Security > Open Anyway, or until it is signed with a Developer ID Installer certificate (`productsign`) and notarized.

To uninstall, quit your DAWs and delete the two bundles listed above, then run `sudo pkgutil --forget audio.thomasross.triad.au` and `sudo pkgutil --forget audio.thomasross.triad.vst3` in Terminal.

Your hosted effects must have native Apple Silicon VST3 versions installed. Triad does not bridge Intel-only binaries. Their existing licenses/authorizations still apply.

## Cloud build (GitHub Actions)

If you would rather not install Apple's developer tools, push this folder to a GitHub repository. `.github/workflows/build-mac.yml` runs `Build-Mac.command` on GitHub's Apple Silicon Mac runner, uploads the `.pkg` as the **Triad-macOS-AppleSilicon-pkg** artifact, then installs it on the runner and checks it with `auval`, the integration tests and pluginval. A downloaded package is quarantined by macOS: Control-click it and choose **Open**, or allow it under System Settings > Privacy & Security.

## Use

1. Drag the two round handles/dashed dividers on the frequency display to set the band boundaries. The **LOW / MID** and **MID / HIGH** controls below it are available for numeric adjustment.
2. Click **Load effect...** in a card. The menu lists VST3 bundles in standard plugin locations. Choose one, or browse to its `.vst3` bundle.
3. Click **Open** for the effect's normal interface.
4. Turn the small **Effect Mix** knob to blend that processed band with its dry signal.
5. Save your Ableton or FL Studio project to capture all three plugin states.

For a delay or reverb, setting the hosted plugin's own mix to fully wet usually makes Triad's mix knob the easiest way to control the blend.

Suggested first DAW check: use an EQ on Low, distortion on Mid, and a delay on High. Confirm each mix at 0%, 50%, and 100%; save, close, reopen; then compare playback and an offline export. These are checks to perform, not claims of completed compatibility testing.

## Build manually

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DTRIAD_BUILD_TESTS=OFF
cmake --build build --config Release --parallel 4
```

On Apple Silicon add `-DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=12.0` to the first command. Intel or universal builds can be requested separately using CMake's architecture setting, but have not been validated here.

For an existing JUCE 8.0.12 checkout, add `-DJUCE_SOURCE_DIR=/absolute/path/to/JUCE`. The fallback download pins commit `29396c22c93392d6738e021b83196283d6e4d850`.

Linux build dependencies: a C++17 compiler, CMake, Ninja or Make, pkg-config, ALSA, X11/Xext/Xinerama/Xrandr/Xcursor development libraries, FreeType/fontconfig, and OpenGL development libraries. Linux builds are for development/testing, not for installation into Mac DAWs.

The point-based mix LFO is deferred until after testing the core host; see `ROADMAP.md`. There is no LFO tab in this version.

## Implementation and limits

- The low branch has a compensating all-pass at the upper crossover. The three dry bands sum to a flat-magnitude, phase-rotated signal. This is a minimum-phase crossover, not a linear-phase or bit-identical dry bypass.
- Crossover transitions are smoothed over 35 ms, mix transitions over 20 ms.
- The frequency panel shows band boundaries and a frequency grid; it is not a live spectrum analyzer. Narrow-band labels on the plot are hidden when they would not fit, while the cards always retain both endpoints. Nearby crossover frequency readouts stack vertically to stay readable.
- Frequencies are kept in order in the DSP. If typed values or automation cross the boundaries, the lower effective boundary is limited below the upper one; the card labels/graphic show the effective frequencies. The upper boundary is limited to 45% of sample rate.
- Stereo effect hosting only. No AU/VST2 hosting, instruments, sidechain inputs, MIDI routing, mono layouts, chains of multiple effects per band, or exposed nested-parameter automation in this version.
- Standard bundle discovery is file-based; it does not perform a full plugin scan just to draw the menu. Selecting an effect instantiates it in-process. Plugin shell bundles currently choose the first compatible effect they expose.
- Effects run inside the DAW process. There is no crash isolation, bridge process, or scan blacklist. Loading/replacing plugins and opening some plugin editors can briefly interrupt audio. Do initial compatibility checks in a test project.
- Dry/wet compensation assumes the child correctly reports its latency. Child latency changes are reconciled on the message thread; a brief silent interval prevents misaligned output while that happens. Latency above 262,144 samples per effect is unsupported and the effect is bypassed with a status message.
- Plugins restored from a background thread are instantiated on the message thread. Audio stays silent until restoration completes. Missing plugins retain their description/state for later recovery and their band passes dry once restoration finishes.
- Processing does not create host buffers or wait on the control lock in the audio callback. Third-party plugin realtime behavior is outside Triad's control.
- DAW transport information is forwarded. If a host supplies a block larger than the prepared maximum, Triad chunks it; the transport position is not offset per internal chunk in this first version.
- macOS binaries are locally signed for personal testing. No Developer ID distribution signing/notarization or commercial installer is included.

## Tests

With `TRIAD_BUILD_TESTS=ON` (the default), CMake creates a real VST3 test effect that applies stored gain and reports 73 samples of latency, plus an integration-test executable. It tests actual VST3 loading, processing, and state recall rather than mocking the host boundary. Native editor-window testing runs when a display server is available and is explicitly skipped otherwise; the main interface can still be rendered offscreen.

```sh
cmake --build build --config Release --target TriadTests TriadTestEffect_VST3 Triad_VST3 --parallel 3
# Linux, including a virtual display for plugin editors:
xvfb-run -a build/TriadTests_artefacts/Release/TriadTests \
  "$PWD/build/TriadTestEffect_artefacts/Release/VST3/Triad Test Effect.vst3" \
  "$PWD/Triad-preview.png" \
  "$PWD/build/Triad_artefacts/Release/VST3/Triad.vst3"
```

The test plugin is only a development fixture and is not installed by the Mac scripts. See `VALIDATION.md` for the actual results and remaining Mac checks.

## Source map

- `Source/AudioEngine.h`: crossovers, phase compensation, integer delays.
- `Source/PluginProcessor.*`: VST3 host, state, mixing, latency and lifecycle.
- `Source/PluginEditor.*`: three-card interface, knobs, numeric frequency controls, plugin chooser.
- `Source/CrossoverDisplay.*`: draggable crossover display, logarithmic mapping, host gestures, and keyboard interaction.
- `Tests/`: real VST3 fixture and integration tests.
- `Build-Mac.command`: Mac dependency check, AU/VST3 build and `.pkg` installer creation.
- `Install-Mac.command`: optional per-user install without the package.

JUCE and its included SDKs retain their own upstream licenses. JUCE is fetched as a dependency and is not bundled in this source archive. Consult the pinned checkout's license and <https://juce.com/> before distributing a product built with it.
