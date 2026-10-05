#!/bin/bash
# openwrt-gpt.sh - Generate SDMMC & eMMC GPT binary files using ptgen
#
# Usage:
#   ./openwrt-gpt.sh
#
# Output:
#   output_gpt/openwrt-emmc-gpt.bin
#   output_gpt/openwrt-sdmmc-gpt.bin
#
# Optional:
#   ROOTFS_PARTSIZE   production partition size in MiB  (default: 1024)
#
# Options:
#   --help, -h          Show this help message and exit

usage() {
	sed -n '2,/^[^#]/p' "$0" | grep -E '^#( |$)' | sed 's/^# \?//'
	exit 0
}

case "${1:-}" in
	--help|-h) usage ;;
esac

ROOTFS_PARTSIZE=${ROOTFS_PARTSIZE:-1024}
OUTDIR="./output_gpt"

# Compile ptgen from source before use
echo "==> Compiling ptgen..."
gcc -Wall -O2 tools/ptgen.c -o tools/ptgen || { echo "Error: ptgen compilation failed"; exit 1; }
PTGEN="tools/ptgen"

mkdir -p "$OUTDIR"

# ------------------------------------------------------------------
# eMMC GPT
# ------------------------------------------------------------------
echo "==> Generating eMMC GPT (production=${ROOTFS_PARTSIZE}M)..."
"$PTGEN" -g -o "$OUTDIR/openwrt-emmc-gpt.bin" -a 1 -l 1024 \
	-t 0x83 -N ubootenv -r -p 512k@4M \
	-t 0x83 -N factory   -r -p 2M@4608k \
	-t 0xef -N fip       -r -p 4M@6656k \
	      -N recovery   -r -p 32M@12M \
	-t 0x2e -N production    -p ${ROOTFS_PARTSIZE}M@64M

echo "   eMMC GPT -> $OUTDIR/openwrt-emmc-gpt.bin"

# ------------------------------------------------------------------
# SDMMC GPT
# ------------------------------------------------------------------
echo "==> Generating SDMMC GPT (production=${ROOTFS_PARTSIZE}M)..."
"$PTGEN" -g -o "$OUTDIR/openwrt-sdmmc-gpt.bin" -a 1 -l 1024 \
	-H \
	-t 0x83 -N bl2       -r -p 4079k@17k \
	-t 0x83 -N ubootenv  -r -p 512k@4M \
	-t 0x83 -N factory    -r -p 2M@4608k \
	-t 0xef -N fip        -r -p 4M@6656k \
	      -N recovery    -r -p 32M@12M \
	      -N install     -r -p 20M@44M \
	-t 0x2e -N production     -p ${ROOTFS_PARTSIZE}M@64M

echo "   SDMMC GPT -> $OUTDIR/openwrt-sdmmc-gpt.bin"

echo ""
echo "Done."
ls -la "$OUTDIR/openwrt-emmc-gpt.bin" "$OUTDIR/openwrt-sdmmc-gpt.bin"
