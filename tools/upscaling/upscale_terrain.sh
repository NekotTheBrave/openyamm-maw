#!/usr/bin/env bash
set -euo pipefail
trap 'echo "Error on line $LINENO: $BASH_COMMAND"' ERR

usage()
{
    echo "Usage:"
    echo "  $0 [--mode blend|crisp] <input_image> [output_dir]"
    echo
    echo "Modes:"
    echo "  blend   = ESRGAN + Lanczos blend + sharpen"
    echo "  crisp   = pure ESRGAN + crisp post-process"
    echo
    echo "Environment variables:"
    echo "  REALESRGAN_BIN    Path to realesrgan-ncnn-vulkan"
    echo "  MODEL             Real-ESRGAN model (default: realesrgan-x4plus)"
    echo "  SCALE             Upscale factor (default: 4)"
    echo "  COLORKEY          Colorkey RGB triplet (default: 252,0,252)"
    echo "  COLORKEY_FUZZ     ImageMagick fuzz for colorkey matching (default: 0%)"
    echo "  ENABLE_COLORKEY   1 = convert colorkey to alpha, 0 = skip (default: 1)"
    echo "  AI_WEIGHT         Blend weight of AI result (default: 45)"
    echo "  ORIG_WEIGHT       Blend weight of resized original (default: 55)"
    echo "  ORIG_FILTER       Filter for original upscale: Lanczos or Mitchell (default: Lanczos)"
    echo "  SHARPEN           Unsharp parameters for blend mode (default: 0x0.7+0.5+0.02)"
    echo "  CRISP_UNSHARP     Unsharp for crisp mode (default: 0x1.1+0.8+0.01)"
    echo "  CRISP_BC          Brightness/contrast for crisp mode (default: 0x6)"
    echo "  PROD_DIR          Final production output dir (default: ./out_prod)"
    echo
    echo "Examples:"
    echo "  $0 Grastyl.bmp"
    echo "  $0 --mode crisp Grastyl.bmp"
    echo "  ORIG_FILTER=Mitchell $0 --mode blend Grastyl.bmp ./out"
    exit 1
}

MODE="blend"

while [ $# -gt 0 ]; do
    case "$1" in
        --mode)
            shift
            [ $# -gt 0 ] || { echo "Error: --mode requires a value"; usage; }
            MODE="$1"
            shift
            ;;
        -h|--help)
            usage
            ;;
        --)
            shift
            break
            ;;
        -*)
            echo "Error: unknown option: $1"
            usage
            ;;
        *)
            break
            ;;
    esac
done

if [ $# -lt 1 ] || [ $# -gt 2 ]; then
    usage
fi

if [ "$MODE" != "blend" ] && [ "$MODE" != "crisp" ]; then
    echo "Error: MODE must be 'blend' or 'crisp'"
    exit 1
fi

INPUT="$1"
OUTDIR="${2:-./out_terrain_land}"

REALESRGAN_BIN="${REALESRGAN_BIN:-realesrgan-ncnn-vulkan}"
MODEL="${MODEL:-realesrgan-x4plus}"
SCALE="${SCALE:-4}"

COLORKEY="${COLORKEY:-252,0,252}"
COLORKEY_FUZZ="${COLORKEY_FUZZ:-0%}"
ENABLE_COLORKEY="${ENABLE_COLORKEY:-1}"

AI_WEIGHT="${AI_WEIGHT:-45}"
ORIG_WEIGHT="${ORIG_WEIGHT:-55}"
ORIG_FILTER="${ORIG_FILTER:-Lanczos}"
SHARPEN="${SHARPEN:-0x0.7+0.5+0.02}"

CRISP_UNSHARP="${CRISP_UNSHARP:-0x1.1+0.8+0.01}"
CRISP_BC="${CRISP_BC:-0x6}"

PROD_DIR="${PROD_DIR:-./out_prod}"

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

if [ "$ORIG_FILTER" != "Lanczos" ] && [ "$ORIG_FILTER" != "Mitchell" ]; then
    echo "Error: ORIG_FILTER must be Lanczos or Mitchell"
    exit 1
fi

mkdir -p "$OUTDIR"
mkdir -p "$PROD_DIR"

BASE="$(basename "${INPUT%.*}")"
WORKDIR="$OUTDIR/$BASE"
mkdir -p "$WORKDIR"

SRC_PNG32="$WORKDIR/${BASE}_src.png"
TILE3="$WORKDIR/${BASE}_3x3.png"
UPSCALED3="$WORKDIR/${BASE}_3x3_${MODEL}_x${SCALE}.png"
CENTER_AI="$WORKDIR/${BASE}_${MODEL}_center_raw.png"
RESIZED_ORIG="$WORKDIR/${BASE}_${ORIG_FILTER,,}4x.png"
FINAL="$WORKDIR/${BASE}_terrain_land_final.png"
PREVIEW="$WORKDIR/${BASE}_terrain_land_preview.png"
PROD_FINAL="$PROD_DIR/${BASE}.png"

echo "[1/8] Convert source to PNG32 (with optional colorkey -> alpha)..."
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
FINAL_W=$((W * SCALE))
FINAL_H=$((H * SCALE))
OFFSET_X=$((W * SCALE))
OFFSET_Y=$((H * SCALE))

echo "Mode        : ${MODE}"
echo "Input size  : ${W}x${H}"
echo "Output size : ${FINAL_W}x${FINAL_H}"

echo "[2/8] Create 3x3 tiled source..."
montage \
    "$SRC_PNG32" "$SRC_PNG32" "$SRC_PNG32" \
    "$SRC_PNG32" "$SRC_PNG32" "$SRC_PNG32" \
    "$SRC_PNG32" "$SRC_PNG32" "$SRC_PNG32" \
    -background none \
    -tile 3x3 -geometry +0+0 \
    PNG32:"$TILE3"

echo "[3/8] Upscale 3x3 tiled source with Real-ESRGAN..."
"$REALESRGAN_BIN" \
    -i "$TILE3" \
    -o "$UPSCALED3" \
    -n "$MODEL" \
    -s "$SCALE"

echo "[4/8] Crop center tile..."
convert "$UPSCALED3" \
    -crop "${FINAL_W}x${FINAL_H}+${OFFSET_X}+${OFFSET_Y}" +repage \
    PNG32:"$CENTER_AI"

if [ "$MODE" = "blend" ]; then
    echo "[5/8] Create ${ORIG_FILTER} 4x version of original..."
    convert "$SRC_PNG32" \
        -filter "$ORIG_FILTER" -resize "${SCALE}00%" \
        PNG32:"$RESIZED_ORIG"

    echo "[6/8] Blend AI and ${ORIG_FILTER} result, then sharpen..."
    convert "$CENTER_AI" "$RESIZED_ORIG" \
        -compose blend -define compose:args="${AI_WEIGHT},${ORIG_WEIGHT}" -composite \
        -unsharp "$SHARPEN" \
        PNG32:"$FINAL"
else
    echo "[5/8] Apply crisp post-process to pure ESRGAN result..."
    convert "$CENTER_AI" \
        -unsharp "$CRISP_UNSHARP" \
        -brightness-contrast "$CRISP_BC" \
        PNG32:"$FINAL"
fi

echo "[7/8] Write production output..."
convert "$FINAL" PNG32:"$PROD_FINAL"

echo "[8/8] Create 3x3 preview..."
montage \
    "$FINAL" "$FINAL" "$FINAL" \
    "$FINAL" "$FINAL" "$FINAL" \
    "$FINAL" "$FINAL" "$FINAL" \
    -background none \
    -tile 3x3 -geometry +0+0 \
    PNG32:"$PREVIEW"

echo
echo "Done."
echo "Mode              : $MODE"
echo "Work dir          : $WORKDIR"
echo "Source PNG32      : $SRC_PNG32"
if [ "$MODE" = "blend" ]; then
    echo "Resized original  : $RESIZED_ORIG"
fi
echo "Final output      : $FINAL"
echo "Production output : $PROD_FINAL"
echo "3x3 preview       : $PREVIEW"