#!/usr/bin/env bash
set -euo pipefail
trap 'echo "Error on line $LINENO: $BASH_COMMAND"' ERR

usage()
{
    echo "Usage:"
    echo "  $0 [--mode base|softcontrast|crisp] <input_image> [output_dir]"
    echo
    echo "Modes:"
    echo "  base          = pure ESRGAN result"
    echo "  softcontrast  = ESRGAN + softer contrast"
    echo "  crisp         = ESRGAN + crisp post-process"
    echo
    echo "Environment variables:"
    echo "  REALESRGAN_BIN    Path to realesrgan-ncnn-vulkan"
    echo "  MODEL             Real-ESRGAN model (default: realesrgan-x4plus)"
    echo "  SCALE             Upscale factor (default: 4)"
    echo "  SOFT_UNSHARP      Unsharp for softcontrast mode (default: 0x0.4+0.3+0.02)"
    echo "  SOFT_BC           Brightness/contrast for softcontrast mode (default: 0x-8)"
    echo "  CRISP_UNSHARP     Unsharp for crisp mode (default: 0x1.1+0.8+0.01)"
    echo "  CRISP_BC          Brightness/contrast for crisp mode (default: 0x6)"
    echo "  PROD_DIR          Final production output dir (default: ./out_prod)"
    echo
    echo "Examples:"
    echo "  $0 HDWTR000.bmp"
    echo "  $0 --mode softcontrast HDWTR000.bmp"
    echo "  PROD_DIR=../../assets_hd/worlds/mm8/textures $0 --mode crisp HDWTR000.bmp"
    exit 1
}

MODE="base"

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

if [ "$MODE" != "base" ] && [ "$MODE" != "softcontrast" ] && [ "$MODE" != "crisp" ]; then
    echo "Error: MODE must be 'base', 'softcontrast', or 'crisp'"
    exit 1
fi

INPUT="$1"
OUTDIR="${2:-./out_terrain_water}"

REALESRGAN_BIN="${REALESRGAN_BIN:-realesrgan-ncnn-vulkan}"
MODEL="${MODEL:-realesrgan-x4plus}"
SCALE="${SCALE:-4}"

SOFT_UNSHARP="${SOFT_UNSHARP:-0x0.4+0.3+0.02}"
SOFT_BC="${SOFT_BC:-0x-8}"

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

mkdir -p "$OUTDIR"
mkdir -p "$PROD_DIR"

BASE="$(basename "${INPUT%.*}")"
WORKDIR="$OUTDIR/$BASE"
mkdir -p "$WORKDIR"

SRC_PNG="$WORKDIR/${BASE}_src.png"
TILE3="$WORKDIR/${BASE}_3x3.png"
UPSCALED3="$WORKDIR/${BASE}_3x3_${MODEL}_x${SCALE}.png"
CENTER_AI="$WORKDIR/${BASE}_${MODEL}_center_raw.png"
FINAL="$WORKDIR/${BASE}_terrain_water_final.png"
PREVIEW="$WORKDIR/${BASE}_terrain_water_preview.png"
PROD_FINAL="$PROD_DIR/${BASE}.png"

echo "[1/6] Convert source to PNG..."
convert "$INPUT" -alpha off -type TrueColor "$SRC_PNG"

read -r W H < <(identify -format "%w %h\n" "$SRC_PNG")
FINAL_W=$((W * SCALE))
FINAL_H=$((H * SCALE))
OFFSET_X=$((W * SCALE))
OFFSET_Y=$((H * SCALE))

echo "Mode        : ${MODE}"
echo "Input size  : ${W}x${H}"
echo "Output size : ${FINAL_W}x${FINAL_H}"

echo "[2/6] Create 3x3 tiled source..."
montage \
    "$SRC_PNG" "$SRC_PNG" "$SRC_PNG" \
    "$SRC_PNG" "$SRC_PNG" "$SRC_PNG" \
    "$SRC_PNG" "$SRC_PNG" "$SRC_PNG" \
    -tile 3x3 -geometry +0+0 \
    "$TILE3"

echo "[3/6] Upscale 3x3 tiled source with Real-ESRGAN..."
"$REALESRGAN_BIN" \
    -i "$TILE3" \
    -o "$UPSCALED3" \
    -n "$MODEL" \
    -s "$SCALE"

echo "[4/6] Crop center tile..."
convert "$UPSCALED3" \
    -crop "${FINAL_W}x${FINAL_H}+${OFFSET_X}+${OFFSET_Y}" +repage \
    "$CENTER_AI"

echo "[5/6] Apply water mode..."
case "$MODE" in
    base)
        cp "$CENTER_AI" "$FINAL"
        ;;
    softcontrast)
        convert "$CENTER_AI" \
            -unsharp "$SOFT_UNSHARP" \
            -brightness-contrast "$SOFT_BC" \
            "$FINAL"
        ;;
    crisp)
        convert "$CENTER_AI" \
            -unsharp "$CRISP_UNSHARP" \
            -brightness-contrast "$CRISP_BC" \
            "$FINAL"
        ;;
esac

echo "[6/6] Write production output and create 3x3 preview..."
convert "$FINAL" PNG32:"$PROD_FINAL"

montage \
    "$FINAL" "$FINAL" "$FINAL" \
    "$FINAL" "$FINAL" "$FINAL" \
    "$FINAL" "$FINAL" "$FINAL" \
    -tile 3x3 -geometry +0+0 \
    PNG32:"$PREVIEW"

echo
echo "Done."
echo "Mode              : $MODE"
echo "Work dir          : $WORKDIR"
echo "Source PNG        : $SRC_PNG"
echo "Final output      : $FINAL"
echo "Production output : $PROD_FINAL"
echo "3x3 preview       : $PREVIEW"