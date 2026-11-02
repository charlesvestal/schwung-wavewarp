#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(dirname "$SCRIPT_DIR")"
VITAL_ROOT="${VITAL_ROOT:-$REPO_ROOT/../_vital_src}"
IMAGE_NAME="schwung-wavewarp-builder"

if [ ! -d "$VITAL_ROOT/src" ]; then
    echo "ERROR: Vital source not found at $VITAL_ROOT"
    echo "Set VITAL_ROOT to the Vital source checkout directory."
    exit 1
fi

if [ -z "$CROSS_PREFIX" ] && [ ! -f "/.dockerenv" ]; then
    echo "=== Building WaveWarp Module (via Docker) ==="
    if ! docker image inspect "$IMAGE_NAME" >/dev/null 2>&1; then
        docker build -t "$IMAGE_NAME" -f "$SCRIPT_DIR/Dockerfile" "$REPO_ROOT"
    fi

    docker run --rm \
        -v "$REPO_ROOT:/build" \
        -v "$VITAL_ROOT:/vital_src:ro" \
        -u "$(id -u):$(id -g)" \
        -w /build \
        -e VITAL_ROOT=/vital_src \
        "$IMAGE_NAME" \
        ./scripts/build.sh
    exit 0
fi

CROSS_PREFIX="${CROSS_PREFIX:-aarch64-linux-gnu-}"

cd "$REPO_ROOT"
mkdir -p build dist/wavewarp

${CROSS_PREFIX}g++ \
    -shared -fPIC -O2 -std=c++14 -pthread \
    -march=armv8-a -mtune=cortex-a72 \
    -DLINUX=1 \
    -DNDEBUG=1 \
    -DHEADLESS=1 \
    -DNO_AUTH=1 \
    -DJUCE_USE_CURL=0 \
    -DJUCE_DSP_USE_SHARED_FFTW=0 \
    -DJUCE_DSP_USE_STATIC_FFTW=0 \
    -DJUCE_USE_XRANDR=0 \
    -DJucePlugin_Build_VST=0 \
    -DJucePlugin_Build_VST3=0 \
    -DJucePlugin_Build_AU=0 \
    -DJucePlugin_Build_AUv3=0 \
    -DJucePlugin_Build_RTAS=0 \
    -DJucePlugin_Build_AAX=0 \
    -DJucePlugin_Build_Standalone=0 \
    -DJucePlugin_Build_Unity=0 \
    -Wno-error=date-time \
    -I src/dsp \
    -I "$VITAL_ROOT/headless/JuceLibraryCode" \
    -I "$VITAL_ROOT/third_party/JUCE/modules" \
    -I "$VITAL_ROOT/src/common" \
    -I "$VITAL_ROOT/src/common/wavetable" \
    -I "$VITAL_ROOT/src/interface/editor_components" \
    -I "$VITAL_ROOT/src/interface/editor_sections" \
    -I "$VITAL_ROOT/src/interface/look_and_feel" \
    -I "$VITAL_ROOT/src/interface/wavetable" \
    -I "$VITAL_ROOT/src/interface/wavetable/editors" \
    -I "$VITAL_ROOT/src/interface/wavetable/overlays" \
    -I "$VITAL_ROOT/src/standalone" \
    -I "$VITAL_ROOT/src/synthesis/synth_engine" \
    -I "$VITAL_ROOT/src/synthesis/effects" \
    -I "$VITAL_ROOT/src/synthesis/filters" \
    -I "$VITAL_ROOT/src/synthesis/framework" \
    -I "$VITAL_ROOT/src/synthesis/lookups" \
    -I "$VITAL_ROOT/src/synthesis/modulators" \
    -I "$VITAL_ROOT/src/synthesis/modules" \
    -I "$VITAL_ROOT/src/synthesis/producers" \
    -I "$VITAL_ROOT/src/synthesis/utilities" \
    -I "$VITAL_ROOT/third_party" \
    src/dsp/wavewarp.cpp \
    "$VITAL_ROOT/src/unity_build/common.cpp" \
    "$VITAL_ROOT/src/unity_build/synthesis.cpp" \
    "$VITAL_ROOT/headless/JuceLibraryCode/include_juce_audio_basics.cpp" \
    "$VITAL_ROOT/headless/JuceLibraryCode/include_juce_audio_formats.cpp" \
    "$VITAL_ROOT/headless/JuceLibraryCode/include_juce_core.cpp" \
    "$VITAL_ROOT/headless/JuceLibraryCode/include_juce_data_structures.cpp" \
    "$VITAL_ROOT/headless/JuceLibraryCode/include_juce_dsp.cpp" \
    "$VITAL_ROOT/headless/JuceLibraryCode/include_juce_events.cpp" \
    -o build/dsp.so \
    -ldl -lpthread -lrt -lm

cp src/module.json dist/wavewarp/module.json
cp src/help.json dist/wavewarp/help.json
cp src/ui.js dist/wavewarp/ui.js
cp build/dsp.so dist/wavewarp/dsp.so
chmod +x dist/wavewarp/dsp.so
cp -r src/presets dist/wavewarp/presets

# Licence texts. dsp.so is GPL-3.0 (Vital); the third-party code it compiles
# in (JUCE, kissfft, concurrentqueue, nlohmann/json, FLAC, Ogg Vorbis) needs
# its notice to travel with the binary. Copies are vendored under licenses/
# because the Vital tree is outside this repo. Unconditional on purpose: a
# missing file must fail the build rather than ship without it.
mkdir -p dist/wavewarp/licenses
cp LICENSE dist/wavewarp/LICENSE
for f in JUCE.txt kissfft-BSD-3-Clause.txt concurrentqueue.txt nlohmann-json-MIT.txt \
         FLAC-BSD-3-Clause.txt OggVorbis-BSD-3-Clause.txt; do
    cp "licenses/$f" "dist/wavewarp/licenses/$f"
done

cd dist
tar -czvf wavewarp-module.tar.gz wavewarp/
