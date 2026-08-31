# RT60 Room Decay Corrector v0.4 — Intelligent Decay Match

Standalone room measurement + closed-loop correction application and VST3 monitor-correction plugin.

## Priority #1

The project has one primary goal before any 3D/AI-room features are added:

**Measure which frequencies decay too slowly, choose one common decay target, correct the persistent modes, re-measure the real room through the active correction, and keep refining until the measured decay is as uniform as safely achievable at the measurement position.**

## What changed in v0.4

### 1. Intelligent common target

`AUTO TARGET` derives one target decay from the reliable measured bands using a robust lower-percentile statistic. It deliberately does not chase the single shortest/noisiest band. Manual Target RT60 remains available by disabling AUTO TARGET.

The corrector never boosts a short-decay/null band merely to make its decay longer.

### 2. Measurement confidence and noise-floor rejection

Every 1/3-octave decay result now includes:

- fitted RT60 / EDT / T20 / T30
- fit quality
- estimated late noise floor
- usable dynamic range
- confidence score

Low-confidence bands are excluded from automatic correction and from automatic target selection.

### 3. Modal estimator instead of only broad-band RT60

A long 1/3-octave band is not assumed to be one exact mode. The engine:

- inspects the late-field spectrum
- performs sub-bin peak interpolation
- estimates modal Q
- isolates the candidate with an adaptive narrow band-pass
- fits that mode's own late decay envelope

This produces a much better estimate of the actual pole used by the decay shaper.

### 4. Digital-twin optimization from the measured impulse response

Before the correction is played into the room, v0.4 uses the measured impulse response itself as a local digital twin.

The engine simulates the correction on that measured IR and performs constrained parameter search in two phases:

1. identify/refine the room-pole radius while the replacement pole is fixed at Target;
2. allow only a small fine adjustment of the replacement pole.

This prevents the old failure mode where zero and replacement pole could move together into a false solution.

### 5. Real closed-loop verification

The verification sweep is played **through the active correction**. The microphone capture is analysed again and becomes `Measured After`.

The convergence report now evaluates:

- number of trusted bands
- correctable bands matched to Target
- overshoot
- mean absolute decay error
- RMS decay error
- full decay spread in ms
- uniformity score

If the real room differs from the digital-twin prediction, residual passes retune existing modal stages cautiously instead of blindly stacking another filter on the same mode.

### 6. Real-time monitor correction

The standalone app writes the active measured profile to the user application-data folder. The VST3 loads that profile and applies the optimized correction in real time to program audio. For Cubase, use the VST3 on the Control Room monitor path so the room correction is not rendered into exports.

## Standalone workflow

1. Select the measurement microphone input and monitor output.
2. Leave `AUTO TARGET` enabled for intelligent common-decay matching, or disable it and enter a manual target.
3. Set maximum correction frequency and sweep level.
4. Press `MEASURE ROOM`.
5. The app reports trusted bands and the common target.
6. Press `AUTO CORRECT RT60`.
7. The measured IR is optimized as a digital twin.
8. A new sweep is played through the correction.
9. `Measured After` is the real microphone result, not a prediction.
10. Residual mismatch is refined for up to six physical verification passes.

## Deterministic tests

The core test suite currently covers:

- decay model behaviour
- sweep/deconvolution/RT60 measurement
- modal correction and profile serialization
- intelligent two-mode digital-twin optimization
- automatic common-target selection and decay-uniformity scoring

The two-mode deterministic test contains approximately:

- 63 Hz / 900 ms
- 125 Hz / 700 ms
- common target 300 ms

The intelligent engine detects the two modes, optimizes the measured model and brings both simulated measured decays close to the 300 ms target.

## Important physical boundary

This is loudspeaker-to-microphone transfer-function correction at the measured position. Software cannot make the physical RT60 identical at every point in a room from one microphone position, and it should not use aggressive boosts to fill spatial nulls. Multi-position/3D modelling is intentionally postponed until the single-position closed-loop engine is proven with real-room measurements.

v0.4 is still an engineering build. Passing deterministic tests proves the DSP path is internally coherent; real-room measurements are required before claiming production-grade acoustic correction.

## Verified-pass safety in v0.4

The intelligent closed loop treats the real verification sweep as the authority, not the digital-twin prediction.

- A newly designed profile is not persisted until the first corrected sweep proves a meaningful measured improvement.
- The verification objective keeps the denominator anchored to trusted baseline problem bands, so a band that disappears because of low SNR cannot be counted as a success.
- Modal stages require a trustworthy late-field exponential decay fit and sufficient modal SNR; a frequency-response peak by itself is not enough to create a cancellation stage.
- Feedback retuning measures decay at the exact modal frequency and can move the replacement pole in either direction: stronger when decay remains long, weaker after over-damping.
- Every accepted verification pass becomes the new best-known profile. If a later pass regresses or overshoots, the standalone restores the best measured profile and stops instead of forcing the target.
- If the first corrected pass does not beat the raw-room measurement, the candidate is rejected and is not saved.

The target is therefore a goal with a safety stop, not a promise. A real room may stop at the best verified result when measurement confidence or physical controllability prevents further improvement.
