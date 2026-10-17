# Triad roadmap

## Current testing version — 0.4.0 ("Triad v4")

0.4.0 (2026-10-16): the **slope dot** from Thomas's notes below — a second dot on every crossover
divider: up = soft slope (bands blend), down = hard cut; readout 6–48 dB/oct (24 by default), click
it for a list; each band drawn as its own coloured response curve so the overlap shows. Slopes are
DAW parameters, saved with projects/presets. Next in v4: the LFO randomizer (Angular, Soft and Chaos
sliders, trained on Thomas's Infiltrator / Gross Beat presets once he sends them).

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

## Next update — notes from Thomas (2026-10-16, after using 0.3.2)

Thomas: 0.3.2 "seems to be working really good"; the LFO editor feels like ShaperBox, which he likes.
Order: **1. LFO randomizer and presets first**, then 2. soft crossovers, then 3. knobs inside third-party effects.

### 1. LFO randomizer ("Generate") — do this first

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

### 3. Knobs inside third-party effects (detail)

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
