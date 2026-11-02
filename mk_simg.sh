#!/bin/sh
# mk_simg.sh - Batch build MediaTek single image (simg)
#
# This script scans sub-folders under mt798x_simg/, detects board type from
# folder name, then calls ATF single image wrapper:
#   ${ATF_DIR}/tools/dev/single_img_wrapper/mk_image.sh
#
# Folder naming convention (suggested):
#   mt7981-xxx                 -> non-mmc (default spim-nand)
#   mt7986-xxx-emmc            -> eMMC
#   mt7986-xxx-sd              -> SD
#   mt7986-xxx-spim-nor        -> explicit non-mmc device
#   mt7986-xxx-spim-nand       -> explicit non-mmc device
#   mt7986-xxx-snfi-nand       -> explicit non-mmc device
#   (append) -dual-image       -> enable dual image mode
#
# Outputs are written to output_simg/<folder>-simg.bin
#
# Usage:
#   ./mk_simg.sh [options]
#
# Options:
#   -c, --config <file>        Use a specific partition config yml for all boards
#   -C, --config-dir <dir>     Use a directory containing <device>.yml for each board
#                              (e.g. emmc.yml, sd.yml, spim-nand.yml, snfi-nand.yml, spim-nor.yml)
#   -h, --help                 Show this help
#
# Environment variables:
#   ATF_DIR       ATF source folder (default depends on VERSION)
#   VERSION       2025/2026 (default: 2025)
#   SIMG_DIR      input folder containing board subfolders (default: mt798x_simg)
#   OUTPUT_DIR    output folder (default: output_simg)
#   DRY_RUN=1     only print commands, do not execute
#
# Partition config override (env):
#   PARTITION_CONFIG      Same as --config; if it is a directory, treated as --config-dir
#
# Notes:
#   - This script is meant to be executed on Linux.
#   - It calls: atf-20250711/tools/dev/single_img_wrapper/mk_image.sh (or ATF_DIR override).

TOOL_MK_IMAGE_REL="tools/dev/single_img_wrapper/mk_image.sh"

SIMG_DIR="${SIMG_DIR:-mt798x_simg}"
OUTPUT_DIR="${OUTPUT_DIR:-output_simg}"

VERSION=${VERSION:-2025}

# Determine ATF_DIR
if [ -z "${ATF_DIR:-}" ]; then
    if [ "$VERSION" = "2025" ]; then
        ATF_DIR="atf-20250711"
    elif [ "$VERSION" = "SP2" ]; then
        ATF_DIR="atf-20260123"
    else
        echo "Error: Unsupported VERSION. Please specify VERSION=2025/SP2 or set ATF_DIR."
        exit 1
    fi
fi

MK_IMAGE="${ATF_DIR}/${TOOL_MK_IMAGE_REL}"

# Optional: override partition config passed to mk_image.sh
# - GLOBAL_PARTITION_CONFIG: one yml applied to all boards (unless per-board yml exists)
# - PARTITION_CONFIG_DIR: directory containing <device>.yml (e.g. emmc.yml/sd.yml/spim-nand.yml...)
GLOBAL_PARTITION_CONFIG=""
PARTITION_CONFIG_DIR=""

usage() {
    sed -n '2,/^[^#]/p' "$0" | grep -E '^#( |$)' | sed 's/^# \?//'
    exit 0
}

say() {
    echo "$*"
}

abs_path() {
    # abs_path <path>
    # Prints an absolute path if possible; falls back to original string.
    _p="$1"
    [ -z "$_p" ] && return 0

    if command -v realpath >/dev/null 2>&1; then
        realpath "$_p" 2>/dev/null && return 0
    fi
    if command -v readlink >/dev/null 2>&1; then
        readlink -f "$_p" 2>/dev/null && return 0
    fi

    _d=$(dirname "$_p")
    _b=$(basename "$_p")
    (cd "$_d" 2>/dev/null && printf "%s/%s\n" "$(pwd)" "$_b") && return 0
    echo "$_p"
}

die() {
    echo "Error: $*"
    exit 1
}

need_file() {
    _path="$1"
    _what="$2"
    if [ -z "$_path" ] || [ ! -f "$_path" ]; then
        return 1
    fi
    return 0
}

find_first() {
    # find_first <dir> <pattern1> [pattern2 ...]
    _dir="$1"
    shift
    for _pat in "$@"; do
        _found=$(find "$_dir" -maxdepth 1 -type f -iname "$_pat" 2>/dev/null | head -n 1)
        if [ -n "$_found" ]; then
            echo "$_found"
            return 0
        fi
    done
    echo ""
    return 1
}

infer_platform() {
    # infer_platform <foldername>
    echo "$1" | cut -d'-' -f1
}

infer_dual() {
    # infer_dual <foldername> -> 0/1
    _lc=$(echo "$1" | tr 'A-Z' 'a-z')
    echo "$_lc" | grep -q -- "-dual-image" && echo 1 && return 0
    echo "$_lc" | grep -q -- "-dual_image" && echo 1 && return 0
    echo 0
}

infer_device() {
    # infer_device <foldername>
    # Returns one of: emmc|sd|snfi-nand|spim-nand|spim-nor
    _lc=$(echo "$1" | tr 'A-Z' 'a-z')

    case "$_lc" in
        *-emmc) echo "emmc" ;;
        *-sd) echo "sd" ;;
        *-snfi-nand*) echo "snfi-nand" ;;
        *-spim-nand*) echo "spim-nand" ;;
        *-spim-nor*) echo "spim-nor" ;;
        *) echo "spim-nand" ;;
    esac
}

validate_platform() {
    case "$1" in
        mt7981|mt7986|mt7988|mt7987) return 0 ;;
        *) return 1 ;;
    esac
}

if [ "${1:-}" = "-h" ] || [ "${1:-}" = "--help" ]; then
    usage
    exit 0
fi

# Parse options
while [ $# -gt 0 ]; do
    case "$1" in
        -h|--help)
            usage
            exit 0
            ;;
        -c|--config)
            shift
            [ -n "${1:-}" ] || die "Missing argument for --config"
            GLOBAL_PARTITION_CONFIG="$1"
            ;;
        -C|--config-dir)
            shift
            [ -n "${1:-}" ] || die "Missing argument for --config-dir"
            PARTITION_CONFIG_DIR="$1"
            ;;
        --)
            shift
            break
            ;;
        -* )
            die "Unknown option: $1"
            ;;
        * )
            # no positional args supported
            die "Unexpected argument: $1"
            ;;
    esac
    shift
done

# Env override for partition config
if [ -n "${PARTITION_CONFIG:-}" ] && [ -z "$GLOBAL_PARTITION_CONFIG" ] && [ -z "$PARTITION_CONFIG_DIR" ]; then
    if [ -d "$PARTITION_CONFIG" ]; then
        PARTITION_CONFIG_DIR="$PARTITION_CONFIG"
    else
        GLOBAL_PARTITION_CONFIG="$PARTITION_CONFIG"
    fi
fi

[ -d "$SIMG_DIR" ] || die "SIMG_DIR '$SIMG_DIR' not found. Create it and add board folders."
[ -d "$ATF_DIR" ] || die "ATF_DIR '$ATF_DIR' not found."
[ -f "$MK_IMAGE" ] || die "mk_image.sh not found: $MK_IMAGE"

mkdir -p "$OUTPUT_DIR" || die "Unable to create OUTPUT_DIR '$OUTPUT_DIR'"

# Resolve output dir to absolute path (mk_image.sh will cd into its own folder)
OUTPUT_DIR_ABS=$(cd "$OUTPUT_DIR" 2>/dev/null && pwd)
[ -n "$OUTPUT_DIR_ABS" ] || die "Unable to resolve OUTPUT_DIR '$OUTPUT_DIR'"

# Enumerate board folders
board_list=$(find "$SIMG_DIR" -mindepth 1 -maxdepth 1 -type d 2>/dev/null | sort)
if [ -z "$board_list" ]; then
    die "No board folders found in '$SIMG_DIR'."
fi

fail_count=0
built_count=0

for board_dir in $board_list; do
    board=$(basename "$board_dir")
    board_dir_abs=$(cd "$board_dir" 2>/dev/null && pwd)
    [ -n "$board_dir_abs" ] || die "Unable to resolve board_dir: $board_dir"
    platform=$(infer_platform "$board")

    if ! validate_platform "$platform"; then
        say "Skipping '$board' (unknown platform '$platform')"
        continue
    fi

    dual=$(infer_dual "$board")
    device=$(infer_device "$board")

    out_file="${OUTPUT_DIR_ABS}/${board}-simg.bin"

    # Auto-detect input files in the board folder (case-insensitive)
    bl2=$(find_first "$board_dir_abs" "*bl2*.img" "*bl2*.bin")
    fip=$(find_first "$board_dir_abs" "*fip*.bin" "*fip*.img")
    gpt=$(find_first "$board_dir_abs" "*gpt*.bin" "*gpt*.img")
    rf=$(find_first "$board_dir_abs" "*factory*.bin" "*factory*.img")
    # kernel image is optional; try common patterns (keep it strict to avoid false positives)
    kernel=$(find_first "$board_dir_abs" "*sysupgrade*" "*kernel*" "*firmware*" "*openwrt*" "*.ubi" "*.itb")

    # Convert to absolute paths for mk_image.sh (it cd's into its own DIR)
    [ -n "$bl2" ] && bl2=$(abs_path "$bl2")
    [ -n "$fip" ] && fip=$(abs_path "$fip")
    [ -n "$gpt" ] && gpt=$(abs_path "$gpt")
    [ -n "$rf" ] && rf=$(abs_path "$rf")
    [ -n "$kernel" ] && kernel=$(abs_path "$kernel")

    # Basic sanity: if kernel picked equals one of known inputs, drop it.
    if [ -n "$kernel" ]; then
        [ "$kernel" = "$bl2" ] && kernel=""
        [ "$kernel" = "$fip" ] && kernel=""
        [ "$kernel" = "$gpt" ] && kernel=""
        [ "$kernel" = "$rf" ] && kernel=""
    fi

    say "Building simg: $board (platform=$platform, device=$device, dual=$dual)"

    missing=""

    # Required files by device
    case "$device" in
        emmc)
            if ! need_file "$gpt" "GPT"; then missing="$missing GPT"; fi
            if ! need_file "$fip" "FIP"; then missing="$missing FIP"; fi
            ;;
        sd)
            if ! need_file "$gpt" "GPT"; then missing="$missing GPT"; fi
            if ! need_file "$bl2" "BL2"; then missing="$missing BL2"; fi
            if ! need_file "$fip" "FIP"; then missing="$missing FIP"; fi
            ;;
        snfi-nand|spim-nand|spim-nor)
            if ! need_file "$bl2" "BL2"; then missing="$missing BL2"; fi
            if ! need_file "$fip" "FIP"; then missing="$missing FIP"; fi
            ;;
        *)
            missing="$missing <unknown-device>"
            ;;
    esac

    if [ -n "$missing" ]; then
        say "Error: missing required file(s):$missing"
        say "  folder: $board_dir_abs"
        say "  detected:"
        say "    GPT : ${gpt:-<none>}"
        say "    BL2 : ${bl2:-<none>}"
        say "    FIP : ${fip:-<none>}"
        say "    RF  : ${rf:-<none>}"
        say "    KERN: ${kernel:-<none>}"
        say "----------------------------------------"
        fail_count=$((fail_count + 1))
        continue
    fi

    # Build argv (avoid 'sh -c' quoting issues; mk_image.sh is a bash script)
    set -- -p "$platform" -d "$device"

    # Partition config precedence:
    #  1) per-board yml inside board folder (MUST match folder name: <board>.yml/.yaml)
    #  2) --config <file>
    #  3) --config-dir <dir>/<device>.yml
    board_cfg1="$board_dir_abs/${board}.yml"
    board_cfg2="$board_dir_abs/${board}.yaml"

    # If any yml/yaml exists in board folder but does NOT match <board>.yml/.yaml, stop.
    other_cfg=$(find "$board_dir_abs" -maxdepth 1 -type f \( -iname "*.yml" -o -iname "*.yaml" \) \
        ! -iname "${board}.yml" ! -iname "${board}.yaml" 2>/dev/null | head -n 1)
    if [ -n "$other_cfg" ]; then
        say "Warning: found partition yml in board folder but filename does not match board folder name."
        say "  board : $board"
        say "  found : $other_cfg"
        say "  expect: $board_cfg1 (or $board_cfg2)"
        exit 1
    fi

    if [ -f "$board_cfg1" ]; then
        set -- "$@" -c "$(abs_path "$board_cfg1")"
    elif [ -f "$board_cfg2" ]; then
        set -- "$@" -c "$(abs_path "$board_cfg2")"
    elif [ -n "$GLOBAL_PARTITION_CONFIG" ]; then
        [ -f "$GLOBAL_PARTITION_CONFIG" ] || die "Partition config not found: $GLOBAL_PARTITION_CONFIG"
        set -- "$@" -c "$(abs_path "$GLOBAL_PARTITION_CONFIG")"
    elif [ -n "$PARTITION_CONFIG_DIR" ]; then
        [ -d "$PARTITION_CONFIG_DIR" ] || die "Partition config dir not found: $PARTITION_CONFIG_DIR"
        cfg_try1="$PARTITION_CONFIG_DIR/${device}.yml"
        cfg_try2="$PARTITION_CONFIG_DIR/${device}.yaml"
        if [ -f "$cfg_try1" ]; then
            set -- "$@" -c "$(abs_path "$cfg_try1")"
        elif [ -f "$cfg_try2" ]; then
            set -- "$@" -c "$(abs_path "$cfg_try2")"
        else
            die "Partition config for device '$device' not found in dir: $PARTITION_CONFIG_DIR"
        fi
    fi

    if [ "$device" = "emmc" ] || [ "$device" = "sd" ]; then
        set -- "$@" -g "$gpt"
    fi

    if [ "$device" != "emmc" ]; then
        set -- "$@" -b "$bl2"
    fi

    [ -n "$rf" ] && set -- "$@" -r "$rf"
    set -- "$@" -f "$fip"
    [ -n "$kernel" ] && set -- "$@" -k "$kernel"

    [ "$dual" = "1" ] && set -- "$@" --dual-image

    set -- "$@" -o "$out_file"

    if [ "${DRY_RUN:-0}" = "1" ]; then
        say "- [DRY_RUN] bash \"$MK_IMAGE\" $*"
        built_count=$((built_count + 1))
        say "----------------------------------------"
        continue
    fi

    # Execute
    bash "$MK_IMAGE" "$@"
    rc=$?
    if [ "$rc" != "0" ]; then
        say "Error: build failed for $board (exit=$rc)"
        say "----------------------------------------"
        fail_count=$((fail_count + 1))
        continue
    fi

    if [ -f "$out_file" ]; then
        say "Built: $out_file"
        built_count=$((built_count + 1))
    else
        say "Error: output not found: $out_file"
        fail_count=$((fail_count + 1))
    fi

    say "----------------------------------------"

done

say "Done. built=$built_count, failed=$fail_count"

if [ "$fail_count" -ne 0 ]; then
    exit 1
fi

exit 0
