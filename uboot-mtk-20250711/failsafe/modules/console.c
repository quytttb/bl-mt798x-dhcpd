/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2026 Yuzhii0718
 *
 * All rights reserved.
 *
 * This file is part of the project bl-mt798x-dhcpd
 * You may not use, copy, modify or distribute this file except in compliance with the license agreement.
 *
 * Failsafe Web console
 */

#include <command.h>
#include <env.h>
#include <malloc.h>
#include <console.h>
#include <membuf.h>
#include <asm/global_data.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <net/mtk_httpd.h>

#include "../failsafe_internal.h"

DECLARE_GLOBAL_DATA_PTR;

#define WEB_CONSOLE_CMD_MAX		256
#define WEB_CONSOLE_POLL_MAX	8192
#define WEB_CONSOLE_EXEC_BUF_SIZE	32768

bool webconsole_exec_busy;

static const char *failsafe_get_prompt(void)
{
	const char *p = env_get("prompt");

	if (p && p[0])
		return p;

#ifdef CONFIG_SYS_PROMPT
	return CONFIG_SYS_PROMPT;
#else
	return "MTK> ";
#endif
}

static int failsafe_webconsole_require_token(struct httpd_request *request,
	struct httpd_response *response)
{
	const char *tok;
	struct httpd_form_value *v;
	size_t toklen;

	tok = env_get("failsafe_console_token");
	if (!tok || !tok[0])
		return 0;

	if (!request || request->method != HTTP_POST)
		goto deny;

	v = httpd_request_find_value(request, "token");
	if (!v || !v->data)
		goto deny;

	toklen = strlen(tok);
	if (v->size != toklen)
		goto deny;

	if (memcmp(v->data, tok, toklen))
		goto deny;

	return 0;

deny:
	response->status = HTTP_RESP_STD;
	response->info.code = 403;
	response->info.connection_close = 1;
	response->info.content_type = "text/plain";
	response->data = "forbidden";
	response->size = strlen(response->data);
	return -EACCES;
}

int failsafe_webconsole_ensure_recording(void)
{
	int ret;
	struct membuf new_mb;

	if (!gd)
		return -ENODEV;

	if (!gd->console_out.start) {
		ret = console_record_init();
		if (ret)
			return ret;
	}

	/*
	 * The stock CONSOLE_RECORD_OUT_SIZE (0x400 = 1 KiB) is too small
	 * for command output (e.g. "help").  Upgrade to a 32 KiB buffer so
	 * output is not truncated.
	 */
	if (membuf_size((struct membuf *)&gd->console_out) <
	    WEB_CONSOLE_EXEC_BUF_SIZE) {
		/* Preserve any data already recorded */
		int old_avail = membuf_avail((struct membuf *)&gd->console_out);
		char *old_data = NULL;

		if (old_avail > 0) {
			old_data = malloc(old_avail);
			if (old_data)
				membuf_get((struct membuf *)&gd->console_out,
					   old_data, old_avail);
		}

		if (!membuf_new(&new_mb, WEB_CONSOLE_EXEC_BUF_SIZE)) {
			if (old_data) {
				membuf_put(&new_mb, old_data, old_avail);
				free(old_data);
			}
			membuf_dispose((struct membuf *)&gd->console_out);
			gd->console_out = new_mb;
		}
	}

	gd->flags |= GD_FLG_RECORD;
	return 0;
}

void webconsole_poll_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response)
{
	char *chunk = NULL, *esc = NULL, *json = NULL;
	int ret, avail, want, got;
	size_t esc_sz, json_sz;

	if (status == HTTP_CB_CLOSED) {
		failsafe_free_session(status, response);
		return;
	}

	if (status != HTTP_CB_NEW)
		return;

	response->status = HTTP_RESP_STD;
	response->info.code = 200;
	response->info.connection_close = 1;
	response->info.content_type = "application/json";

	if (!request || request->method != HTTP_POST) {
		response->info.code = 405;
		response->data = "{\"error\":\"method\"}\n";
		response->size = strlen(response->data);
		return;
	}

	if (failsafe_webconsole_require_token(request, response))
		return;

	/*
	 * If a command is currently executing (webconsole_exec_handler),
	 * return an empty response so the buffer is not consumed
	 * mid-command.
	 */
	if (webconsole_exec_busy) {
		response->data = "{\"data\":\"\",\"avail\":0}\n";
		response->size = strlen(response->data);
		return;
	}

	ret = failsafe_webconsole_ensure_recording();
	if (ret) {
		response->info.code = 503;
		response->data = "{\"error\":\"no_console\"}\n";
		response->size = strlen(response->data);
		return;
	}

	avail = membuf_avail(&gd->console_out);
	want = min(avail, (int)WEB_CONSOLE_POLL_MAX);

	chunk = malloc(want + 1);
	if (!chunk) {
		response->info.code = 500;
		response->data = "{\"error\":\"oom\"}\n";
		response->size = strlen(response->data);
		return;
	}

	got = want ? membuf_get(&gd->console_out, chunk, want) : 0;
	chunk[got] = '\0';

	/*
	 * The overflow flag is sticky in console_record_putc/puts
	 * (common/console.c): once set it is never cleared.  Clear it
	 * here after consuming data, but only when the buffer is no
	 * longer dangerously full -- so the client sees a transient
	 * "truncated" warning rather than a permanent one.
	 */
	if ((gd->flags & GD_FLG_RECORD_OVF) &&
	    membuf_free((struct membuf *)&gd->console_out) >
	    WEB_CONSOLE_EXEC_BUF_SIZE / 4)
		gd->flags &= ~GD_FLG_RECORD_OVF;

	/* Worst case: every char becomes ' ' or escaped with one extra backslash */
	esc_sz = (size_t)got * 2 + 64;
	esc = malloc(esc_sz);
	if (!esc) {
		free(chunk);
		response->info.code = 500;
		response->data = "{\"error\":\"oom\"}\n";
		response->size = strlen(response->data);
		return;
	}

	json_escape(esc, esc_sz, chunk);
	free(chunk);

	json_sz = strlen(esc) + 128;
	json = malloc(json_sz);
	if (!json) {
		free(esc);
		response->info.code = 500;
		response->data = "{\"error\":\"oom\"}\n";
		response->size = strlen(response->data);
		return;
	}

	snprintf(json, json_sz, "{\"data\":\"%s\",\"avail\":%d,\"overflow\":%s}\n", esc,
		membuf_avail(&gd->console_out),
		(gd->flags & GD_FLG_RECORD_OVF) ? "true" : "false");
	free(esc);

	response->data = json;
	response->size = strlen(json);
	response->session_data = json;
}

void webconsole_exec_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response)
{
	struct httpd_form_value *cmdv;
	char cmd[WEB_CONSOLE_CMD_MAX + 1];
	char *esc = NULL, *json = NULL;
	int ret;
	size_t esc_sz, json_sz;

	if (status == HTTP_CB_CLOSED) {
		failsafe_free_session(status, response);
		return;
	}

	if (status != HTTP_CB_NEW)
		return;

	response->status = HTTP_RESP_STD;
	response->info.code = 200;
	response->info.connection_close = 1;
	response->info.content_type = "application/json";

	/* Guard against reentrancy */
	if (webconsole_exec_busy) {
		response->info.code = 503;
		response->data = "{\"error\":\"busy\"}\n";
		response->size = strlen(response->data);
		return;
	}

	if (!request || request->method != HTTP_POST) {
		response->info.code = 405;
		response->data = "{\"error\":\"method\"}\n";
		response->size = strlen(response->data);
		return;
	}

	if (failsafe_webconsole_require_token(request, response))
		return;

	ret = failsafe_webconsole_ensure_recording();
	if (ret) {
		response->info.code = 503;
		response->data = "{\"error\":\"no_console\"}\n";
		response->size = strlen(response->data);
		return;
	}

	cmdv = httpd_request_find_value(request, "cmd");
	if (!cmdv || !cmdv->data || !cmdv->size) {
		response->info.code = 400;
		response->data = "{\"error\":\"no_cmd\"}\n";
		response->size = strlen(response->data);
		return;
	}

	memset(cmd, 0, sizeof(cmd));
	memcpy(cmd, cmdv->data, min((size_t)WEB_CONSOLE_CMD_MAX, cmdv->size));

	/*
	 * Set a busy flag so poll requests do not consume output
	 * mid-command.  The output buffer was already enlarged to
	 * WEB_CONSOLE_EXEC_BUF_SIZE by ensure_recording().
	 */
	webconsole_exec_busy = true;

	/* Echo to console so browser sees what was executed */
	{
		const char *prompt = failsafe_get_prompt();
		size_t plen = prompt ? strlen(prompt) : 0;
		bool need_space = plen && prompt[plen - 1] != ' ' &&
				  prompt[plen - 1] != '\t';

		if (!prompt || !prompt[0])
			prompt = "MTK> ";

		printf("%s%s%s\n", prompt, need_space ? " " : "", cmd);
	}
	ret = run_command(cmd, 0);
	{
		const char *prompt = failsafe_get_prompt();

		if (!prompt || !prompt[0])
			prompt = "MTK> ";

		if (prompt[0] != '\n')
			printf("\n%s", prompt);
		else
			printf("%s", prompt);
	}

	webconsole_exec_busy = false;

	esc_sz = strlen(cmd) * 2 + 64;
	esc = malloc(esc_sz);
	if (!esc)
		goto out_oom;

	json_escape(esc, esc_sz, cmd);
	json_sz = strlen(esc) + 128;
	json = malloc(json_sz);
	if (!json)
		goto out_oom;

	snprintf(json, json_sz, "{\"ok\":true,\"ret\":%d,\"cmd\":\"%s\"}\n", ret, esc);
	free(esc);

	response->data = json;
	response->size = strlen(json);
	response->session_data = json;
	return;

out_oom:
	webconsole_exec_busy = false;
	free(esc);
	free(json);
	response->info.code = 500;
	response->data = "{\"error\":\"oom\"}\n";
	response->size = strlen(response->data);
}

void webconsole_clear_handler(enum httpd_uri_handler_status status,
	struct httpd_request *request,
	struct httpd_response *response)
{
	char *json;

	if (status == HTTP_CB_CLOSED) {
		failsafe_free_session(status, response);
		return;
	}

	if (status != HTTP_CB_NEW)
		return;

	response->status = HTTP_RESP_STD;
	response->info.code = 200;
	response->info.connection_close = 1;
	response->info.content_type = "application/json";

	if (!request || request->method != HTTP_POST) {
		response->info.code = 405;
		response->data = "{\"error\":\"method\"}\n";
		response->size = strlen(response->data);
		return;
	}

	if (failsafe_webconsole_require_token(request, response))
		return;

	if (failsafe_webconsole_ensure_recording()) {
		response->info.code = 503;
		response->data = "{\"error\":\"no_console\"}\n";
		response->size = strlen(response->data);
		return;
	}

	console_record_reset();

	json = malloc(64);
	if (!json) {
		response->info.code = 500;
		response->data = "{\"error\":\"oom\"}\n";
		response->size = strlen(response->data);
		return;
	}

	snprintf(json, 64, "{\"ok\":true}\n");
	response->data = json;
	response->size = strlen(json);
	response->session_data = json;
}

#ifdef CONFIG_WEBUI_FAILSAFE_CONSOLE
void console_register_handlers(struct httpd_instance *inst)
{
	/* Enable recording early so we can stream output to the browser */
	failsafe_webconsole_ensure_recording();
	httpd_register_uri_handler(inst, "/console.html", &html_handler, NULL);
	httpd_register_uri_handler(inst, "/console_js.js", &js_handler, NULL);
	httpd_register_uri_handler(inst, "/console/poll", &webconsole_poll_handler, NULL);
	httpd_register_uri_handler(inst, "/console/exec", &webconsole_exec_handler, NULL);
	httpd_register_uri_handler(inst, "/console/clear", &webconsole_clear_handler, NULL);
}
#endif
