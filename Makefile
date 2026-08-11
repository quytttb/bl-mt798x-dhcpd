# SPDX-License-Identifier: GPL-2.0
#
# Copyright (C) 2026 Yuzhii0718
#
# All rights reserved.
#
# This file is part of the project bl-mt798x-dhcpd
# You may not use, copy, modify or distribute this file except in compliance with the license agreement.
#
# Quick Build Scripts
#

.DEFAULT_GOAL := build
SHELL := /bin/bash

CONFIG_FILE ?= .config
-include $(CONFIG_FILE)

MENUCONFIG_UBOOT_DIR ?= uboot-mtk-20250711

strip_quotes = $(subst ",,$(1))
config_bool = $(shell if grep -q '^$(1)=y$$' '$(CONFIG_FILE)' 2>/dev/null; then echo y; elif grep -q '^# $(1) is not set$$' '$(CONFIG_FILE)' 2>/dev/null; then echo n; else echo $(2); fi)

ifeq ($(origin VERSION), undefined)
  ifeq ($(strip $(CONFIG_VERSION_SP1)),y)
    VERSION := SP1
  else ifeq ($(strip $(CONFIG_VERSION_SP2)),y)
    VERSION := SP2
  else
    VERSION := 2025
  endif
endif

ifeq ($(origin VARIANT), undefined)
  ifeq ($(strip $(CONFIG_VARIANT_UBOOTMOD)),y)
    VARIANT := ubootmod
  else ifeq ($(strip $(CONFIG_VARIANT_UBI)),y)
	VARIANT := ubi
  else ifeq ($(strip $(CONFIG_VARIANT_NONMBM)),y)
    VARIANT := nonmbm
  else ifeq ($(strip $(CONFIG_VARIANT_OPENWRT)),y)
    VARIANT := openwrt
  else
    VARIANT := default
  endif
endif

ifeq ($(origin FSTHEME), undefined)
  ifeq ($(strip $(CONFIG_FSTHEME_GL)),y)
    FSTHEME := gl
  else ifeq ($(strip $(CONFIG_FSTHEME_MTK)),y)
    FSTHEME := mtk
  else
    FSTHEME := bootstrap
  endif
endif

ifeq ($(filter 2025,$(VERSION)),2025)
	BOARD_ATF_CFG_DIR := atf-20250711
	BOARD_UBOOT_CFG_DIR := uboot-mtk-20250711
else ifneq ($(filter SP1 sp1,$(VERSION)),)
	BOARD_ATF_CFG_DIR := atf-20240117-bacca82a8
	BOARD_UBOOT_CFG_DIR := uboot-mtk-20250711
else ifneq ($(filter SP2 sp2,$(VERSION)),)
	BOARD_ATF_CFG_DIR := atf-20260123
	BOARD_UBOOT_CFG_DIR := uboot-mtk-20250711
endif

# Collect selected boards from .config (BOARD_*=y)
# Deferred to recipe time via EVAL_BOARDS to avoid recursive $(MAKE) at parse time.
# If BUILD_ALL_BOARDS=y, discover all boards from configs directories.
# Otherwise, collect individual BOARD_*=y selections and also expand
# platform-wide BUILD_ALL_MT7981/7986/7987/7988 selections.
ifeq ($(origin BOARDS), undefined)
  ifeq ($(origin BOARD),command line)
    # BOARD passed on command line takes priority
    BOARDS := $(BOARD)
    EVAL_BOARDS := 0
  else ifeq ($(call config_bool,CONFIG_BUILD_ALL_BOARDS,n),y)
    EVAL_BOARDS := 1
  else
    EVAL_BOARDS := 1
		BOARDS := $(shell if [ -f '$(CONFIG_FILE)' ] && [ -n '$(BOARD_ATF_CFG_DIR)' ] && [ -n '$(BOARD_UBOOT_CFG_DIR)' ]; then if grep -q '^CONFIG_VARIANT_UBI=y$$' '$(CONFIG_FILE)'; then atf_d='$(BOARD_ATF_CFG_DIR)/configs-ubi'; uboot_d='$(BOARD_UBOOT_CFG_DIR)/configs-ubi'; elif grep -q '^CONFIG_VARIANT_UBOOTMOD=y$$' '$(CONFIG_FILE)'; then atf_d='$(BOARD_ATF_CFG_DIR)/configs-ubi'; uboot_d='$(BOARD_UBOOT_CFG_DIR)/configs-fit'; elif grep -q '^CONFIG_VARIANT_NONMBM=y$$' '$(CONFIG_FILE)'; then atf_d='$(BOARD_ATF_CFG_DIR)/configs-nonmbm'; uboot_d='$(BOARD_UBOOT_CFG_DIR)/configs-nonmbm'; elif grep -q '^CONFIG_VARIANT_OPENWRT=y$$' '$(CONFIG_FILE)'; then atf_d='$(BOARD_ATF_CFG_DIR)/configs'; uboot_d='$(BOARD_UBOOT_CFG_DIR)/configs-openwrt'; else atf_d='$(BOARD_ATF_CFG_DIR)/configs'; uboot_d='$(BOARD_UBOOT_CFG_DIR)/configs'; fi; atf_list=$$(mktemp); uboot_list=$$(mktemp); if [ -d "$$atf_d" ] && [ -d "$$uboot_d" ]; then find -L "$$atf_d" -maxdepth 1 -type f -name '*_defconfig' -printf '%f\n' | sed 's/_defconfig$$//' | sort -u > "$$atf_list"; find -L "$$uboot_d" -maxdepth 1 -type f -name '*_defconfig' -printf '%f\n' | sed 's/_defconfig$$//' | sort -u > "$$uboot_list"; else find -L '$(BOARD_ATF_CFG_DIR)/configs' -maxdepth 1 -type f -name '*_defconfig' -printf '%f\n' | sed 's/_defconfig$$//' | sort -u > "$$atf_list"; find -L '$(BOARD_UBOOT_CFG_DIR)/configs' -maxdepth 1 -type f -name '*_defconfig' -printf '%f\n' | sed 's/_defconfig$$//' | sort -u > "$$uboot_list"; fi; all_cfgs=$$(comm -12 "$$atf_list" "$$uboot_list"); rm -f "$$atf_list" "$$uboot_list"; selected_boards=$$(grep '^CONFIG_BOARD_[A-Z0-9_]*=y$$' '$(CONFIG_FILE)' | sed 's/^CONFIG_BOARD_//; s/=y$$//' | tr '[:upper:]' '[:lower:]'); selected_platforms=$$(grep '^CONFIG_BUILD_ALL_[A-Z0-9_]*=y$$' '$(CONFIG_FILE)' | sed 's/^CONFIG_BUILD_ALL_//; s/=y$$//' | tr '[:upper:]' '[:lower:]'); for cfg in $$all_cfgs; do cfg_norm=$$(printf '%s\n' "$$cfg" | tr '-' '_'); include=0; for sel in $$selected_boards; do [ "$$sel" = "$$cfg_norm" ] && include=1 && break; done; if [ "$$include" -eq 0 ]; then for plat in $$selected_platforms; do prefix="$${plat}_"; if [ "$$cfg_norm" != "$${cfg_norm#$$prefix}" ]; then include=1; break; fi; done; fi; [ "$$include" -eq 1 ] && printf '%s\n' "$$cfg" | sed 's/^[^_]*_//'; done | sort -u; fi)
  endif
else
  EVAL_BOARDS := 0
endif
# For backward compatibility, set BOARD to the first selected board
BOARD := $(firstword $(BOARDS))
ifeq ($(origin ATFCFG_DIR), undefined)
  ATFCFG_DIR := $(if $(strip $(CONFIG_ATFCFG_DIR)),$(call strip_quotes,$(CONFIG_ATFCFG_DIR)),mt798x_atf)
endif
ifeq ($(origin CFG_SUBDIR), undefined)
  CFG_SUBDIR := $(call strip_quotes,$(CONFIG_CFG_SUBDIR))
endif
ifeq ($(origin OUTPUT_DIR), undefined)
  OUTPUT_DIR := $(if $(strip $(CONFIG_OUTPUT_DIR)),$(call strip_quotes,$(CONFIG_OUTPUT_DIR)),output_bl2)
endif
ifeq ($(origin ATF_DIR), undefined)
  ATF_DIR := $(call strip_quotes,$(CONFIG_ATF_DIR))
endif
ifeq ($(origin UBOOT_DIR), undefined)
  UBOOT_DIR := $(call strip_quotes,$(CONFIG_UBOOT_DIR))
endif
ifeq ($(origin TOOLCHAIN), undefined)
  TOOLCHAIN := $(call strip_quotes,$(CONFIG_TOOLCHAIN))
endif

ifeq ($(origin MULTI_LAYOUT), undefined)
  MULTI_LAYOUT := $(if $(filter y,$(call config_bool,CONFIG_MULTI_LAYOUT,y)),1,0)
endif
ifeq ($(origin FIXED_MTDPARTS), undefined)
  FIXED_MTDPARTS := $(if $(filter y,$(call config_bool,CONFIG_FIXED_MTDPARTS,y)),1,0)
endif
ifeq ($(origin SIMG), undefined)
  SIMG := $(if $(filter y,$(call config_bool,CONFIG_SIMG,n)),1,0)
endif
ifeq ($(origin UBIMNG), undefined)
  UBIMNG := $(if $(filter y,$(call config_bool,CONFIG_UBIMNG,n)),1,0)
endif
ifeq ($(origin TELNETD), undefined)
  TELNETD := $(if $(filter y,$(call config_bool,CONFIG_TELNETD,n)),1,0)
endif
ifeq ($(origin NAND_RAW), undefined)
  NAND_RAW := $(if $(filter y,$(call config_bool,CONFIG_NAND_RAW,n)),1,0)
endif
ifeq ($(origin COPY_BL2), undefined)
  COPY_BL2 := $(if $(filter y,$(call config_bool,CONFIG_COPY_BL2,y)),1,0)
endif
ifeq ($(origin FIP_COMPRESS), undefined)
  FIP_COMPRESS := $(if $(filter y,$(call config_bool,CONFIG_COMPRESS_FIP,n)),1,0)
endif
ifeq ($(origin SILENT), undefined)
  SILENT := $(if $(filter y,$(call config_bool,CONFIG_SILENT,y)),Y,N)
endif
ifeq ($(origin BUILD_FIP), undefined)
	BUILD_FIP := $(call config_bool,CONFIG_BUILD_FIP,y)
endif
ifeq ($(origin BUILD_ATF), undefined)
	BUILD_ATF := $(call config_bool,CONFIG_BUILD_ATF,n)
endif
ifeq ($(origin BUILD_GPT), undefined)
	BUILD_GPT := $(call config_bool,CONFIG_BUILD_GPT,n)
endif
ifeq ($(origin SHOW), undefined)
  SHOW := $(if $(filter y,$(call config_bool,CONFIG_SHOW,n)),1,0)
endif
ifeq ($(origin DRAW), undefined)
  ifeq ($(call config_bool,CONFIG_GPT_DRAW,n),y)
    DRAW := $(if $(call config_bool,CONFIG_GPT_DRAW_NOTITLE,n),notitle,1)
  else
    DRAW := 0
  endif
endif
ifeq ($(origin SDMMC), undefined)
  SDMMC := $(if $(filter y,$(call config_bool,CONFIG_SDMMC,n)),1,0)
endif
ifeq ($(origin BUILD_LOG), undefined)
  BUILD_LOG := $(call config_bool,CONFIG_BUILD_LOG,n)
endif

.PHONY: all build boards board-configs gen-board-kconfig menuconfig defconfig atf gpt clean distclean help

build:
	@set -euo pipefail; \
	is_enabled() { case "$$1" in y|Y|1|yes|YES|true|TRUE) return 0 ;; *) return 1 ;; esac; }; \
	run_fip=0; run_atf=0; run_gpt=0; \
	if is_enabled "$(BUILD_FIP)"; then run_fip=1; fi; \
	if is_enabled "$(BUILD_ATF)"; then run_atf=1; fi; \
	if is_enabled "$(BUILD_GPT)"; then run_gpt=1; fi; \
	if [[ "$$run_fip" -eq 0 && "$$run_atf" -eq 0 && "$$run_gpt" -eq 0 ]]; then \
		echo "Error: no build action enabled. Use make menuconfig and enable BUILD_FIP/BUILD_ATF/BUILD_GPT." >&2; \
		exit 1; \
	fi; \
	if [[ "$$run_fip" -eq 1 ]]; then \
		boards="$(BOARDS)"; \
		if [[ "$(EVAL_BOARDS)" == "1" && -z "$$boards" ]]; then \
			case "$(VERSION)" in \
				2025) ATF_DIR_TMP="atf-20250711"; UBOOT_DIR_TMP="uboot-mtk-20250711" ;; \
				SP1|sp1) ATF_DIR_TMP="atf-20240117-bacca82a8"; UBOOT_DIR_TMP="uboot-mtk-20250711" ;; \
				SP2|sp2) ATF_DIR_TMP="atf-20260123"; UBOOT_DIR_TMP="uboot-mtk-20250711" ;; \
				*) echo "Error: unsupported VERSION='$(VERSION)'." >&2; exit 1 ;; \
			esac; \
			var_lower=$$(echo "$(VARIANT)" | tr '[:upper:]' '[:lower:]'); \
			case "$$var_lower" in \
				ubootmod) \
					atf_cfg="$$ATF_DIR_TMP/configs"; uboot_cfg="$$UBOOT_DIR_TMP/configs-fit" ;; \
				ubi) \
					atf_cfg="$$ATF_DIR_TMP/configs-ubi"; uboot_cfg="$$UBOOT_DIR_TMP/configs-ubi" ;; \
				nonmbm) \
					atf_cfg="$$ATF_DIR_TMP/configs-nonmbm"; uboot_cfg="$$UBOOT_DIR_TMP/configs-nonmbm" ;; \
				openwrt) \
					atf_cfg="$$ATF_DIR_TMP/configs"; uboot_cfg="$$UBOOT_DIR_TMP/configs-openwrt" ;; \
				*) \
					atf_cfg="$$ATF_DIR_TMP/configs"; uboot_cfg="$$UBOOT_DIR_TMP/configs" ;; \
			esac; \
			atf_list=$$(mktemp); uboot_list=$$(mktemp); \
			trap 'rm -f "$$atf_list" "$$uboot_list"' EXIT; \
			if [[ -d "$$atf_cfg" && -d "$$uboot_cfg" ]]; then \
				find -L "$$atf_cfg" -maxdepth 1 -type f -name '*_defconfig' -printf '%f\n' | sed 's/_defconfig$$//' | sort -u > "$$atf_list"; \
				find -L "$$uboot_cfg" -maxdepth 1 -type f -name '*_defconfig' -printf '%f\n' | sed 's/_defconfig$$//' | sort -u > "$$uboot_list"; \
				boards=$$(comm -12 "$$atf_list" "$$uboot_list" | sed 's/^[^_]*_//'); \
			else \
				echo "Warning: variant-specific config directory missing for VARIANT=$(VARIANT), falling back to default." >&2; \
				find -L "$$ATF_DIR_TMP/configs" -maxdepth 1 -type f -name '*_defconfig' -printf '%f\n' | sed 's/_defconfig$$//' | sort -u > "$$atf_list"; \
				find -L "$$UBOOT_DIR_TMP/configs" -maxdepth 1 -type f -name '*_defconfig' -printf '%f\n' | sed 's/_defconfig$$//' | sort -u > "$$uboot_list"; \
				boards=$$(comm -12 "$$atf_list" "$$uboot_list" | sed 's/^[^_]*_//'); \
			fi; \
			if ! grep -q '^CONFIG_BUILD_ALL_BOARDS=y$$' '$(CONFIG_FILE)'; then \
				sel_board=$$(grep '^CONFIG_BOARD_[A-Z0-9_]*=y$$' '$(CONFIG_FILE)' | sed 's/^CONFIG_BOARD_//; s/=y$$//' | tr '[:upper:]' '[:lower:]'); \
				sel_plat=$$(grep '^CONFIG_BUILD_ALL_[A-Z0-9_]*=y$$' '$(CONFIG_FILE)' | sed 's/^CONFIG_BUILD_ALL_//; s/=y$$//' | tr '[:upper:]' '[:lower:]'); \
				if [ -n "$$sel_board" ] || [ -n "$$sel_plat" ]; then \
					newb=""; \
					for b in $$boards; do b_n=$$(printf '%s\n' "$$b" | tr '-' '_'); ok=0; \
						for s in $$sel_board; do [ "$$s" = "$$b_n" ] && ok=1 && break; done; \
						if [ "$$ok" -eq 0 ]; then for p in $$sel_plat; do [ "$$b_n" != "$${b_n#$${p}_}" ] && ok=1 && break; done; fi; \
						[ "$$ok" -eq 1 ] && newb="$$newb $$b"; \
					done; \
					boards="$$newb"; \
				fi; \
			fi; \
		fi; \
		if [[ -z "$$boards" ]]; then \
			echo "Error: no BOARD selected. Run 'make menuconfig' and select boards, or pass BOARD=<board>." >&2; \
			exit 1; \
		fi; \
		build_one_board() { \
			local board="$$1"; \
			mkdir -p output; \
			echo "----------------------------------------------------------------------"; \
			echo "Building BOARD=$$board (VERSION=$(VERSION), VARIANT=$(VARIANT))"; \
			if [[ "$(BUILD_LOG)" =~ ^[yY1] ]]; then \
				local log_file="output/build-$${board}-$(VERSION)-$(VARIANT).log"; \
				echo "Log: $$log_file"; \
			env -u MAKEFLAGS -u MAKELEVEL -u MFLAGS \
			BOARD="$$board" VERSION="$(VERSION)" VARIANT="$(VARIANT)" FSTHEME="$(FSTHEME)" \
			MULTI_LAYOUT="$(MULTI_LAYOUT)" FIXED_MTDPARTS="$(FIXED_MTDPARTS)" SIMG="$(SIMG)" \
			UBIMNG="$(UBIMNG)" TELNETD="$(TELNETD)" NAND_RAW="$(NAND_RAW)" COPY_BL2="$(COPY_BL2)" FIP_COMPRESS="$(FIP_COMPRESS)" SILENT="$(SILENT)" ./build.sh 2>&1 | tee "$$log_file"; \
		else \
			env -u MAKEFLAGS -u MAKELEVEL -u MFLAGS \
			BOARD="$$board" VERSION="$(VERSION)" VARIANT="$(VARIANT)" FSTHEME="$(FSTHEME)" \
			MULTI_LAYOUT="$(MULTI_LAYOUT)" FIXED_MTDPARTS="$(FIXED_MTDPARTS)" SIMG="$(SIMG)" \
			UBIMNG="$(UBIMNG)" TELNETD="$(TELNETD)" NAND_RAW="$(NAND_RAW)" COPY_BL2="$(COPY_BL2)" FIP_COMPRESS="$(FIP_COMPRESS)" SILENT="$(SILENT)" ./build.sh; \
		fi; \
	}; \
	success_count=0; \
	fail_count=0; \
	total_count=0; \
		for board in $$boards; do \
			total_count=$$((total_count + 1)); \
		done; \
		index=0; \
		for board in $$boards; do \
			index=$$((index + 1)); \
			echo "[$$index/$$total_count] $$board"; \
			if build_one_board "$$board"; then \
				success_count=$$((success_count + 1)); \
			else \
				fail_count=$$((fail_count + 1)); \
				echo "Build failed for BOARD=$$board, continuing..." >&2; \
			fi; \
		done; \
		echo "----------------------------------------------------------------------"; \
		echo "Build summary: success=$$success_count, failed=$$fail_count, total=$$total_count"; \
		if [[ "$$fail_count" -gt 0 ]]; then \
			exit 1; \
		fi; \
	fi; \
	if [[ "$$run_atf" -eq 1 ]]; then \
		printf '%s\n' "env -u MAKEFLAGS -u MAKELEVEL -u MFLAGS ATF_DIR=\"$(ATF_DIR)\" VERSION=\"$(VERSION)\" VARIANT=\"$(VARIANT)\" ATFCFG_DIR=\"$(ATFCFG_DIR)\" CFG_SUBDIR=\"$(CFG_SUBDIR)\" OUTPUT_DIR=\"$(OUTPUT_DIR)\" TOOLCHAIN=\"$(TOOLCHAIN)\" ./compile_atf.sh"; \
		env -u MAKEFLAGS -u MAKELEVEL -u MFLAGS \
		ATF_DIR="$(ATF_DIR)" VERSION="$(VERSION)" VARIANT="$(VARIANT)" \
		ATFCFG_DIR="$(ATFCFG_DIR)" CFG_SUBDIR="$(CFG_SUBDIR)" OUTPUT_DIR="$(OUTPUT_DIR)" \
		TOOLCHAIN="$(TOOLCHAIN)" ./compile_atf.sh; \
	fi; \
	if [[ "$$run_gpt" -eq 1 ]]; then \
		printf '%s\n' "env -u MAKEFLAGS -u MAKELEVEL -u MFLAGS VERSION=\"$(VERSION)\" SHOW=\"$(SHOW)\" DRAW=\"$(DRAW)\" SDMMC=\"$(SDMMC)\" ./generate_gpt.sh"; \
		env -u MAKEFLAGS -u MAKELEVEL -u MFLAGS \
		VERSION="$(VERSION)" SHOW="$(SHOW)" DRAW="$(DRAW)" SDMMC="$(SDMMC)" ./generate_gpt.sh; \
	fi

menuconfig:
	@set -euo pipefail; \
	$(MAKE) --no-print-directory gen-board-kconfig; \
	ln -sf "$(CURDIR)/boards.kconfig" "$(MENUCONFIG_UBOOT_DIR)/boards.kconfig"; \
	echo "Tip: select VARIANT first, then the board list will filter to compatible boards only."; \
	printf '%s\n' "env -u MAKEFLAGS -u MAKELEVEL -u MFLAGS KBUILD_KCONFIG=\"$(CURDIR)/Kconfig\" KCONFIG_CONFIG=\"$(CURDIR)/$(CONFIG_FILE)\" make -C \"$(MENUCONFIG_UBOOT_DIR)\" menuconfig"; \
	env -u MAKEFLAGS -u MAKELEVEL -u MFLAGS \
	KBUILD_KCONFIG="$(CURDIR)/Kconfig" \
	KCONFIG_CONFIG="$(CURDIR)/$(CONFIG_FILE)" \
	$(MAKE) -j1 -C "$(MENUCONFIG_UBOOT_DIR)" menuconfig; \
	rm -f "$(MENUCONFIG_UBOOT_DIR)/boards.kconfig"

defconfig:
	@set -euo pipefail; \
	if [ ! -f "$(MENUCONFIG_UBOOT_DIR)/scripts/kconfig/conf" ]; then \
		echo "Building kconfig conf tool..."; \
		env -u MAKEFLAGS -u MAKELEVEL -u MFLAGS \
		KBUILD_KCONFIG=/dev/null KCONFIG_CONFIG=/dev/null \
		$(MAKE) -j1 -C "$(MENUCONFIG_UBOOT_DIR)" scripts/kconfig/conf; \
	fi; \
	$(MAKE) --no-print-directory gen-board-kconfig; \
	ln -sf "$(CURDIR)/boards.kconfig" "$(MENUCONFIG_UBOOT_DIR)/boards.kconfig"; \
	old_hash=""; \
	if [ -f "$(CURDIR)/$(CONFIG_FILE)" ]; then \
		old_hash=$$(md5sum "$(CURDIR)/$(CONFIG_FILE)" | awk '{print $$1}'); \
		echo "Filling in missing defaults from existing $(CONFIG_FILE)..."; \
		KBUILD_KCONFIG="$(CURDIR)/Kconfig" KCONFIG_CONFIG="$(CURDIR)/$(CONFIG_FILE)" \
			"$(MENUCONFIG_UBOOT_DIR)/scripts/kconfig/conf" --olddefconfig "$(CURDIR)/Kconfig"; \
	else \
		echo "Generating default $(CONFIG_FILE)..."; \
		KBUILD_KCONFIG="$(CURDIR)/Kconfig" KCONFIG_CONFIG="$(CURDIR)/$(CONFIG_FILE)" \
			"$(MENUCONFIG_UBOOT_DIR)/scripts/kconfig/conf" --alldefconfig "$(CURDIR)/Kconfig"; \
	fi; \
	rm -f "$(MENUCONFIG_UBOOT_DIR)/boards.kconfig"; \
	new_hash=$$(md5sum "$(CURDIR)/$(CONFIG_FILE)" | awk '{print $$1}'); \
	if [ "$$old_hash" = "$$new_hash" ]; then \
		echo "$(CONFIG_FILE) unchanged."; \
	else \
		echo "$(CONFIG_FILE) updated."; \
	fi

all:
	@set -euo pipefail; \
	case "$(VERSION)" in \
		2025) ATF_DIR="atf-20250711"; UBOOT_DIR="uboot-mtk-20250711" ;; \
		SP1|sp1) ATF_DIR="atf-20240117-bacca82a8"; UBOOT_DIR="uboot-mtk-20250711" ;; \
		SP2|sp2) ATF_DIR="atf-20260123"; UBOOT_DIR="uboot-mtk-20250711" ;; \
		*) echo "Error: unsupported VERSION='$(VERSION)'." >&2; echo "Supported: 2025/SP1/SP2" >&2; exit 1 ;; \
	esac; \
	collect_board_configs() { \
		local var_lower=$$(echo "$(VARIANT)" | tr '[:upper:]' '[:lower:]'); \
		local atf_cfg_dir="$$ATF_DIR/configs"; \
		local uboot_cfg_dir="$$UBOOT_DIR/configs"; \
		case "$$var_lower" in \
			ubootmod) atf_cfg_dir="$$ATF_DIR/configs-ubi"; uboot_cfg_dir="$$UBOOT_DIR/configs-fit" ;; \
			ubi) atf_cfg_dir="$$ATF_DIR/configs-ubi"; uboot_cfg_dir="$$UBOOT_DIR/configs-ubi" ;; \
			nonmbm) atf_cfg_dir="$$ATF_DIR/configs-nonmbm"; uboot_cfg_dir="$$UBOOT_DIR/configs-nonmbm" ;; \
			openwrt) uboot_cfg_dir="$$UBOOT_DIR/configs-openwrt" ;; \
		esac; \
		if [[ ! -d "$$atf_cfg_dir" || ! -d "$$uboot_cfg_dir" ]]; then \
			echo "Warning: variant-specific config directory missing for VARIANT=$(VARIANT), falling back to default." >&2; \
			atf_cfg_dir="$$ATF_DIR/configs"; \
			uboot_cfg_dir="$$UBOOT_DIR/configs"; \
		fi; \
		if [[ ! -d "$$atf_cfg_dir" || ! -d "$$uboot_cfg_dir" ]]; then \
			echo "Error: both configs directories must exist:" >&2; \
			echo "  $$atf_cfg_dir" >&2; \
			echo "  $$uboot_cfg_dir" >&2; \
			return 1; \
		fi; \
		atf_list="$$(mktemp)"; \
		uboot_list="$$(mktemp)"; \
		trap 'rm -f "$$atf_list" "$$uboot_list"' RETURN; \
		find -L "$$atf_cfg_dir" -maxdepth 1 -type f -name '*_defconfig' -printf '%f\n' | sed 's/_defconfig$$//' | sort -u > "$$atf_list"; \
		find -L "$$uboot_cfg_dir" -maxdepth 1 -type f -name '*_defconfig' -printf '%f\n' | sed 's/_defconfig$$//' | sort -u > "$$uboot_list"; \
		comm -12 "$$atf_list" "$$uboot_list"; \
	}; \
	build_one_board() { \
		local cfg_base="$$1"; \
		local soc="$${cfg_base%%_*}"; \
		local board="$${cfg_base#*_}"; \
		mkdir -p output; \
		echo "----------------------------------------------------------------------"; \
		echo "Building BOARD=$$board (SOC=$$soc, VERSION=$(VERSION), VARIANT=$(VARIANT))"; \
		if [[ "$(BUILD_LOG)" =~ ^[yY1] ]]; then \
			local log_file="output/build-$${board}-$(VERSION)-$(VARIANT).log"; \
			echo "Log: $$log_file"; \
		env -u MAKEFLAGS -u MAKELEVEL -u MFLAGS \
		BOARD="$$board" SOC="$$soc" VERSION="$(VERSION)" VARIANT="$(VARIANT)" FSTHEME="$(FSTHEME)" \
		MULTI_LAYOUT="$(MULTI_LAYOUT)" FIXED_MTDPARTS="$(FIXED_MTDPARTS)" SIMG="$(SIMG)" \
		UBIMNG="$(UBIMNG)" TELNETD="$(TELNETD)" NAND_RAW="$(NAND_RAW)" COPY_BL2="$(COPY_BL2)" FIP_COMPRESS="$(FIP_COMPRESS)" SILENT="$(SILENT)" ./build.sh 2>&1 | tee "$$log_file"; \
	else \
		env -u MAKEFLAGS -u MAKELEVEL -u MFLAGS \
		BOARD="$$board" SOC="$$soc" VERSION="$(VERSION)" VARIANT="$(VARIANT)" FSTHEME="$(FSTHEME)" \
		MULTI_LAYOUT="$(MULTI_LAYOUT)" FIXED_MTDPARTS="$(FIXED_MTDPARTS)" SIMG="$(SIMG)" \
		UBIMNG="$(UBIMNG)" TELNETD="$(TELNETD)" NAND_RAW="$(NAND_RAW)" COPY_BL2="$(COPY_BL2)" FIP_COMPRESS="$(FIP_COMPRESS)" SILENT="$(SILENT)" ./build.sh; \
		fi; \
	}; \
	mapfile -t board_cfgs < <(collect_board_configs); \
	if [[ "$${#board_cfgs[@]}" -eq 0 ]]; then \
		echo "Error: no buildable BOARD found under $$ATF_DIR/configs and $$UBOOT_DIR/configs." >&2; \
		exit 1; \
	fi; \
	success_count=0; \
	fail_count=0; \
	total_count="$${#board_cfgs[@]}"; \
	index=0; \
	for cfg in "$${board_cfgs[@]}"; do \
		index=$$((index + 1)); \
		cfg_board="$${cfg#*_}"; \
		echo "[$$index/$$total_count] $$cfg_board"; \
		if build_one_board "$$cfg"; then \
			success_count=$$((success_count + 1)); \
		else \
			fail_count=$$((fail_count + 1)); \
			echo "Build failed for BOARD=$$cfg_board, continuing..." >&2; \
		fi; \
	done; \
	echo "----------------------------------------------------------------------"; \
	echo "Build summary: success=$$success_count, failed=$$fail_count, total=$$total_count"; \
	if [[ "$$fail_count" -gt 0 ]]; then \
		exit 1; \
	fi

boards:
	@set -euo pipefail; \
	case "$(VERSION)" in \
		2025) ATF_DIR="atf-20250711"; UBOOT_DIR="uboot-mtk-20250711" ;; \
		SP1|sp1) ATF_DIR="atf-20240117-bacca82a8"; UBOOT_DIR="uboot-mtk-20250711" ;; \
		SP2|sp2) ATF_DIR="atf-20260123"; UBOOT_DIR="uboot-mtk-20250711" ;; \
		*) echo "Error: unsupported VERSION='$(VERSION)'." >&2; exit 1 ;; \
	esac; \
	collect_boards_for_variant() { \
		local variant="$$1"; \
		local variant_lower=$$(echo "$$variant" | tr '[:upper:]' '[:lower:]'); \
		local atf_cfg_dir="$$ATF_DIR/configs"; \
		local uboot_cfg_dir="$$UBOOT_DIR/configs"; \
		case "$$variant_lower" in \
			default) ;; \
			ubootmod) atf_cfg_dir="$$ATF_DIR/configs-ubi"; uboot_cfg_dir="$$UBOOT_DIR/configs-fit" ;; \
			ubi) atf_cfg_dir="$$ATF_DIR/configs-ubi"; uboot_cfg_dir="$$UBOOT_DIR/configs-ubi" ;; \
			nonmbm) atf_cfg_dir="$$ATF_DIR/configs-nonmbm"; uboot_cfg_dir="$$UBOOT_DIR/configs-nonmbm" ;; \
			openwrt) uboot_cfg_dir="$$UBOOT_DIR/configs-openwrt" ;; \
			all) atf_cfg_dir="$$ATF_DIR/configs-ubi"; uboot_cfg_dir="$$UBOOT_DIR/configs-fit"; uboot_cfg_dir2="$$UBOOT_DIR/configs-ubi" ;; \
			*) echo "Error: unsupported VARIANT='$$variant'." >&2; return 1 ;; \
		esac; \
		local atf_list="$$(mktemp)"; \
		local uboot_list="$$(mktemp)"; \
		trap 'rm -f "$$atf_list" "$$uboot_list"' RETURN; \
		if [[ "$$variant_lower" = "all" ]]; then \
			for d in "$$ATF_DIR/configs" "$$ATF_DIR/configs-ubi" "$$ATF_DIR/configs-nonmbm"; do \
				[[ -d "$$d" ]] && find -L "$$d" -maxdepth 1 -type f -name '*_defconfig' -printf '%f\n'; \
			done | sort -u > "$$atf_list"; \
			for d in "$$UBOOT_DIR/configs" "$$UBOOT_DIR/configs-fit" "$$UBOOT_DIR/configs-ubi" "$$UBOOT_DIR/configs-nonmbm" "$$UBOOT_DIR/configs-openwrt"; do \
				[[ -d "$$d" ]] && find -L "$$d" -maxdepth 1 -type f -name '*_defconfig' -printf '%f\n'; \
			done | sort -u > "$$uboot_list"; \
		else \
			[[ -d "$$atf_cfg_dir" ]] && find -L "$$atf_cfg_dir" -maxdepth 1 -type f -name '*_defconfig' -printf '%f\n' | sed 's/_defconfig$$//' | sort -u > "$$atf_list"; \
			[[ -d "$$uboot_cfg_dir" ]] && find -L "$$uboot_cfg_dir" -maxdepth 1 -type f -name '*_defconfig' -printf '%f\n' | sed 's/_defconfig$$//' | sort -u > "$$uboot_list"; \
		fi; \
		comm -12 "$$atf_list" "$$uboot_list"; \
	}; \
	mapfile -t board_cfgs < <(collect_boards_for_variant "$(or $(VARIANT),all)"); \
	if [[ "$${#board_cfgs[@]}" -eq 0 ]]; then \
		echo "No buildable BOARD found for VARIANT=$(or $(VARIANT),all)."; \
		exit 0; \
	fi; \
	echo "Buildable MODEL list for VARIANT=$(or $(VARIANT),all) (use BOARD=<name>):"; \
	printf '  %s\n' "$${board_cfgs[@]#*_}"

board-configs:
	@set -euo pipefail; \
	case "$(VERSION)" in \
		2025) ATF_DIR="atf-20250711"; UBOOT_DIR="uboot-mtk-20250711" ;; \
		SP1|sp1) ATF_DIR="atf-20240117-bacca82a8"; UBOOT_DIR="uboot-mtk-20250711" ;; \
		SP2|sp2) ATF_DIR="atf-20260123"; UBOOT_DIR="uboot-mtk-20250711" ;; \
		*) echo "Error: unsupported VERSION='$(VERSION)'." >&2; exit 1 ;; \
	esac; \
	collect_boards_for_variant() { \
		local variant="$$1"; \
		local variant_lower=$$(echo "$$variant" | tr '[:upper:]' '[:lower:]'); \
		local atf_cfg_dir="$$ATF_DIR/configs"; \
		local uboot_cfg_dir="$$UBOOT_DIR/configs"; \
		case "$$variant_lower" in \
			default) ;; \
			ubootmod) atf_cfg_dir="$$ATF_DIR/configs-ubi"; uboot_cfg_dir="$$UBOOT_DIR/configs-fit" ;; \
			ubi) atf_cfg_dir="$$ATF_DIR/configs-ubi"; uboot_cfg_dir="$$UBOOT_DIR/configs-ubi" ;; \
			nonmbm) atf_cfg_dir="$$ATF_DIR/configs-nonmbm"; uboot_cfg_dir="$$UBOOT_DIR/configs-nonmbm" ;; \
			openwrt) uboot_cfg_dir="$$UBOOT_DIR/configs-openwrt" ;; \
			all) atf_cfg_dir="$$ATF_DIR/configs-ubi"; uboot_cfg_dir="$$UBOOT_DIR/configs-fit"; uboot_cfg_dir2="$$UBOOT_DIR/configs-ubi" ;; \
			*) echo "Error: unsupported VARIANT='$$variant'." >&2; return 1 ;; \
		esac; \
		local atf_list="$$(mktemp)"; \
		local uboot_list="$$(mktemp)"; \
		trap 'rm -f "$$atf_list" "$$uboot_list"' RETURN; \
		if [[ "$$variant_lower" = "all" ]]; then \
			for d in "$$ATF_DIR/configs" "$$ATF_DIR/configs-ubi" "$$ATF_DIR/configs-nonmbm"; do \
				[[ -d "$$d" ]] && find -L "$$d" -maxdepth 1 -type f -name '*_defconfig' -printf '%f\n'; \
			done | sort -u > "$$atf_list"; \
			for d in "$$UBOOT_DIR/configs" "$$UBOOT_DIR/configs-fit" "$$UBOOT_DIR/configs-ubi" "$$UBOOT_DIR/configs-nonmbm" "$$UBOOT_DIR/configs-openwrt"; do \
				[[ -d "$$d" ]] && find -L "$$d" -maxdepth 1 -type f -name '*_defconfig' -printf '%f\n'; \
			done | sort -u > "$$uboot_list"; \
		else \
			[[ -d "$$atf_cfg_dir" ]] && find -L "$$atf_cfg_dir" -maxdepth 1 -type f -name '*_defconfig' -printf '%f\n' | sed 's/_defconfig$$//' | sort -u > "$$atf_list"; \
			[[ -d "$$uboot_cfg_dir" ]] && find -L "$$uboot_cfg_dir" -maxdepth 1 -type f -name '*_defconfig' -printf '%f\n' | sed 's/_defconfig$$//' | sort -u > "$$uboot_list"; \
		fi; \
		comm -12 "$$atf_list" "$$uboot_list"; \
	}; \
	mapfile -t board_cfgs < <(collect_boards_for_variant "$(or $(VARIANT),all)"); \
	printf '%s\n' "$${board_cfgs[@]}"

gen-board-kconfig:
	@set -euo pipefail; \
	case "$(VERSION)" in \
		2025) ATF_DIR="atf-20250711"; UBOOT_DIR="uboot-mtk-20250711" ;; \
		SP1|sp1) ATF_DIR="atf-20240117-bacca82a8"; UBOOT_DIR="uboot-mtk-20250711" ;; \
		SP2|sp2) ATF_DIR="atf-20260123"; UBOOT_DIR="uboot-mtk-20250711" ;; \
		*) echo "Error: unsupported VERSION='$(VERSION)'." >&2; exit 1 ;; \
	esac; \
	capture_list() { \
		local dir="$$1"; local out="$$2"; \
		if [[ -d "$$dir" ]]; then \
			find -L "$$dir" -maxdepth 1 -type f -name '*_defconfig' -printf '%f\n' | sed 's/_defconfig$$//' | sort -u > "$$out"; \
		else \
			true > "$$out"; \
		fi; \
	}; \
	atf_def="$$(mktemp)"; atf_ubi="$$(mktemp)"; atf_nmbm="$$(mktemp)"; \
	ubt_def="$$(mktemp)"; ubt_fit="$$(mktemp)"; ubt_ubi="$$(mktemp)"; ubt_nmbm="$$(mktemp)"; ubt_owrt="$$(mktemp)"; \
	trap 'rm -f "$$atf_def" "$$atf_ubi" "$$atf_nmbm" "$$ubt_def" "$$ubt_fit" "$$ubt_ubi" "$$ubt_nmbm" "$$ubt_owrt"' RETURN; \
	capture_list "$$ATF_DIR/configs"       "$$atf_def"; \
	capture_list "$$ATF_DIR/configs-ubi"   "$$atf_ubi"; \
	capture_list "$$ATF_DIR/configs-nonmbm" "$$atf_nmbm"; \
	capture_list "$$UBOOT_DIR/configs"          "$$ubt_def"; \
	capture_list "$$UBOOT_DIR/configs-fit"      "$$ubt_fit"; \
	capture_list "$$UBOOT_DIR/configs-ubi"      "$$ubt_ubi"; \
	capture_list "$$UBOOT_DIR/configs-nonmbm"   "$$ubt_nmbm"; \
	capture_list "$$UBOOT_DIR/configs-openwrt"  "$$ubt_owrt"; \
	\
	boards_def=$$(comm -12 "$$atf_def"  "$$ubt_def"); \
	boards_ubi_fit=$$(comm -12 "$$atf_ubi"  "$$ubt_fit"); \
	boards_ubi_ubi=$$(comm -12 "$$atf_ubi"  "$$ubt_ubi"); \
	boards_nmb=$$(comm -12 "$$atf_nmbm" "$$ubt_nmbm"); \
	boards_owrt=$$(comm -12 "$$atf_def" "$$ubt_owrt"); \
	\
	all_boards="$$(printf '%s\n' "$$boards_def" "$$boards_ubi_fit" "$$boards_ubi_ubi" "$$boards_nmb" "$$boards_owrt" | sort -u)"; \
	if [[ -z "$$all_boards" ]]; then \
		echo "Error: no buildable BOARD found across any variant." >&2; \
		exit 1; \
	fi; \
	\
	variant_dep() { \
		local cfg="$$1"; \
		local def=0 fit=0 ubi=0 nmb=0 owrt=0; \
		grep -qxF "$$cfg" "$$atf_def"  && grep -qxF "$$cfg" "$$ubt_def"  && def=1; \
		grep -qxF "$$cfg" "$$atf_ubi"  && grep -qxF "$$cfg" "$$ubt_fit"  && fit=1; \
		grep -qxF "$$cfg" "$$atf_ubi"  && grep -qxF "$$cfg" "$$ubt_ubi"  && ubi=1; \
		grep -qxF "$$cfg" "$$atf_nmbm" && grep -qxF "$$cfg" "$$ubt_nmbm" && nmb=1; \
		grep -qxF "$$cfg" "$$atf_def"  && grep -qxF "$$cfg" "$$ubt_owrt" && owrt=1; \
		local deps=""; \
		[ "$$def"  -eq 1 ] && deps="$${deps}VARIANT_DEFAULT || "; \
		[ "$$fit"  -eq 1 ] && deps="$${deps}VARIANT_UBOOTMOD || "; \
		[ "$$ubi"  -eq 1 ] && deps="$${deps}VARIANT_UBI || "; \
		[ "$$nmb"  -eq 1 ] && deps="$${deps}VARIANT_NONMBM || "; \
		[ "$$owrt" -eq 1 ] && deps="$${deps}VARIANT_OPENWRT || "; \
		deps="$${deps% || }"; \
		printf '%s' "$$deps"; \
	}; \
	\
	platforms="$$(printf '%s\n' "$$all_boards" | awk -F_ '{print $$1}' | sort -u)"; \
	out="boards.kconfig"; \
	{ \
		echo "# Auto-generated by: make gen-board-kconfig"; \
		echo "# Do not edit manually. Regenerate with: make gen-board-kconfig"; \
		echo ""; \
		echo "menu \"Board selection\""; \
		echo ""; \
		echo "config BUILD_ALL_BOARDS"; \
		echo "	bool \"Build all boards\""; \
		echo "	default n"; \
		echo "	help"; \
		echo "	  When enabled, all discovered boards (across all variants) will be built."; \
		echo "	  Individual board selections below are ignored."; \
		echo ""; \
	} > "$$out"; \
	for plat in $$platforms; do \
		plat_sym=$$(echo "$$plat" | tr '[:lower:]' '[:upper:]'); \
		printf 'config BUILD_ALL_%s\n' "$$plat_sym" >> "$$out"; \
		printf '\tbool "Build all %s boards"\n' "$$plat" >> "$$out"; \
		printf '\tdepends on !BUILD_ALL_BOARDS\n\n' >> "$$out"; \
	done; \
	while IFS= read -r cfg; do \
		[[ -z "$$cfg" ]] && continue; \
		sym_name=$$(echo "$$cfg" | tr '[:lower:]-' '[:upper:]_'); \
		plat_sym=$$(echo "$${cfg%%_*}" | tr '[:lower:]' '[:upper:]'); \
		var_deps=$$(variant_dep "$$cfg"); \
		printf 'config BOARD_%s\n' "$$sym_name" >> "$$out"; \
		printf '\tbool "%s"\n' "$$cfg" >> "$$out"; \
		printf '\tdepends on !BUILD_ALL_BOARDS && !BUILD_ALL_%s && (%s)\n\n' "$$plat_sym" "$$var_deps" >> "$$out"; \
	done <<< "$$all_boards"; \
	echo "endmenu" >> "$$out"; \
	total=$$(echo "$$all_boards" | wc -l); \
	echo "Generated $$out with $${total} boards (variant-aware)."

atf:
	@set -euo pipefail; \
	printf '%s\n' "env -u MAKEFLAGS -u MAKELEVEL -u MFLAGS ATF_DIR=\"$(ATF_DIR)\" VERSION=\"$(VERSION)\" VARIANT=\"$(VARIANT)\" ATFCFG_DIR=\"$(ATFCFG_DIR)\" CFG_SUBDIR=\"$(CFG_SUBDIR)\" OUTPUT_DIR=\"$(OUTPUT_DIR)\" OC7981=\"$(OC7981)\" OC7986=\"$(OC7986)\" TOOLCHAIN=\"$(TOOLCHAIN)\" ./compile_atf.sh"; \
	env -u MAKEFLAGS -u MAKELEVEL -u MFLAGS \
	ATF_DIR="$(ATF_DIR)" VERSION="$(VERSION)" VARIANT="$(VARIANT)" \
	ATFCFG_DIR="$(ATFCFG_DIR)" CFG_SUBDIR="$(CFG_SUBDIR)" OUTPUT_DIR="$(OUTPUT_DIR)" \
	OC7981="$(OC7981)" OC7986="$(OC7986)" TOOLCHAIN="$(TOOLCHAIN)" ./compile_atf.sh

gpt:
	@set -euo pipefail; \
	printf '%s\n' "env -u MAKEFLAGS -u MAKELEVEL -u MFLAGS VERSION=\"$(VERSION)\" SHOW=\"$(SHOW)\" DRAW=\"$(DRAW)\" SDMMC=\"$(SDMMC)\" ./generate_gpt.sh"; \
	env -u MAKEFLAGS -u MAKELEVEL -u MFLAGS \
	VERSION="$(VERSION)" SHOW="$(SHOW)" DRAW="$(DRAW)" SDMMC="$(SDMMC)" ./generate_gpt.sh

clean:
	@set -euo pipefail; \
	printf '%s\n' "env -u MAKEFLAGS -u MAKELEVEL -u MFLAGS ./build.sh --clean"; \
	env -u MAKEFLAGS -u MAKELEVEL -u MFLAGS ./build.sh --clean

distclean: clean
	@set -euo pipefail; \
	echo "Cleaning output directories..."; \
	rm -rf output output_gpt output_bl2 output_mtmips; \
	echo "Cleaning config files..."; \
	rm -f .config .config.old boards.kconfig; \
	echo "Cleaning generated files..."; \
	rm -rf model_lists; \
	echo "Cleaning u-boot generated files..."; \
	rm -f uboot-mtk-20250711/.config; \
	rm -f uboot-mtk-20250711/boards.kconfig; \
	echo "Distclean complete."

help:
	@printf '%s\n' \
		'Quick build entry points' \
		'' \
		'Usage:' \
		'  make                     # build the current .config selection (single or multi board)' \
		'  make all                 # build all BOARDs found in the intersection of atf/configs and uboot/configs' \
		'  make BOARD=<board>       # build a single BOARD (override .config)' \
		'  make menuconfig          # edit the root .config with a U-Boot-like menu UI' \
		'  make defconfig           # apply default configuration to .config' \
		'  make atf                 # call compile_atf.sh' \
		'  make gpt                 # call generate_gpt.sh' \
		'  make boards              # list buildable BOARDs' \
		'  make board-configs       # list buildable config names (for automation)' \
		'  make clean               # clean build artifacts (via build.sh)' \
		'  make distclean           # clean everything including output dirs and config files' \
		'  make help                # show this help' \
		'' \
		'Board selection (via menuconfig):' \
		'  - Use "Build all boards" for every board, or "Build all mt798x boards" for a platform' \
		'  - Or individually toggle boards in the list (multi-select supported)' \
		'  - For FIP workflow, MODEL=all-mt7981/all-mt7986/all-mt7987/all-mt7988 maps to the matching platform scope' \
		'  - boards.kconfig is auto-generated by: make gen-board-kconfig' \
		'' \
		'Common variables:' \
		'  VERSION=2025|SP1|SP2' \
		'  VARIANT=default|ubootmod|ubi|nonmbm|openwrt' \
		'  FSTHEME=bootstrap|gl|mtk' \
		'  BUILD_FIP=Y|N' \
		'  BUILD_ATF=Y|N' \
		'  BUILD_GPT=Y|N' \
		'  FIP_COMPRESS=0|1' \
		'  FIXED_MTDPARTS=0|1' \
		'  SIMG=0|1' \
		'  UBIMNG=0|1' \
		'  TELNETD=0|1' \
		'  NAND_RAW=0|1' \
		'  SILENT=Y|N' \
		'' \
		'ATF / GPT helpers:' \
		'  make atf ATFCFG_DIR=mt798x_atf CFG_SUBDIR=normal OUTPUT_DIR=output_bl2' \
		'  make gpt SHOW=1' \
		'  make gpt DRAW=1' \
		'  make gpt SDMMC=1' \
		'' \
		'Notes:' \
		'  - BOARD discovery only uses the default configs directories, and only keeps entries' \
		'    that exist in both ATF and U-Boot, matching the FIP build workflow.' \
		'  - make runs with SILENT=Y by default so batch builds do not stop for prompts.'
