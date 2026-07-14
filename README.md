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

On macOS, the plugin is typically installed to:
- `~/Library/Audio/Plug-Ins/VST3/PhotoSynthesis.vst3`

If `COPY_PLUGIN_AFTER_BUILD` is enabled (default in this project), build/install is automatic.

## Futures / Roadmap

- Higher-quality anti-aliasing for scanned waveforms
- Additional modulation sources (MPE/aftertouch/macros)
- Improved preset browser and tagging
- Performance profiling and CPU optimization pass
- Extended export/import tooling for scanner snapshots
- Additional plugin formats and packaging automation

## License

This project is released under the MIT License.
