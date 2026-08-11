#!/bin/sh
# ============================================================================
# build.sh - Main build script for MediaTek MT798x platforms (ATF + U-Boot)
#
#   Run './build.sh --help' for full usage information.
# ============================================================================

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

info()    { printf "${GREEN}[INFO]${NC}  %s\n" "$*"; }
warn()    { printf "${YELLOW}[WARN]${NC}  %s\n" "$*"; }
error()   { printf "${RED}[ERROR]${NC} %s\n" "$*"; }
step()    { printf "\n${BLUE}=== %s ===${NC}\n" "$*"; }

AUTHOR="quytttb"

TOOLCHAIN_ARM=arm-linux-gnueabi-
TOOLCHAIN_AARCH64=aarch64-linux-gnu-

# ATF directory and uboot directory names
ATF24=atf-20240117-bacca82a8
ATF25=atf-20250711
ATF26=atf-20260123
UBOOT25=uboot-mtk-20250711

# Default selection
VERSION=${VERSION:-2025}
VARIANT=${VARIANT:-default}
FSTHEME=${FSTHEME:-bootstrap}
fixedparts=${FIXED_MTDPARTS:-1}
multilayout=${MULTI_LAYOUT:-0}
simg=${SIMG:-0}
UBIMNG=${UBIMNG:-0}
TELNETD=${TELNETD:-0}
NAND_RAW=${NAND_RAW:-0}
COPY_BL2=${COPY_BL2:-1}
FIP_COMPRESS=${FIP_COMPRESS:-0}
clean_mode=0

print_help() {
	cat <<EOF
build.sh - Build ATF + U-Boot for MediaTek MT798x platforms

Usage:
  BOARD=<board> [OPTIONS] ./build.sh
  ./build.sh --clean
  ./build.sh --help

Required:
  BOARD               Target board name (e.g. cmcc_a10, sn_r1)

Optional:
  SOC                 SoC: mt7981 | mt7986 | mt7987 | mt7988 (auto-detected if omitted)
  VERSION             Firmware version: 2025 | SP1 | SP2        (default: 2025)
  VARIANT             Build variant: default | ubootmod | ubi | nonmbm | openwrt
                      (default: default)
  FSTHEME             Failsafe UI theme: bootstrap | gl | mtk   (default: bootstrap)
  FIXED_MTDPARTS      Enable fixed MTD partitions: 0 | 1        (default: 1)
  MULTI_LAYOUT        Enable multi MTD layout: 0 | 1            (default: 0)
  SIMG                Enable failsafe SIMG support: 0 | 1       (default: 0)
  UBIMNG              Enable failsafe UBI management: 0 | 1     (default: 0)
  TELNETD             Enable telnetd: 0 | 1                     (default: 0)
  NAND_RAW            Enable NAND raw OOB backup: 0 | 1          (default: 0)
  COPY_BL2            Copy bl2.img to output/: 0 | 1            (default: 1)
  FIP_COMPRESS        Enable FIP image compression (XZ): 0 | 1  (default: 0)
                      Compresses BL31, BL33 inside FIP to reduce file size

Options:
  --clean, -c         Distclean all source directories and exit
  --help, -h          Show this help message and exit
EOF
	exit 0
}

case "${1:-}" in
	--help|-h) print_help ;;
	--clean|-c) clean_mode=1 ;;
esac

if [ "$VERSION" = "2025" ]; then
    UBOOT_DIR=$UBOOT25
    ATF_DIR=$ATF25
elif [ "$VERSION" = "SP1" ] || [ "$VERSION" = "sp1" ]; then
	VERSION="SP1"
    UBOOT_DIR=$UBOOT25
    ATF_DIR=$ATF24
elif [ "$VERSION" = "SP2" ] || [ "$VERSION" = "sp2" ]; then
	VERSION="SP2"
    UBOOT_DIR=$UBOOT25
    ATF_DIR=$ATF26
else
	error "Unsupported VERSION. Please specify VERSION=2025/SP1/SP2."
    exit 1
fi

if [ "$clean_mode" = "1" ]; then
	for dir in "$UBOOT_DIR" "$ATF24" "$ATF25" "$ATF26"; do
		if [ -d "$dir" ]; then
			info "Cleaning $dir"
			(
				cd "$dir" && make distclean
			)
		else
			warn "$dir does not exist."
		fi
	done

	info "Clean done."
    exit 0
fi

if [ -z "$BOARD" ]; then
	error "BOARD is required. Run '$0 --help' for usage information."
	exit 1
fi

# Config Dir
CONFIGS_DIR_DEFAULT="configs"
CONFIGS_DIR_FIT="configs-fit"
CONFIGS_DIR_UBI="configs-ubi"
CONFIGS_DIR_OPENWRT="configs-openwrt"
CONFIGS_DIR_NONMBM="configs-nonmbm"

detect_soc() {
	matched=""
	for dir in "$UBOOT_DIR/$CONFIGS_DIR_DEFAULT" "$UBOOT_DIR/$CONFIGS_DIR_FIT" "$UBOOT_DIR/$CONFIGS_DIR_UBI" "$UBOOT_DIR/$CONFIGS_DIR_NONMBM" "$UBOOT_DIR/$CONFIGS_DIR_OPENWRT"; do
			[ -d "$dir" ] || continue
		for file in "$dir"/*_"$BOARD"_defconfig "$dir"/*_"$BOARD"_multi_layout_defconfig; do
					[ -f "$file" ] || continue
					base=$(basename "$file")
			soc=${base%%_"$BOARD"_defconfig}
					if [ "$base" = "$soc" ]; then
				soc=${base%%_"$BOARD"_multi_layout_defconfig}
					fi
					matched="$matched $soc"
				done
			done

	unique=""
	for s in $matched; do
		case " $unique " in
			*" $s "*) ;;
			*) unique="$unique $s" ;;
		esac
	done

	set -- $unique
	count=$#
	if [ "$count" -eq 1 ]; then
		echo "$1"
		return 0
	fi
	if [ "$count" -gt 1 ]; then
		echo "$unique"
		return 2
	fi
	return 1
}

if [ -z "$SOC" ]; then
	SOC_DETECTED=$(detect_soc)
	status=$?
	if [ "$status" -eq 0 ]; then
		SOC="$SOC_DETECTED"
		info "Auto-detected SOC: $SOC"
	elif [ "$status" -eq 2 ]; then
		error "Multiple SOC matches for BOARD=$BOARD:$SOC_DETECTED"
		error "Please set SOC manually."
		exit 1
	else
		error "Unable to auto-detect SOC for BOARD=$BOARD"
		error "Please set SOC manually."
		exit 1
	fi
fi

step "Checking environment..."

info "Trying npm..."
command -v npm
[ "$?" != "0" ] && { error "npm is not installed on this system."; exit 0; }

ensure_failsafe_js_deps() {
	failsafe_dir="$UBOOT_DIR/failsafe"
	embed_dir="$failsafe_dir/embedded"
	package_json="$embed_dir/package.json"
	marker="$embed_dir/.npm-install-done"

	if [ ! -f "$package_json" ]; then
		info "Skipping failsafe JS dependency setup: $package_json not found."
		return 0
	fi

	if [ -f "$marker" ] && [ -d "$embed_dir/node_modules/uglify-js" ]; then
		info "Failsafe JS build dependencies already installed."
		return 0
	fi

	command -v npm >/dev/null 2>&1 || { error "npm is not installed on this system."; exit 1; }
	info "Installing failsafe JS build dependencies..."
	(cd "$embed_dir" && npm install --no-audit --no-fund) || exit 1
	touch "$marker"
	info "Failsafe JS build dependencies installed."
}

info "npm found, checking failsafe JS dependencies..."
ensure_failsafe_js_deps

info "Trying python3..."
command -v python3
[ "$?" != "0" ] && { error "Python3 is not installed on this system."; exit 0; }

if [ -z "$TOOLCHAIN" ]; then
	if [ "$SOC" = "mt7629" ]; then
		TOOLCHAIN=$TOOLCHAIN_ARM
	else
		TOOLCHAIN=$TOOLCHAIN_AARCH64
	fi
	info "Using toolchain $TOOLCHAIN for SOC $SOC"
fi

info "Trying cross compiler..."
command -v "${TOOLCHAIN}gcc"
[ "$?" != "0" ] && { error "${TOOLCHAIN}gcc not found!"; exit 0; }
export CROSS_COMPILE="$TOOLCHAIN"

ATF_CFG_SOURCE="${SOC}_${BOARD}_defconfig"
UBOOT_CFG_SOURCE="${SOC}_${BOARD}_defconfig"
UBOOT_CFG_MULTILAYOUT_SOURCE="${SOC}_${BOARD}_multi_layout_defconfig"

# Backup the configuration files in sources
ATF_CFG="${ATF_CFG:-$ATF_CFG_SOURCE}"
UBOOT_CFG="${UBOOT_CFG:-$UBOOT_CFG_SOURCE}"
UBOOT_CFG_MULTILAYOUT="${UBOOT_CFG_MULTILAYOUT:-$UBOOT_CFG_MULTILAYOUT_SOURCE}"

# ATF Config Path
ATF_CFG_PATH_DEFAULT="$ATF_DIR/$CONFIGS_DIR_DEFAULT/$ATF_CFG"
ATF_CFG_PATH_FIT="$ATF_DIR/$CONFIGS_DIR_FIT/$ATF_CFG"
ATF_CFG_PATH_UBI="$ATF_DIR/$CONFIGS_DIR_UBI/$ATF_CFG"
ATF_CFG_PATH_OPENWRT="$ATF_DIR/$CONFIGS_DIR_OPENWRT/$ATF_CFG"
ATF_CFG_PATH_NONMBM="$ATF_DIR/$CONFIGS_DIR_NONMBM/$ATF_CFG"

# U-Boot Config Path
UBOOT_CFG_PATH_DEFAULT="$UBOOT_DIR/$CONFIGS_DIR_DEFAULT/$UBOOT_CFG"
UBOOT_CFG_PATH_MULTILAYOUT="$UBOOT_DIR/$CONFIGS_DIR_DEFAULT/$UBOOT_CFG_MULTILAYOUT"
UBOOT_CFG_PATH_FIT="$UBOOT_DIR/$CONFIGS_DIR_FIT/$UBOOT_CFG"
UBOOT_CFG_PATH_UBI="$UBOOT_DIR/$CONFIGS_DIR_UBI/$UBOOT_CFG"
UBOOT_CFG_PATH_OPENWRT="$UBOOT_DIR/$CONFIGS_DIR_OPENWRT/$UBOOT_CFG"
UBOOT_CFG_PATH_NONMBM="$UBOOT_DIR/$CONFIGS_DIR_NONMBM/$UBOOT_CFG"
UBOOT_CFG_PATH_NONMBM_MULTILAYOUT="$UBOOT_DIR/$CONFIGS_DIR_NONMBM/$UBOOT_CFG_MULTILAYOUT"

if [ "$VARIANT" = "default" ] || [ "$VARIANT" = "DEFAULT" ]; then
	ATF_CFG_PATH=$ATF_CFG_PATH_DEFAULT
	UBOOT_CFG_PATH=$UBOOT_CFG_PATH_DEFAULT
	if [ "$multilayout" = "1" ]; then
		UBOOT_CFG_PATH=$UBOOT_CFG_PATH_MULTILAYOUT
	fi
	if [ "$multilayout" = "1" ] && [ ! -f "$UBOOT_CFG_PATH" ]; then
		warn "Multi layout config not found, will fallback to single-layout.(Y/n):"
		if [ "$SILENT" != "Y" ]; then
			read answer
		fi
		if [ "$answer" = "y" ] || [ "$answer" = "Y" ] || [ "$SILENT" = "Y" ]; then
			multilayout=0
			UBOOT_CFG_PATH=$UBOOT_CFG_PATH_DEFAULT
		else
			info "Canceled."
		fi
	fi
elif [ "$VARIANT" = "ubootmod" ] || [ "$VARIANT" = "UBOOTMOD" ]; then
	fixedparts=0
	ATF_CFG_PATH=$ATF_CFG_PATH_DEFAULT
	UBOOT_CFG_PATH=$UBOOT_CFG_PATH_FIT
	if [ "$multilayout" = "1" ]; then
		warn "No multi layout with ubootmod variant, will disabled it.(Y/n):"
		if [ "$SILENT" != "Y" ]; then
			read answer
		fi
		if [ "$answer" = "y" ] || [ "$answer" = "Y" ] || [ "$SILENT" = "Y" ]; then
			multilayout=0
		else
			info "Canceled."
		fi
	fi
elif [ "$VARIANT" = "ubi" ] || [ "$VARIANT" = "UBI" ]; then
	fixedparts=0
	ATF_CFG_PATH=$ATF_CFG_PATH_UBI
	UBOOT_CFG_PATH=$UBOOT_CFG_PATH_UBI
	if [ "$multilayout" = "1" ]; then
		warn "No multi layout with ubi variant, will disabled it.(Y/n):"
		if [ "$SILENT" != "Y" ]; then
			read answer
		fi
		if [ "$answer" = "y" ] || [ "$answer" = "Y" ] || [ "$SILENT" = "Y" ]; then
			multilayout=0
		else
			info "Canceled."
		fi
	fi
elif [ "$VARIANT" = "openwrt" ] || [ "$VARIANT" = "OPENWRT" ]; then
	fixedparts=0
	ATF_CFG_PATH=$ATF_CFG_PATH_DEFAULT
	UBOOT_CFG_PATH=$UBOOT_CFG_PATH_OPENWRT
	if [ "$multilayout" = "1" ]; then
		warn "No multi layout with openwrt variant, will disabled it.(Y/n):"
		if [ "$SILENT" != "Y" ]; then
			read answer
		fi
		if [ "$answer" = "y" ] || [ "$answer" = "Y" ] || [ "$SILENT" = "Y" ]; then
			multilayout=0
		else
			info "Canceled."
		fi
	fi
elif [ "$VARIANT" = "nonmbm" ] || [ "$VARIANT" = "NONMBM" ]; then
	ATF_CFG_PATH=$ATF_CFG_PATH_NONMBM
	UBOOT_CFG_PATH=$UBOOT_CFG_PATH_NONMBM
	if [ "$multilayout" = "1" ]; then
		UBOOT_CFG_PATH=$UBOOT_CFG_PATH_NONMBM_MULTILAYOUT
	fi
	if [ "$multilayout" = "1" ] && [ ! -f "$UBOOT_CFG_PATH" ]; then
		warn "Multi layout config not found, fallback to single-layout.(Y/n):"
		if [ "$SILENT" != "Y" ]; then
			read answer
		fi
		if [ "$answer" = "y" ] || [ "$answer" = "Y" ] || [ "$SILENT" = "Y" ]; then
			multilayout=0
			UBOOT_CFG_PATH=$UBOOT_CFG_PATH_NONMBM
		else
			info "Canceled."
		fi
	fi
else
    error "Unsupported VARIANT. Please specify VARIANT=default/ubootmod/ubi/nonmbm/openwrt."
    exit 1
fi

# No fixed-mtdparts or multilayout for EMMC
if grep -Eq "CONFIG_FLASH_DEVICE_EMMC=y|_BOOT_DEVICE_EMMC=y" "$ATF_CFG_PATH" ; then
	fixedparts=0
	multilayout=0
fi

if [ "$fixedparts" = "0" ] && [ "$multilayout" = "1" ]; then
	error "Multi layout is not compatible with fixed-mtdparts disabled build. Please disable multi layout or enable fixed-mtdparts."
	exit 1
fi

for file in "$ATF_CFG_PATH" "$UBOOT_CFG_PATH"; do
	if [ ! -f "$file" ]; then
		error "$file not found!"
		exit 1
	fi
done

step "Configuration:"

info "VERSION: $VERSION"
info "VARIANT: $VARIANT"
info "TARGET: ${SOC}_${BOARD}"
info "ATF Dir: $ATF_DIR"
info "U-Boot Dir: $UBOOT_DIR"
info "ATF CFG: $ATF_CFG_PATH"
info "U-Boot CFG: $UBOOT_CFG_PATH"
info "Features: fixed-mtdparts: $fixedparts, multi-layout: $multilayout"
info "Failsafe theme: $FSTHEME"
info "Failsafe functions: SIMG support: $simg, UBI Management support: $UBIMNG"
info "Telnetd support: $TELNETD, NAND RAW R/W support: $NAND_RAW"
info "COPY BL2: $COPY_BL2"
info "FIP Compression: $FIP_COMPRESS"

step "Build u-boot..."

rm -f "$UBOOT_DIR/u-boot.bin"
cp -f "$UBOOT_CFG_PATH" "$UBOOT_DIR/.config"
if [ "$fixedparts" = "1" ]; then
	info "Build u-boot with fixed-mtdparts!"
	echo "CONFIG_MEDIATEK_UBI_FIXED_MTDPARTS=y" >> "$UBOOT_DIR/.config"
	echo "CONFIG_MTK_FIXED_MTD_MTDPARTS=y" >> "$UBOOT_DIR/.config"
fi
if [ -n "$VARIANT" ]; then
	info "Build u-boot with variant: $VARIANT"
	echo "CONFIG_WEBUI_FAILSAFE_BUILD_VARIANT=\"$(echo "$VARIANT" | tr '[:upper:]' '[:lower:]')\"" >> "$UBOOT_DIR/.config"
fi
if [ "$FSTHEME" = "bootstrap" ] || [ "$FSTHEME" = "Bootstrap" ]; then
	info "Build u-boot with bootstrap fstheme!"
fi
if [ "$FSTHEME" = "gl" ] || [ "$FSTHEME" = "GL" ]; then
	info "Build u-boot with gl fstheme!"
	echo "CONFIG_WEBUI_FAILSAFE_UI_GL=y" >> "$UBOOT_DIR/.config"
fi
if [ "$FSTHEME" = "mtk" ] || [ "$FSTHEME" = "MTK" ]; then
	info "Build u-boot with mtk fstheme!"
	echo "CONFIG_WEBUI_FAILSAFE_UI_MTK=y" >> "$UBOOT_DIR/.config"
fi
if [ "$simg" = "1" ]; then
	info "Build u-boot with failsafe simg support!"
	echo "CONFIG_WEBUI_FAILSAFE_SIMG=y" >> "$UBOOT_DIR/.config"
fi
if [ "$UBIMNG" = "1" ]; then
	info "Build u-boot with failsafe UBI management support!"
	echo "CONFIG_WEBUI_FAILSAFE_UBI=y" >> "$UBOOT_DIR/.config"
fi
if [ "$TELNETD" = "1" ]; then
	info "Build u-boot with telnetd support!"
	echo "CONFIG_MTK_TELNETD=y" >> "$UBOOT_DIR/.config"
fi
if [ "$NAND_RAW" = "1" ]; then
	info "Build u-boot with NAND raw OOB backup support!"
	echo "CONFIG_WEBUI_FAILSAFE_NAND_RAW=y" >> "$UBOOT_DIR/.config"
fi

make -C "$UBOOT_DIR" olddefconfig
make -C "$UBOOT_DIR" clean
make -C "$UBOOT_DIR" -j $(nproc) all
if [ -f "$UBOOT_DIR/u-boot.bin" ]; then
	cp -f "$UBOOT_DIR/u-boot.bin" "$ATF_DIR/u-boot.bin"
	info "u-boot build done!"
else
	error "u-boot build fail!"
	exit 1
fi

step "Build atf..."

if [ -e "$ATF_DIR/makefile" ]; then
	ATF_MKFILE="makefile"
else
	ATF_MKFILE="Makefile"
fi

ATF_CFG_TARGET="$ATF_CFG"
ATF_CFG_STAGE_FILE=""
if [ "$ATF_CFG_PATH" != "$ATF_CFG_PATH_DEFAULT" ]; then
	ATF_CFG_TARGET="__variant_${SOC}_${BOARD}_defconfig"
	ATF_CFG_STAGE_FILE="$ATF_DIR/$CONFIGS_DIR_DEFAULT/$ATF_CFG_TARGET"
	cp -f "$ATF_CFG_PATH" "$ATF_CFG_STAGE_FILE"
	info "Staged ATF config: $ATF_CFG_PATH -> $ATF_CFG_STAGE_FILE"
fi

make -C "$ATF_DIR" -f "$ATF_MKFILE" clean CONFIG_CROSS_COMPILER="$TOOLCHAIN" CROSS_COMPILER="$TOOLCHAIN"
rm -rf "$ATF_DIR/build"
make -C "$ATF_DIR" -f "$ATF_MKFILE" "$ATF_CFG_TARGET" CONFIG_CROSS_COMPILER="$TOOLCHAIN" CROSS_COMPILER="$TOOLCHAIN"
if [ "$FIP_COMPRESS" = "1" ]; then
	info "Enable FIP compression (XZ)..."
	sed -i 's/# _ENABLE_FIP_COMPRESS is not set/_ENABLE_FIP_COMPRESS=y/' "$ATF_DIR/build/.config"
	printf 'FIP_COMPRESS=1\n' >> "$ATF_DIR/build/.config"
fi
make -C "$ATF_DIR" -f "$ATF_MKFILE" all CONFIG_CROSS_COMPILER="$TOOLCHAIN" CROSS_COMPILER="$TOOLCHAIN" CONFIG_BL33="../$UBOOT_DIR/u-boot.bin" BL33="../$UBOOT_DIR/u-boot.bin" -j $(nproc)
if [ -n "$ATF_CFG_STAGE_FILE" ] && [ -f "$ATF_CFG_STAGE_FILE" ]; then
	rm -f "$ATF_CFG_STAGE_FILE"
fi

step "Copying output files..."

mkdir -p "output"
if [ -f "$ATF_DIR/build/${SOC}/release/fip.bin" ]; then
	FIP_NAME="fip-${SOC}_${BOARD}_${VERSION}-${AUTHOR}-dhcpd"
	if [ "$VARIANT" = "ubootmod" ] || [ "$VARIANT" = "UBOOTMOD" ]; then
		FIP_NAME="${FIP_NAME}-fit"
	fi
	if [ "$VARIANT" = "ubi" ] || [ "$VARIANT" = "UBI" ]; then
		FIP_NAME="${FIP_NAME}-ubi"
	fi
	if [ "$VARIANT" = "openwrt" ] || [ "$VARIANT" = "OPENWRT" ]; then
		FIP_NAME="${FIP_NAME}-openwrt"
	fi
	if [ "$VARIANT" = "nonmbm" ] || [ "$VARIANT" = "NONMBM" ]; then
		FIP_NAME="${FIP_NAME}-nonmbm"
	fi
	if [ "$fixedparts" = "1" ]; then
		FIP_NAME="${FIP_NAME}-fixed-parts"
	fi
	if [ "$multilayout" = "1" ]; then
		FIP_NAME="${FIP_NAME}-multi-layout"
	fi
	if [ "$FIP_COMPRESS" = "1" ]; then
		FIP_NAME="${FIP_NAME}-fipc"
	fi
	FIP_MD5=$(md5sum "$ATF_DIR/build/${SOC}/release/fip.bin" | awk '{print $1}')
	FIP_NAME="${FIP_NAME}_md5-${FIP_MD5}"
	info "fip-${SOC}_${BOARD}_${VERSION}_${VARIANT} build done"
	info "fip.bin md5sum: $FIP_MD5"
	info "fip.bin size: $(stat -c%s "$ATF_DIR/build/${SOC}/release/fip.bin") bytes"
	cp -f "$ATF_DIR/build/${SOC}/release/fip.bin" "output/${FIP_NAME}.bin"
	info "Output: output/${FIP_NAME}.bin"
else
	error "fip build fail!"
	exit 1
fi
if grep -Eq "(^_|CONFIG_TARGET_ALL_NO_SEC_BOOT=y)" "$ATF_CFG_PATH"; then
	if [ -f "$ATF_DIR/build/${SOC}/release/bl2.img" ]; then
		BL2_NAME="bl2-${SOC}_${BOARD}_${VERSION}"
		if [ "$VARIANT" = "ubootmod" ] || [ "$VARIANT" = "UBOOTMOD" ]; then
			BL2_NAME="${BL2_NAME}-fit"
		fi
		if [ "$VARIANT" = "ubi" ] || [ "$VARIANT" = "UBI" ]; then
			BL2_NAME="${BL2_NAME}-ubi"
		fi
		if [ "$VARIANT" = "openwrt" ] || [ "$VARIANT" = "OPENWRT" ]; then
			BL2_NAME="${BL2_NAME}-openwrt"
		fi
		if [ "$VARIANT" = "nonmbm" ] || [ "$VARIANT" = "NONMBM" ]; then
			BL2_NAME="${BL2_NAME}-nonmbm"
		fi
		BL2_MD5=$(md5sum "$ATF_DIR/build/${SOC}/release/bl2.img" | awk '{print $1}')
		BL2_NAME="${BL2_NAME}_md5-${BL2_MD5}"
		info "bl2-${SOC}_${BOARD}_${VERSION}_${VARIANT} build done"
		info "bl2.img md5sum: $BL2_MD5"
		info "bl2.img size: $(stat -c%s "$ATF_DIR/build/${SOC}/release/bl2.img") bytes"
		if [ "$COPY_BL2" = "1" ]; then
			cp -f "$ATF_DIR/build/${SOC}/release/bl2.img" "output/${BL2_NAME}.img"
			info "Output: output/${BL2_NAME}.img"
		else
			info "Skipping bl2 copy because COPY_BL2 is disabled"
			info "You may find the bl2 image at: $ATF_DIR/build/${SOC}/release/bl2.img"
		fi
	else
		error "bl2 build fail!"
		exit 1
	fi
fi
