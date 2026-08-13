#!/bin/bash
#===============================================================================
# generate_gpt.sh - Generate GPT binary files from JSON partition layouts
#
# Usage: [OPTIONS] ./generate_gpt.sh
#        ./generate_gpt.sh --help
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

# User-configurable variables
VERSION="${VERSION:-2025}"
SHOW="${SHOW:-0}"
DRAW="${DRAW:-0}"
SDMMC="${SDMMC:-0}"

# Paths
INPUT_FOLDER="./mt798x_gpt"
INPUT_FOLDER_SHOW="./mt798x_gpt_bin"
OUTPUT_FOLDER="./output_gpt"
PARSE_GPT_TOOL="./tools/parse_gpt.py"

#------------------------------------------------------------------------------
# Resolve VERSION -> tools_folder
#------------------------------------------------------------------------------
case "${VERSION}" in
	2025)
		TOOLS_FOLDER="./atf-20250711/tools/dev/gpt_editor"
		;;
	SP1|sp1)
		VERSION="SP1"
		TOOLS_FOLDER="./atf-20240117-bacca82a8/tools/dev/gpt_editor"
		;;
	SP2|sp2)
		VERSION="SP2"
		TOOLS_FOLDER="./atf-20260123/tools/dev/gpt_editor"
		;;
	*)
		error "Unsupported VERSION. Please specify VERSION=2025/SP1/SP2."
		exit 1
		;;
esac

#------------------------------------------------------------------------------
# --help / -h
#------------------------------------------------------------------------------
show_help() {
	cat <<EOF
generate_gpt.sh - Generate GPT (GUID Partition Table) binary files from
                  JSON partition layout descriptions for MT798x platforms

Usage:
  [OPTIONS] ./generate_gpt.sh

Modes:
  Default:            Convert JSON partition layouts -> GPT binary (.bin) files
                      Output: output_gpt/gpt-<name>-Yuzhii_md5-<hash>.bin
  SHOW=1:             Display partition info from existing GPT bin/img files
                      Output: output_gpt/info/<name>_gptinfo.txt
                      Output: output_gpt/info/<name>_gpt.json  (partition layout)
  DRAW=1:             Also generate partition layout PNG visualization
                      Output: output_gpt/picture/gpt-<name>.png
  SDMMC=1:            Generate GPT binary for SD/MMC boot mode

Optional:
  VERSION             Firmware version: 2025 | SP1 | SP2        (default: 2025)
  SHOW                Show existing GPT info: 0 | 1             (default: 0)
  DRAW                Generate PNG visualization: 0 | 1 | notitle (default: 0)
                      "notitle" draws without title text
  SDMMC               Generate for SD/MMC boot: 0 | 1           (default: 0)

Dependencies:
  python2.7           Required for GPT generation
  python3             Required for DRAW mode and SHOW JSON generation

Options:
  --help, -h          Show this help message and exit
EOF
	exit 0
}

case "${1:-}" in
	--help|-h) show_help ;;
esac

#------------------------------------------------------------------------------
# Environment Check
#------------------------------------------------------------------------------
check_environment() {
	step "Environment Check"

	# --- Python 2.7 ---
	if ! command -v python2.7 &>/dev/null; then
		error "Python2.7 is not installed on this system."
		exit 1
	fi
	info "Python2.7: $(python2.7 --version 2>&1)"

	# --- Python 3 ---
	if ! command -v python3 &>/dev/null; then
		error "Python3 is not installed on this system."
		exit 1
	fi
	info "Python3: $(python3 --version 2>&1)"

	info "Using GPT tools from: ${TOOLS_FOLDER}"

	mkdir -p "${OUTPUT_FOLDER}"
	mkdir -p "${OUTPUT_FOLDER}/picture"
	mkdir -p "${OUTPUT_FOLDER}/info"
	info "Environment Check passed"
}

#------------------------------------------------------------------------------
# Run SHOW Mode (display GPT info from existing bin/img files)
#------------------------------------------------------------------------------
run_show_mode() {
	step "SHOW Mode: Display GPT info from existing files"

	built_count=0
	fail_count=0

	local bin_file filename filename_no_extension output_file output_json json_ret
	for bin_file in "${INPUT_FOLDER_SHOW}"/*.bin "${INPUT_FOLDER_SHOW}"/*.img; do
		[ -e "${bin_file}" ] || continue

		filename=$(basename -- "${bin_file}")
		filename_no_extension="${filename%.*}"

		output_file="${OUTPUT_FOLDER}/info/${filename_no_extension}_gptinfo.txt"
		output_json="${OUTPUT_FOLDER}/info/${filename_no_extension}_gpt.json"

		echo ""
		echo "=============================="
		echo ""
		echo "Processing: ${filename}"
		echo ""
		echo "=============================="
		echo ""

		# Generate the raw GPT info dump
		python2.7 "${TOOLS_FOLDER}/mtk_gpt.py" --show "${bin_file}" > "${output_file}"

		if [ -f "${output_file}" ]; then
			info "Done: ${filename}, info written to: ${output_file}"

			# Parse the info dump and generate GPT partition JSON
			python3 "${PARSE_GPT_TOOL}" --i "${output_file}" --o "${output_json}"
			json_ret=$?
			if [ "${json_ret}" -eq 0 ] && [ -f "${output_json}" ]; then
				built_count=$((built_count + 1))
			else
				warn "JSON generation failed (exit code: ${json_ret})"
				fail_count=$((fail_count + 1))
			fi
		else
			error "Failed: ${filename} (output not found: ${output_file})"
			fail_count=$((fail_count + 1))
		fi

		echo ""
		echo "=============================="
		echo ""
	done

	info "All files processed"
	info "Success: ${built_count}  Failed: ${fail_count}"

	SHOW_BUILT_COUNT="${built_count}"
	SHOW_FAIL_COUNT="${fail_count}"
}

#------------------------------------------------------------------------------
# Run Generate Mode (convert JSON layouts -> GPT binaries)
#------------------------------------------------------------------------------
run_generate_mode() {
	step "Generate Mode: Convert JSON partition layouts -> GPT binaries"

	built_count=0
	fail_count=0
	png_built_count=0
	png_fail_count=0

	local json_file filename filename_no_extension
	local output_file output_file_sdmmc output_png
	local built_out_file_raw gpt_md5 built_base built_name_no_extension built_extension built_out_file

	for json_file in "${INPUT_FOLDER}"/*.json; do
		filename=$(basename -- "${json_file}")
		filename_no_extension="${filename%.*}"

		output_file="${OUTPUT_FOLDER}/gpt-${filename_no_extension}.bin"
		output_file_sdmmc="${OUTPUT_FOLDER}/gpt-${filename_no_extension}.sdmmc.bin"
		output_png="${OUTPUT_FOLDER}/picture/gpt-${filename_no_extension}.png"

		echo ""
		echo "=============================="
		echo ""
		echo "Processing: ${filename}"
		echo ""
		echo "=============================="
		echo ""

		if [ "${SDMMC}" = "1" ]; then
			python2.7 "${TOOLS_FOLDER}/mtk_gpt.py" --i "${json_file}" --o "${output_file_sdmmc}" --sdmmc
			built_out_file_raw="${output_file_sdmmc}"
		else
			python2.7 "${TOOLS_FOLDER}/mtk_gpt.py" --i "${json_file}" --o "${output_file}"
			built_out_file_raw="${output_file}"
		fi

		# Generate PNG visualization
		if [ "${DRAW}" = "notitle" ]; then
			python3 "${TOOLS_FOLDER}/partition_layout.py" --i "${json_file}" --o "${output_png}"
		fi
		if [ "${DRAW}" = "1" ]; then
			python3 "${TOOLS_FOLDER}/partition_layout.py" --i "${json_file}" --o "${output_png}" --title
		fi

		if [ -f "${built_out_file_raw}" ]; then
			gpt_md5=$(md5sum "${built_out_file_raw}" | awk '{print $1}')
			built_base=$(basename -- "${built_out_file_raw}")
			built_name_no_extension="${built_base%.*}"
			built_extension="${built_base##*.}"
			built_out_file="${OUTPUT_FOLDER}/${built_name_no_extension}-${AUTHOR}_md5-${gpt_md5}.${built_extension}"
			mv -f "${built_out_file_raw}" "${built_out_file}"
			info "Built: ${built_out_file}"
			info "Size: $(stat -c%s "${built_out_file}") bytes"
			built_count=$((built_count + 1))
		else
			error "output not found: ${built_out_file_raw}"
			fail_count=$((fail_count + 1))
		fi

		if [ "${DRAW}" = "1" ]; then
			if [ -f "${output_png}" ]; then
				info "Built: ${output_png}"
				png_built_count=$((png_built_count + 1))
			else
				error "output not found: ${output_png}"
				png_fail_count=$((png_fail_count + 1))
			fi
		fi

		echo ""
		echo "=============================="
		echo ""
		echo "Converted: ${filename}"
		echo ""
		echo "=============================="
		echo ""
	done

	info "All files converted"
	info "GPT bin Success: ${built_count}  Failed: ${fail_count}"
	if [ "${DRAW}" = "1" ]; then
		info "PNG Success: ${png_built_count}  Failed: ${png_fail_count}"
	fi

	GEN_BUILT_COUNT="${built_count}"
	GEN_FAIL_COUNT="${fail_count}"
	GEN_PNG_BUILT_COUNT="${png_built_count}"
	GEN_PNG_FAIL_COUNT="${png_fail_count}"
}

#------------------------------------------------------------------------------
# Print Summary
#------------------------------------------------------------------------------
print_summary() {
	echo ""
	echo "==========================================================================="
	if [ "${SHOW}" = "1" ]; then
		echo -e "  ${GREEN}GPT info extraction completed!${NC}"
		echo "  Success: ${SHOW_BUILT_COUNT}    Failed: ${SHOW_FAIL_COUNT}"
	else
		if [ "${GEN_FAIL_COUNT}" -eq 0 ]; then
			echo -e "  ${GREEN}GPT binary generation completed!${NC}"
		else
			echo -e "  ${YELLOW}GPT binary generation completed with failures${NC}"
		fi
		echo "  GPT bin Success: ${GEN_BUILT_COUNT}    Failed: ${GEN_FAIL_COUNT}"
		if [ "${DRAW}" = "1" ]; then
			echo "  PNG      Success: ${GEN_PNG_BUILT_COUNT}    Failed: ${GEN_PNG_FAIL_COUNT}"
		fi
	fi
	echo "==========================================================================="
	echo ""
	echo "  Output directory: ${OUTPUT_FOLDER}/"
	echo "==========================================================================="
}

#------------------------------------------------------------------------------
# Main
#------------------------------------------------------------------------------
main() {
	echo ""
	echo "==========================================================================="
	echo "  MT798x GPT generator script"
	echo "  VERSION: ${VERSION}"
	if [ "${SHOW}" = "1" ]; then
		echo "  Mode:    SHOW (display GPT info from existing files)"
	elif [ "${SDMMC}" = "1" ]; then
		echo "  Mode:    Generate (SD/MMC boot)"
	else
		echo "  Mode:    Generate (default)"
	fi
	[ "${DRAW}" != "0" ] && echo "  DRAW:    ${DRAW}"
	echo "  Tools:   ${TOOLS_FOLDER}"
	echo "  Output:  ${OUTPUT_FOLDER}"
	echo "==========================================================================="

	check_environment

	if [ "${SHOW}" = "1" ]; then
		run_show_mode
	else
		run_generate_mode
	fi

	print_summary
}

main "$@"
