# PhotoSynthesis

This folder contains a minimal JUCE-based C++ plugin workspace to start your picture-driven synth work.

Current status:
- Builds a synth plugin (VST3 + Standalone target)
- 32-voice polyphonic sine synth core
- ADSR parameters and output gain in a simple UI
- Ready to evolve into image-scanned oscillator logic

## Clean Setup On A New Machine

If you copied this project from another operating system, delete the existing `build/` folder before configuring on the new machine. The copied CMake cache stores absolute compiler and dependency paths from the old host and cannot be reused safely across Linux and macOS.

## Prerequisites

Common requirements:

- CMake 3.22+
- A C++20 compiler
- Git
- Ninja (recommended)

### macOS

Install the Apple command line tools:

```sh
xcode-select --install
```

Install Ninja with Homebrew:

```sh
brew install ninja
```

### Linux

Example (Debian/Ubuntu style):

```sh
sudo apt update
sudo apt install -y build-essential cmake ninja-build \
  libasound2-dev libjack-jackd2-dev libx11-dev libxext-dev libxinerama-dev \
  libxrandr-dev libxcursor-dev libxcomposite-dev libfreetype6-dev libglu1-mesa-dev \
  mesa-common-dev pkg-config git
```

## Configure

From this folder:

```sh
rm -rf build
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
```

Notes:

- JUCE is fetched automatically from GitHub by CMake, so the first configure needs network access.
- Keep the same generator for re-configures, or clear `build/` before switching generators.

## Build

```sh
cmake --build build -j
```

## Artifacts

After build, look in:

- Standalone app: `build/PictureWaveSynth_artefacts/Release/Standalone/`
- VST3 plugin bundle: `build/PictureWaveSynth_artefacts/Release/VST3/`

On macOS, copy the `.vst3` bundle to:

- `~/Library/Audio/Plug-Ins/VST3`

On Linux, copy the `.vst3` bundle to one of these paths:

- `~/.vst3`
- `~/.local/share/vst3`

Then rescan plugins in your DAW.

## Next steps for your concept

1. Replace sine voice with image-scanned wavetable generation.
2. Add image loader and scanner controls (angle, position, width).
3. Add RGBA mapping matrix to stereo (later multichannel).
4. Add anti-alias strategy for scanned waveforms.
