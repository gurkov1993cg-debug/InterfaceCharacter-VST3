# Roadmap

## Етап 1 — завършен прототип

- независим DSP core;
- четири профила;
- stereo processing;
- state-safe параметри;
- тестове за finite output и bypass;
- VST3 adapter source.

## Етап 2 — Windows build

- официален Steinberg VST3 SDK;
- Visual Studio 2022 x64;
- VST3 validator;
- generic editor с профил, Drive, Amount, Mix, Output и Bypass;
- инсталационен пакет за Cubase.

## Етап 3 — измерени модели

- реални loopback записи;
- автоматично извличане на честотни криви;
- fitted nonlinear transfer curves;
- AB и null-test набор;
- отделни factory presets само след измерване.

## Етап 4 — production quality

- качествен oversampling;
- sample-accurate parameter ramps;
- по-точна double-precision обработка;
- denormal защита;
- CPU benchmark и crash/validator regression tests.
