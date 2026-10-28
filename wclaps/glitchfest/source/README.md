# Glitchfest

A glitch effect as a WebCLAP audio effect. The page is 236 x 176, the size of openDAW's device panel (feature
`opendaw:device-panel`, extension `opendaw.device-panel/1`), every other host shows it in a window, 1 to 3 times as
large.

## Layout

```
build.sh        builds module.wasm and copies the page into $OUT (CLAP headers in $CLAP_INCLUDE)
plugin/         glitchfest.cpp, the CLAP plugin: effects, patterns, sequence, state, the page's messages
page/           index.html, the sequence strip, the pattern grid and the parameter cells
```

## Patterns

- Twelve rows, one per effect, top to bottom in signal order, at first Stutter, Shuffle, Reverse, Scratch, Freeze,
  Tape, Pitch, Crush, Filter, Gate, Pan, Throw. Drag a row's name up or down to move it, its blocks move with it.
- A new device starts with every pattern empty.
- Four patterns A to D of 32 columns. Length spans the columns over 1, 2 or 4 bars.
- A block switches its row on for its columns. Prob is the chance it plays on a pass (rolled each time the block
  starts), Ramp fades the effect in and out over the block.
- The sequence has eight slots, each one pattern long, each holding a pattern or nothing (dry). It follows the host
  transport, a stopped transport plays dry.

## Effects

The time effects (Stutter to Pitch) read a shared 20 s history of the input and replace the signal while they play,
the lower row wins when two play at once, a block that starts while the last one still sounds fades from it in 5 ms.
Crush, Filter, Gate, Pan and Throw process the signal as it arrives at their row, so they shape a time effect above
them. Throw sends its block into a synced echo whose tail keeps sounding after the block.

## Page

Click A to D to edit a pattern, double-click one to clear it, click a slot to cycle it through A, B, C, D and off.
Drag a row's name to move the row. Drag on a row to draw a block, drag a block to move it, double-click (or Delete)
to remove it. The cells below show the selected row's two parameters, then Prob and Ramp of the selected block, or
Len and Dice without one. Drag a cell up or down, click a choice to step it, double-click to reset. Click Dice to
roll a random pattern, drag it to set how dense it rolls. Mix is a host parameter.
