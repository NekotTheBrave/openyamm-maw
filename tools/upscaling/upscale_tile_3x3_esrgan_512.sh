#!/usr/bin/env bash
set -euo pipefail
trap 'echo "Error on line $LINENO: $BASH_COMMAND"' ERR

usage()
{
    echo "Usage:"
    echo "  $0 <input_image> [work_output_dir]"
    echo
    echo "Environment variables:"
    echo "  REALESRGAN_BIN    Path to realesrgan-ncnn-vulkan"
    echo "  MODEL             Real-ESRGAN model (default: realesrgan-x4plus)"
    echo "  SCALE             Real-ESRGAN scale factor (default: 4)"
    echo "  FINAL_SIZE        Final tile size (default: 512)"
    echo "  PROD_DIR          Final production output dir (default: ./out_prod)"
    echo "  ENABLE_COLORKEY   1 = convert colorkey to alpha, 0 = skip (default: 0)"
    echo "  COLORKEY          Colorkey RGB triplet (default: 252,0,252)"
    echo "  COLORKEY_FUZZ     ImageMagick fuzz for colorkey matching (default: 0%)"
    echo
    echo "Examples:"
    echo "  $0 DIRTtyl.bmp"
    echo "  ENABLE_COLORKEY=1 $0 wtrdrNW.bmp"
    echo "  REALESRGAN_BIN=/path/to/realesrgan-ncnn-vulkan $0 DIRTtyl.bmp ./out_work"
    exit 1
}

if [ $# -lt 1 ] || [ $# -gt 2 ]; then
    usage
fi

INPUT="$1"
OUTDIR="${2:-./out_tile_3x3_esrgan}"

REALESRGAN_BIN="${REALESRGAN_BIN:-realesrgan-ncnn-vulkan}"
MODEL="${MODEL:-realesrgan-x4plus}"
SCALE="${SCALE:-4}"
FINAL_SIZE="${FINAL_SIZE:-512}"
PROD_DIR="${PROD_DIR:-./out_prod}"

ENABLE_COLORKEY="${ENABLE_COLORKEY:-0}"
COLORKEY="${COLORKEY:-252,0,252}"
COLORKEY_FUZZ="${COLORKEY_FUZZ:-0%}"

for cmd in convert identify montage "$REALESRGAN_BIN"; do
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

mkdir -p "$OUTDIR" "$PROD_DIR"

BASE="$(basename "${INPUT%.*}")"
WORKDIR="$OUTDIR/$BASE"
mkdir -p "$WORKDIR"

SRC="$WORKDIR/${BASE}_src.png"
TILE3="$WORKDIR/${BASE}_3x3.png"
UPSCALED3="$WORKDIR/${BASE}_3x3_${MODEL}_x${SCALE}.png"
CENTER_RAW="$WORKDIR/${BASE}_center_raw.png"
FINAL="$WORKDIR/${BASE}_final_${FINAL_SIZE}.png"
PREVIEW="$WORKDIR/${BASE}_preview_3x3.png"
PROD_FINAL="$PROD_DIR/${BASE}.png"

echo "[1/6] Convert source to PNG..."
if [ "$ENABLE_COLORKEY" = "1" ]; then
    convert "$INPUT" \
        -alpha set \
        -fuzz "$COLORKEY_FUZZ" \
        -transparent "rgb($COLORKEY)" \
        PNG32:"$SRC"
else
    convert "$INPUT" -alpha off -type TrueColor PNG32:"$SRC"
fi

read -r W H < <(identify -format "%w %h\n" "$SRC")
if [ "$W" -ne "$H" ]; then
    echo "Warning: input is not square: ${W}x${H}"
fi

UPSCALED_TILE_W=$((W * SCALE))
UPSCALED_TILE_H=$((H * SCALE))
OFFSET_X=$((W * SCALE))
OFFSET_Y=$((H * SCALE))

echo "Input size          : ${W}x${H}"
echo "ESRGAN center crop  : ${UPSCALED_TILE_W}x${UPSCALED_TILE_H}+${OFFSET_X}+${OFFSET_Y}"
echo "Final size          : ${FINAL_SIZE}x${FINAL_SIZE}"

echo "[2/6] Create 3x3 tiled source..."
montage \
    "$SRC" "$SRC" "$SRC" \
    "$SRC" "$SRC" "$SRC" \
    "$SRC" "$SRC" "$SRC" \
    -background none \
    -tile 3x3 -geometry +0+0 \
    PNG32:"$TILE3"

echo "[3/6] Upscale 3x3 source with Real-ESRGAN..."
"$REALESRGAN_BIN" \
    -i "$TILE3" \
    -o "$UPSCALED3" \
    -n "$MODEL" \
    -s "$SCALE"

echo "[4/6] Crop center upscaled tile..."
convert "$UPSCALED3" \
    -crop "${UPSCALED_TILE_W}x${UPSCALED_TILE_H}+${OFFSET_X}+${OFFSET_Y}" +repage \
    PNG32:"$CENTER_RAW"

echo "[5/6] Resize/correct final tile to ${FINAL_SIZE}x${FINAL_SIZE}..."
convert "$CENTER_RAW" \
    -filter Lanczos \
    -resize "${FINAL_SIZE}x${FINAL_SIZE}!" \
    PNG32:"$FINAL"

convert "$FINAL" PNG32:"$PROD_FINAL"

echo "[6/6] Create 3x3 preview..."
montage \
    "$FINAL" "$FINAL" "$FINAL" \
    "$FINAL" "$FINAL" "$FINAL" \
    "$FINAL" "$FINAL" "$FINAL" \
    -background none \
    -tile 3x3 -geometry +0+0 \
    PNG32:"$PREVIEW"

echo
echo "Done."
echo "Work dir          : $WORKDIR"
echo "3x3 source        : $TILE3"
echo "Upscaled 3x3      : $UPSCALED3"
echo "Center crop       : $CENTER_RAW"
echo "Final tile        : $FINAL"
echo "Production output : $PROD_FINAL"
echo "3x3 preview       : $PREVIEW"