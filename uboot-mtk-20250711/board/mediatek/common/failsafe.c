// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 MediaTek Inc. All Rights Reserved.
 *
 * Author: Weijie Gao <weijie.gao@mediatek.com>
 *
 * Failsafe operations
 */

#include <command.h>
#include <errno.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <asm/global_data.h>
#include <failsafe/fw_type.h>
#include <failsafe/error.h>
#ifdef CONFIG_CMD_GL_BTN
#include <glbtn.h>
#endif
#include "upgrade_helper.h"
#include <failsafe/cprint.h>

DECLARE_GLOBAL_DATA_PTR;

const char *fw_to_part_name(failsafe_fw_t fw)
{
	switch (fw)
	{
		case FW_TYPE_GPT: return "gpt";
		case FW_TYPE_BL2: return "bl2";
		case FW_TYPE_FIP: return "fip";
		case FW_TYPE_FW: return "fw";
		case FW_TYPE_FACTORY: return "factory";
		case FW_TYPE_SIMG: return "simg";
		default: return "err";
	}
}

static const struct data_part_entry *find_part(const struct data_part_entry *parts,
					       u32 num_parts, const char *abbr)
{
	u32 i;

	if (!abbr)
		return NULL;

	for (i = 0; i < num_parts; i++) {
		if (!strcmp(parts[i].abbr, abbr))
			return &parts[i];
	}

	failsafe_error(-EINVAL, "Invalid upgrading part");

	return NULL;
}

void *httpd_get_upload_buffer_ptr(size_t size)
{
	/* Skip BL31 address range started from 0x43000000 */
	return (void *)gd->ram_base + 0x6000000;
}

int failsafe_validate_image(const void *data, size_t size, failsafe_fw_t fw)
{
	const struct data_part_entry *upgrade_parts, *dpe;
	u32 num_parts;

	board_upgrade_data_parts(&upgrade_parts, &num_parts);

	if (!upgrade_parts || !num_parts) {
		printf("mtkupgrade is not configured!\n");
		return failsafe_error(-ENOSYS, "mtkupgrade is not configured");
	}

	dpe = find_part(upgrade_parts, num_parts, fw_to_part_name(fw));
	if (!dpe && fw == FW_TYPE_SIMG) {
		static const char * const simg_abbrs[] = {
			"simg",
			"simg-emmc",
			"simg-snor",
		};
		u32 i;

		for (i = 0; i < ARRAY_SIZE(simg_abbrs) && !dpe; i++)
			dpe = find_part(upgrade_parts, num_parts, simg_abbrs[i]);
	}
#ifdef __mips__
	if (!dpe && fw == FW_TYPE_FIP)
		dpe = find_part(upgrade_parts, num_parts, "bl");
#endif
	if (!dpe)
		return failsafe_error(-ENODEV, "no data partition for '%s'",
				      fw_to_part_name(fw));

	if (dpe->validate) {
		int ret = dpe->validate(dpe->priv, dpe, data, size);

		if (ret)
			return failsafe_error(ret,
				"'%s' image validation FAILED", dpe->name);
	}

	return 0;
}

int failsafe_write_image(const void *data, size_t size, failsafe_fw_t fw)
{
	const struct data_part_entry *upgrade_parts, *dpe;
	u32 num_parts;
	int ret;

	board_upgrade_data_parts(&upgrade_parts, &num_parts);

	if (!upgrade_parts || !num_parts) {
		printf("mtkupgrade is not configured!\n");
		return failsafe_error(-ENOSYS, "mtkupgrade is not configured");
	}

	dpe = find_part(upgrade_parts, num_parts, fw_to_part_name(fw));
	if (!dpe && fw == FW_TYPE_SIMG) {
		static const char * const simg_abbrs[] = {
			"simg",
			"simg-emmc",
			"simg-snor",
		};
		u32 i;

		for (i = 0; i < ARRAY_SIZE(simg_abbrs) && !dpe; i++)
			dpe = find_part(upgrade_parts, num_parts, simg_abbrs[i]);
	}
#ifdef __mips__
	if (!dpe && fw == FW_TYPE_FIP)
		dpe = find_part(upgrade_parts, num_parts, "bl");
#endif
	if (!dpe)
		return failsafe_error(-ENODEV, "no data partition for '%s'",
				      fw_to_part_name(fw));

#ifdef CONFIG_CMD_GL_BTN
	led_control("led", "system_led", "off");
	led_control("ledblink", "blink_led", "100");
#endif
	printf("\n");
	cprintln(PROMPT, "*** Upgrading %s ***", dpe->name);
	cprintln(PROMPT, "*** Data: %zd (0x%zx) bytes at 0x%08lx ***",
		 size, size, (ulong)data);
	printf("\n");

	ret = dpe->write(dpe->priv, dpe, data, size);
#ifdef CONFIG_CMD_GL_BTN
	led_control("ledblink", "blink_led", "0");
	led_control("led", "system_led", "on");
#endif
	if (ret)
		return failsafe_error(ret, "'%s' image write FAILED", dpe->name);

	printf("\n");
	cprintln(PROMPT, "*** %s upgrade completed! ***", dpe->name);
	printf("\n");

	if (dpe->do_post_action)
		dpe->do_post_action(dpe->priv, dpe, data, size);

	return 0;
}
