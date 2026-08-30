# Interface Character VST3

Това е първият самостоятелен DSP прототип за VST3 плъгин, който ще има
избираеми профили за:

- Universal Audio Apollo/UAD;
- Pro Tools TDM с референтен 888|24 път;
- Pro Tools HDX с референтен HD I/O път;
- Prism Sound Lyra 1.

## Какво има в тази версия

`src/CharacterProcessor.*` съдържа реалновременния DSP core. Той включва:

- измеримо-заменяеми low/high tone криви;
- level-dependent nonlinear stage;
- втори и трети хармоник;
- малко memory поведение;
- TDM-style 24-bit quantization режим;
- stereo crosstalk;
- mix, drive, output и bypass параметри.

Параметрите в първия прототип са консервативни стартови стойности. Те не са
представени като точни измервания на Universal Audio, Avid или Prism Sound.

## Компилация на DSP тестовете

С наличния компилатор може да се изпълни:

```bash
g++ -std=c++17 -Wall -Wextra -Wpedantic \
    -Isrc src/CharacterProcessor.cpp tests/test_character.cpp \
    -o build/interface_character_tests
./build/interface_character_tests
```

За пълния VST3 binary е необходим официалният Steinberg VST3 SDK и Windows
x64 build. Адаптерът вече е в `src/vst3/` и извиква същия DSP core; няма
отделна DSP логика за Cubase. При наличие на Visual Studio 2022 и SDK може да
се използва:

```powershell
cmake -S . -B build-vst3 -A x64 `
  -DBUILD_VST3_ADAPTER=ON `
  -DVST3_SDK_DIR="C:/src/vst3sdk"
cmake --build build-vst3 --config Release
```

SDK-то се изтегля рекурсивно от официалното хранилище на Steinberg. Не го
включвам в source архива, за да остане проектът малък и да се спазят неговите
лицензионни условия.

## Измервателен етап

За реална прилика ще се запишат loopback тестове през конкретните устройства
при еднакви sample rate и calibration:

1. sweep за честотната и фазовата характеристика;
2. 1 kHz и multitone при -36, -18, -6, -1 и 0 dBFS;
3. THD/хармоници спрямо ниво и честота;
4. stereo crosstalk и noise floor;
5. overload recovery и latency.

След това стартовите стойности се заменят с измерени профили и се проверяват с
AB/null test.

## Важно разграничение

TDM и HDX са цифрови системи, а Prism Lyra е конверторен хардуер. Един VST3
плъгин може да моделира техния измерим сигнален отпечатък, но не може да стане
реалният физически ADC/DAC или clock на устройството.
