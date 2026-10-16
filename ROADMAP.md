# Triad roadmap

## Current testing version — 0.2.2 ("Triad v2")

- Multiband effects host: opens with 3 bands, add up to 10 while working, remove any.
- Effect slots per band in processing order (FL Studio mixer style), as many as needed: each band starts with Slot 1 and a "+ ADD SLOT" row underneath; every slot has its own dry/wet knob (value shown inside) and on/off light.
- Band Mix per band. VST3 and Audio Unit effects; effects-only plug-in list built without loading plug-ins inside the DAW (Triad Scanner helper for the rest).
- Live spectrum behind draggable crossovers; effect windows open on load.

## Next: mix modulation / LFO tab

Add a dedicated mix-modulation/LFO tab:

- Placement (agreed with Thomas, 2026-10-15): a tab switch at the top of the window, in the area where "BAND CROSSOVERS" is now — **BANDS | LFO**. BANDS is today's view (crossover display and band strips); LFO opens the new section. Start this after 0.2.2 has been tried in FL Studio and Ableton.

- A point-based line editor for drawing each band's mix over time, inspired by the editing interaction in Gross Beat and ShaperBox.
- A way to link each band's Mix control (and possibly slot mixes) to its own envelope/LFO.
- Editable points and segments representing 0–100% mix.
- Tempo synchronization, cycle length, curves/steps, and interaction with the manual mix control to be settled when this phase begins.
- Routing between bands/effects is planned alongside the LFO work; Thomas will describe it when that phase starts.

This is a saved development plan, not a scheduled background task.
