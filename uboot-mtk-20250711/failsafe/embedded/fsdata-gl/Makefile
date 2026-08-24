# SPDX-License-Identifier: GPL-2.0

# example:
# obj-y += XXXX.o <- object file to generate
# FILE_XXXX.o := YYYY.html <- actual html file to be embedded
# FSPATH_XXXX.o := ZZZ/WWW.EXT <- virtual fs path to be used inside u-boot

obj-y += 404.o
FILE_404.o := .min/404.html.gz
FSPATH_404.o := 404.html

obj-y += bl2.o
FILE_bl2.o := .min/bl2.html.gz
FSPATH_bl2.o := bl2.html

obj-y += booting.o
FILE_booting.o := .min/booting.html.gz
FSPATH_booting.o := booting.html

obj-$(CONFIG_WEBUI_FAILSAFE_BACKUP) += backup.o
FILE_backup.o := .min/backup.html.gz
FSPATH_backup.o := backup.html

obj-$(CONFIG_WEBUI_FAILSAFE_FLASH) += flash.o
FILE_flash.o := .min/flash.html.gz
FSPATH_flash.o := flash.html

obj-y += fail.o
FILE_fail.o := .min/fail.html.gz
FSPATH_fail.o := fail.html

obj-y += flashing.o
FILE_flashing.o := .min/flashing.html.gz
FSPATH_flashing.o := flashing.html

obj-$(CONFIG_WEBUI_FAILSAFE_GPT) += gpt.o
FILE_gpt.o := .min/gpt.html.gz
FSPATH_gpt.o := gpt.html

obj-y += index.o
FILE_index.o := .min/index.html.gz
FSPATH_index.o := index.html

obj-y += initramfs.o
FILE_initramfs.o := .min/initramfs.html.gz
FSPATH_initramfs.o := initramfs.html

obj-y += main.o
FILE_main.o := .min/main.js.gz
FSPATH_main.o := main.js

obj-$(CONFIG_WEBUI_FAILSAFE_ENV) += env.o
FILE_env.o := .min/env.html.gz
FSPATH_env.o := env.html

obj-$(CONFIG_WEBUI_FAILSAFE_FACTORY) += factory.o
FILE_factory.o := .min/factory.html.gz
FSPATH_factory.o := factory.html

obj-$(CONFIG_WEBUI_FAILSAFE_CONSOLE) += console.o
FILE_console.o := .min/console.html.gz
FSPATH_console.o := console.html

obj-y += reboot.o
FILE_reboot.o := .min/reboot.html.gz
FSPATH_reboot.o := reboot.html

obj-$(CONFIG_WEBUI_FAILSAFE_SIMG) += simg.o
FILE_simg.o := .min/simg.html.gz
FSPATH_simg.o := simg.html

obj-y += style.o
FILE_style.o := .min/style.css.gz
FSPATH_style.o := style.css

obj-y += uboot.o
FILE_uboot.o := .min/uboot.html.gz
FSPATH_uboot.o := uboot.html

# customized build rules
strip_path = $(subst /,_,$(subst -,_,$(subst .,_,$(1))))
WEBUI_FAILSAFE_JS_MINIFY_SCRIPT = $(srctree)/$(src)/../minify-js.cjs

WEBUI_FAILSAFE_CSS_MINIFY_SCRIPT = $(srctree)/$(src)/../minify-css.cjs
WEBUI_FAILSAFE_HTML_MINIFY_SCRIPT = $(srctree)/$(src)/../minify-html.cjs
WEBUI_FAILSAFE_GZIP_SCRIPT = $(srctree)/$(src)/../gzip-compress.cjs

quiet_cmd_minify_css = CLEANCSS $@
cmd_minify_css = mkdir -p $(dir $@) && node $(WEBUI_FAILSAFE_CSS_MINIFY_SCRIPT) $< $@

quiet_cmd_minify_html = HTMLMIN $@
cmd_minify_html = mkdir -p $(dir $@) && node $(WEBUI_FAILSAFE_HTML_MINIFY_SCRIPT) $< $@

quiet_cmd_minify_js = UGLIFY  $@
cmd_minify_js = mkdir -p $(dir $@) && node $(WEBUI_FAILSAFE_JS_MINIFY_SCRIPT) $< $@

quiet_cmd_fsdagl_gzembed = GZIP    $@
cmd_fsdagl_gzembed = node $(WEBUI_FAILSAFE_GZIP_SCRIPT) $< $@

quiet_cmd_as_o_html = AS      $@
cmd_as_o_html = $(CC) $(a_flags) -c -o $@ \
		-DDATA_SECT_NAME="\".rodata.fsdata.$(call strip_path,$(FSPATH_$(@F)))\"" \
		-DDATA_OBJ_FILE="\"$(srctree)/$(obj)/$(FILE_$(@F))\"" \
		-DDATA_OBJ_NAME="fsdata_$(call strip_path,$(FSPATH_$(@F)))" \
		-DDATA_SIZE_NAME="fsdata_size_$(call strip_path,$(FSPATH_$(@F)))" \
		-DDATA_SECT_FSPATH_NAME="\".rodata.fsdata.path.$(call strip_path,$(FSPATH_$(@F)))\"" \
		-DDATA_OBJ_FSPATH_NAME="fsdata_path_$(call strip_path,$(FSPATH_$(@F)))" \
		-DDATA_OBJ_FSPATH="\"$(FSPATH_$(@F))\"" \
		-DUBOOT_LIST_SECT_NAME="\"__u_boot_list_2_fs_2_$(call strip_path,$(FSPATH_$(@F)))\"" \
		-DUBOOT_LIST_NAME="fsdata_ulist_$(call strip_path,$(FSPATH_$(@F)))" \
		$(srctree)/$(src)/embed.S

# Step 1: Minify source files
$(obj)/.min/%.html: $(src)/%.html $(WEBUI_FAILSAFE_HTML_MINIFY_SCRIPT) FORCE
	$(call cmd,force_checksrc)
	$(call if_changed,minify_html)

$(obj)/.min/%.css: $(src)/%.css $(WEBUI_FAILSAFE_CSS_MINIFY_SCRIPT) FORCE
	$(call cmd,force_checksrc)
	$(call if_changed,minify_css)

$(obj)/.min/%.js: $(src)/%.js $(WEBUI_FAILSAFE_JS_MINIFY_SCRIPT) FORCE
	$(call cmd,force_checksrc)
	$(call if_changed,minify_js)

# Step 2: GZip-compress minified files
$(obj)/.min/%.html.gz: $(obj)/.min/%.html $(WEBUI_FAILSAFE_GZIP_SCRIPT) FORCE
	$(call if_changed,fsdagl_gzembed)

$(obj)/.min/%.css.gz: $(obj)/.min/%.css $(WEBUI_FAILSAFE_GZIP_SCRIPT) FORCE
	$(call if_changed,fsdagl_gzembed)

$(obj)/.min/%.js.gz: $(obj)/.min/%.js $(WEBUI_FAILSAFE_GZIP_SCRIPT) FORCE
	$(call if_changed,fsdagl_gzembed)

# Step 3: Embed GZip-compressed files into object files
$(obj)/%.o: $(obj)/.min/%.html.gz FORCE
	$(call if_changed_dep,as_o_html)

$(obj)/%.o: $(obj)/.min/%.css.gz FORCE
	$(call if_changed_dep,as_o_html)

$(obj)/%.o: $(obj)/.min/%.js.gz FORCE
	$(call if_changed_dep,as_o_html)

clean-files += .min/*
