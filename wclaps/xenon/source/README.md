# Xenon

A granular synthesizer as a WebCLAP instrument. The page is 512 x 176, the size of openDAW's device panel (feature
`opendaw:device-panel`, extension `opendaw.device-panel/1`), every other host shows it in a window, 1 to 3 times as
large.

## Layout

```
build.sh        builds module.wasm and copies the page into $OUT (CLAP headers in $CLAP_INCLUDE)
plugin/         xenon.cpp, the CLAP plugin: grains, voices, modulation, state, the page's messages
page/           index.html, the waveform (WebGL2) and the controls
```

## Sound

- A note starts a voice, the voice emits grains at a density or synced to the tempo. A grain reads the sample at the
  position (plus spray and the voice's scan), with its size, pitch (key tracked, random within a scale), direction,
  envelope shape, filter (LP, BP, HP) and gain.
- Every grain is placed in 3D: azimuth and spread, elevation, distance (with random), motion around the head.
  Interaural time and level difference, head shadow on the far ear, a brighter or darker tilt for elevation, level
  and air absorption for distance, and a send into a small shared room.
- At most 128 grains sound at once, a grain that finds none free is skipped.

## Modulation

Sources: LFO 1 and 2, the amp envelope, envelope 2, per-grain random, velocity, key, mod wheel, pressure and grain
age. Every source has a depth on every grain parameter (a matrix, saved with the state). Age moves pitch, cutoff,
resonance, azimuth, elevation, distance and gain while a grain sounds, the others apply when it starts.

In the page: click a source (top right) to arm it, drag a parameter to set its depth, double-click to clear it,
Escape or the source again to disarm.

## Sample

Drop an audio file on the waveform or use LOAD. The page decodes it at 48 kHz, mixes it to mono and sends it in
chunks. The state keeps it as 16-bit mono after the parameters, at most 60 s (a longer file is cut).
