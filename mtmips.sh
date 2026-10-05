#!/bin/bash
# mtmips.sh - Build U-Boot for MediaTek MT7620/MT7621/MT7628/MT7688 (MIPS)
#
# Usage: SOC=<mt7620|mt7621|mt7628|mt7688> BOARD=<board_name> [VERSION=2025] ./mtmips.sh
#
# Build U-Boot for MediaTek MT7620/MT7621/MT7628/MT7688 (MIPS) platform.
#
# Note: mt7628 and mt7688 share the same mt76x8 toolchain.
#
# Required:
#   SOC=<mt7620|mt7621|mt7628|mt7688>   Target SoC
#   BOARD=<board>           Target board name (matches defconfig: ${SOC}_${BOARD}_defconfig)
#
# Options:
#   VERSION=2025    U-Boot version (default: 2025 -> uboot-mtk-20250711)
#   TOOLCHAIN=...   Cross-compiler prefix (auto-detected from ./openwrt*/toolchain-mipsel*)
#   JOBS=<n>        Parallel make jobs (default: nproc)
#   STAGING_DIR=... Staging directory (auto-detected from TOOLCHAIN)
#   --help, -h, help    Show this help message and exit
#
# Examples:
#   SOC=mt7620 BOARD=rfb                     ./mtmips.sh
#   SOC=mt7621 BOARD=rfb                     ./mtmips.sh
#   SOC=mt7628 BOARD=rfb                     ./mtmips.sh
#   SOC=mt7688 BOARD=rfb                     ./mtmips.sh

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

info()    { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn()    { echo -e "${YELLOW}[WARN]${NC}  $*"; }
error()   { echo -e "${RED}[ERROR]${NC} $*"; }
step()    { echo -e "\n${BLUE}=== $* ===${NC}"; }
die()     { error "$*"; exit 1; }

# User-configurable variables
VERSION="${VERSION:-2025}"

# Output
OUTPUT_DIR="output_mtmips"

# URL of the prebuilt OpenWrt toolchain (can be overridden by env)
TOOLCHAIN_URL="${TOOLCHAIN_URL:-}"

#------------------------------------------------------------------------------
# --help / -h
#------------------------------------------------------------------------------
usage() {
	sed -n '2,/^[^#]/p' "$0" | grep -E '^#( |$)' | sed 's/^# \?//'
	exit 0
}

case "${1:-}" in
	--help|-h|help)
		usage
		;;
esac

#------------------------------------------------------------------------------
# SOC parameter check
#------------------------------------------------------------------------------
SOC="${SOC,,}" # Transform to lowercase

if [ -z "${SOC}" ]; then
	error "SOC is required."
	echo "Usage: SOC=<mt7620|mt7621|mt7628|mt7688> BOARD=<board_name> [VERSION=2025] $0"
	echo "Try '$0 --help' for more information."
	exit 1
fi

# Map SOC to OpenWrt toolchain target (mt7628/mt7688 both use mt76x8 toolchain)
case "${SOC}" in
	mt7620)
		TOOLCHAIN_SOC="mt7620"
		TOOLCHAIN_URL_NAME="ramips-mt7620"
		TOOLCHAIN_PATTERN="openwrt*mt7620*"
		;;
	mt7621)
		TOOLCHAIN_SOC="mt7621"
		TOOLCHAIN_URL_NAME="ramips-mt7621"
		TOOLCHAIN_PATTERN="openwrt*mt7621*"
		;;
	mt7628|mt7688)
		TOOLCHAIN_SOC="mt76x8"
		TOOLCHAIN_URL_NAME="ramips-mt76x8"
		TOOLCHAIN_PATTERN="openwrt*mt76x8*"
		;;
	*)
		error "Unsupported SOC='${SOC}'. Valid values: mt7620, mt7621, mt7628, mt7688"
		exit 1
		;;
esac

SOC_UPPER="${SOC^^}"

if [ -z "${BOARD}" ]; then
	error "BOARD is required."
	echo "Usage: SOC=${SOC} BOARD=<board_name> [VERSION=2025] $0"
	echo "Try '$0 --help' for more information."
	exit 1
fi

#------------------------------------------------------------------------------
# Resolve VERSION -> UBOOT_DIR
#------------------------------------------------------------------------------
case "${VERSION}" in
	2025)
		UBOOT_DIR="uboot-mtk-20250711"
		;;
	*)
		error "Unsupported VERSION='${VERSION}'. Please specify VERSION=2025."
		exit 1
		;;
esac

# Default toolchain URL (set after SOC is known)
if [ -z "${TOOLCHAIN_URL}" ]; then
	TOOLCHAIN_URL="https://downloads.openwrt.org/releases/25.12.5/targets/ramips/${TOOLCHAIN_SOC}/openwrt-toolchain-25.12.5-${TOOLCHAIN_URL_NAME}_gcc-14.3.0_musl.Linux-x86_64.tar.zst"
fi

# Config
UBOOT_CFG="${SOC}_${BOARD}_defconfig"

# Helpers
get_config() {
	grep -oP "^CONFIG_$1=\K.*" "${UBOOT_DIR}/.config" 2>/dev/null || true
}

#------------------------------------------------------------------------------
# Auto-detect or Download Toolchain
#------------------------------------------------------------------------------
resolve_toolchain() {
	step "Resolve Toolchain [${SOC_UPPER}]"

	TOOLCHAIN_BIN=$(cd ./${TOOLCHAIN_PATTERN}/toolchain-mipsel*/bin 2>/dev/null && pwd || true)
	if [ -z "${TOOLCHAIN_BIN}" ]; then
		warn "Toolchain not found in current directory."
		info "Looking for: ./${TOOLCHAIN_PATTERN}/toolchain-mipsel*/bin"
		read -p "Download it now? [Y/n] " dlcc
		dlcc="${dlcc:-Y}"
		case "${dlcc}" in
			[Yy]*)
				info "Downloading toolchain from: ${TOOLCHAIN_URL}"
				if command -v wget &>/dev/null; then
					wget -O - "${TOOLCHAIN_URL}" | tar --zstd -xf - || die "Toolchain download failed."
				elif command -v curl &>/dev/null; then
					curl -L "${TOOLCHAIN_URL}" | tar --zstd -xf - || die "Toolchain download failed."
				else
					die "Neither wget nor curl found. Install one or download manually: ${TOOLCHAIN_URL}"
				fi
				TOOLCHAIN_BIN=$(cd ./${TOOLCHAIN_PATTERN}/toolchain-mipsel*/bin 2>/dev/null && pwd || true)
				[ -n "${TOOLCHAIN_BIN}" ] || die "Toolchain not found after extraction."
				;;
			*)
				die "Toolchain required. Set TOOLCHAIN=... or place ${TOOLCHAIN_PATTERN}/toolchain-mipsel*/ in current directory."
				;;
		esac
	fi

	TOOLCHAIN="${TOOLCHAIN_BIN}/mipsel-openwrt-linux-"
	Staging="${TOOLCHAIN_BIN%/bin}"
	Staging="${Staging%/toolchain-*}"

	info "Toolchain: ${TOOLCHAIN}"
	info "STAGING_DIR: ${Staging}"
}

#------------------------------------------------------------------------------
# Environment Check
#------------------------------------------------------------------------------
check_environment() {
	step "Environment Check [SOC: ${SOC_UPPER}] [BOARD: ${BOARD}]"

	if ! command -v python3 &>/dev/null; then
		error "Python 3 is not installed."
		error "Please install: sudo apt install -y python3"
		exit 1
	fi
	info "Python3: $(python3 --version 2>&1)"

	if ! command -v "${TOOLCHAIN}gcc" &>/dev/null; then
		error "${TOOLCHAIN}gcc not found!"
		exit 1
	fi
	info "Toolchain: $(${TOOLCHAIN}gcc --version | head -1)"

	if [ ! -d "${UBOOT_DIR}" ]; then
		error "U-Boot directory '${UBOOT_DIR}' not found!"
		exit 1
	fi
	info "U-Boot Dir: ${UBOOT_DIR}"

	if [ ! -f "${UBOOT_DIR}/configs/${UBOOT_CFG}" ]; then
		error "Defconfig not found: ${UBOOT_DIR}/configs/${UBOOT_CFG}"
		exit 1
	fi
	info "Defconfig: ${UBOOT_CFG}"

	# Parallel jobs
	if [ -z "${JOBS}" ]; then
		if command -v nproc &>/dev/null; then
			JOBS=$(nproc)
		else
			JOBS=1
		fi
	fi
	info "JOBS: ${JOBS}"

	info "Environment Check passed"
}

#------------------------------------------------------------------------------
# Print Configuration
#------------------------------------------------------------------------------
print_configuration() {
	step "Configuration"
	info "VERSION:     ${VERSION}"
	info "SOC:         ${SOC}"
	info "BOARD:       ${BOARD}"
	info "U-Boot Dir:  ${UBOOT_DIR}"
	info "Toolchain:   ${TOOLCHAIN}"
	info "STAGING_DIR: ${Staging}"
	info "Defconfig:   ${UBOOT_CFG}"
}

#------------------------------------------------------------------------------
# Build U-Boot
#------------------------------------------------------------------------------
build_uboot() {
	step "Build U-Boot [${SOC_UPPER}]"

	rm -f "${UBOOT_DIR}/u-boot.bin" "${UBOOT_DIR}/u-boot-with-spl.bin"
	cp -f "${UBOOT_DIR}/configs/${UBOOT_CFG}" "${UBOOT_DIR}/.config"

	make -C "${UBOOT_DIR}" olddefconfig
	make -C "${UBOOT_DIR}" clean
	make -C "${UBOOT_DIR}" CROSS_COMPILE="${TOOLCHAIN}" STAGING_DIR="${Staging}" -j "${JOBS}" all

	# Determine output image: respect CONFIG_BUILD_TARGET (e.g. u-boot-with-spl.bin for SPL builds)
	local uboot_bin
	uboot_bin=$(get_config "BUILD_TARGET")
	uboot_bin=$(echo "${uboot_bin}" | tr -d '"')
	[ -n "${uboot_bin}" ] || uboot_bin="u-boot.bin"

	if [ ! -f "${UBOOT_DIR}/${uboot_bin}" ]; then
		error "U-Boot build failed! ${uboot_bin} not generated."
		exit 1
	fi
	info "U-Boot build done! (image: ${uboot_bin}, $(stat -c%s "${UBOOT_DIR}/${uboot_bin}") bytes)"

	UBOOT_BIN="${uboot_bin}"
}

#------------------------------------------------------------------------------
# Copy Output Files
#------------------------------------------------------------------------------
copy_outputs() {
	step "Copy Output Files"

	mkdir -p "${OUTPUT_DIR}"

	local md5sum
	md5sum=$(md5sum "${UBOOT_DIR}/${UBOOT_BIN}" | awk '{print $1}')
	info "${UBOOT_BIN} md5: ${md5sum}"

	local ubootname="${SOC}-u-boot-${BOARD}-${VERSION}_md5-${md5sum}.bin"
	cp -f "${UBOOT_DIR}/${UBOOT_BIN}" "${OUTPUT_DIR}/${ubootname}"

	info "${SOC}-u-boot-${BOARD}-${VERSION} build done"
	info "Output: ${OUTPUT_DIR}/${ubootname}"

	UBOOT_OUTPUT="${OUTPUT_DIR}/${ubootname}"
}

#------------------------------------------------------------------------------
# Print Summary
#------------------------------------------------------------------------------
print_summary() {
	echo ""
	echo "==========================================================================="
	echo -e "  ${GREEN}${SOC_UPPER} ${BOARD} U-Boot build completed!${NC}"
	echo "==========================================================================="
	echo ""
	echo "  Output directory: ${OUTPUT_DIR}/"
	echo ""

	local size
	if [ -n "${UBOOT_OUTPUT}" ] && [ -f "${UBOOT_OUTPUT}" ]; then
		size=$(stat -c%s "${UBOOT_OUTPUT}")
		printf "    %-50s  %10s bytes\n" "${UBOOT_OUTPUT}" "${size}"
	fi
	echo ""
	echo "==========================================================================="
}

#------------------------------------------------------------------------------
# Main
#------------------------------------------------------------------------------
main() {
	echo ""
	echo "==========================================================================="
	echo "  MTMIPS U-Boot build script"
	echo "  SOC:      ${SOC_UPPER}"
	echo "  BOARD:    ${BOARD}"
	echo "  VERSION:  ${VERSION}"
	echo "  U-Boot:   ${UBOOT_DIR}"
	echo "  Output:   ${OUTPUT_DIR}"
	echo "==========================================================================="

	resolve_toolchain
	check_environment
	print_configuration
	build_uboot
	copy_outputs

	print_summary
}

main "$@"
