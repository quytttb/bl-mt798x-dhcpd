/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026 Yuzhii0718
 *
 * All rights reserved.
 *
 * This file is part of the project bl-mt798x-dhcpd
 * You may not use, copy, modify or distribute this file except in compliance with the license agreement.
 *
 * Failsafe theme and picture handlers
 */

#include <env.h>
#include <errno.h>
#include <malloc.h>
#include <linux/ctype.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <net/mtk_httpd.h>

#include <failsafe/fs.h>
#include <failsafe/internal.h>

#define THEME_COLOR_ENV "failsafe_theme_color"
#define THEME_COLOR_MAX_LEN 8
#define THEME_COLOR_RAINBOW "rainbow"
#define THEME_MODE_ENV "failsafe_theme_mode"
#define THEME_MODE_MAX_LEN 8
#define THEME_DARK_VARIANT_ENV "failsafe_theme_dark_variant"
#define THEME_DARK_VARIANT_MAX_LEN 12

static int failsafe_theme_normalize_hex(const char *in, char *out, size_t out_sz)
{
	const char *p;
	size_t len, i;

	if (!in || !out || out_sz < THEME_COLOR_MAX_LEN)
		return -EINVAL;

	p = in;
	while (*p && isspace((unsigned char)*p))
		p++;

	if (*p == '#')
		p++;

	len = strlen(p);
	while (len && isspace((unsigned char)p[len - 1]))
		len--;

	if (len != 3 && len != 6)
		return -EINVAL;

	for (i = 0; i < len; i++) {
		if (!isxdigit((unsigned char)p[i]))
			return -EINVAL;
	}

	out[0] = '#';
	if (len == 3) {
		for (i = 0; i < 3; i++) {
			char c = tolower((unsigned char)p[i]);
			out[1 + i * 2] = c;
			out[2 + i * 2] = c;
		}
		out[7] = '\0';
		return 0;
	}

	for (i = 0; i < 6; i++)
		out[1 + i] = tolower((unsigned char)p[i]);
	out[7] = '\0';
	return 0;
}

static bool failsafe_theme_valid_mode(const char *mode)
{
	if (!mode || !mode[0])
		return false;
	return !strcmp(mode, "auto") || !strcmp(mode, "light") ||
		!strcmp(mode, "dark");
}

static bool failsafe_theme_valid_dark_variant(const char *variant)
{
	if (!variant || !variant[0])
		return false;
	return !strcmp(variant, "standard") || !strcmp(variant, "amoled");
}

static const char *failsafe_guess_content_type(const char *path)
{
	const char *ext;

	if (!path)
		return "application/octet-stream";

	ext = strrchr(path, '.');
	if (!ext)
		return "application/octet-stream";

	if (!strcasecmp(ext, ".svg"))
		return "image/svg+xml";
	if (!strcasecmp(ext, ".png"))
		return "image/png";
	if (!strcasecmp(ext, ".jpg") || !strcasecmp(ext, ".jpeg"))
		return "image/jpeg";
	if (!strcasecmp(ext, ".gif"))
		return "image/gif";
	if (!strcasecmp(ext, ".ico"))
		return "image/x-icon";

	return "application/octet-stream";
}

static int output_binary_file(struct httpd_response *response,
	const char *filename, const char *content_type)
{
	const struct fs_desc *file;
	int ret = 0;

	file = fs_find_file(filename);

	response->status = HTTP_RESP_STD;

	if (file) {
		response->data = file->data;
		response->size = file->size;
		/* embedded binary assets are gzip-compressed at build time */
		response->info.content_encoding = "gzip";
	} else {
		response->data = "Not Found";
		response->size = strlen(response->data);
		response->info.code = 404;
		response->info.content_encoding = NULL;
		ret = 1;
	}

	if (!response->info.code)
		response->info.code = 200;
	response->info.connection_close = 1;
	response->info.content_type = content_type ? content_type : "application/octet-stream";

	return ret;
}

void picture_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response)
{
	const char *uri;
	const char *file;
	const char *fallback = NULL;
	const char *ctype;

	if (status != HTTP_CB_NEW)
		return;

	uri = request && request->urih ? request->urih->uri : NULL;
	if (!uri || !uri[0]) {
		response->status = HTTP_RESP_STD;
		response->data = "Not Found";
		response->size = strlen(response->data);
		response->info.code = 404;
		response->info.connection_close = 1;
		response->info.content_type = "text/plain";
		return;
	}

	file = uri[0] == '/' ? uri + 1 : uri;
	if (!strcmp(file, "favicon.ico"))
		fallback = "favicon.svg";

	ctype = failsafe_guess_content_type(file);
	if (output_binary_file(response, file, ctype) && fallback) {
		ctype = failsafe_guess_content_type(fallback);
		output_binary_file(response, fallback, ctype);
	}
}

void theme_get_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response)
{
	const char *val;
	const char *theme;
	const char *dark_variant;
	char color[THEME_COLOR_MAX_LEN] = "";
	static char resp[160];

	if (status != HTTP_CB_NEW)
		return;

	if (!request || request->method != HTTP_GET) {
		failsafe_http_reply_json(response, 405,
			"{\"ok\":false,\"error\":\"method\"}");
		return;
	}

	val = env_get(THEME_COLOR_ENV);
	if (val && !strcmp(val, THEME_COLOR_RAINBOW)) {
		strlcpy(color, THEME_COLOR_RAINBOW, sizeof(color));
	} else if (!val || failsafe_theme_normalize_hex(val, color, sizeof(color))) {
		color[0] = '\0';
	}

	theme = env_get(THEME_MODE_ENV);
	if (!failsafe_theme_valid_mode(theme))
		theme = "";

	dark_variant = env_get(THEME_DARK_VARIANT_ENV);
	if (!failsafe_theme_valid_dark_variant(dark_variant))
		dark_variant = "";

	snprintf(resp, sizeof(resp),
		"{\"ok\":true,\"color\":\"%s\",\"theme\":\"%s\",\"dark_variant\":\"%s\"}",
		color, theme ? theme : "", dark_variant ? dark_variant : "");

	failsafe_http_reply_json(response, 200, resp);
}

void theme_set_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response)
{
	char *color = NULL;
	char *theme = NULL;
	char *dark_variant = NULL;
	char norm[THEME_COLOR_MAX_LEN];
	int ret;
	bool changed = false;
	const char *err = NULL;

	if (status != HTTP_CB_NEW)
		return;

	if (!request || request->method != HTTP_POST) {
		failsafe_http_reply_json(response, 405,
			"{\"ok\":false,\"error\":\"method\"}");
		return;
	}

	ret = failsafe_get_form_value(request, "color", &color,
		THEME_COLOR_MAX_LEN, true, true);
	if (ret) {
		failsafe_http_reply_json(response, 400,
			"{\"ok\":false,\"error\":\"bad_color\"}");
		return;
	}

	ret = failsafe_get_form_value(request, "theme", &theme,
		THEME_MODE_MAX_LEN, true, true);
	if (ret) {
		free(color);
		failsafe_http_reply_json(response, 400,
			"{\"ok\":false,\"error\":\"bad_theme\"}");
		return;
	}

	ret = failsafe_get_form_value(request, "dark_variant", &dark_variant,
		THEME_DARK_VARIANT_MAX_LEN, true, true);
	if (ret) {
		free(color);
		free(theme);
		failsafe_http_reply_json(response, 400,
			"{\"ok\":false,\"error\":\"bad_dark_variant\"}");
		return;
	}

	if (color) {
		if (!color[0]) {
			ret = env_set(THEME_COLOR_ENV, NULL);
			if (ret)
				goto out_free;
			changed = true;
		} else if (!strcmp(color, THEME_COLOR_RAINBOW)) {
			ret = env_set(THEME_COLOR_ENV, THEME_COLOR_RAINBOW);
			if (ret)
				goto out_free;
			changed = true;
		} else {
			if (failsafe_theme_normalize_hex(color, norm, sizeof(norm))) {
				ret = -EINVAL;
				err = "bad_color";
				goto out_free;
			}
			ret = env_set(THEME_COLOR_ENV, norm);
			if (ret)
				goto out_free;
			changed = true;
		}
	}

	if (theme) {
		if (!theme[0]) {
			ret = env_set(THEME_MODE_ENV, NULL);
			if (ret)
				goto out_free;
			changed = true;
		} else {
			if (!failsafe_theme_valid_mode(theme)) {
				ret = -EINVAL;
				err = "bad_theme";
				goto out_free;
			}
			ret = env_set(THEME_MODE_ENV, theme);
			if (ret)
				goto out_free;
			changed = true;
		}
	}

	if (dark_variant) {
		/* "standard" is the implicit default — store as unset so the
		 * env stays clean and `printenv` doesn't show a redundant key */
		if (!dark_variant[0] || !strcmp(dark_variant, "standard")) {
			ret = env_set(THEME_DARK_VARIANT_ENV, NULL);
			if (ret)
				goto out_free;
			changed = true;
		} else {
			if (!failsafe_theme_valid_dark_variant(dark_variant)) {
				ret = -EINVAL;
				err = "bad_dark_variant";
				goto out_free;
			}
			ret = env_set(THEME_DARK_VARIANT_ENV, dark_variant);
			if (ret)
				goto out_free;
			changed = true;
		}
	}

	if (changed) {
		ret = env_save();
		if (ret)
			goto out_free;
	}

	free(color);
	free(theme);
	free(dark_variant);
	failsafe_http_reply_json(response, 200, "{\"ok\":true}");
	return;

out_free:
	free(color);
	free(theme);
	free(dark_variant);
	if (ret == -EINVAL) {
		if (err && !strcmp(err, "bad_color"))
			failsafe_http_reply_json(response, 400,
				"{\"ok\":false,\"error\":\"bad_color\"}");
		else if (err && !strcmp(err, "bad_dark_variant"))
			failsafe_http_reply_json(response, 400,
				"{\"ok\":false,\"error\":\"bad_dark_variant\"}");
		else
			failsafe_http_reply_json(response, 400,
				"{\"ok\":false,\"error\":\"bad_theme\"}");
	}
	else
		failsafe_http_reply_json(response, 500,
			"{\"ok\":false,\"error\":\"save\"}");
}

#ifdef CONFIG_WEBUI_FAILSAFE_UI_BOOTSTRAP
void theme_register_handlers(struct httpd_instance *inst)
{
	httpd_register_uri_handler(inst, "/favicon.svg", &picture_handler, NULL);
	httpd_register_uri_handler(inst, "/settings.html", &html_handler, NULL);
	httpd_register_uri_handler(inst, "/settings_js.js", &js_handler, NULL);
	httpd_register_uri_handler(inst, "/theme.js", &js_handler, NULL);
	httpd_register_uri_handler(inst, "/theme/get", &theme_get_handler, NULL);
	httpd_register_uri_handler(inst, "/theme/set", &theme_set_handler, NULL);
}
#endif
