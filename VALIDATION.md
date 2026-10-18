# Validation — Triad

## Triad v5 (0.6.0) on macOS — 2026-10-18

Commit `3042036`, run 39105684238 (macos-14 runner, virtual Apple M1, 3 cores; the run before it, `a3c5ba1` /
39103531873, also passed and found the undo issue fixed in `3042036`). New in 0.6.0: LFOs on the knobs inside
effects (list and LEARN), undo / redo arrows, Copy / Paste plugin state, dragging effects up and down, the LAT
tooltip breakdown, the "Triad v5" name.

| Check | Result |
| --- | --- |
| Installer `Triad-v5-0.6.0-macOS-AppleSilicon.pkg` | Built and installed over simulated Triad, v2, v3 and v4 installs (all removed); AU VALIDATION SUCCEEDED; pluginval SUCCESS (VST3 and AU); integration tests passed (incl. knob linking, undo / redo, plugin clipboard, slot dragging) |
| Knob linking with the 51 real effects (each in its own process) | 51 of 51 have knobs to link (median 15, up to 666); an LFO on up to 4 knobs each: the knobs followed in 47 (BYOD, GATE-12 and ChowMatrix AU step or limit them), the sound followed in 49; LEARN linked a knob in 51 of 51; none crashed (AUPitch crashed in 0.5.0's probe, which moved knobs every 128 samples from outside Triad) |
| Undo with the 51 real effects | A knob turned (as in its window) and undone came back in 51 of 51 (45 before `3042036`: three Chowdhury effects keep a knob out of their saved settings; undo now also keeps knob positions) |
| Undo, 200 effects in one Triad | Snapshot 72 ms with real effects (13 ms with the test effect); undoing a removed effect 56 ms (it is loaded again, the other 199 stay loaded); undoing a slot mix 52 ms; redoing both 107 ms |
| Stress tests (test effects; 51 real effects, 5 x 3,000 random edits) | All passed, no NaN/Inf output; each real sequence had 120-145 undo / redo clicks, 55-65 copy / paste plugin state, 45-70 slot drags, 65-90 knob links / LEARN; live monitoring: 0 silenced blocks |
| Drawing, whole window at 1x / 2x | BANDS 4.0 / 11.4 ms; LFO 1.5 / 4.0 ms; MATRIX 1.7 / 5.8 ms; crossover display per frame 1.8 / 3.3 ms |

## Triad v4 (0.5.0) on macOS — 2026-10-17

Commit `479315b`, run 39096363512 (macos-14 runner, virtual Apple M1). New in 0.5.0: latency and CPU meters, LIVE,
startup preset and Initialize, 60 fps animation with cached drawing, close-up fixes.

| Check | Result |
| --- | --- |
| Installer `Triad-v4-0.5.0-macOS-AppleSilicon.pkg` | Built and installed; AU VALIDATION SUCCEEDED; pluginval SUCCESS (VST3 and AU); integration tests passed (incl. startup preset, Initialize, latency/CPU meters, LIVE, GUI stress) |
| Stress tests (test effects; 51 real effects, 5 x 3,000 edits incl. LIVE on/off) | All passed; no NaN/Inf output; live monitoring: 0 silenced blocks |
| Drawing, whole window at 1x / 2x (Retina) | BANDS 9.2 / 13.0 ms; LFO 2.2 / 3.9 ms (0.4.3: 10.0 / 31.9); MATRIX 1.6 / 7.1 ms |
| Drawing per frame (what moves) at 2x | Crossover display 3.8 ms (was 9.0, the whole display); LFO grid playhead strip under 0.1 ms (was the whole grid, 24.7 ms); band meter and knob dot about 0.1 ms |
| LFOs on parameters inside effects (feasibility) | 50 of 51 effects expose automatable parameters (median 15, up to 666); moved from the audio thread every 128 samples for 2 s: the sound followed in 48, values read back exactly in 42 (the rest step or limit them, e.g. AU versions of chowdsp and Tilr effects); 1 crashed inside its own code: Apple's AUPitch (its Rate) |

## Triad v4 (0.4.3) on macOS — 2026-10-17

Commit `801c304`, run 39027201610 (macos-14 runner, virtual Apple M1, 3 cores). New in 0.4.3: no dropouts from
Triad's latency timer or from saving; the live vocal monitoring test (TriadStress section 4).

| Check | Result |
| --- | --- |
| Installer `Triad-v4-0.4.3-macOS-AppleSilicon.pkg` | Built and installed; AU VALIDATION SUCCEEDED; pluginval SUCCESS (VST3 and AU); integration tests passed |
| Stress tests (test effects; 51 real effects, 5 x 3,000 edits) | All passed; no NaN/Inf output |
| Latency Triad adds | 0 samples at every slope (6-96 dB/oct) and band count (3 and 10 bands); measured with an impulse |
| Effects that add latency (11 of 51) | BYOD 4 samples, CHOWTapeModel 39, FILT-R 49, AUPeakLimiter 96, AUDynamicsProcessor and AUMultibandCompressor 256, AUPitch 3,719, AUNewPitch 4,096; Triad reports exactly the largest band total (measured: AUNewPitch, 4,096) |
| Crossover phase (group delay of the bands added back) | 24 dB/oct: 2.0 ms at 250 Hz, 0.3 ms at 1 kHz; 96 dB/oct: 12.3 ms at 250 Hz, 1.1 ms at 1 kHz; 6 dB/oct: none |
| Real-time, 48 kHz, window open, autosave every second | Default 3 bands: 0.5-1.5% of each buffer's time at 32-256 samples; 5-effect vocal chain with LFOs: 2-6%; 10 bands at 96 dB/oct: 11-35%. Silenced blocks (dropouts): 0 in every case (before 0.4.3: about 1 in 4,500 with the window open) |

## Triad v4 (0.4.2) on macOS — 2026-10-17

Commit `d60948b`, run 39010733817 (macos-14 runner; same plug-in as `bd68d52`, run 39010122678, which also passed). New in 0.4.2: EXTRA and USER folders in the LFO palette.

| Check | Result |
| --- | --- |
| Installer `Triad-v4-0.4.2-macOS-AppleSilicon.pkg` | Built and installed; AU VALIDATION SUCCEEDED; pluginval SUCCESS (VST3 and AU) |
| LFO presets | 1,049 shapes in 14 folders (EXTRA: 355, each also filed under its type); every page reachable |
| USER folder | Save, same-name save, recall exact shape, rename, delete; folder lists the saved shapes a page at a time |
| Stress test, test effects | Passed; 94 LFO palette edits (folders, pages, USER save/delete, dice) while audio plays |
| Stress test, real plug-ins (51 effects) | All passed: 5 x 3,000 random edits incl. 82-105 palette edits each, 25-38 copy/paste band state, 42-58 slope changes; no NaN/Inf output |

## Triad v4 (0.4.1) on macOS — 2026-10-16

Commit `20119a3`, run 39006301559 (macos-14 runner). New in 0.4.1: LFO generator, preset folders with pages, slopes up to 96 dB/oct, copy / paste band state.

| Check | Result |
| --- | --- |
| Installer `Triad-v4-0.4.1-macOS-AppleSilicon.pkg` | Built and installed; AU VALIDATION SUCCEEDED; pluginval SUCCESS (VST3 and AU) |
| Every slope, 6 to 96 dB/oct | Bands add back flat (max error 3e-5); shapes match Butterworth / Linkwitz-Riley (max 2.5e-5); 96 dB/oct: -96.5 dB one octave away |
| Ten bands with mixed slopes (up to 96 dB/oct) | Flat (max error 7.9e-5) |
| Copy / paste band state | Same effects with the same settings, slot mixes, LFOs, band mix and volume; source unchanged; across two Triads; band menu items |
| LFO presets | 339 shapes in 13 folders (75 hand-made, 6 x 44 generated); every page reachable, mouse wheel turns pages |
| Stress tests (test effects; 20 real VST3s and Apple AUs) | All passed, including 35-45 random copy/paste band state edits and slopes up to 96 dB/oct; no NaN/Inf output |

## Triad v4 (0.4.0) on macOS — 2026-10-16

Commit `6da619b`, run 38988111461 (macos-14 runner). New in 0.4.0: crossover slope dot, 6–48 dB/oct.

| Check | Result |
| --- | --- |
| Installer `Triad-v4-0.4.0-macOS-AppleSilicon.pkg`, removes Triad / v2 / v3 | Built and installed; AU VALIDATION SUCCEEDED; pluginval SUCCESS (VST3 and AU) |
| Every slope, 6 to 48 dB/oct | Bands add back flat (max error 3e-5); low/high shapes match Butterworth / Linkwitz-Riley (max 2.4e-5); two octaves away: -12.5 dB at 6 dB/oct ... -97.6 dB at 48 dB/oct |
| Ten bands with mixed slopes | Flat (max error 4.5e-5) |
| Slopes changed 30 times while playing | No clicks (largest sample step = the input sines' own) |
| Slope parameters | Saved and recalled; earlier projects open at 24 dB/oct; slopes move with their crossovers |
| Slope dot UI | Drag up/down, readout list, double-click reset, arrow keys, host gestures balanced |
| Real plug-ins (51 effects), 5 x 3,000 random edits incl. slope changes | All passed; FILT-R and AUNewPitch produced NaN at times, silenced by Triad |

## Triad v3 (0.3.2) on macOS — 2026-10-16

Same runner; commit `02ebf2d`, run 38953057961. 0.3.2 keeps one idle copy of each third-party effect loaded for the session (see 0.3.1, "Still open").

| Check | Result |
| --- | --- |
| Installer `Triad-v3-0.3.2-macOS-AppleSilicon.pkg`, auval, pluginval (VST3 + AU) | Built; AU VALIDATION SUCCEEDED; pluginval SUCCESS for both |
| Integration tests, standard stress test | All passed (including: removed effects deleted after their pending messages, one idle copy kept per effect) |
| Real plug-ins (51 effects) | All sections passed: 200 effects with every LFO on (LFO cost 1.3%), reopened in 6.1 s; **all five 3,000-edit sequences passed** (15,000 edits, up to 69 effects at once); 24 instances; plug-in list |
| Invalid audio caught | FILT-R (VST3 and AU), and Apple's AUDistortion and AUPitch, produced NaN/Inf at times during the random edits (sample-rate changes, automation); Triad silenced it each time and no NaN/Inf reached the output |

## Triad v3 (0.3.1) on macOS — 2026-10-16

GitHub Actions `macos-14` runner (Apple M1, virtualised, 3 cores, no GPU), macOS 14.8, JUCE 8.0.12; commit `c03321e`, run 38902310830.

| Check | Result |
| --- | --- |
| AU and VST3 build, installer `Triad-v3-0.3.1-macOS-AppleSilicon.pkg` | Built; 3.2 MB per format, helper 326 KB, package 3.2 MB |
| Apple `auval -v aufx Trid Tsrt` | AU VALIDATION SUCCEEDED |
| pluginval, strictness 5 (GUI tests skipped) | VST3: SUCCESS; AU: SUCCESS |
| Integration tests | All passed, including: an effect whose sidechain input cannot be switched off (fed silence), an effect that outputs NaN (silenced, restarted 3 times, bypassed), effects and windows deleted only after their own pending messages (removed, or Triad closed) |
| Standard stress test (test effect + Apple AUs) | Passed: 200 effects with all 220 LFO routes on (LFO cost 5%), 3,000 random edits while playing, 24 instances, 60-plug-in scan with 20 crashing |
| Real third-party plug-ins | 51 effects (VST3 + AU): Surge XT Effects, BYOD, ChowCentaur, ChowMatrix, ChowMultiTool, ChowPhaser Mono/Stereo, CHOW Tape Model, Dragonfly ER/Plate/Room/Hall, Airwindows Consolidated, GATE-12, TIME-12, FILT-R, REEV-R, wolf-shaper, 16 Apple AUs. Every one loaded and opened its window on top of Triad |
| Real plug-ins: big project | 200 effects, all LFOs on: no NaN; saved (3 MB) and reopened in 5.8 s |
| Real plug-ins: 5 x 3,000 random edits | Run 38898025628: all five sequences passed (15,000 edits). Run 38902310830: four passed; the fifth crashed once inside FILT-R's own message handling (see below) |
| Real plug-ins: 24 instances, plug-in list | Passed (run 38898025628) |

Problems found with real plug-ins and fixed in 0.3.1:

- Surge XT Effects (AU) was refused: its sidechain input cannot be switched off. It now loads; the sidechain is fed silence.
- ChowMatrix crashed (pure virtual call) when deleted before a message it had posted to itself arrived — after loading, playing, or when a whole Triad was closed. Removed effects, closed effect windows and the effects of a closed Triad now go to a shared "graveyard" and are deleted about a second later.
- FILT-R sometimes outputs NaN. Triad now silences invalid audio from any effect, restarts the effect up to three times, then bypasses it with a message.
- The integration test process crashed at exit when the graveyard was deleted after singletons it needs; an empty graveyard now deletes itself so the next one is made after them.

Still open:

- Rare crash inside FILT-R / REEV-R (tiagolr) message handling (`juce::MessageQueue::runLoopSourceCallback`, SIGSEGV), seen twice in about 30,000 machine-speed edits, right after the last instance of the plug-in was deleted. It looks like the plug-in shutting its own JUCE copy down while one of its messages is being delivered. Possible Triad-side workaround: keep one idle instance of each plug-in loaded for the session.
- Plug-in windows drawn with OpenGL (Dragonfly) are opened but not left on screen on the CI Mac, which has no GPU (software OpenGL stalls for minutes).

## Triad v2 (0.2.2) on macOS — 2026-10-15

GitHub Actions `macos-14` runner (Apple M1, virtualised, 3 cores), macOS 14.8, Xcode 15.4, JUCE 8.0.12; `Build-Mac.command` run unmodified (commit `8f6a818`). Also built and tested on Linux (debug) before the Mac run.

| Check | Result |
| --- | --- |
| AU and VST3 build (arm64, Release, macOS 12.0+) | Built; each bundle contains `Contents/Helpers/Triad Scanner` |
| Shipped binary size (stripped) | 2.9 MB per format; Triad Scanner helper 326 KB |
| Installer `Triad-v2-0.2.2-macOS-AppleSilicon.pkg` | 2.9 MB; installs over an earlier "Triad"; signatures valid; installed helper runs |
| Apple `auval -v aufx Trid Tsrt` | AU VALIDATION SUCCEEDED |
| pluginval, strictness 5 (GUI tests skipped) | VST3: SUCCESS; AU: SUCCESS |
| All earlier audio, recall and UI checks | Passed (max error ≤ 6e-8) |
| Slots | Bands start with one slot; slots added and removed; 16-effect chain on one band (1,168 samples) recalled exactly |
| Triad 0.2.0/0.2.1 project | Slot positions and slot mixes recalled; old slot parameters dropped |
| Effects-only list | Nothing loaded inside Triad: plug-ins read from moduleinfo.json, Audio Unit registry, or Triad Scanner. A plug-in that aborts on load and one that hangs (stopped after 2 s) only affect the helper |
| Stress: 200 effects in one Triad (VST3 + 20 Apple AUs, 10 bands × 20 slots) | Loaded in 0.1 s; 20 s of audio at 4.7 ms per 256-sample block (89% of one core); saved and reopened complete |
| Stress: 3,000 random edits while audio plays on another thread | Bands, slots, effects, mixes, windows, project reloads, sample-rate changes: no crash, no NaN/Inf |
| Stress: 24 instances, 288 hosted effects | Created in 0.3 s; closed cleanly; 40 open/close cycles |
| Stress: plug-in list of 60, 20 of which crash | 4.9 s first scan, 0.2 s from cache; Triad unaffected |

The CPU figures above come from a shared virtual Mac with Apple's reverbs and dynamics processors in every chain; real projects depend on the effects used, and DAWs spread tracks across cores.

## Triad v2 (0.2.1) on macOS — 2026-10-15

GitHub Actions `macos-14` runner (Apple M1, virtualised), macOS 14.8, Xcode 15.4, CMake 4.4, JUCE 8.0.12; `Build-Mac.command` run unmodified (commit `384f566`). 0.2.1 renames the installed plug-in to "Triad v2"; the audio code is the same as 0.2.0.

| Check | Result |
| --- | --- |
| AU and VST3 build (arm64, Release, macOS 12.0+) | Built |
| Shipped binary size (stripped) | 2.9 MB per format |
| Installer `Triad-v2-0.2.1-macOS-AppleSilicon.pkg` | 2.6 MB; AU and VST3 choices; arm64, ad-hoc signed, not relocatable |
| Upgrade over an earlier "Triad" | Old `Triad.vst3` / `Triad.component` removed; `Triad v2` installed; auval lists "Triad v2" |
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

The 0.1.1 snapshots have since been replaced by 0.2.0 ones (`Triad-preview.png`, `Tests/UI/`). These were rendered by the compiled JUCE editor, not generated as mockups.

## Not yet validated

- Ableton Live and FL Studio scanning, playback, automation (including after adding/removing bands), bypass, project recall, and offline rendering on the user's Mac.
- The effect list against the user's full plug-in collection (hundreds of VST3/AU effects and instruments).
- Native hosted-plugin editor windows. This environment cannot create a display-server socket; the native-window test explicitly skipped. The main plugin interface was rendered and inspected offscreen.
- Compatibility with specific commercial third-party plugins, shell bundles, license managers, or plugins that change latency dynamically.
- Realtime stress testing, very large plugin states, and long sessions.

This is a tested source prototype, not a certified production release. The next practical step is to build it on the user's Mac and test a few of the actual effects they want to host.
