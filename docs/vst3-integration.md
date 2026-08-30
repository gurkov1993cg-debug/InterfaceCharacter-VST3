# VST3 integration boundary

The DSP core is intentionally independent of a plugin SDK. The eventual VST3
adapter should:

1. expose one stereo audio bus;
2. expose `profile`, `driveDb`, `amount`, `mix`, `outputDb` and `bypass`;
3. call `prepare(sampleRate)` during setup;
4. call `setParameters(...)` at block boundaries;
5. call `processMono` or `processStereo` from the audio process callback;
6. keep all allocation and file I/O out of the process callback.

The adapter must be built against the official Steinberg VST3 SDK. The DSP
tests can run without the SDK, which keeps the model deterministic and easy to
compare against measured reference files.
