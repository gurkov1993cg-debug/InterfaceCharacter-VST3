# Build status

Verified in the current workspace:

- DSP test binary compiles with GCC 13.3;
- all four profiles process stereo audio without NaN/Inf;
- bypass/mix-zero path is bit-identical in the test;
- Apollo starting profile produces a controlled nonlinear difference;
- VST3 adapter source passes a GCC C++17 syntax check against Steinberg VST3
  SDK 3.8.1 headers with `RELEASE` defined.

The final Windows `.vst3` binary still needs a Windows x64 toolchain (Visual
Studio 2022 or compatible) and the recursive official VST3 SDK checkout. The
Linux workspace cannot produce the Windows DLL package used by Cubase.

`.github/workflows/windows-vst3.yml` performs that build on GitHub Actions,
runs the DSP tests, and publishes `InterfaceCharacter-VST3-Windows-x64` as a
downloadable artifact.
