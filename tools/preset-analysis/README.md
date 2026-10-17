# Preset analysis (for the LFO randomizer)

Readers for the preset formats Thomas uses, so the randomizer can learn from the shapes in them.
Presets themselves are not stored in this repository.

- `fst.py` — FL Studio `.fst` preset files (FLhd/FLdt event stream). Event 213 holds the plug-in state.
- `juce_vt.py` — JUCE ValueTree binary streams, as found zlib-compressed inside ShaperBox 3 states
  (`WaveStateModel` → `points` → `Point` with `x` 0–8192 ticks, `y`, `factor` = curve).
- Infiltrator 2 presets are XML: `MultipointEnvelopeData` → `node` (`x`, `value`, `curve`, `curveAux`, `shape`),
  and `envelopeSequencerData` → `step` (`state`, one 0/1 per effect row) for the step sequencer.
- Gross Beat `.fst`: state in event 213; envelope points are (double x, double y, 8 bytes of flags) records
  inside named slots — layout still being worked out.
