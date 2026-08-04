// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 MediaTek Inc. All Rights Reserved.
 *
 * Author: Weijie Gao <weijie.gao@mediatek.com>
 */

#include <hang.h>
#include "bootmenu_common.h"
#include "autoboot_helper.h"
#include "mtd_helper.h"

static const struct data_part_entry mtd_parts[] = {
	{
		.name = "ATF BL2",
		.abbr = "bl2",
		.env_name = "bootfile.bl2",
		.validate = generic_validate_bl2,
		.write = generic_mtd_write_bl2,
	},
	{
		.name = "ATF FIP",
		.abbr = "fip",
		.env_name = "bootfile.fip",
		.validate = generic_validate_fip,
		.write = generic_mtd_write_fip,
		.post_action = UPGRADE_ACTION_CUSTOM,
		//.do_post_action = generic_invalidate_env,
	},
#ifdef CONFIG_MTK_FIP_SUPPORT
	{
		.name = "BL31 of ATF FIP",
		.abbr = "bl31",
		.env_name = "bootfile.bl31",
		.validate = generic_validate_bl31,
		.write = generic_mtd_update_bl31,
		.post_action = UPGRADE_ACTION_CUSTOM,
	},
	{
		.name = "BL33 of ATF FIP",
		.abbr = "bl33",
		.env_name = "bootfile.bl33",
		.validate = generic_validate_bl33,
		.write = generic_mtd_update_bl33,
		.post_action = UPGRADE_ACTION_CUSTOM,
		//.do_post_action = generic_invalidate_env,
	},
#endif
#ifdef CONFIG_MTK_CHAINLOAD_BL
	{
		.name = "Next stage bootloader",
		.abbr = "nextbl",
		.env_name = "bootfile.nextbl",
		.validate = generic_validate_next_bl,
#ifdef CONFIG_MTK_NEXT_BL_IN_UBI
		.write = generic_ubi_write_next_bl,
#else
		.write = generic_mtd_write_next_bl,
#endif
	},
#endif
	{
		.name = "Firmware",
		.abbr = "fw",
		.env_name = "bootfile",
		.post_action = UPGRADE_ACTION_BOOT,
		.validate = generic_mtd_validate_fw,
		.write = generic_mtd_write_fw,
	},
	{
		.name = "Factory",
		.abbr = "factory",
		.env_name = "bootfile.factory",
		.write = generic_mtd_write_factory,
	},
	{
		.name = "Single image",
		.abbr = "simg",
		.env_name = "bootfile.simg",
		.validate = generic_validate_simg,
		.write = generic_mtd_write_simg,
	},
};

void board_upgrade_data_parts(const struct data_part_entry **dpes, u32 *count)
{
	*dpes = mtd_parts;
	*count = ARRAY_SIZE(mtd_parts);
}

int board_boot_default(bool do_boot)
{
	return generic_mtd_boot_image(do_boot);
}

#ifdef CONFIG_MTK_CHAINLOAD_BL
int board_chainload_default(bool do_boot)
{
#ifdef CONFIG_MTK_NEXT_BL_IN_UBI
	return generic_ubi_boot_next_bl(do_boot);
#else
	return generic_mtd_boot_next_bl(do_boot);
#endif
}
#endif

static const struct bootmenu_entry mtd_bootmenu_entries[] = {
#ifdef CONFIG_MTK_AUTO_CHAINLOAD_BL
	{
		.desc = "Chainload next-stage bootloader (Default)",
		.cmd = "mtkchainload"
	},
	{
		.desc = "Startup system",
		.cmd = "mtkboardboot"
	},
#elif defined(CONFIG_MTK_KEENETIC_RAW_FIT_FW)
	/* Keenetic: raw FIT in MTD "ubi" — no mtkboardboot / no full-chip simg. */
	{
		.desc = "Boot Keenetic OS (Default)",
#ifdef CONFIG_MTK_NR3053_UCONFIG_AUTOPROVISION
		.cmd = "nr3053_autoboot"
#else
		.cmd = "run bootcmd"
#endif
	},
#else
	{
		.desc = "Startup system (Default)",
		.cmd = "mtkboardboot"
	},
#endif
	{
		.desc = "Upgrade firmware",
		.cmd = "mtkupgrade fw"
	},
	{
		.desc = "Upgrade ATF BL2",
		.cmd = "mtkupgrade bl2"
	},
	{
		.desc = "Upgrade ATF FIP",
		.cmd = "mtkupgrade fip"
	},
#ifdef CONFIG_MTK_FIP_SUPPORT
	{
		.desc = "  Upgrade ATF BL31 only",
		.cmd = "mtkupgrade bl31"
	},
	{
		.desc = "  Upgrade bootloader only",
		.cmd = "mtkupgrade bl33"
	},
#endif
#ifndef CONFIG_MTK_KEENETIC_RAW_FIT_FW
	{
		.desc = "Upgrade single image",
		.cmd = "mtkupgrade simg"
	},
#endif
#ifdef CONFIG_MTK_CHAINLOAD_BL
	{
		.desc = "Upgrade next-stage bootloader",
		.cmd = "mtkupgrade nextbl"
	},
#ifndef CONFIG_MTK_AUTO_CHAINLOAD_BL
	{
		.desc = "Chainload next-stage bootloader",
		.cmd = "mtkchainload"
	},
#endif
#endif
	{
		.desc = "Load image",
		.cmd = "mtkload"
	},
#ifdef CONFIG_MTK_WEB_FAILSAFE
	{
		.desc = "Start Web failsafe",
		.cmd = "httpd"
	},
#endif
	{
		.desc = "Change boot configuration",
		.cmd = "mtkbootconf"
	},
};

void board_bootmenu_entries(const struct bootmenu_entry **menu, u32 *count)
{
	*menu = mtd_bootmenu_entries;
	*count = ARRAY_SIZE(mtd_bootmenu_entries);
}

void default_boot_set_defaults(void *fdt)
{
#ifdef CONFIG_MTD_VERIFY_FDT
	int ret;

	ret = mtd_verify_linux_fdt(fdt);
	if (ret) {
#ifdef CONFIG_MTD_VERIFY_FDT_WARN_ONLY
		printf("\nWarning: FDT verification failed, continue booting as requested\n\n");
#else
		hang();
#endif
	}
#endif
	mtd_boot_set_defaults(fdt);
}

int board_late_init(void)
{
	return 0;
}
