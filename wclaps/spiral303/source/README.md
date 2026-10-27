# Spiral303, WebAssembly build

The wasm half of Spiral303, a TB-303 acid line and step sequencer. Upstream source:
<https://github.com/Spiralfractalplugins/spiral303>, tag `v0.37.2`. The native plugin is
built with JUCE; these files are the JUCE-free DSP core plus the CLAP glue the project
already uses for its `wclap` build (`tools/build_wclap.sh` upstream), compiled here with
Emscripten instead of the wasi-sdk. The DSP code is identical, so the sound is bit-exact
with the native plugin.

## What is here

- `Source/wclap/` - the CLAP implementation: entry point, the plugin itself, parameter
  table, state save/load, the sequencer, and the `clap.webview/3` page server.
- `Source/dsp/`, `Source/core/`, `Source/PluginParameters.h` - the DSP core and a small
  stand-in for the JUCE types it uses (`JuceLite.h`).
- `presets/` - 102 factory presets, hand-written JSON in the project's own format, the
  same files the native build ships. No third-party samples: the plugin synthesises
  everything, it ships no audio.
- `web/` - the interface: one HTML page, one plain JavaScript file, one stylesheet, all
  written by the author. The module embeds them (see below) and also serves them beside
  `module.wasm`.
- `tools/embed_presets.py`, `tools/embed_web.py` - turn `presets/` and `web/` into C++
  headers, run by `build.sh`.
- `emscripten_glue.cpp` - the empty `emscripten_notify_memory_growth` hook.
- `LICENSE` - GPL-3.0-only, the license of the whole project.

The page is served from the module's own memory through `clap.webview/3`
(`get_uri` returns `/index.html`, `get_resource` streams the embedded files), so the
plugin works from a bare `module.wasm` as well as from the bundle.

## Building

`build.sh` needs `em++` (Emscripten 3.1.51), `python3`, and the CLAP headers
(free-audio/clap 1.2.10) in `$CLAP_INCLUDE`. It writes `module.wasm` and the three
interface files into `$OUT`.

To check the module by hand the way upstream does, the repository carries a Node smoke
host (`wclap/tools/wclap-host.mjs` upstream) that loads `module.wasm`, feeds it MIDI and
expects samples.

## Reproducing this folder from upstream

```
git clone https://github.com/Spiralfractalplugins/spiral303.git
git -C spiral303 checkout v0.37.2
# copy Source/wclap/*.{cpp,h}, the headers listed in build.sh, Source/core/,
# Source/PluginParameters.h, presets/*.json, web/* and wclap/tools/embed_*.py
# into this folder, keeping the paths.
```
