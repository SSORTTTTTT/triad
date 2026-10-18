# Triad roadmap

Release names (Thomas, 2026-10-17): every new installer gets the next name, **Triad v5**, then **Triad v6**, **Triad v7**
and so on, and the version number goes up with it (0.6.0, 0.7.0, ...). Each one replaces the earlier ones when
installed; the plug-in ID stays the same, so projects made with any earlier Triad open with the new one.

## Next build — Triad v6 (0.7.0): Auto-Tune and live vocals (Thomas, 2026-10-18)

Thomas tested v4 on a raw vocal with Auto-Tune Pro (retune speed 0), Fresh Air, an LA-2A compressor and a basic EQ,
split into three bands: it "works pretty good", but there was a lot of latency with Auto-Tune in its normal mode
(the same four plug-ins straight on a track felt instant), and its Low Latency mode made little difference apart
from some warbling. First thing for the next build:

- **Thomas's screenshots (Ableton, v4 0.5.0):** the same chain in all three bands (Auto-Tune Pro, UADx LA-2A, Fresh
  Air, kHs 3-Band EQ), crossovers at 96 dB/oct. Auto-Tune Pro reports **55.6 ms** in its normal mode (2.3 ms in Low
  Latency mode) and the LA-2A 1.8 ms, so each band is 57.4 ms (4.1 ms in Low Latency mode) and so is Triad's total:
  exactly the effects' own latency, nothing added by Triad. Auto-Tune in band 3 (above 5.6-9.4 kHz) hears only breath
  and "s", and the three copies correct separately: the likely warble. For now: Auto-Tune once, on the track before
  Triad (or in a 1-band Triad), in Low Latency mode for tracking.
- **Fixed for v6 (found in the screenshots):** latency readouts of 10 ms and more showed every decimal ("57.4375 ms
  latency", "55.625ms", "57.43..." on the LAT meter); now whole milliseconds ("57 ms").
- **Find where the delay comes from, measured.** Triad itself adds 0 samples (measured on every CI run); it reports
  and lines the bands up to what the hosted effects report. Compare, in FL Studio and Ableton, the same chain on a
  plain track and inside Triad: the latency each reports, the delay actually heard (an impulse through each),
  AU vs VST3, Auto-Tune's normal vs Low Latency mode (does Triad pass the new, lower latency on straight away, and
  does the DAW take it?), and what Triad gives the hosted effect (sample rate, block size, offline/real-time flags)
  compared with what the DAW gives it directly. Thomas's LAT tooltip (per band, per effect) from v5 is the first clue.
- **Free pitch correctors in the CI stress tests** (e.g. MAutoPitch, Graillon's free edition, if their installers
  can be fetched), to test pitch correction inside Triad: latency reported vs heard, Low Latency switching, warble.
- **PRE chain:** a full-range chain of slots before the bands split (and maybe a POST chain after they come back
  together), for effects that need the whole voice: pitch correction, a de-esser, a gate, the first EQ. Auto-Tune
  in a band only hears that band's part of the voice (the note is in the low band; the high bands are mostly breath
  and "s"), so each copy tracks differently and the corrected parts no longer line up: likely the warble. Latency of
  effects in different bands does not add up (bands play side by side); in the PRE chain it simply adds once.
- **Hard wall for every divider in one click** (Thomas): "All dividers: hard wall (96 dB/oct)" and "All dividers:
  soft / default" in the display's right-click menu and the slope list, keeping the sloping as it is. Each divider
  can already be set to 96 dB/oct. (A true brickwall made with linear-phase filters would add latency, so not for
  live vocals.)
- Maybe: a **1-band / full-range** switch (no split at all) for chains that should not be split.

## Current testing version — 0.6.0 ("Triad v5")

0.6.0 (2026-10-17), from Thomas's notes after 0.5.0:
- **LFOs on the knobs inside your effects.** In the LFO page, **+ ROUTE TO...** lists every knob inside the band's effects
  (a submenu per effect, 40 knobs a page, names from the plug-in), or choose **LEARN** and turn a knob in the effect's
  own window: it is linked to the LFO. The knob moves from where it was set, by the route's amount (invert and on/off
  as for any route); switching the link off or removing it puts the knob back. Links are saved with projects, presets,
  chain presets and Copy band state, by the effect's own parameter ID; a link to an effect that is missing waits for it.
  Triad sets the knob every 64 samples, just before the effect plays.
- **Copy plugin state / Paste plugin state** in an effect's arrow menu (with Open window, Replace, Add to favourites):
  copies the effect with every knob as it is, and its mix. Paste onto the same effect in another slot or band (only
  its settings change), onto a different effect or an empty slot (the copied effect is loaded there), or into a new
  slot from the band's ... menu ("Paste plugin (name) into a new slot"). Works between Triads in the same project.
- **Drag effects up and down**: press an effect's row and drag it to another place in its band; the others make
  room, and its mix knob, LFO and links go with it. (Move up / Move down are still in the menu.)
- **Undo / Redo arrows** left of the settings gear, for everything on the main page and in the LFO and MATRIX tabs:
  effects loaded, replaced, removed, moved or dragged, pasted plugin or band states, slot and band mixes, volumes,
  crossovers and slopes, bands added, removed or moved, LFOs drawn or linked, presets loaded or Initialize, and knobs
  turned inside the effects' windows. Effects that are still there stay loaded (only their settings go back), so undo
  is quick and their windows stay open; an effect that was removed is loaded again with its settings. Up to 100 steps.
  A project opened by the DAW starts a fresh history. LIVE is not an undo step. Each LFO still has its own undo too.
- **Latency tooltip** on the LAT meter: each band's latency and the effects in it, all the effects added up, and the
  total Triad reports (see "Latency questions" below).

### Latency questions (Thomas, 2026-10-17)

- **"Combine the latencies":** with Infiltrator in band 2 (2.0 ms) and Omnisphere FX in another band (0.8 ms), the LAT
  meter shows 2.0 ms, not 2.8 ms. The bands play **side by side**, not one after the other, so the slowest band sets
  the delay and the others wait for it (they are delayed to line up). Two effects in the **same** band do add up,
  and the band's footer shows that sum. Since 0.6.0, the LAT meter's tooltip lists each band ("Band 2: 2.0 ms
  (Infiltrator 2.0 ms)"), all effects added up (2.8 ms), and explains the total.
- **Infiltrator presets with 60 or 170 ms:** that is Infiltrator's own latency, reported by the plug-in for those
  presets (modules such as pitch shifting, spectral or granular effects and look-ahead need audio "ahead" of time).
  Triad adds none (0 samples). Every host shows the same: FL Studio's mixer and Ableton's device delay would report
  the same 60 or 170 ms. For tracking vocals, use a preset without those modules, or LIVE.
- **LIVE:** you still hear your voice and every effect that adds no latency; only the effects that add latency are
  passed by (their slot shows "LIVE OFF"), so what you hear is not delayed. Switch LIVE off for mixing and they play
  again. The DAW records your dry voice either way (Triad sits on the track's insert, after the recording point).
- **Normal?** Yes: every host works this way (DAWs, Infiltrator itself, other effect racks). Effects that look ahead
  have to delay the sound; the host delays the other tracks to keep them in time.

### Look and feel — waiting for Thomas's fonts, colours and lighting styles

Thomas will send fonts and aesthetic references soon; the theme work (see "Look and feel" below) starts then.

### Ideas from this work (later)

- Drag an effect onto **another band's** strip to move it there (today: up and down within its band, or Copy / Paste
  plugin state).
- Right-click the undo arrow for a list of the last steps ("Undo: removed Infiltrator from band 2", ...), and
  Cmd+Z / Cmd+Shift+Z while Triad's window has focus (DAWs use those keys themselves, so as an option).
- **DAW automation of the knobs inside effects:** FL Studio and Ableton cannot see inside Triad; a set of macro
  parameters (e.g. 16) that the DAW can automate, each linked to any knob inside any hosted effect.
- Links: a value range per link (min / max instead of amount), and the LFO line showing the value the effect took.

### 0.5.0 ("Triad v4")

0.5.0 (2026-10-17), from Thomas's notes after 0.4.3:
- **Latency meter** (LAT) and **CPU meter** beside IN / OUT; each band's latency in its strip; a latency badge on each
  effect that adds it; **LIVE** passes every latency-adding effect by while recording (saved with the project).
- **Startup preset**: gear menu → Change startup preset (Init, any saved preset, or "Use the current settings as the
  startup preset"); **Initialize** in the PRESET menu.
- **Smoother, cleaner interface**: meters, knob LFO dots, spectrum and the LFO playhead move at up to 60 frames a
  second in step with the screen (were 20-30); the crossover display and the LFO grid draw their still parts once
  into cached pictures (the LFO grid's playhead now redraws a 12-pixel strip, not the whole grid); no repaint of the
  whole display or the preset button 20 times a second; slot knobs show numbers that fit; the band meter lines up
  with its slider; slope readouts never cover each other, the pills or other dividers (compact "24" when tight);
  divider lines stop at pills; palette thumbnails are drawn from the shapes' points (Gate 1/8 was a grey block).
- New tests: visual check (drawing time of each moving part, cached pictures not redrawn per frame), GUI stress
  (400 random interface actions, window drawn every 20), and a stress-test section that moves the parameters
  inside every hosted effect from the audio thread (for "LFOs on knobs inside effects", done in 0.6.0).

### 0.4.3

0.4.3 (2026-10-17): found by the new **live vocal monitoring** stress test — with Triad's window open, about one
audio block in 4,500 came out silent (a click): Triad's 10-per-second latency check, and every project save or DAW
autosave, briefly held the lock the audio thread uses, and the audio thread outputs silence rather than wait. Now
the check only locks when an effect's latency actually changed, and saving no longer locks the audio: no dropouts
in the test (window open, autosave every second, 32–256-sample buffers). Measured: Triad alone adds 0 samples of
latency; see VALIDATION.md.

### 0.4.2

0.4.2 (2026-10-16): LFO palette — an **EXTRA** folder with all 355 shapes from the generator's preview sheets
(each also filed under its type: SQUARES, STEPS, TRIANGLES, ANGLES, HARD + CURVE, SOFT), and a **USER** folder:
**SAVE** on the LFO toolbar keeps the current shape (`~/Music/Triad/LFO Shapes`), right-click to rename or delete.
The folder list is in sections: by type, generated, extra, your shapes.

### 0.4.1

0.4.1 (2026-10-16): the **LFO randomizer** (GENERATE panel: Angular / Soft / Chaos sliders, HARD / MIX / SOFT,
NEW SHAPE dice; studied from Thomas's 846 Infiltrator favourite shapes, not copied), a **preset bank** in folders
with pages (75 hand-made hard-angle shapes in 7 folders + 6 GEN folders of 44 generated shapes, 4 pages each),
**harder crossover cuts** (54–96 dB/oct, near brickwall, after 6–48 in the slope list), and **Copy / Paste band
state** (band ... menu and right-click on a band).

### 0.4.0

0.4.0 (2026-10-16): the **slope dot** from Thomas's notes below — a second dot on every crossover
divider: up = soft slope (bands blend), down = hard cut; readout 6–48 dB/oct (24 by default), click
it for a list; each band drawn as its own coloured response curve so the overlap shows. Slopes are
DAW parameters, saved with projects/presets.

### 0.3.2 ("Triad v3")

0.3.2 (2026-10-16): one idle copy of each third-party effect stays loaded for the session, so a
plug-in's last copy is never deleted mid-session (FILT-R and REEV-R occasionally crashed then).

### 0.3.1

0.3.1 (2026-10-16) is 0.3.0 hardened by stress tests with 51 real third-party effects on the cloud Mac:
Surge XT Effects (AU) now loads; removed effects, closed effect windows and the effects of a closed
Triad are deleted a moment later (ChowMatrix and REEV-R crashed when deleted right after loading or
opening); effects that output NaN/Inf (FILT-R did) are silenced, restarted and then bypassed.

### 0.3.0

Built from the notes below (2026-10-15/16):

- Tabs **BANDS | LFO | MATRIX** at the top.
- LFO tab: a point/grid LFO for every effect's mix knob and every Band Mix (pre-linked, on as soon as you draw), extra LFOs, ShaperBox-style length menu (triplet / straight / dotted / free), 2x, 1/2, reverse, flip, snap, triplet grid, curves, copy/paste, undo/redo, shape palette, routing list.
- MATRIX tab: per band, SOURCE | AMOUNT | INV | DESTINATION with on/off lights, scrolling sideways like the bands.
- Effect windows stay in front of Triad; unused keys (spacebar) go back to the DAW (macOS).
- Drag a band's header to move its chain to another band; right-click the spectrum to remove a band.
- Band Volume with level meter; IN/OUT meters; smaller, wider window with a size setting.
- Settings gear: sort by company or A-Z, auto-check for new plug-ins, extra plug-in folders, window size. Favourites at the top of the effect list.
- Presets: whole-Triad presets and per-band chain presets.

Still to come: see "Next update" below (LFO randomizer, soft crossovers, knobs inside third-party effects) and "Later" (most-used sorting, routing between bands).

## LFOs on the knobs inside your effects (Thomas, 2026-10-17) — feasible; done in 0.6.0

Asked: can Triad's LFOs move parameters inside the hosted plug-ins, linked from the LFO page? **Yes.** Tested in 0.5.0's
stress test with the 51 effects on the CI Mac: 50 expose automatable parameters (median 15 each, up to 666 for
ChowMatrix), and moving four of them from the audio thread every 128 samples (as an LFO would) worked in all 50: the
sound followed in 48 (the other two did not change the test noise), no NaN/Inf. One effect crashed inside its own code
when moved this way: Apple's AUPitch (its Rate parameter, meant for offline time-stretching).

Plan:
- **Learn:** in the LFO page (or a slot's ~ menu), "Link a knob inside this effect": move a knob in the effect's own
  window and Triad links the LFO to it; or pick it from a searchable list of the effect's parameters (names from the
  plug-in). Several LFOs per effect, several knobs per LFO, each with amount / invert / on-off, in the MATRIX too.
- **How it plays:** Triad sets the value just before the effect processes each block (smoothed every 32 samples for
  fast LFOs); the effect's own knob moves on screen; projects save the links by parameter ID.
- **Safety:** parameters that are not automatable are not offered; a short list of known-unsafe ones (like AUPitch's
  Rate) is left out; an effect that produces NaN/Inf while modulated is silenced and its link paused, as today.
- Some effects step or limit values (AU versions of chowdsp and Tilr effects read values back slightly differently):
  fine for sound, the LFO line shows the value the effect actually took.

## Next update — latency meter (Thomas, 2026-10-17, after using 0.4.2) — done in 0.5.0

Thomas: 0.4.2 "is working great". Next: a **latency meter**, shown like the IN/OUT and band volume meters, so he
can see at a glance whether Triad is still zero-latency (for recording vocals through it) and, if not, what adds it.

- **Total:** next to IN / OUT, Triad's latency in samples and ms (what Triad reports to the DAW). Green at 0
  ("ZERO LATENCY"), amber when an effect adds latency, red above about 10 ms (too late to sing through).
- **Per band:** each strip's footer shows its chain's latency (the slowest band sets the total); the others are
  delayed to line up with it.
- **Per slot:** a small badge on any effect that reports latency (e.g. "1.0 ms"), and the effect causing the most
  is highlighted, so it can be bypassed or swapped while recording.
- **CPU load** beside it (share of the audio buffer Triad uses), since dropouts at small buffers sound like clicks.
- Maybe: a **LIVE** switch that bypasses every effect with latency while recording (Triad then reports 0), and turns
  them back on for mixing.
- **Lighter CPU at the extreme setting:** 10 bands at 96 dB/oct uses about 23% of one core on the test machines
  (the default 3 bands at 24 dB/oct: about 1%); processing both channels at once (SIMD) would roughly halve it.
- Background: see VALIDATION.md, "Live vocal monitoring" (Triad alone adds no latency; what the crossovers'
  phase does to timing; how Triad keeps up at 32–256-sample buffers).

### Startup preset in the settings (Thomas, 2026-10-17) — done in 0.5.0

- Settings (gear) → **Change startup preset ▸**: a list with "Init (empty Triad)" at the top and every saved preset
  below it, the current choice ticked; every new Triad opens with it (projects that already have Triad keep their
  own settings). Also "Use the current settings as the startup preset". (Thomas, 2026-10-17: just for the roadmap.)
- Preset menu → **Initialize**: back to Init (3 bands, empty slots, 24 dB/oct, LFOs off), as one undoable step.
- Works the same in FL Studio and Ableton (it is Triad's own setting, not the DAW's default-preset feature).

### Smoother interface (visual tests, 2026-10-17) — done in 0.5.0 (except eased hover/press transitions and a lighter CPU at 10 bands x 96 dB/oct)

Measured by the new visual check in TriadTests (drawing times of each part, plus 4x close-ups of the knobs,
sliders and dots; the CI Mac prints its own numbers). Linux test machine, optimised build, at 1x / 2x (Retina):
whole window BANDS 12.7 / 31.5 ms, LFO 26.3 / 42.0 ms, MATRIX 5.4 / 16.1 ms; crossover display 3.4 / 8.5 ms;
LFO grid 8.6 / 22.9 ms; a band strip 1.1 / 1.7 ms. A frame at 60 fps has 16.7 ms.

- **Frame rate:** meters, band strips and the knobs' LFO dots update 20 times a second, the spectrum and LFO
  playhead 30 — visibly steppy (a 1/16-note LFO moves its dot round in only ~2 frames). Move to 60 fps in step
  with the screen (VBlankAttachment), and glide the dots and meters between values.
- **Draw less per frame:** keep the parts that do not move (grid, labels, band curves, the LFO shape and its
  hatching) as cached pictures and redraw only what moves (spectrum, playhead, meters, dots); on the test machine the LFO grid at
  Retina size takes longer than a 60 fps frame (22.9 ms). The window's 20 Hz timer also re-lays-out and repaints
  the whole crossover display on top of its own 30 Hz repaint, and repaints the preset button every time: only
  repaint what changed.
- **Details seen in the close-ups:** the slot knobs' value text ("100%") is wider than the knob (smaller text,
  or show it while hovering/dragging); the band meter is shorter than the volume slider above it and "-inf"
  floats beside it; slope readouts can overlap each other and the frequency pills when dividers are close; the
  dashed divider line runs through the pills; palette thumbnails sample the shape at 48 points, so dense shapes
  (Gate 1/8) turn into a grey block (draw them from the shape's points).
- **Feel:** short eased transitions (about 120 ms) for hover, press, tab and page changes; knob arcs with a little
  depth and a soft glow while an LFO moves them; value changes that glide rather than jump.

### Look and feel: Triad's own theme and font (Thomas, 2026-10-17)

So Triad does not look like other plug-ins, and looks sleeker:

- **References from Thomas:** screenshots of anything whose look he likes (plug-ins, apps, websites, hardware,
  album art), each with a word on what he likes (colours, flatness or depth, glow, roundness, density), plus fonts
  he likes. They are used as mood references only: Triad gets its own look, not another product's colours,
  layout, knob graphics or logo.
- **Thomas will provide** font files (.ttf / .otf; the licence must allow embedding the font in software, as
  OFL fonts and most "desktop + app" licences do), colour schemes (hex codes or pictures) and lighting styles
  (glow, gradients, shadows, glass, neon...), to make Triad look that specific way.
- **Three directions as mockups** of Triad's real screens (BANDS and LFO), side by side; Thomas picks one or mixes.
- **Built as a theme system:** colours (background, panels, accent, the ten band colours, text), font and
  weights, corner radius, line weights, knob style (ring, arc, dot), meter style — so several themes can ship and
  be switched in the settings (and an accent colour picker, maybe).
- **Font embedded in Triad** so it looks the same on every Mac: a free, openly licensed font (e.g. Inter,
  Manrope, DM Sans, Space Grotesk, IBM Plex Sans), with tabular figures so changing numbers do not jitter.
- Done together with "Smoother interface" above, since the knobs, meters and readouts get redrawn anyway.

## Next update — notes from Thomas (2026-10-16, after using 0.3.2)

Thomas: 0.3.2 "seems to be working really good"; the LFO editor feels like ShaperBox, which he likes.
Order: **1. LFO randomizer and presets first**, then 2. soft crossovers, then 3. knobs inside third-party effects.

### 1. LFO randomizer ("Generate") — first version done in 0.4.1 (see above); still open: Smooth amount, "Save to my library"

A dice/Generate control in the LFO tab toolbar that draws a new shape for the selected LFO, with controls that
rein the randomness in, so results are usable rhythms rather than noise:

- **Style: Wavy <-> Hard.** Wavy = smooth curves between points; Hard = straight lines and steps on the grid
  (gate/stutter shapes, like Gross Beat volume patterns). In between mixes the two.
- **Density: Simple <-> Busy.** How many points/steps per cycle — from a few slow moves to a crazier rhythm.
- **Randomness: Subtle <-> Wild.** Low = a variation of the current shape (or of a preset); high = a completely new shape.
- Follows the grid (1/16, triplet, ...) and SNAP, so hard shapes land on the beat; one click Undo, like every edit.
- Generator presets, e.g. Wavy, Gated, Stutter, Pump, Glitch — each a starting setting of the three controls.
- **Trained on Thomas's own presets ("My style").** The generator learns from a library of shapes Thomas likes:
  which step lengths, gaps, ramps and curve shapes they use, and builds new shapes from those habits (recombining
  and varying rhythms from the library) instead of pure random numbers. Thomas will provide:
  - Gross Beat banks/presets (.fst or the bank files), ShaperBox presets, Infiltrator presets — any he actually uses;
    if a format can't be read, screenshots of each preset's LFO/envelope screen (the shapes are traced from those).
  - A short note on favourites. 20–50 or more shapes is a good start; more makes it closer to his style.
  - Later: a "Save to my library" button in Triad, so shapes he draws or keeps from Generate train it further.
  - Used as a private style reference on his Mac; other companies' factory presets are not copied into Triad as-is.
- **More preset shapes** in the palette (gates, stutters, pumps, triplet and dotted patterns, ramps), organised by type.
- **Smoother LFO visuals** and a **Smooth** amount per LFO that rounds hard corners (also avoids clicks).

### 2. Soft crossovers ("bleed" between bands)

Today each crossover is a fixed, steep Linkwitz-Riley 24 dB/octave split, so an effect stops sharply at the band edge
(e.g. at 2.5 kHz). Thomas (2026-10-16): one of the **two things to focus on for the next version** (with the LFO randomizer).

- A **slope dot** for each crossover: besides the frequency handle at the bottom of the spectrum display, a second dot
  higher up on the same crossover line, in the middle of the grid.
- Click and drag it to pull the slope in (gentle, broad: the effect bleeds into the neighbouring bands) or out (steep,
  hard cut at that frequency), like the band splitting in ShaperBox's multiband view.
- While you drag (and on hover), the **slope picture** appears: the two neighbouring bands' filter curves drawn over
  the spectrum, so you can see how much of each band reaches the other.
- A **readout** next to the dot shows the slope in dB per octave (e.g. "12 dB/oct"); double-click resets it to 24.
- Steps: 6, 12, 24, 36 and 48 dB/oct (Linkwitz-Riley-type filters at each step, so the bands still add back to the
  original sound when no effects are loaded). Per crossover, saved with projects and presets, automatable.
- Visual reference from Thomas: `docs/reference/shaperbox-multiband-slope.jpg` (ShaperBox 3 multiband: dashed line
  per crossover, a slope label such as "6dB" above the frequency label "228Hz", the selected band shaded).
- **Band colours overlap across the slope:** each band's region is tinted in its own colour (the per-band colours
  Thomas likes), and across a crossover the two colours fade into each other. A gentle slope gives a wide overlap,
  a hard cut a narrow one, so you can see at a glance how much of one band bleeds into the next.
- ShaperBox picks the slope from a dropdown; Triad does both: drag the dot, or click the dB readout to pick from a list.

### Later: SEQUENCE tab (on/off step sequencer per band)

Thomas (2026-10-16), for later: a **SEQUENCE** tab beside BANDS | LFO | MATRIX, so effects can be switched on
and off in rhythm without drawing LFOs. Reference: `docs/reference/infiltrator-sequencer.jpg` (Infiltrator 2's
sequence page).

- Pick a band (band chips, like the LFO tab). The grid shows **one row per effect in that band**, named after
  the effect, and one column per step.
- **Click blocks to turn that effect on for that step**; click-drag to paint or erase a run of steps.
  An empty (off) block means the effect is switched off there and the band plays without it.
- **Length:** 16 steps by default, with start/end handles above the grid to shorten or loop part of it.
- **Speed** per step (1/16, 1/8, triplet and dotted values, like the LFO lengths), locked to the song.
- **Crossfade** between on and off (5 ms by default, adjustable) so switching never clicks.
- Tools beside the grid like Infiltrator's: clear, randomize (uses the LFO randomizer's ideas), rotate left/right.
- A switch per row: when a row's sequencer is off, that effect simply stays on.
- How "off" sounds, a choice per row: **cut** (the effect's sound stops at once) or **tails** (the effect stops
  receiving sound but its reverb/delay rings out). Latency compensation stays intact either way.

### 3. Knobs inside third-party effects (detail) — Learn and the list done in 0.6.0; DAW macros still to come

Possible, and planned (see "Later" further down):

- **Learn:** click Learn on an LFO or matrix row, move a knob in the effect's own window, and it is linked; or pick
  from a list of the effect's parameters. The matrix shows the parameter's real name ("Band 1 · Slot 2 · KHS
  Distortion · Drive"). Every LFO can then drive any knob in any hosted effect, as well as the mix knobs.
- **DAW automation of hosted knobs:** FL Studio and Ableton cannot see inside Triad, so Triad would offer a set of
  assignable macro parameters (e.g. 16) that the DAW can automate, each linked to any hosted knob.
- Limits: a few plug-ins don't announce knob moves (Learn then falls back to the list); knobs are moved at Triad's
  control rate (every 32 samples, smoothed), and some plug-ins click or use more CPU when a knob moves constantly.

## Notes from Thomas (2026-10-15) — built in 0.3.0 except where marked "Later"

Do these with the LFO/routing work, once Thomas has tried 0.2.2 with his own plug-ins and sent the LFO and routing prompts. Guiding rule from Thomas: **make everyday use easy, with as few clicks as possible and nothing finicky.**

### Top priority: effect windows open on top of Triad

- Today a hosted effect's window can open **behind** Triad's window, so you have to move Triad aside to use the effect. **It must open in front of Triad**, with Triad visible behind it.
- Options to weigh: keep effect windows always in front of Triad (and of the DAW's plug-in window); and/or show the effect **inside Triad's own window**, as an overlay with a close/back button, since Triad is the host. Possibly a setting for which one.
- **Keyboard:** while an effect window has focus, FL Studio's spacebar (play/stop) does nothing, so you have to click out first. Pass keys the effect doesn't use (spacebar, transport shortcuts) back to the DAW, and don't hold keyboard focus unless the effect needs typing.

### Move a band's effects to another band

- Press and hold a band strip (or its header), drag it left or right, and drop it on another band position, e.g. drag Band 1's chain to Band 3. The bands in between shift over (or the two swap), with a clear "picked up" look and a drop marker while dragging.
- The frequency ranges stay where they are; the effect chain (slots, slot mixes, band mix, on/off states) moves to the new range. Use case: try a distortion on the lows, then the low-mids, without rebuilding the chain.
- To decide: whether a band's colour travels with its effects or stays with its position.

### Other notes

- **Remove a band from the crossover display**, not only with the × on its strip: right-click inside a band's area of the spectrum (or right-click a crossover dot) for a menu with "Remove band" (an "Add band here" item fits there too).
- **Smaller Triad window**: less square, more of a wide rectangle that takes less of the screen; later a size setting (e.g. 75% / 100% / 125%) and/or a resizable window.
- **Settings gear** at the top right, just left of "VST3 + AU EFFECTS / V2". First contents: the plug-in list ("Browse for a plug-in file", "Rescan for new plug-ins", "Rescan everything", how many effects were found, plug-ins that could not be checked). Window size goes here too. The same items can stay in the slot menu.
- **Presets / saved chains**:
  - **Chain presets**: save one band's effect chain (its effects in order, each effect's own settings, slot mixes and on/off lights) by name, then load it into any band later; the effects open with exactly the settings they were saved with.
  - **Full Triad presets**: save the whole setup (bands, crossovers, every chain, mixes) and load it in another project.
  - A preset menu/browser (save, load, rename, delete), files kept in a Triad presets folder on the Mac so they can be backed up or shared. If a preset uses an effect that is not installed, its slot shows as missing and keeps the saved settings, as projects already do.
- **Band volume**: a simple level control on each band strip (a dB slider or knob, e.g. −24 dB to +12 dB, double-click for 0 dB) with a small dB readout/meter showing the band's level. Applied after the band's chain and Band Mix; automatable in the DAW like the band mixes.
- **In / Out meters on the spectrum display**: an input level meter and an output level meter (with dB readouts, e.g. peak and a held peak) at the side of the crossover/spectrum area, so you can compare what goes into Triad with what comes out.
- **Organising the effect list** (setting, in the gear menu):
  - Sort by **company** (today's layout, the default) or **A–Z by plug-in name** (one flat alphabetical list, company shown next to each name).
  - **Favourites**: mark an effect as a favourite (e.g. a star or right-click "Add to favourites" in the list); favourites appear in their own group at the top of the effect menu. Colour tags could come later.
  - **Later:** **most used** first, from how often each effect is loaded in Triad (counted on this Mac only).
- **Plug-in catalog**: already a saved file (`~/Library/Application Support/Triad/PluginCache-v2.xml`) that is reused instead of re-scanning; only new or changed plug-ins are checked. Possible setting: "check for new plug-ins automatically" on/off, so the folder check only runs when Rescan is pressed (like FL Studio / Ableton).

### Keep as they are (Thomas likes these)

- Effect list grouped as VST3 / Audio Units, then by company, then plug-in, with the separate "Not checked" group.
- "Browse for a plug-in file...", "Refresh plug-in list" and "Rescan all plug-ins" at the bottom of the slot menu.
- A different colour for each band.
- Each band starting with one slot and "+ ADD SLOT" underneath.

## Next: mix modulation / LFO tab

Add a dedicated mix-modulation/LFO tab:

- Placement (agreed with Thomas, 2026-10-15): a tab switch at the top of the window, in the area where "BAND CROSSOVERS" is now — **BANDS | LFO**. BANDS is today's view (crossover display and band strips); LFO opens the new section. Start this after 0.2.2 has been tried in FL Studio and Ableton.

- A point-based line editor for drawing each band's mix over time, inspired by the editing interaction in Gross Beat and ShaperBox.
- A way to link each band's Mix control (and possibly slot mixes) to its own envelope/LFO.
- Editable points and segments representing 0–100% mix.
- Tempo synchronization, cycle length, curves/steps, and interaction with the manual mix control to be settled when this phase begins.
- **Later (after v3): modulate knobs inside third-party effects.** Like Ableton (move a parameter and it is ready to automate) or FL Studio (right-click > browse parameters): move any knob in a hosted effect's window and Triad offers to link it to one of its LFOs. The matrix destination then shows the plug-in's own parameter name (e.g. "Valhalla Room · Decay"). JUCE can see hosted plug-ins' parameter changes, so this is feasible.
- **Later: LFO randomizer** (ShaperBox's dice button).
- Routing between bands/effects is planned alongside the LFO work; Thomas will describe it when that phase starts.
- Thomas is researching how other plug-ins handle LFOs, including randomised LFOs, before writing the LFO prompts.

This is a saved development plan, not a scheduled background task.
