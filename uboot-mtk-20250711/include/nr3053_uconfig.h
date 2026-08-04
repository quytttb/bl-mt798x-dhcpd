/* SPDX-License-Identifier: GPL-2.0+ */
#ifndef __NR3053_UCONFIG_H__
#define __NR3053_UCONFIG_H__

#include <linux/types.h>

#define NR3053_UCONFIG_IMAGE_SIZE	0x80000

enum nr3053_uconfig_slot {
	NR3053_UCONFIG_ACTIVE = 0,
	NR3053_UCONFIG_RECOVERY = 1,
};

enum nr3053_uconfig_action {
	NR3053_UCONFIG_ACTION_NONE = 0,
	NR3053_UCONFIG_ACTION_PRESERVED,
	NR3053_UCONFIG_ACTION_REPAIRED_ACTIVE,
	NR3053_UCONFIG_ACTION_REPAIRED_RECOVERY,
	NR3053_UCONFIG_ACTION_PROVISIONED,
	NR3053_UCONFIG_ACTION_RESTORED,
};

struct nr3053_uconfig_slot_status {
	bool crc[2];
	bool identity_valid;
	bool redundant;
	bool country_vn;
	bool recovery_defaults;
};

struct nr3053_uconfig_status {
	struct nr3053_uconfig_slot_status active;
	struct nr3053_uconfig_slot_status recovery;
	bool copies_identical;
	bool healthy;
};

int nr3053_uconfig_get_status(struct nr3053_uconfig_status *status);
int nr3053_uconfig_provision(enum nr3053_uconfig_action *action);
int nr3053_uconfig_autoboot_prepare(void);
int nr3053_uconfig_read_slot(enum nr3053_uconfig_slot slot, void *buffer,
			     size_t length);
int nr3053_uconfig_restore(const void *image, size_t length,
			   enum nr3053_uconfig_action *action);
const char *nr3053_uconfig_action_name(enum nr3053_uconfig_action action);

#endif /* __NR3053_UCONFIG_H__ */
