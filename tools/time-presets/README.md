# Triad Time's TIME folders (trained on Gross Beat banks)

Makes `Source/TimePresets.h`: 1,000 new TIME patterns in five folders (200 each) (OCTAVE DOWN, OCTAVE UP, REVERSE, CHOPS,
PITCH + CHOPS), drawn by a generator that learned how the time envelopes in Thomas's Gross Beat banks are built.
The banks themselves are not stored here, and no bank pattern is copied: every pattern made is checked to be at
least 0.12 beats (mean difference over the bar) from every pattern in the banks.

    GB_BANKS="/path/to/Gross Beat" python3 dataset.py   # gb-time.json: the unique time envelopes (first 36 slots of each bank)
    python3 model.py                                    # what was learned (runs, contents, jumps) -> gb-model.json
    python3 jumps.py                                    # how the jumps between runs go
    python3 generate.py                                 # batch.json: the first 120 (seeded: the same batch every time)
    cp batch.json batch-120.json; python3 generate_more.py   # 880 more (200 a folder), the first 120 kept
    python3 header.py ../../Source/TimePresets.h
    python3 sheet.py batch.json batch.png               # a picture of the batch

- `gbparse.py`: Gross Beat banks (.fst): the plug-in state (event 213) holds 72 named slots (36 time, 36 volume); each
  slot: a name (length byte), int 5, int 5, 7 zero bytes, int 3, int 3, the number of points, then 24 bytes a point
  (double x as beats since the previous point, double y with 1 = live and 0 = 2 bars back, float tension, a byte
  for the curve type, three flag bytes). The type applies to the line into the point (0 single curve ... 2 hold ...).
- In Triad Time's units: delay in beats = (1 - y) x 8; Triad Time's y (1 bar long, 1 bar back) = 1 - delay / 4.
