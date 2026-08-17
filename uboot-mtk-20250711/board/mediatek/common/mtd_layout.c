// SPDX-License-Identifier: GPL-2.0+

#include <command.h>
#include <env.h>
#include <mtd_node.h>
#include <linux/mtd/mtd.h>
#include <linux/string.h>
#include <dm/ofnode.h>

#define MTD_LAYOUT_ENV		"mtd_layout"
#define MTD_LAYOUT_ENV_LEGACY	"mtd_layout_label"
#define MTD_LAYOUT_CUSTOM_LABEL	"custom"
#define MTD_LAYOUT_CUSTOM_ENV	"mtd_layout_custom"

#ifdef CONFIG_MTD_LAYOUT_SPI_NAND
static char mtd_layout_ids_buf[256];
static char mtd_layout_parts_buf[512];

const char *mtd_layout_spi_nand_replace(const char *str, char *buf, size_t bufsz)
{
	char *pos;

	if (!str)
		return NULL;

	strncpy(buf, str, bufsz - 1);
	buf[bufsz - 1] = '\0';

	pos = buf;
	while ((pos = strstr(pos, "nmbm0"))) {
		/* "spi-nand0" is 9 chars, "nmbm0" is 5 chars */
		memmove(pos + 9, pos + 5, strlen(pos + 5) + 1);
		memcpy(pos, "spi-nand0", 9);
		pos += 9;
	}

	return buf;
}
#endif

#ifdef CONFIG_MEDIATEK_MTD_LAYOUT_PRINT
static void log_mtd_layout_state(const char *layout_label, const char *mtdids,
				 const char *mtdparts)
{
	printf("MTD layout: current layout = %s\n",
	       layout_label ? layout_label : "default");
	printf("MTD layout: effective mtdids = %s\n",
	       mtdids ? mtdids : "(none)");
	printf("MTD layout: effective mtdparts = %s\n",
	       mtdparts ? mtdparts : "(none)");
}
#endif

static ofnode ofnode_get_mtd_layout(const char *layout_label)
{
	ofnode node, layout;
	const char *label;

	node = ofnode_path("/mtd-layout");
	if (!ofnode_valid(node)) {
		return ofnode_null();
	}

	if (!ofnode_get_child_count(node)) {
		return ofnode_null();
	}

	ofnode_for_each_subnode(layout, node) {
		label = ofnode_read_string(layout, "label");
		if (label && !strcmp(layout_label, label)) {
			return layout;
		}
	}

	return ofnode_null();
}

static ofnode ofnode_get_default_mtd_layout(void)
{
	ofnode node, layout;

	layout = ofnode_get_mtd_layout("default");
	if (ofnode_valid(layout))
		return layout;

	node = ofnode_path("/mtd-layout");
	if (!ofnode_valid(node) || !ofnode_get_child_count(node))
		return ofnode_null();

	return ofnode_first_subnode(node);
}

static const char *get_custom_mtdparts(void)
{
	const char *parts;

	if (!(gd->flags & GD_FLG_ENV_READY))
		return NULL;

	parts = env_get(MTD_LAYOUT_CUSTOM_ENV);
	if (parts && parts[0])
		return parts;

	return NULL;
}

const char *get_mtd_layout_label(void)
{
	const char *layout_label = NULL;
	const char *legacy_label = NULL;

	if (gd->flags & GD_FLG_ENV_READY) {
		layout_label = env_get(MTD_LAYOUT_ENV);
		legacy_label = env_get(MTD_LAYOUT_ENV_LEGACY);

		if (!layout_label || !layout_label[0])
			layout_label = legacy_label;

		if (!layout_label || !layout_label[0])
			layout_label = "default";

		if (!legacy_label || strcmp(legacy_label, layout_label))
			env_set(MTD_LAYOUT_ENV_LEGACY, layout_label);

		if (!env_get(MTD_LAYOUT_ENV) ||
		    strcmp(env_get(MTD_LAYOUT_ENV), layout_label))
			env_set(MTD_LAYOUT_ENV, layout_label);

		layout_label = env_get(MTD_LAYOUT_ENV);
		if (layout_label && layout_label[0])
			return layout_label;
	}

	if (!layout_label)
		layout_label = "default";

	return layout_label;
}

void board_mtdparts_default(const char **mtdids, const char **mtdparts)
{
	const char *ids = NULL;
	const char *parts = NULL;
	const char *layout_label = NULL;
	const char *boot_part = NULL;
	const char *factory_part = NULL;
	const char *sysupgrade_kernel_ubipart = NULL;
	const char *sysupgrade_rootfs_ubipart = NULL;
	const char *cmdline = NULL;
	const char *custom_parts = NULL;
	ofnode layout_node;

	layout_label = get_mtd_layout_label();
	if (!strcmp(layout_label, MTD_LAYOUT_CUSTOM_LABEL)) {
		custom_parts = get_custom_mtdparts();
		if (custom_parts)
			layout_node = ofnode_get_default_mtd_layout();
		else
			layout_node = ofnode_get_mtd_layout("default");
	} else {
		layout_node = ofnode_get_mtd_layout(layout_label);
	}

	if (ofnode_valid(layout_node)) {
		ids = ofnode_read_string(layout_node, "mtdids");
		parts = ofnode_read_string(layout_node, "mtdparts");
		boot_part = ofnode_read_string(layout_node, "boot_part");
		factory_part = ofnode_read_string(layout_node, "factory_part");
		sysupgrade_kernel_ubipart = ofnode_read_string(layout_node, "sysupgrade_kernel_ubipart");
		sysupgrade_rootfs_ubipart = ofnode_read_string(layout_node, "sysupgrade_rootfs_ubipart");
		cmdline = ofnode_read_string(layout_node, "cmdline");
	}

#ifdef CONFIG_MTD_LAYOUT_SPI_NAND
	ids = mtd_layout_spi_nand_replace(ids, mtd_layout_ids_buf,
					  sizeof(mtd_layout_ids_buf));
	parts = mtd_layout_spi_nand_replace(parts, mtd_layout_parts_buf,
					    sizeof(mtd_layout_parts_buf));
#endif

	if (custom_parts)
		parts = custom_parts;
	else if (!strcmp(layout_label, MTD_LAYOUT_CUSTOM_LABEL))
		layout_label = "default";

	if (ids && parts) {
		*mtdids = ids;
		*mtdparts = parts;
		//printf("%s: mtdids=%s & mtdparts=%s\n", __func__, ids, parts);
	}

#ifdef CONFIG_MEDIATEK_MTD_LAYOUT_PRINT
	log_mtd_layout_state(layout_label, ids, parts);
#endif

	if (IS_ENABLED(CONFIG_MTK_FDT_BOOTARGS_FALLBACK)) {
		/* Only set bootargs if layout explicitly defines cmdline,
		 * otherwise preserve existing bootargs (e.g. from official FDT) */
		if (cmdline)
			env_set("bootargs", cmdline);
	} else {
		env_set("bootargs", cmdline);
	}
	env_set(MTD_LAYOUT_ENV, layout_label);
	env_set(MTD_LAYOUT_ENV_LEGACY, layout_label);
	env_set("ubi_boot_part", boot_part);
	env_set("factory_part", factory_part);
	env_set("sysupgrade_kernel_ubipart", sysupgrade_kernel_ubipart);
	env_set("sysupgrade_rootfs_ubipart", sysupgrade_rootfs_ubipart);
}
