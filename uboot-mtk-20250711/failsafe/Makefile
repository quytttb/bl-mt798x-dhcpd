# SPDX-License-Identifier: GPL-2.0

WEBUI_FAILSAFE_GIT_HASH := $(or $(strip $(shell \
	for try in . .. ../..; do \
		git -C $(srctree)/$$try rev-parse --short=7 HEAD 2>/dev/null && break; \
	done)),unknown)

WEBUI_FAILSAFE_GIT_DIRTY := $(or $(strip $(shell \
	for try in . .. ../..; do \
		if git -C $(srctree)/$$try rev-parse --is-inside-work-tree >/dev/null 2>&1; then \
			test -n "$$(git -C $(srctree)/$$try status --porcelain --untracked-files=no 2>/dev/null)" && echo 1 || echo 0; \
			break; \
		fi; \
	done)),0)

ccflags-y += -DWEBUI_FAILSAFE_GIT_HASH=\"$(WEBUI_FAILSAFE_GIT_HASH)\"
ccflags-y += -DWEBUI_FAILSAFE_GIT_DIRTY=$(WEBUI_FAILSAFE_GIT_DIRTY)
subdir-ccflags-y += -DWEBUI_FAILSAFE_GIT_HASH=\"$(WEBUI_FAILSAFE_GIT_HASH)\"
subdir-ccflags-y += -DWEBUI_FAILSAFE_GIT_DIRTY=$(WEBUI_FAILSAFE_GIT_DIRTY)

ifeq ($(CONFIG_XPL_BUILD),)

obj-y += fs.o failsafe_core.o
obj-y += modules/
obj-y += embedded/

endif
