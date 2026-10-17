#!/usr/bin/env bash
set -euo pipefail
trap 'echo "Error on line $LINENO: $BASH_COMMAND"' ERR

usage()
{
    echo "Usage:"
    echo "  $0 [--mode static|animated] <input_image>"
    echo
    echo "Modes:"
    echo "  static    = decorations / static billboards"
    echo "  animated  = animated billboard frames / monsters / actors"
    echo
    echo "Environment variables:"
    echo "  REALESRGAN_BIN    Path to realesrgan-ncnn-vulkan"
    echo "  MODEL             Real-ESRGAN model (default: realesrgan-x4plus)"
    echo "  SCALE             Upscale factor (default: 4)"
    echo "  PROD_DIR          Final output dir (default: ./out_prod_billboard)"
    echo "  BORDER            Transparent border in source pixels (default: 4)"
    echo "  COLORKEY_FUZZ     Fuzz for colorkey conversion (default: 0%)"
    echo "  STATIC_UNSHARP    Static mode sharpen (default: 0x0.6+0.4+0.02)"
    echo "  ANIM_UNSHARP      Animated mode sharpen (default: 0x0.4+0.3+0.02)"
    echo
    echo "Examples:"
    echo "  $0 tree01.bmp"
    echo "  $0 --mode animated goblin_attack_00.bmp"
    echo "  REALESRGAN_BIN=/path/to/realesrgan-ncnn-vulkan $0 sign01.bmp"
    exit 1
}

MODE="static"

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

if [ $# -ne 1 ]; then
    usage
fi

if [ "$MODE" != "static" ] && [ "$MODE" != "animated" ]; then
    echo "Error: MODE must be 'static' or 'animated'"
    exit 1
fi

INPUT="$1"

REALESRGAN_BIN="${REALESRGAN_BIN:-realesrgan-ncnn-vulkan}"
MODEL="${MODEL:-realesrgan-x4plus}"
SCALE="${SCALE:-4}"
PROD_DIR="${PROD_DIR:-./out_prod_billboard}"

BORDER="${BORDER:-4}"
COLORKEY_FUZZ="${COLORKEY_FUZZ:-0%}"

STATIC_UNSHARP="${STATIC_UNSHARP:-0x0.6+0.4+0.02}"
ANIM_UNSHARP="${ANIM_UNSHARP:-0x0.4+0.3+0.02}"

STATIC_BC="${STATIC_BC:-0x0}"
ANIM_BC="${ANIM_BC:-0x0}"

for cmd in convert identify "$REALESRGAN_BIN"; do
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

SRC="$TMPDIR/${BASE}_src.png"
BORDERED="$TMPDIR/${BASE}_bordered.png"
UPSCALED="$TMPDIR/${BASE}_upscaled.png"
CROPPED="$TMPDIR/${BASE}_cropped.png"

sample_colorkey()
{
    convert "$1" -crop 1x1+0+0 txt:- | awk -F'[()]' 'NR==2{print $2}' | cut -d',' -f1-3 | tr -d ' '
}

COLORKEY="$(sample_colorkey "$INPUT")"

echo "[1/5] Convert colorkey from top-left pixel to alpha..."
echo "Detected colorkey: $COLORKEY"
convert "$INPUT" \
    -alpha set \
    -fuzz "$COLORKEY_FUZZ" \
    -transparent "rgb($COLORKEY)" \
    PNG32:"$SRC"

read -r W H < <(identify -format "%w %h\n" "$SRC")
FINAL_W=$((W * SCALE))
FINAL_H=$((H * SCALE))
BORDER_OUT=$((BORDER * SCALE))

echo "[2/5] Add transparent border..."
convert "$SRC" \
    -bordercolor none -border "${BORDER}x${BORDER}" \
    PNG32:"$BORDERED"

echo "[3/5] Upscale with Real-ESRGAN..."
"$REALESRGAN_BIN" \
    -i "$BORDERED" \
    -o "$UPSCALED" \
    -n "$MODEL" \
    -s "$SCALE"

echo "[4/5] Crop border away..."
convert "$UPSCALED" \
    -crop "${FINAL_W}x${FINAL_H}+${BORDER_OUT}+${BORDER_OUT}" +repage \
    PNG32:"$CROPPED"

echo "[5/5] Apply post-process and write production output..."
if [ "$MODE" = "static" ]; then
    convert "$CROPPED" \
        -channel RGB -unsharp "$STATIC_UNSHARP" -brightness-contrast "$STATIC_BC" +channel \
        PNG32:"$PROD_FINAL"
else
    convert "$CROPPED" \
        -channel RGB -unsharp "$ANIM_UNSHARP" -brightness-contrast "$ANIM_BC" +channel \
        PNG32:"$PROD_FINAL"
fi

echo
echo "Done."
echo "Mode   : $MODE"
echo "Output : $PROD_FINAL"