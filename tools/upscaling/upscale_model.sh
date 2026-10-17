#!/usr/bin/env bash
set -euo pipefail
trap 'echo "Error on line $LINENO: $BASH_COMMAND"' ERR

usage()
{
    echo "Usage:"
    echo "  $0 <input_image>"
    echo
    echo "Environment variables:"
    echo "  REALESRGAN_BIN    Path to realesrgan-ncnn-vulkan"
    echo "  MODEL             Real-ESRGAN model (default: realesrgan-x4plus)"
    echo "  SCALE             Upscale factor (default: 4)"
    echo "  PROD_DIR          Final output dir (default: ./out_prod_bmodel)"
    echo "  ENABLE_COLORKEY   1 = convert colorkey to alpha, 0 = skip (default: 0)"
    echo "  COLORKEY          Colorkey RGB triplet (default: 252,0,252)"
    echo "  COLORKEY_FUZZ     ImageMagick fuzz for colorkey matching (default: 0%)"
    echo "  SHARPEN           Unsharp parameters (default: 0x0.7+0.5+0.02)"
    echo
    echo "Examples:"
    echo "  $0 house01.bmp"
    echo "  REALESRGAN_BIN=/path/to/realesrgan-ncnn-vulkan $0 house01.bmp"
    echo "  ENABLE_COLORKEY=1 $0 fountain_masked.bmp"
    exit 1
}

if [ $# -ne 1 ]; then
    usage
fi

INPUT="$1"

REALESRGAN_BIN="${REALESRGAN_BIN:-realesrgan-ncnn-vulkan}"
MODEL="${MODEL:-realesrgan-x4plus}"
SCALE="${SCALE:-4}"
PROD_DIR="${PROD_DIR:-./out_prod_bmodel}"

ENABLE_COLORKEY="${ENABLE_COLORKEY:-0}"
COLORKEY="${COLORKEY:-252,0,252}"
COLORKEY_FUZZ="${COLORKEY_FUZZ:-0%}"

SHARPEN="${SHARPEN:-0x0.7+0.5+0.02}"

for cmd in convert "$REALESRGAN_BIN"; do
    command -v "$cmd" >/dev/null 2>&1 || {
        echo "Error: required command not found: $cmd"
        exit 1
    }
done

if [ ! -f "$INPUT" ]; then
    echo "Error: input file does not exist: $INPUT"
    exit 1
fi

MODEL_DIR="$(dirname "$REALESRGAN_BIN")/models"
if [ ! -f "$MODEL_DIR/${MODEL}.param" ] || [ ! -f "$MODEL_DIR/${MODEL}.bin" ]; then
    echo "Error: model files not found for '$MODEL' in $MODEL_DIR"
    echo "Expected:"
    echo "  $MODEL_DIR/${MODEL}.param"
    echo "  $MODEL_DIR/${MODEL}.bin"
    exit 1
fi

mkdir -p "$PROD_DIR"

BASE="$(basename "${INPUT%.*}")"
PROD_FINAL="$PROD_DIR/${BASE}.png"

TMPDIR="$(mktemp -d)"
cleanup()
{
    rm -rf "$TMPDIR"
}
trap cleanup EXIT
trap 'echo "Error on line $LINENO: $BASH_COMMAND"' ERR

SRC="$TMPDIR/${BASE}_src.png"
UPSCALED="$TMPDIR/${BASE}_esrgan_x${SCALE}.png"

echo "[1/3] Convert source..."
if [ "$ENABLE_COLORKEY" = "1" ]; then
    convert "$INPUT" \
        -alpha set \
        -fuzz "$COLORKEY_FUZZ" \
        -transparent "rgb($COLORKEY)" \
        PNG32:"$SRC"
else
    convert "$INPUT" PNG32:"$SRC"
fi

echo "[2/3] Upscale with Real-ESRGAN..."
"$REALESRGAN_BIN" \
    -i "$SRC" \
    -o "$UPSCALED" \
    -n "$MODEL" \
    -s "$SCALE"

echo "[3/3] Sharpen and write production output..."
convert "$UPSCALED" \
    -unsharp "$SHARPEN" \
    PNG32:"$PROD_FINAL"

echo
echo "Done."
echo "Output: $PROD_FINAL"