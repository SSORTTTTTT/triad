# Validation — Triad

## Triad 0.2.0 on macOS — 2026-10-15

GitHub Actions `macos-14` runner (Apple M1, virtualised), macOS 14.8, Xcode 15.4, CMake 4.4, JUCE 8.0.12; `Build-Mac.command` run unmodified (commit `cffb2ab`).

| Check | Result |
| --- | --- |
| AU and VST3 build (arm64, Release, macOS 12.0+) | Built |
| Shipped binary size (stripped) | 2.9 MB per format |
| Installer `Triad-0.2.0-macOS-AppleSilicon.pkg` | 2.6 MB; AU and VST3 choices; arm64, ad-hoc signed, not relocatable |
| Installed with `sudo installer` | Both bundles in `/Library/Audio/Plug-Ins`; signatures valid |
| Apple `auval -v aufx Trid Tsrt` | AU VALIDATION SUCCEEDED |
| pluginval, strictness 5 (GUI tests skipped) | VST3: SUCCESS; AU: SUCCESS |
| Crossover reconstruction, 1–10 bands | Max error 2.7e-7 against a cascade of JUCE all-pass filters |
| Built-in Linkwitz–Riley filter vs JUCE | Max error 0 at 44.1, 48 and 96 kHz |
| One effect per band, band mix 0/50/100% | Max error ≤ 3e-8 |
| Slot mix 50%, slot + band mix together | Max error ≤ 6e-8 |
| Three-effect chain on one band, other bands latency-aligned (219 samples) | Max error 2.2e-8 |
| Bypassed slot (latency and processing removed), slot at 0%, moving a slot | Max error 1.5e-8; mix knob travels with the effect |
| Add a band at 1 kHz with effects loaded; remove a band | Effects and mixes stay with their bands; max error ≤ 3e-8 |
| Ten bands with a ten-effect chain (730 samples), save and recall | Max error 3e-8 |
| Project recall with edited hosted state; outer Triad VST3 with nested effects | Max error ≤ 6e-8 |
| Triad 0.1 project opened in 0.2 | Effect in band 2, crossovers 300 Hz / 3 kHz; max error 3e-8 |
| Missing plug-ins | Reported; slots pass audio; stored state preserved |
| Effects-only list | Test effect listed; test instrument and a metadata-less instrument excluded; crash-marked plug-in skipped; cache reused |
| Audio Unit hosting | Apple AUDelay listed as an effect and hosted in a slot |
| Effect windows | Open on load and on click; close cleanly |
| Interface | Rendered with 3, 5 and 10 bands, live spectrum visible |

## Triad 0.1.1 on macOS — 2026-10-15

GitHub Actions `macos-14` runner (Apple M1, virtualised), macOS 14.8, Xcode 15.4 / Apple clang 15, CMake 4.4, JUCE 8.0.12 at the pinned commit; `Build-Mac.command` run unmodified, commit `443b180`.

| Check | Result |
| --- | --- |
| AU and VST3 build (arm64, Release, macOS 12.0+) | Built |
| Shipped binary size (stripped) | 2.6 MB per format (was 3.5 MB before the size work) |
| Installer `Triad-0.1.1-macOS-AppleSilicon.pkg` | 2.4 MB; AU and VST3 choices; payload verified arm64, ad-hoc signed, not relocatable |
| `sudo installer -pkg ... -target /` | Installed to `/Library/Audio/Plug-Ins/Components` and `/Library/Audio/Plug-Ins/VST3`; signatures valid |
| Apple `auval -v aufx Trid Tsrt` | AU VALIDATION SUCCEEDED |
| pluginval, strictness 5 (GUI tests skipped) | VST3: SUCCESS; AU: SUCCESS |
| Integration tests (native arm64) | All passed, including the native hosted-effect editor window, which is skipped on headless Linux |
| Built-in Linkwitz–Riley filter vs JUCE `dsp::LinkwitzRileyFilter` | Max error 0 at 44.1, 48 and 96 kHz |

Still to check by hand: Ableton Live and FL Studio scanning, playback, automation, project recall and offline rendering with your own third-party effects. The installer is ad hoc signed, not Developer ID signed or notarized.

## Triad 0.1.1 source validation (Linux)

Completed 2026-09-29 (Los Angeles time) / 2026-09-30 UTC.

### Built successfully

Ubuntu x86-64; GCC 13.3; CMake 3.28.3; JUCE 8.0.12 at commit `29396c22c93392d6738e021b83196283d6e4d850`; Release configuration.

- Triad VST3 plugin bundle
- Triad standalone executable
- Triad Test Effect VST3 bundle
- TriadTests integration-test executable
- Updated draggable-crossover UI compiled into the VST3 and standalone targets
- Mac helper scripts passed shell syntax validation

The source archive deliberately omits Linux binaries: they cannot be installed into the user's Mac DAWs.

### Passed

| Check | Result |
| --- | --- |
| Logarithmic frequency-to-position mapping | Round-trip checks passed across the display range |
| Actual handle mouse callbacks | Move the crossover, preserve click offset, and notify host parameter listeners |
| Automation gesture lifecycle | Begin/end balanced, including closing the view during a drag |
| Band ordering and sample-rate guard | No crossover crossing; cutoff limited below Nyquist |
| Narrow-band hit areas | Both handles remain reachable when their hit rectangles overlap |
| Keyboard, fine adjustment, and double-click reset | Passed |
| External automation and project recall | Handle positions and crossover values update correctly |
| Empty slots with a block larger than the prepared size | Exact reference match |
| Three-band sum against an independent pair of all-pass filters | Maximum sample error 1.20e-7 |
| One delayed effect and two dry bands | Maximum sample error below 3e-8 |
| All three bands fully wet | Maximum sample error below 1.5e-8 |
| Mix at 0%, with plugin latency retained | Exact reference match |
| Mix at 50% | Maximum sample error below 3e-8 |
| Recall of changed hosted-plugin state and mix | Maximum sample error below 3e-8 |
| Loading Triad itself as VST3, saving, destroying, recreating, and restoring nested effects | Maximum sample error below 3e-8 |
| Missing plugins | Clear status; dry audio; opaque child state preserved |
| Attempted self-hosting | Rejected; existing effect preserved |
| Removing effects | Dry reference match; latency returns to zero |
| Headless editor-open request | Handled without crashing |
| Main interface | Rendered and visually inspected at default and very narrow crossover spacing |

Audio tests exercised 44.1, 48, and 96 kHz; crossover controls were also checked at 8 kHz for their sample-rate guard. Mouse interaction was tested by dispatching JUCE mouse callbacks; native pointer interaction in a Mac DAW is still pending. The fixture is a real stereo VST3 effect with stored gain and 73 samples of latency. Test output is in `test-results.txt`.

The default native UI snapshot is `Triad-preview.png`. A narrow-band snapshot is included in `Tests/UI/`. These were rendered by the compiled JUCE editor, not generated as mockups.

## Not yet validated

- Ableton Live and FL Studio scanning, playback, automation (including after adding/removing bands), bypass, project recall, and offline rendering on the user's Mac.
- The effect list against the user's full plug-in collection (hundreds of VST3/AU effects and instruments).
- Native hosted-plugin editor windows. This environment cannot create a display-server socket; the native-window test explicitly skipped. The main plugin interface was rendered and inspected offscreen.
- Compatibility with specific commercial third-party plugins, shell bundles, license managers, or plugins that change latency dynamically.
- Realtime stress testing, very large plugin states, and long sessions.

This is a tested source prototype, not a certified production release. The next practical step is to build it on the user's Mac and test a few of the actual effects they want to host.
