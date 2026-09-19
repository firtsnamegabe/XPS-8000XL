# XPS-8000XL

A Linux VST3 + Standalone drum sampler built with JUCE.

*Xolo Production Sampler 8000XL* — workflow modeled on classic hardware
sample-groovebox conventions: 4 banks x 16 pads, drag-and-drop sample
loading, choke groups, chop tools, filters, lo-fi bit-crush and
distortion, reverb ("Xolo Hall") and delay ("Xolo Echo") sends, velocity
layers, MIDI Learn, light/dark theme, portable Kit save/load.

<img width="1178" height="984" alt="xolo-xolo" src="https://github.com/user-attachments/assets/62067397-8553-4f3e-8637-d84986c58da8" />
<img width="1178" height="984" alt="xolo-axolo" src="https://github.com/user-attachments/assets/70a15479-570f-405f-b62b-11c8100c5ea7" />


## Features

**Sampling & playback**
- 4 banks x 16 pads, drag-and-drop loading (WAV/AIFF/FLAC/OGG/MP3), or
  right-click a pad to load, record, or manage it
- Direct recording from any connected audio input, with a live input
  level meter
- Per-pad volume, pitch, pan, attack/release, one-shot/hold/loop
  (+ping-pong)/reverse playback modes, and choke groups
- Poly/mono voice modes, velocity sensitivity, and per-pad trigger offset
- Up to 3 extra velocity layers per pad (for velocity-switching or
  stacking), each with its own independent volume

**Filters & effects**
- Per-pad low/band/high-pass filter with cutoff and resonance
- Lo-fi bit-crush and sample-rate reduction, plus a drive/distortion stage
- Two shared sends - "Xolo Hall" reverb and "Xolo Echo" delay
- A MASTER tab with input/output gain and a master-bus compressor

**Chopping & editing**
- Zoomable waveform view with three auto-chop modes (transient, equal,
  lazy) plus fully manual marker placement, dragging, and deletion
- Snap-to-zero-crossing or snap-to-beat-grid while placing markers
- Drag a chopped region straight onto any pad
- Non-destructive trim, plus a destructive "Trim Sample" and "Reset
  Effects" for starting clean
- BPM-synced time-stretch and a VARISPEED/TIME LOCK pitch mode

**Workflow**
- Drag a pad onto another pad (resample or swap), or across banks
  (hovering over a bank tab switches to it mid-drag)
- Dropping a file, pad, or chop region onto an already-loaded pad offers
  a Layer-or-Replace choice instead of silently overwriting
- Full MIDI Learn and chromatic note mapping
- Portable `.xolo` Kit files that embed the actual sample audio, not just
  file paths - a kit loads correctly on a different machine with none of
  the original files present
- Light/dark theming (XOLO/AXOLO), a resizable UI, and a global KILL
  switch to stop every voice at once

## Build

Requires CMake 3.22+, a C++20 compiler, and the usual JUCE Linux
dependencies (ALSA/JACK dev headers, X11, FreeType - see JUCE's own docs
for the exact package list for your distro).

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
```

Uses CMake `FetchContent` to pull JUCE 8.0.4 automatically on first
configure. If you already have a local JUCE checkout:

```bash
cmake -B build -DJUCE_LOCAL_DIR=/path/to/JUCE
```

Outputs land under `build/XPS8000XL_artefacts/`:
- `VST3/XPS-8000XL.vst3`
- `Standalone/XPS-8000XL`


Only ever built and tested on Linux. It should build on Windows and
macOS too - nothing in the source or CMake setup is Linux-specific - but
that's genuinely untested; treat it as unverified until someone confirms it.

## License

Licensed under the GNU General Public License v3.0

Built with [JUCE](https://juce.com), used here under its AGPLv3 license.
The VST3 format is implemented via the Steinberg VST3 SDK, bundled with
JUCE. VST is a registered trademark of Steinberg Media Technologies GmbH.

This project is not affiliated with, endorsed by, or associated with any
hardware or software sampler manufacturer.

# IMPORTANT NOTE 

I'm not a programmer myself I have little experience - the majority of this codebase
was written with the help of AI. I directed every
feature, made every design decision.
