#!/usr/bin/env bash
set -euo pipefail
trap 'echo "Error on line $LINENO: $BASH_COMMAND"' ERR

usage()
{
    echo "Usage:"
    echo "  $0 <input_image> [work_output_dir]"
    echo
    echo "Environment variables:"
    echo "  ENABLE_COLORKEY   1 = convert colorkey to alpha, 0 = skip (default: 0)"
    echo "  COLORKEY          Colorkey RGB triplet (default: 252,0,252)"
    echo "  COLORKEY_FUZZ     ImageMagick fuzz for colorkey matching (default: 0%)"
    echo "  PROD_DIR          Final production output dir (default: ./out_prod)"
    echo "  FORCE_512         1 = force final resize to 512x512, 0 = keep natural 4x4 size (default: 0)"
    echo
    echo "Examples:"
    echo "  $0 DIRTTYL.bmp"
    echo "  ENABLE_COLORKEY=1 $0 ROADTILE.bmp"
    echo "  PROD_DIR=../../assets_hd/worlds/mm8/textures $0 Grastyl.bmp ./out_repeat"
    exit 1
}

if [ $# -lt 1 ] || [ $# -gt 2 ]; then
    usage
fi

INPUT="$1"
OUTDIR="${2:-./out_repeat_4x4}"

ENABLE_COLORKEY="${ENABLE_COLORKEY:-0}"
COLORKEY="${COLORKEY:-252,0,252}"
COLORKEY_FUZZ="${COLORKEY_FUZZ:-0%}"
PROD_DIR="${PROD_DIR:-./out_prod}"
FORCE_512="${FORCE_512:-0}"

for cmd in convert identify montage; do
    command -v "$cmd" >/dev/null 2>&1 || {
        echo "Error: required command not found: $cmd"
        exit 1
    }
done

if [ ! -f "$INPUT" ]; then
    echo "Error: input file does not exist: $INPUT"
    exit 1
fi

mkdir -p "$OUTDIR" "$PROD_DIR"

BASE="$(basename "${INPUT%.*}")"
WORKDIR="$OUTDIR/$BASE"
mkdir -p "$WORKDIR"

SRC_PNG32="$WORKDIR/${BASE}_src.png"
REPEAT_4X4="$WORKDIR/${BASE}_4x4.png"
FINAL="$WORKDIR/${BASE}_final.png"
PREVIEW="$WORKDIR/${BASE}_preview.png"
PROD_FINAL="$PROD_DIR/${BASE}.png"

echo "[1/5] Convert source to PNG32 (optional colorkey -> alpha)..."
if [ "$ENABLE_COLORKEY" = "1" ]; then
    convert "$INPUT" \
        -alpha set \
        -fuzz "$COLORKEY_FUZZ" \
        -transparent "rgb($COLORKEY)" \
        PNG32:"$SRC_PNG32"
else
    convert "$INPUT" PNG32:"$SRC_PNG32"
fi

read -r W H < <(identify -format "%w %h\n" "$SRC_PNG32")
echo "Input size: ${W}x${H}"

if [ "$W" -ne "$H" ]; then
    echo "Warning: input is not square (${W}x${H})."
fi

NATURAL_W=$((W * 4))
NATURAL_H=$((H * 4))
echo "Natural 4x4 size: ${NATURAL_W}x${NATURAL_H}"

echo "[2/5] Create 4x4 repeated tile..."
montage \
    "$SRC_PNG32" "$SRC_PNG32" "$SRC_PNG32" "$SRC_PNG32" \
    "$SRC_PNG32" "$SRC_PNG32" "$SRC_PNG32" "$SRC_PNG32" \
    "$SRC_PNG32" "$SRC_PNG32" "$SRC_PNG32" "$SRC_PNG32" \
    "$SRC_PNG32" "$SRC_PNG32" "$SRC_PNG32" "$SRC_PNG32" \
    -background none \
    -tile 4x4 -geometry +0+0 \
    PNG32:"$REPEAT_4X4"

echo "[3/5] Prepare final tile..."
if [ "$FORCE_512" = "1" ]; then
    convert "$REPEAT_4X4" \
        -filter Lanczos \
        -resize 512x512! \
        PNG32:"$FINAL"
else
    cp "$REPEAT_4X4" "$FINAL"
fi

echo "[4/5] Write production output..."
convert "$FINAL" PNG32:"$PROD_FINAL"

echo "[5/5] Create preview..."
montage \
    "$FINAL" "$FINAL" \
    "$FINAL" "$FINAL" \
    -background none \
    -tile 2x2 -geometry +0+0 \
    PNG32:"$PREVIEW"

read -r FW FH < <(identify -format "%w %h\n" "$FINAL")

echo
echo "Done."
echo "Work dir          : $WORKDIR"
echo "Source PNG32      : $SRC_PNG32"
echo "Repeated 4x4      : $REPEAT_4X4"
echo "Final output      : $FINAL"
echo "Production output : $PROD_FINAL"
echo "Preview           : $PREVIEW"
echo "Final size        : ${FW}x${FH}"