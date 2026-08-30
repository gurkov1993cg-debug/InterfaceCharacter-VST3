# Prism Sound Lyra 1 model

Current development is focused on one target only: **Prism Sound Lyra 1 analogue line output / DAC path**.

## Published anchors used

From Prism Sound's Lyra specifications:

- 0 dBFS = +18 dBu in +4 dBu mode;
- output impedance: 100 ohm balanced;
- THD: -107 dB at -0.1 dBFS;
- THD+N: -106 dB at -0.1 dBFS;
- dynamic range: 115 dB;
- LF response: -0.05 dB at 8 Hz, -3 dB below 1 Hz;
- HF -3 dB: 22.0 kHz at 44.1 kHz, 23.9 kHz at 48 kHz, 47.8 kHz at 96 kHz, 76.0 kHz at 192 kHz;
- inter-channel crosstalk: below -135 dB at 1 kHz and below -120 dB from 20 Hz to 20 kHz.

Source: https://www.prismsound.com/music_recording/products_subs/lyra/online_manual/specifications.htm

## What the DSP models

The Lyra path is no longer a static EQ preset. It contains:

1. a stateful sub-Hz high-pass chosen to reproduce the published 8 Hz/LF points closely;
2. a sample-rate-dependent Butterworth reconstruction roll-off at the published -3 dB HF point;
3. very low level-dependent H2/H3 polynomial distortion constrained to the published THD magnitude;
4. a -115 dBFS RMS residual-noise floor using neutral TPDF as a temporary spectrum;
5. -135 dB 1 kHz stereo crosstalk;
6. the filter phase rotation naturally created by the stateful IIR stages.

## What is not claimed yet

This is a **published-spec model**, not a measurement-perfect hardware clone. Prism does not publish the exact H2/H3 phase split, residual-noise spectrum, overload recovery, or unit-to-unit component tolerances. Those parameters should be replaced after loopback measurements from a real Lyra 1.
