# Triad roadmap

Release names (Thomas, 2026-10-17): every new installer gets the next name, **Triad v5**, then **Triad v6**, **Triad v7**
and so on, and the version number goes up with it (0.6.0, 0.7.0, ...). Each one replaces the earlier ones when
installed; the plug-in ID stays the same, so projects made with any earlier Triad open with the new one.

## Current testing version — 0.11.0 ("Triad v10"): Triad Time's grid with ShaperBox's proportions

0.11.0 (2026-10-20), Thomas: "when I'm doing the little triangular cut, it's not pitching things down the same ... only
by five semitones or seven semitones not the full 12 ... I need the triad time grid to have the same point and same
pitching system as shaperbox":
- The cause: v9 already had ShaperBox's grid lines and pitch maths (a cut from 0 to -1/16 over 1/8 plays at half speed),
  but its grid was 1.7 times as wide as tall where ShaperBox's is 3.48 times. The same angle on screen was half the
  slope, so cuts drawn by eye came out at -5 st (and -7) instead of -12.
- **Triad Time's window laid out as ShaperBox's**: the grid across the whole window at ShaperBox's proportions
  (3.48 : 1, 1020 x 293), the knobs and the dice in a row under it, TIME / VOLUME next to the title. The same angle is
  now the same pitch, and Thomas's ShaperBox pattern sits on the same grid lines point for point.
- ShaperBox's Time view: labels 0, 1/4 ... 4/4 across and 0, -2/4, -4/4 down; bright quarters both ways; broad faint
  stripes in the triangle under the corner-to-corner diagonal instead of a fill under the line.
- **Pitch labels on the TIME lines** (Thomas: "say how many semitones it's moving it up or down, or if it's reversing ...
  FWD or REV ... in the settings"): each straight line reads which way it plays and how far the pitch moves:
  "FWD -12 st" (half speed), "FWD -24 st" (a quarter), "FWD +12 st" (double), "REV 0 st" (backwards at normal speed),
  "REV -12 st" (backwards at half speed), "FREEZE". A setting, **Pitch labels**: Off, While dragging a point (the
  default) or Always, on every line; in Triad Time's new gear (top right of its window) and in Triad's gear menu
  (TRIAD TIME), shared by every Triad and Triad Time on the computer.
- Tested: that cut plays a 440 Hz tone at 220.0 Hz (-12 st). Triad's LFO tab is unchanged.

### 0.10.0 ("Triad v9"): Triad Time's grid as in ShaperBox

0.10.0 (2026-10-20), Thomas, comparing Triad Time with ShaperBox 3 side by side: "make the grid ... exactly the same as a
shaper box ... a 4x4 square ... so there's more small points ... keep the LFO tab on Triad exactly the same":
- **Triad Time's TIME grid** reaches one pattern length back (ShaperBox's "As LFO Length", Gross Beat's convention: the
  diagonal holds the sound still) instead of two, with labels 0, -1/4 ... -4/4.
- **ShaperBox's grid layout** in Triad Time (both envelopes): 4 x 4 big squares; across, quarters divided in 4 by
  bright lines and in 4 again by faint ones (GRID 1/64, the new default: 64 columns for a 1-bar pattern); down, 16 rows;
  points snap to all of them, so the same small repeats as in ShaperBox can be drawn (measured from Thomas's photo of
  ShaperBox 3 at 1 bar: bright lines every 1/16, faint every 1/64, rows every 1/16 of the range).
- The TIME presets keep their sound on the new grid; patterns saved with 0.8 / 0.9 are converted when they open.
- Triad's LFO tab grid is unchanged (8 rows, its own snapping).
- **Train the dice** (Thomas: "a green check mark and an X under the button ... it trains the randomizer on what you like
  and don't like"): under NEW SHAPE in every generator (Triad's LFO tab and Triad Time), the green check likes the
  shape on the grid and the red X dislikes it and draws a new one. The dice then draws 24 shapes and offers one of the
  best for that taste (curve, make-up of steps / curves / lines, and the Angular / Soft / Chaos it was made with);
  nothing nearly the same as a disliked shape comes back. Tested: liking straight shapes and disliking curvy ones took
  the dice's curvy segments from 48% to 6% at Angular 100 / Soft 60. One taste each for LFOs, TIME and VOLUME.

### 0.9.0 ("Triad v8"): split mode (Triad Split)

0.9.0 (2026-10-19), Thomas: "work on the split mode now ... another VST or tool that allows for that signal to be split
between each band and put onto a new insert or mixer effect":
- **Triad Split** by Thomas Ross, a plug-in of its own with its own installer (Triad-Split-0.9.0-macOS-AppleSilicon.pkg,
  VST3 + AU): Triad's band split, playing one band (**PLAY**: ALL, 1 ... 10, NONE), so each band can go to its own FL mixer
  insert or Ableton rack chain and the DAW's own plug-ins (Gross Beat, Fruity plug-ins, Saturator...) work on one band.
  Same display as Triad (the frequency panel is now one shared part), same crossovers and slopes, up to 10 bands; the band
  tracks add back up exactly to the original (tested: three Triad Splits playing bands 1, 2, 3 sum to ALL, and ALL equals
  Triad with empty bands, bit for bit).
- **LINK groups A to H**: every Triad Split in a group shares band count, dividers and slopes, live (a divider dragged on
  one moves every other within a block of audio; the windows follow 60 times a second). The VST3 and AU builds find the
  same groups (one shared block per DAW process). Adding or removing a band moves every linked track's PLAY with its band;
  the window warns when a band is on no track or on two.
- **Band outputs**: every band on its own output (Band 1 = outputs 3-4 ...), for FL's Patcher or routing plug-in outputs to
  mixer inserts (FL's wrapper: Processing > Connections). PLAY NONE silences the main output.
- **Smoother, sharper interface** (Thomas: "make the whole plugin run at 60+ fps ... 120 fps if there's not issues"): every
  animation follows the screen at up to 120 frames a second (ProMotion), stepping down to 60 when frames come late, the
  drawing takes too long or the audio is busy; automation (dividers, slopes, band edges, Triad Time's patterns, Triad
  Split's display) moves on every frame instead of 15 times a second; playheads (LFO tab, Triad Time) and the knobs' LFO
  dots glide between audio blocks; cached pictures are put back pixel for pixel (no soft edges at 125% and similar sizes);
  rows that did not change are no longer redrawn several times a second.

Next:
- Thomas tests Triad Split in FL (sends to inserts; Patcher) and Ableton (rack chains).
- **A routing helper** per DAW, as an add-on: an FL Patcher preset (and an FL Python script that names and routes the band
  inserts), an Ableton rack preset or Max for Live device that builds the band tracks.
- Possibly: Triad itself joining a LINK group (its dividers driving the Triad Splits) and band outputs on Triad.
- **Pro Tools (AAX) version** of Triad, Triad Time and Triad Split (needs Avid's developer program and signing).
- **Final name: BANDS** (all caps). Triad stays the name while testing; the rename comes last.

### 0.8.0 ("Triad v7"): Triad Time and band MUTE

0.8.0 (2026-10-19), from Thomas's notes after v6 ("I've had friends use it and they love it"):
- **Triad Time** by Thomas Ross: a Gross Beat-style time and volume gate, Triad's only built-in effect (BUILT IN at the top of
  the effect list), and also a plug-in of its own with its own installer (Triad-Time-0.8.0-macOS-AppleSilicon.pkg, VST3 + AU)
  for any track in any DAW. TIME (how far back it plays, from a 12 s rolling buffer: repeats, half speed, tape stops,
  reverse, scratches) and VOLUME (the gate), 12 patterns each, drawn with Triad's own LFO system: the same grid, toolbar,
  every preset folder, the USER folder and the generator (HARD / MIX / SOFT and the dice). SMOOTHING crossfades every jump in
  time (like Gross Beat's click reduction, without the dip); ATTACK and RELEASE smooth the gate; TIME MIX and VOLUME MIX.
  No latency; synced to the song.
- **MUTE** at the bottom of every band strip (automatable, saved; it stays with its band when bands are added, removed or moved).
- Effects that Triad plays in pieces (an LFO on one of their knobs) now see the song's position move on with each piece,
  so tempo-synced effects (Triad Time, gates, delays) stay exactly in time.
- The LFO tab's grid, toolbar, palette and generator became one shared part (Triad's LFO tab and Triad Time use the same code).

Next (Thomas, 2026-10-19):
- **Split mode** (the rest of the week): each band out to its own track in the DAW, so FL's, Ableton's and Pro Tools' own
  plug-ins can work on one band (Gross Beat on the highs, Saturator on the mids...). FL: Triad's band outputs to mixer
  inserts and a sum bus, or Triad inside Patcher; Ableton: an Audio Effect Rack with three Triads set to "this band only",
  dividers linked. Diagram: https://claude.ai/artifact/HBwKVrGQvg8oCEBhn4Lbhe
- **A routing helper** per DAW, as an add-on: an FL Patcher preset (and an FL Python script that names and routes the band
  inserts), an Ableton rack preset or Max for Live device that builds the band tracks. Thomas's idea: in Triad, choose for
  each band "route to insert N"; Triad itself cannot change FL's routing, but its band outputs can be mapped to inserts once
  in FL's plug-in settings, which a script or template can do for you.
- **Pro Tools (AAX) version** of Triad and Triad Time (needs Avid's developer program and signing).
- **Final name: BANDS** (all caps). Triad stays the name while testing; the rename comes last.

### 0.7.0 ("Triad v6"): Auto-Tune and live vocals

0.7.0 (2026-10-18), from Thomas's vocal test with v4 (Ableton): Auto-Tune Pro (retune speed 0), UADx LA-2A, Fresh Air
and a 3-band EQ in each of three bands, dividers at 170 Hz and 9.41 kHz at 96 dB/oct. It "works pretty good", but in
Auto-Tune's normal mode there was a lot of latency, and Low Latency mode brought some warbling.

What the screenshots and the new tests showed:
- **The latency is Auto-Tune's own.** Auto-Tune Pro reports 55.6 ms in its normal mode (2.3 ms in Low Latency mode),
  the LA-2A 1.8 ms: 57.4 ms per band (4.1 ms), and Triad reports exactly that, adding nothing. The new test does the
  same with stand-ins that delay exactly what they report: through Triad the sound comes out exactly the effects'
  latency later (no echo, no extra delay), three copies in three bands cost one copy's latency, a band without the
  effect is lined up exactly, and Triad gives the effect the DAW's block size. The same effect on a plain track is just
  as late, so a plain track that felt instant most likely had Auto-Tune in Low Latency mode, or was heard through the
  audio interface's own monitoring.
- **The warble:** each band's copy of Auto-Tune hears only its part of the voice (in the test, band 3 above 9.41 kHz
  got 0.4% of a sung note), and the copies correct separately.
- **Steep dividers delay the voice's low notes:** right at a divider the sound comes out later, more with steeper
  slopes: at 170 Hz 17 ms at 96 dB/oct, 2.6 ms at 24 (at 242 Hz: 12 ms and 1.9 ms). Not latency (Triad reports none
  and the rest of the voice is on time), but a voice's lowest notes trailing behind can feel like it.

New in 0.7.0:
- **PRE**: a chain of effects on the whole sound before the split, for pitch correction, a de-esser, a gate, the first
  EQ. PRE button beside + BAND; its strip comes first, left of the bands; slots, mix knobs, LFOs, links to knobs,
  LEARN, copy / paste plugin state, dragging, undo; no band mix or volume. Its latency comes first and adds to every
  band's; LIVE passes its latency-adding effects by too. Saved with projects (older projects open with PRE empty).
- **POST** (Thomas, 2026-10-18): the same, after the bands come back together (a limiter, a bus compressor, a final
  EQ or reverb). POST button beside PRE; its strip comes last, right of the bands. Total latency = PRE + the slowest
  band + POST.
- **PRE and POST in the LFO and MATRIX tabs** (Thomas, 2026-10-18): the LFO tab has a PRE chip before band 1 and a
  POST chip after the last band, with the same LFOs, shapes, lengths, + ROUTE TO... (every slot mix and every knob
  inside their effects) and LEARN as any band; the MATRIX tab has a PRE panel first and a POST panel last.
- **All dividers in one click:** "All dividers: hard wall (96 dB/oct)", "default (24)" and "soft (6)" in every slope
  list and in a band's right-click menu.
- **Each slope in the list shows its timing** at that divider (how much later the sound there comes out), computed
  from the filters and checked against a measurement in the tests. At Thomas's dividers: 250 Hz 11.6 ms at 96 dB/oct,
  1.8 ms at 24; 2.5 kHz 1.2 ms and 0.2 ms.
- **Faster when an effect changes its latency** (Auto-Tune's Low Latency switch): Triad lines up again within about
  35-75 ms (was up to 200 ms), silent for that moment rather than out of line.
- **Smoother while recording:** a new effect is prepared, and a removed one released, outside the moment the audio
  waits for the bands to change, so loading or removing a big plug-in no longer makes Triad skip blocks. Removed
  effects are deleted in steps: the window first, the effect 0.4 s later, at least 1.5 s after it left, a few copies
  of a plug-in at a time (crashes seen in the v6 CI runs inside plug-ins' own message queues when many went at once).
- Latency readouts of 10 ms and more in whole milliseconds ("57 ms", was "57.4375 ms latency").
- For tracking: Auto-Tune once, in PRE, in Low Latency mode; dividers in the voice's low notes at 24 dB/oct or softer.

New tests in 0.7.0 (Thomas: "an extra strong stress test for the recording capabilities", with a free pitch corrector
and the dividers at 250 Hz and 2.5 kHz):
- **Live vocals with a pitch corrector** (integration tests, every build): a stand-in with Auto-Tune's latencies
  (55.6 ms, 2.3 ms in Low Latency mode) on a plain track vs in Triad (one band, three copies, PRE, POST); Triad's
  latency is exactly the effects', switching Low Latency mode is silent for under 0.15 s, and what each copy hears.
- **Recording through Triad** (stress test, section 7): a sung vocal take through a vocal session (pitch corrector in
  PRE, 3 bands at 250 Hz / 2.5 kHz, real effects, every LFO on, a knob link, the window open, a save every second) at
  32 / 64 / 128 samples, 44.1 / 48 / 96 kHz, while knobs, LFOs, windows, tabs and undo steps change every 150 ms: no
  block silenced by Triad, no not-a-number. Then layout changes every 400 ms (load / remove / drag effects, bypass,
  LIVE, undo / redo, all dividers, add / remove a band, the corrector's latency switch), each reported with how long
  it interrupted the sound.
- **Pitch correction with a sung vocal** (stress test, section 8, Graillon 3 Free on the CI Mac): the same take on a
  plain track, in Triad with one band, in PRE with 3 bands, and a copy in each band at 96 and at 24 dB/oct: latency
  reported vs heard, how far from the note and how much wobble is left, and how different it sounds from the plain
  track.

Known: under thousands of fast random edits with 40-50 real plug-ins, an intermittent crash inside ChowMatrix's or
FILT-R's own code was seen in some v6 test runs (every plug-in passes on its own, and the final run passed); keep an
eye on it with Chowdhury plug-ins.

Still to do from this list:
- Measure Thomas's exact chain in FL Studio and Ableton on the Mac (plain track vs Triad, AU vs VST3) once he has
  tried v6.
- Maybe a **1-band / full-range** switch (no split at all) for chains that should not be split.

### 0.6.0 ("Triad v5")

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
