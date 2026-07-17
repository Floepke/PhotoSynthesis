# PhotoSynthesis

PhotoSynthesis is an image-driven software synthesizer built with JUCE and C++20.
It scans user-loaded images to generate stereo wavetable content and combines that with a modern modulation workflow, including multi-LFO routing and per-voice variation controls.

The project currently builds both:
- VST3 plugin
- Standalone application

## What It Is

PhotoSynthesis is designed around the idea that image geometry can be used as a sound source.
Instead of a traditional fixed oscillator, scanner modes traverse pixels from an image and map RGBA data into stereo output tables.

The synth includes:
- Polyphonic voice engine (up to 32 voices)
- Image scanner modes (Line, Oval, Rectangle, Triangle, Propellor)
- Modulation matrix with 32 assignable routes
- 8 LFOs with waveform selection, tempo sync, and extended random behavior

## Key Features

- Image-based wavetable synthesis
- Scanner shape controls with direct UI manipulation
- RGBA-to-stereo mapping (independent left/right channel routing)
- 8 LFO tabs with:
  - Sine, Triangle, Saw, Square, Random Steps, Random Linear, Random Perlin
  - Tempo sync divisions including triplets
  - Random phase per voice option for non-random waveforms
- 32-slot modulation routing system
- Per-voice modulation variation for random and random-phase-enabled LFO workflows
- ADSR envelope, gain, note drift, and live drift controls
- Polyphony selector and round-robin/voice-steal behavior
- UI zoom and persisted editor geometry
- Preset save/load and init reset workflow

## Build Requirements

- CMake 3.22+
- C++20 compiler
- Git
- Ninja (recommended)

### macOS

```sh
xcode-select --install
brew install ninja
```

### Linux (example)

```sh
sudo apt update
sudo apt install -y build-essential cmake ninja-build \
  libasound2-dev libjack-jackd2-dev libx11-dev libxext-dev libxinerama-dev \
  libxrandr-dev libxcursor-dev libxcomposite-dev libfreetype6-dev libglu1-mesa-dev \
  mesa-common-dev pkg-config git
```

## Configure And Build

```sh
rm -rf build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

Notes:
- JUCE is fetched automatically from GitHub during configure.
- If moving the project between machines/OSes, remove `build/` first.

## Output Artifacts

- Standalone: `build/PhotoSynthesis_artefacts/Release/Standalone/`
- VST3: `build/PhotoSynthesis_artefacts/Release/VST3/`

On macOS, the standalone app is also copied after each build to:
- `~/Applications/PhotoSynthesis.app`

On macOS, the plugin is typically installed to:
- `~/Library/Audio/Plug-Ins/VST3/PhotoSynthesis.vst3`

If `COPY_PLUGIN_AFTER_BUILD` is enabled (default in this project), build/install is automatic.

## Python RGBA Wavetable Generators

The `python/` folder contains scripts for building wavetable images for PhotoSynthesis.

### Mathematical RGBA Generator

Script:
- `python/generate_rgba_math_wavetable_image.py`

Channel models:
- `R`: Morph left-to-right: `sine -> triangle -> saw -> square`
- `G`: Additive overtone accent model (spectral slope + odd/even emphasis)
- `B`: Dual moving formant model (vowel-like spectral peaks)
- `A`: Phase-distortion family (adds edge/definition)

Example:

```sh
python3 python/generate_rgba_math_wavetable_image.py \
  --width 512 \
  --height 0 \
  --viewer-aspect 2.0 \
  --normalization global \
  --flip-vertical \
  --output python/wavetable_images/init_rgba_math_viewer_ratio_512.png
```

Notes:
- `--height 0` means: auto-compute height from `width / viewer-aspect`.
- If you want a fixed size, set both `--width` and `--height` explicitly.

Useful tuning flags:
- `--harmonics` (default `48`)
- `--viewer-aspect` (default `2.0`)
- `--green-odd-even-depth`
- `--green-slope-min`, `--green-slope-max`
- `--blue-formant-width-1`, `--blue-formant-width-2`, `--blue-formant-mix-2`
- `--alpha-distortion-max`, `--alpha-shape`
- `--normalization {none,global,per-channel,per-wave}`

### Merge Existing WAVs Into RGBA

If you already have four WAV wavetable sources:

```sh
python3 python/build_rgba_init_from_wavs.py \
  --red path/to/r.wav \
  --green path/to/g.wav \
  --blue path/to/b.wav \
  --alpha path/to/a.wav \
  --samples-per-waveform 256 \
  --target-frames 64 \
  --normalization global \
  --flip-vertical \
  --output python/wavetable_images/init_rgba_256x64.png
```

## Futures / Roadmap

- Higher-quality anti-aliasing for scanned waveforms
- Additional modulation sources (MPE/aftertouch/macros)
- Improved preset browser and tagging
- Performance profiling and CPU optimization pass
- Extended export/import tooling for scanner snapshots
- Additional plugin formats and packaging automation

## License

This project is released under the MIT License.
