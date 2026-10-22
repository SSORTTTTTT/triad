# Dice sources

Makes `Source/GeneratorData.h`: what each of the dice's sources (the FROM chips under NEW SHAPE) learned from the
presets Thomas sent. The presets themselves are not stored in the repository (they are other companies' and
producers' packs); only what was learned from them is.

Run from a working folder that has the presets next to it:

```
work/
  sbx/SHAPERBOX/**/*.fst        Thomas's ShaperBox presets (FL Studio .fst)
  gb-zip/Gross Beat/*.fst       his Gross Beat banks
  gb/gb-model.json              made by tools/time-presets (model.py), the Gross Beat time runs and jumps
  gen2/                         run the scripts here
```

1. `python3 sbx_extract.py` reads every LFO wave in the ShaperBox presets (zlib-compressed JUCE ValueTree in FL's
   plug-in state; points x 0..8192, y -1..1, factor 3 straight / 1, 2 curved) into `sbx-waves.json`.
2. `python3 cells.py` cuts every ShaperBox wave (not the Time waves) and every Gross Beat volume envelope into quarter
   and eighth cells, merges near-duplicates with weights, and counts how shapes repeat their cells (AAAA, AABA, ABAB
   ...): `cells.json`.
3. `python3 gendata_header.py ../../triad-repo/Source/GeneratorData.h` writes the header (cells, repeat schemes, and the
   Gross Beat time model).

`Source/Generator.cpp` turns these into shapes: `fromCells` (SHAPERBOX, GROSS BEAT VOLUME), `timeRuns` (GROSS BEAT
TIME), `fromTimePresets` (TIME PRESETS); INFILTRATOR is the original dice. In Triad's LFO tab (`Style::lfoChaos`)
Chaos also favours triangles, squares and busier cells; Triad Time's dice does not use that.
