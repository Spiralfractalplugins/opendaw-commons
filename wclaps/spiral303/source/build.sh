#!/usr/bin/env bash
# Builds module.wasm and the interface files into $OUT.
# The CI provides emcc and the CLAP headers in $CLAP_INCLUDE.
set -euo pipefail
cd "$(dirname "$0")"

VERSION=0.37.2

GEN="$(mktemp -d)"
trap 'rm -rf "$GEN"' EXIT

# The factory presets and the interface files are embedded into the module:
# a page inside a wasm sandbox cannot read anything else.
python3 tools/embed_presets.py presets "$GEN/FactoryPresetData.h"
python3 tools/embed_web.py web "$GEN/WebAssets.h"

em++ -std=c++17 -O3 -fno-exceptions -fno-rtti \
    -DSPIRAL303_CORE_ONLY=1 -DSPIRAL303_VERSION="\"$VERSION\"" \
    -I"$CLAP_INCLUDE" -ISource/wclap -I"$GEN" \
    Source/wclap/ClapEntry.cpp \
    Source/wclap/Spiral303Clap.cpp \
    Source/wclap/ProcessorCore.cpp \
    Source/wclap/ParamTable.cpp \
    Source/wclap/PresetCore.cpp \
    Source/wclap/JsonLite.cpp \
    Source/wclap/StateXml.cpp \
    Source/wclap/WebView.cpp \
    Source/dsp/Sequence.cpp \
    emscripten_glue.cpp \
    -o "$OUT/module.wasm" \
    -sSTANDALONE_WASM=1 --no-entry \
    -sEXPORTED_FUNCTIONS=_malloc,_free -Wl,--export=clap_entry \
    -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=8MB -sSTACK_SIZE=256KB -sALLOW_TABLE_GROWTH=1 \
    -sERROR_ON_UNDEFINED_SYMBOLS=1

# The interface files beside the module, the same page the module serves itself.
cp web/index.html web/app.js web/style.css "$OUT/"
