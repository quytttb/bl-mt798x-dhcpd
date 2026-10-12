// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025 MediaTek Inc. All Rights Reserved.
 *
 * MTK eFuse read/write command
 */

#include <command.h>
#include <errno.h>
#include <hexdump.h>
#include <malloc.h>
#include <vsprintf.h>
#include <string.h>
#include <failsafe/cprint.h>
#include "mtk_efuse.h"

static int do_efuse_read(struct cmd_tbl *cmdtp, int flag, int argc,
			 char *const argv[])
{
	int ret;
	uint32_t len = 0;
	unsigned long field = 0;
	uint8_t *data;

	if (argc < 2)
		return CMD_RET_USAGE;

	ret = strict_strtoul(argv[1], 10, &field);
	if (ret) {
		cprintln(ERROR, "*** Invalid field ***");
		return CMD_RET_USAGE;
	}

	ret = mtk_efuse_get_len(field, &len);
	if (ret)
		return ret;

	data = calloc(len, sizeof(uint8_t));
	if (!data)
		return -ENOMEM;

	ret = mtk_efuse_read(field, data, len);
	if (ret)
		goto out;

	print_hex_dump("", DUMP_PREFIX_OFFSET, 16, 4, data, len, false);

out:
	free(data);
	return ret;
}

static int do_efuse_write(struct cmd_tbl*cmdtp, int flag, int argc,
			  char *const argv[])
{
	int ret;
	uint32_t len = 0;
	unsigned long field = 0;
	uint8_t *buf;
	uint8_t *data;

	if (argc < 3)
		return CMD_RET_USAGE;

	ret = strict_strtoul(argv[1], 10, &field);
	if (ret) {
		cprintln(ERROR, "*** Invalid field ***");
		return CMD_RET_USAGE;
	}

	ret = mtk_efuse_get_len(field, &len);
	if (ret)
		return ret;

	data = calloc(len, sizeof(uint8_t));
	if (!data)
		return -ENOMEM;

	if (strlen(argv[2]) == len * 2) {
		ret = hex2bin(data, argv[2], len);
		if (ret) {
			cprintln(ERROR, "*** Cannot convert hexstring to binary ***");
			goto out;
		}
	} else if (strlen(argv[2]) + 1 == len * 2) {
		/* odd */
		buf = calloc(strlen(argv[2]) + 2, sizeof(uint8_t));
		if (!buf) {
			ret = -ENOMEM;
			goto out;
		}

		buf[0] = '0';
		memcpy(buf + 1, argv[2], strlen(argv[2]));

		ret = hex2bin(data, buf, len);
		if (ret) {
			cprintln(ERROR, "*** Cannot convert hexstring to binary ***");
			free(buf);
			goto out;
		}

		free(buf);
	} else {
		cprintln(ERROR, "*** Invalid length ***");
		ret = -EINVAL;
		goto out;
	}

	ret = mtk_efuse_write(field, (void *)data, len);

out:
	free(data);
	return ret;
}

static struct cmd_tbl efuse_cmd_sub[] = {
	U_BOOT_CMD_MKENT(read, 2, 0, do_efuse_read, "", ""),
	U_BOOT_CMD_MKENT(write, 3, 0, do_efuse_write, "", "")
};

static int do_efuse(struct cmd_tbl *cmdtp, int flag, int argc,
		    char *const argv[])
{
	struct cmd_tbl *cp;

	if (argc < 2)
		return CMD_RET_USAGE;

	argc--;
	argv++;

	cp = find_cmd_tbl(argv[0], efuse_cmd_sub, ARRAY_SIZE(efuse_cmd_sub));
	if (cp)
		return cp->cmd(cmdtp, flag, argc, argv);

	return CMD_RET_USAGE;
}

static char efuse_help_text[] =
	"read <index> - read <index> eFuse field\n"
	"efuse write <index> <data> - write <data> to <index> eFuse field\n";

U_BOOT_CMD(efuse, CONFIG_SYS_MAXARGS, 0, do_efuse, "MTK eFuse read/write commands",
	   efuse_help_text);
