#!/bin/bash
#===============================================================================
# compile_atf.sh - Batch compile ATF BL2 images for MediaTek MT798x platforms
#
# Usage: ./compile_atf.sh [CONFIG...]
#        ./compile_atf.sh --help
#===============================================================================

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

AUTHOR="Yuzhii"

TOOLCHAIN_ARM="arm-linux-gnueabi-"
TOOLCHAIN_AARCH64="aarch64-linux-gnu-"

# User-configurable variables
ATFCFG_DIR="${ATFCFG_DIR:-mt798x_atf}"
CFG_SUBDIR="${CFG_SUBDIR:-}"
OUTPUT_DIR="${OUTPUT_DIR:-output_bl2}"
VERSION="${VERSION:-2025}"
VARIANT="${VARIANT:-}"
VARIANT="${VARIANT,,}"
OC7981="${OC7981:-}"
OC7986="${OC7986:-}"

#------------------------------------------------------------------------------
# --help / -h
#------------------------------------------------------------------------------
show_help() {
	cat <<EOF
compile_atf.sh - Batch compile ATF (ARM Trusted Firmware) BL2 images
                 for MediaTek MT798x platforms

Usage:
  ./compile_atf.sh [CONFIG...]

Description:
  Builds ATF BL2 firmware images for config files under ATFCFG_DIR.
  Without arguments, all .config files are built.  Specify one or more
  config names to build only those configs.

Arguments:
  CONFIG              Config name(s) to build (optional).
                      Can be a plain name (e.g. "mt7981-ddr3-bga-ram"
                      matches "mt798x_atf/mt7981-ddr3-bga-ram.config")
                      or a relative sub-path (e.g. "mt7986/mt7986-ddr3-ram"
                      matches "mt798x_atf/mt7986/mt7986-ddr3-ram.config").
                      Multiple names may be given.  .config suffix is
                      optional.

Optional:
  VERSION             Firmware version: 2025 | SP1 | SP2        (default: 2025)
  ATFCFG_DIR          Config source directory                   (default: mt798x_atf)
  CFG_SUBDIR          Subdirectory under ATFCFG_DIR for extra   (default: empty)
                      configs (e.g. "normal")
  OUTPUT_DIR          Output directory for built images         (default: output_bl2)
  VARIANT             Apply variant options: nonmbm | ubootmod | ubi
                      - nonmbm   -> NAND_SKIP_BAD=y
                      - ubootmod -> NAND_UBI=y
                      - ubi      -> NAND_UBI=y
  OC7981              MT7981 overclock ARMPLL freq: 13~18       (1300~1800 MHz)
  OC7986              MT7986 overclock ARMPLL freq: 16~25       (1600~2500 MHz)

Options:
  --help, -h          Show this help message and exit

Examples:
  ./compile_atf.sh                              # Build all configs
  ./compile_atf.sh mt7981-ddr3-bga-ram          # Build one config
  ./compile_atf.sh mt7981-ddr3-bga-ram mt7986-ddr4-ram  # Build two configs
  ./compile_atf.sh mt7986/mt7986-ddr3-ram       # Build from subdirectory
  VERSION=SP2 ./compile_atf.sh normal/mt7981-ram  # With env variables
EOF
	exit 0
}

#------------------------------------------------------------------------------
# Parse arguments
#------------------------------------------------------------------------------
TARGET_CONFIGS=""
for arg in "$@"; do
	case "${arg}" in
		--help|-h) show_help ;;
		*) TARGET_CONFIGS="${TARGET_CONFIGS} ${arg}" ;;
	esac
done
TARGET_CONFIGS="${TARGET_CONFIGS# }" # strip leading space

#------------------------------------------------------------------------------
# Resolve VERSION -> ATF_DIR
#------------------------------------------------------------------------------
if [ -z "${ATF_DIR}" ]; then
	case "${VERSION}" in
		2025)
			ATF_DIR="atf-20250711"
			;;
		SP1|sp1)
			VERSION="SP1"
			ATF_DIR="atf-20240117-bacca82a8"
			;;
		SP2|sp2)
			VERSION="SP2"
			ATF_DIR="atf-20260123"
			;;
		*)
			error "Unsupported VERSION. Please specify VERSION=2025/SP1/SP2 or set ATF_DIR."
			exit 1
			;;
	esac
fi

# Determine ATF makefile name (some trees use lowercase 'makefile')
if [ -e "${ATF_DIR}/makefile" ]; then
	ATF_MKFILE="makefile"
else
	ATF_MKFILE="Makefile"
fi

#------------------------------------------------------------------------------
# Helpers
#------------------------------------------------------------------------------
append_unique_line() {
	local line="$1"
	local file="$2"
	grep -qxF "${line}" "${file}" 2>/dev/null || echo "${line}" >> "${file}"
}

validate_int_range() {
	local value="$1"
	local min="$2"
	local max="$3"
	local name="$4"
	case "${value}" in
		*[!0-9]*|"")
			error "${name} must be an integer in range ${min}~${max}."
			exit 1
			;;
	esac
	if [ "${value}" -lt "${min}" ] || [ "${value}" -gt "${max}" ]; then
		error "${name} out of range (${min}~${max}): ${value}"
		exit 1
	fi
}

#------------------------------------------------------------------------------
# Environment Check
#------------------------------------------------------------------------------
check_environment() {
	step "Environment Check"

	if [ ! -d "${ATFCFG_DIR}" ]; then
		error "ATFCFG_DIR '${ATFCFG_DIR}' not found."
		exit 1
	fi
	info "ATFCFG_DIR: ${ATFCFG_DIR}"

	if [ ! -d "${ATF_DIR}" ]; then
		error "ATF_DIR '${ATF_DIR}' not found."
		exit 1
	fi
	info "ATF_DIR: ${ATF_DIR}"
	info "ATF Makefile: ${ATF_MKFILE}"
	info "VERSION: ${VERSION}"

	mkdir -p "${OUTPUT_DIR}"
	mkdir -p "${ATF_DIR}/build"
	info "Environment Check passed"
}

#------------------------------------------------------------------------------
# Build Config List
#------------------------------------------------------------------------------
build_config_list() {
	step "Build Config List"

	CONFIG_LIST=""

	# Always include configs directly under ATFCFG_DIR
	local cfg
	for cfg in "${ATFCFG_DIR}"/*.config; do
		[ -f "${cfg}" ] && CONFIG_LIST="${CONFIG_LIST} ${cfg}"
	done

	# Also include configs from selected subdir (e.g. "normal")
	if [ -n "${CFG_SUBDIR}" ]; then
		if [ ! -d "${ATFCFG_DIR}/${CFG_SUBDIR}" ]; then
			error "CFG_SUBDIR '${CFG_SUBDIR}' not found in '${ATFCFG_DIR}'."
			exit 1
		fi
		for cfg in "${ATFCFG_DIR}/${CFG_SUBDIR}"/*.config; do
			[ -f "${cfg}" ] && CONFIG_LIST="${CONFIG_LIST} ${cfg}"
		done
	fi

	# Filter to requested configs when positional arguments are given
	if [ -n "${TARGET_CONFIGS}" ]; then
		local filtered_list=""
		local target cfg_full cfg_rel cfg_rel_nosuffix cfg_name_nosuffix matched
		for target in ${TARGET_CONFIGS}; do
			target="${target%.config}" # strip optional .config suffix
			matched=0
			for cfg_full in ${CONFIG_LIST}; do
				cfg_rel="${cfg_full#"${ATFCFG_DIR}"/}"
				cfg_rel_nosuffix="${cfg_rel%.config}"
				cfg_name_nosuffix="$(basename "${cfg_rel}" .config)"
				if [ "${target}" = "${cfg_rel_nosuffix}" ] || [ "${target}" = "${cfg_name_nosuffix}" ]; then
					filtered_list="${filtered_list} ${cfg_full}"
					matched=1
				fi
			done
			if [ "${matched}" -eq 0 ]; then
				error "config '${target}' not found in '${ATFCFG_DIR}'${CFG_SUBDIR:+ or '${ATFCFG_DIR}/${CFG_SUBDIR}'}"
				exit 1
			fi
		done
		CONFIG_LIST="${filtered_list}"
	fi

	if [ -z "${CONFIG_LIST}" ]; then
		error "no .config files found in '${ATFCFG_DIR}'${CFG_SUBDIR:+ or '${ATFCFG_DIR}/${CFG_SUBDIR}'}"
		exit 1
	fi

	info "Configs to build:$(echo "${CONFIG_LIST}" | tr ' ' '\n' | grep -c '.')"
	for cfg in ${CONFIG_LIST}; do
		info "  - ${cfg#"${ATFCFG_DIR}"/}"
	done
}

#------------------------------------------------------------------------------
# Build One Config (single ATF BL2 image)
#------------------------------------------------------------------------------
build_one_config() {
	local cfg_file="$1"
	local cfg_rel="${cfg_file#"${ATFCFG_DIR}"/}"
	local cfg_name
	cfg_name=$(basename "${cfg_file}")
	local cfg_base="${cfg_name%.config}"
	local cfg_tag
	cfg_tag=$(echo "${cfg_rel}" | sed -e 's/\.config$//' -e 's#/#_#g')
	local feature_tag=""

	# Extract SoC from config filename:
	#   mt7981-ddr3-bga-ram.config  -> soc=mt7981
	#   atf-mt7986-ddr4-ram.config  -> soc=mt7986
	local soc
	soc=$(echo "${cfg_base}" | sed -e 's/^atf-//' | cut -d'-' -f1)

	# Resolve toolchain
	if [ -z "${TOOLCHAIN}" ]; then
		if [ "${soc}" = "mt7629" ]; then
			TOOLCHAIN="${TOOLCHAIN_ARM}"
		else
			TOOLCHAIN="${TOOLCHAIN_AARCH64}"
		fi
		info "Using toolchain ${TOOLCHAIN} for SOC ${soc}"
	fi
	if ! command -v "${TOOLCHAIN}gcc" &>/dev/null; then
		error "${TOOLCHAIN}gcc not found!"
		exit 1
	fi
	export CROSS_COMPILE="${TOOLCHAIN}"

	# Configration overview
	echo "======================================================================"
	echo "Configration overview:"
	echo "======================================================================"
	echo "SOC: ${soc}"
	echo "Version: ${VERSION}"
	echo "Using ATF_DIR: ${ATF_DIR}"
	echo "Using toolchain: ${TOOLCHAIN}gcc"
	echo "Building BL2: ${cfg_rel}"

	# Prepare build/.config
	rm -rf "${ATF_DIR}/build"
	mkdir -p "${ATF_DIR}/build"
	cp -f "${cfg_file}" "${ATF_DIR}/build/.config"

	# Extra options only apply to configs under normal/
	local is_normal_cfg=0
	case "${cfg_rel}" in
		normal/*) is_normal_cfg=1 ;;
	esac

	if [ "${is_normal_cfg}" = "1" ]; then
		case "${VARIANT}" in
			nonmbm)
				append_unique_line "_NAND_SKIP_BAD=y" "${ATF_DIR}/build/.config"
				feature_tag="${feature_tag}-nonmbm"
				;;
			ubootmod)
				append_unique_line "_NAND_UBI=y" "${ATF_DIR}/build/.config"
				feature_tag="${feature_tag}-ubootmod"
				;;
			ubi)
				append_unique_line "_NAND_UBI=y" "${ATF_DIR}/build/.config"
				feature_tag="${feature_tag}-ubi"
				;;
		esac

		# Overclocking for MT7981
		if [ -n "${OC7981}" ] && [ "${soc}" = "mt7981" ]; then
			validate_int_range "${OC7981}" 13 18 "OC7981"
			sed -i '/^MT7981_ARMPLL_FREQ_[0-9][0-9][0-9][0-9]=y$/d' "${ATF_DIR}/build/.config"
			append_unique_line "MT7981_ARMPLL_FREQ_${OC7981}00=y" "${ATF_DIR}/build/.config"
			feature_tag="${feature_tag}-OC${OC7981}00"
		fi

		# Overclocking for MT7986
		if [ -n "${OC7986}" ] && [ "${soc}" = "mt7986" ]; then
			validate_int_range "${OC7986}" 16 25 "OC7986"
			sed -i '/^MT7986_ARMPLL_FREQ_[0-9][0-9][0-9][0-9]=y$/d' "${ATF_DIR}/build/.config"
			append_unique_line "MT7986_ARMPLL_FREQ_${OC7986}00=y" "${ATF_DIR}/build/.config"
			feature_tag="${feature_tag}-OC${OC7986}00"
		fi
	fi

	if echo "${feature_tag}" | grep -q "OC"; then
		info "Feature: Overclocking enabled (${feature_tag})"
	fi
	if echo "${feature_tag}" | grep -q "nonmbm"; then
		info "Feature: NAND_SKIP_BAD enabled"
	fi
	if echo "${feature_tag}" | grep -q "ubootmod"; then
		info "Feature: NAND_UBI enabled (ubootmod)"
	elif echo "${feature_tag}" | grep -q "ubi"; then
		info "Feature: NAND_UBI enabled (ubi)"
	fi

	# Build
	echo "======================================================================"
	echo "Build ATF with config: ${cfg_name}"
	echo "======================================================================"
	local build_ok=1
	if [ "${VERSION}" = "2025" ] || [ "${VERSION}" = "SP1" ] || [ "${VERSION}" = "SP2" ]; then
		make -C "${ATF_DIR}" olddefconfig || build_ok=0
	else
		make -C "${ATF_DIR}" defconfig || build_ok=0
	fi
	make -C "${ATF_DIR}" -f "${ATF_MKFILE}" clean \
		CONFIG_CROSS_COMPILER="${TOOLCHAIN}" CROSS_COMPILER="${TOOLCHAIN}" || build_ok=0
	if [ "${build_ok}" = "1" ]; then
		make -C "${ATF_DIR}" -f "${ATF_MKFILE}" all \
			CONFIG_CROSS_COMPILER="${TOOLCHAIN}" CROSS_COMPILER="${TOOLCHAIN}" \
			-j "$(nproc)" || build_ok=0
	fi
	echo "======================================================================"

	# Collect output
	if [ "${build_ok}" = "1" ] && [ -f "${ATF_DIR}/build/${soc}/release/bl2.img" ]; then
		local src_file="${ATF_DIR}/build/${soc}/release/bl2.img"
		local bl2_md5
		bl2_md5=$(md5sum "${src_file}" | awk '{print $1}')
		local out_name="bl2-${cfg_tag}-${VERSION}${feature_tag}-${AUTHOR}_md5-${bl2_md5}.img"
		cp -f "${src_file}" "${OUTPUT_DIR}/${out_name}"
		info "bl2-${cfg_tag}-${VERSION}${feature_tag} build done"
		info "Output file: ${OUTPUT_DIR}/${out_name}"
		info "bl2.img size: $(stat -c%s "${src_file}") bytes"
		return 0
	elif [ "${build_ok}" = "1" ] && [ -f "${ATF_DIR}/build/${soc}/release/bl2.bin" ]; then
		local src_file="${ATF_DIR}/build/${soc}/release/bl2.bin"
		local bl2_md5
		bl2_md5=$(md5sum "${src_file}" | awk '{print $1}')
		local out_name="bl2-${cfg_tag}-${VERSION}${feature_tag}-${AUTHOR}_md5-${bl2_md5}.bin"
		cp -f "${src_file}" "${OUTPUT_DIR}/${out_name}"
		warn "bl2.img not found, fallback to bl2.bin"
		info "bl2-${cfg_tag}-${VERSION}${feature_tag} build done (using bl2.bin)"
		info "Output file: ${OUTPUT_DIR}/${out_name}"
		info "bl2.bin size: $(stat -c%s "${src_file}") bytes"
		return 0
	else
		error "bl2 build fail for ${cfg_rel}! (neither bl2.img nor bl2.bin found)"
		return 1
	fi
}

#------------------------------------------------------------------------------
# Print Summary
#------------------------------------------------------------------------------
print_summary() {
	echo ""
	echo "==========================================================================="
	if [ "${FAIL_COUNT}" -eq 0 ]; then
		echo -e "  ${GREEN}ATF BL2 batch build completed!${NC}"
	else
		echo -e "  ${YELLOW}ATF BL2 batch build completed with failures${NC}"
	fi
	echo "==========================================================================="
	echo ""
	echo "  Success: ${SUCCESS_COUNT}    Failed: ${FAIL_COUNT}"
	if [ "${FAIL_COUNT}" -gt 0 ]; then
		echo ""
		echo "  Failed configs:"
		for cfg in ${FAILED_CONFIGS}; do
			echo "    - ${cfg}"
		done
	fi
	echo ""
	echo "  Output directory: ${OUTPUT_DIR}/"
	echo "==========================================================================="
}

#------------------------------------------------------------------------------
# Main
#------------------------------------------------------------------------------
main() {
	echo ""
	echo "==========================================================================="
	echo "  MT798x ATF BL2 batch build script"
	echo "  ATF_DIR:  ${ATF_DIR}"
	echo "  VERSION:  ${VERSION}"
	echo "  OUTPUT:   ${OUTPUT_DIR}"
	echo "==========================================================================="

	check_environment
	build_config_list

	SUCCESS_COUNT=0
	FAIL_COUNT=0
	FAILED_CONFIGS=""

	for cfg_file in ${CONFIG_LIST}; do
		if build_one_config "${cfg_file}"; then
			SUCCESS_COUNT=$((SUCCESS_COUNT + 1))
		else
			FAIL_COUNT=$((FAIL_COUNT + 1))
			FAILED_CONFIGS="${FAILED_CONFIGS} ${cfg_file#"${ATFCFG_DIR}"/}"
		fi
	done

	print_summary

	if [ "${SUCCESS_COUNT}" -eq 0 ]; then
		error "all BL2 builds failed."
		exit 1
	fi

	info "At least one BL2 build succeeded, continue workflow."
}

main "$@"
