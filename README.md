# Interface Character - Lyra 1

Current development is focused on **Prism Sound Lyra 1** first. The VST3 now starts on Lyra 1 by default; the older Apollo/TDM/HDX entries remain only as prototype placeholders and are not the current modelling target.

## Lyra 1 DSP in this version

The Lyra path models the published analogue line-output/DAC behaviour rather than applying a simple EQ curve:

- stateful LF response: approximately -0.05 dB at 8 Hz and -3 dB below 1 Hz;
- sample-rate-dependent reconstruction roll-off using the published -3 dB points for 44.1/48/96/192 kHz;
- very small level-dependent H2/H3 nonlinearity constrained to the published -107 dB THD magnitude near full scale;
- approximately -115 dBFS RMS residual noise;
- -135 dB stereo crosstalk at 1 kHz;
- phase behaviour produced by the actual stateful filter stages, not a static magnitude-only EQ.

Prism Sound's published Lyra analogue-output specification is the engineering reference for these limits. The exact harmonic distribution and residual-noise spectrum are not published, so those remain conservative assumptions until a physical Lyra 1 is measured.

Reference: https://www.prismsound.com/music_recording/products_subs/lyra/online_manual/specifications.htm

## DSP tests

```bash
g++ -std=c++17 -Wall -Wextra -Wpedantic \
    -Isrc src/CharacterProcessor.cpp tests/test_character.cpp \
    -o build/interface_character_tests
./build/interface_character_tests
```

The tests verify bypass behaviour, finite output, the Lyra LF anchor, the 48 kHz HF -3 dB anchor, residual-noise level, 1 kHz crosstalk, and the intentionally tiny mid-band difference.

## VST3 build

The existing GitHub Actions Windows x64 workflow builds the plug-in against the official Steinberg VST3 SDK and packages the resulting `.vst3` bundle.

## Accuracy boundary

A VST3 can reproduce the measurable signal-domain signature of the Lyra output path, but it cannot become the physical Lyra DAC, analogue output driver, power supply, or clock. A true clone still requires loopback captures from a real unit for harmonic phase/distribution, noise spectrum, overload recovery and unit-specific tolerances.
