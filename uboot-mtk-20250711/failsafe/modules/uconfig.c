// SPDX-License-Identifier: GPL-2.0+
/* Redacted Keenetic U-Config Web management for Viettel NR3053. */

#include <errno.h>
#include <malloc.h>
#include <net/mtk_httpd.h>
#include <nr3053_uconfig.h>

#include "../failsafe_internal.h"

#define UCONFIG_BACKUP_CHUNK	0x10000

struct uconfig_backup_session {
	size_t offset;
	char header[320];
	int header_len;
	u8 *image;
};

static const char *json_bool(bool value)
{
	return value ? "true" : "false";
}

static const char *uconfig_recommendation(const struct nr3053_uconfig_status *status)
{
	if (status->healthy)
		return "none";
	if (status->active.identity_valid != status->recovery.identity_valid)
		return "repair";
	if (!status->active.identity_valid && !status->recovery.identity_valid)
		return "provision";
	return "restore-or-review";
}

static void uconfig_status_json_handler(enum httpd_uri_handler_status cb_status,
	struct httpd_request *request, struct httpd_response *response)
{
	struct nr3053_uconfig_status status;
	char *json;
	int ret;

	if (cb_status == HTTP_CB_CLOSED) {
		failsafe_free_session(cb_status, response);
		return;
	}
	if (cb_status != HTTP_CB_NEW)
		return;
	if (!request || request->method != HTTP_GET) {
		failsafe_http_reply_json(response, 405,
			"{\"ok\":false,\"error\":\"method\"}\n");
		return;
	}

	ret = nr3053_uconfig_get_status(&status);
	if (ret) {
		failsafe_http_reply_json(response, 500,
			"{\"ok\":false,\"error\":\"status-unavailable\"}\n");
		return;
	}
	json = malloc(1024);
	if (!json) {
		failsafe_http_reply_json(response, 500,
			"{\"ok\":false,\"error\":\"oom\"}\n");
		return;
	}
	snprintf(json, 1024,
		 "{\"ok\":true,\"policy_country\":\"VN\","
		 "\"image_size\":%u,"
		 "\"active\":{\"crc\":[%s,%s],\"identity_valid\":%s,"
		 "\"redundant\":%s,\"country_vn\":%s,"
		 "\"recovery_defaults\":%s},"
		 "\"recovery\":{\"crc\":[%s,%s],\"identity_valid\":%s,"
		 "\"redundant\":%s,\"country_vn\":%s,"
		 "\"recovery_defaults\":%s},"
		 "\"copies_identical\":%s,\"healthy\":%s,"
		 "\"recommendation\":\"%s\",\"values_redacted\":true}\n",
		 NR3053_UCONFIG_IMAGE_SIZE,
		 json_bool(status.active.crc[0]),
		 json_bool(status.active.crc[1]),
		 json_bool(status.active.identity_valid),
		 json_bool(status.active.redundant),
		 json_bool(status.active.country_vn),
		 json_bool(status.active.recovery_defaults),
		 json_bool(status.recovery.crc[0]),
		 json_bool(status.recovery.crc[1]),
		 json_bool(status.recovery.identity_valid),
		 json_bool(status.recovery.redundant),
		 json_bool(status.recovery.country_vn),
		 json_bool(status.recovery.recovery_defaults),
		 json_bool(status.copies_identical), json_bool(status.healthy),
		 uconfig_recommendation(&status));
	failsafe_http_reply_json_alloc(response, 200, json, json);
}

static bool uconfig_form_equals(struct httpd_request *request,
				const char *name, const char *expected)
{
	struct httpd_form_value *value = httpd_request_find_value(request, name);
	size_t expected_len = strlen(expected);

	return value && value->data && value->size == expected_len &&
	       !memcmp(value->data, expected, expected_len);
}

static void uconfig_action_handler(enum httpd_uri_handler_status cb_status,
	struct httpd_request *request, struct httpd_response *response)
{
	struct nr3053_uconfig_status status;
	enum nr3053_uconfig_action action;
	char *json;
	int ret;

	if (cb_status == HTTP_CB_CLOSED) {
		failsafe_free_session(cb_status, response);
		return;
	}
	if (cb_status != HTTP_CB_NEW)
		return;
	if (!request || request->method != HTTP_POST ||
	    !uconfig_form_equals(request, "confirm", "PROVISION")) {
		failsafe_http_reply_json(response, 400,
			"{\"ok\":false,\"error\":\"confirmation-required\"}\n");
		return;
	}

	ret = nr3053_uconfig_provision(&action);
	if (ret) {
		failsafe_http_reply_json(response, 500,
			"{\"ok\":false,\"error\":\"provision-failed\"}\n");
		return;
	}
	ret = nr3053_uconfig_get_status(&status);
	if (ret) {
		failsafe_http_reply_json(response, 500,
			"{\"ok\":false,\"error\":\"verify-failed\"}\n");
		return;
	}
	json = malloc(160);
	if (!json) {
		failsafe_http_reply_json(response, 500,
			"{\"ok\":false,\"error\":\"oom\"}\n");
		return;
	}
	snprintf(json, 160,
		 "{\"ok\":true,\"action\":\"%s\",\"healthy\":%s,"
		 "\"values_redacted\":true}\n",
		 nr3053_uconfig_action_name(action), json_bool(status.healthy));
	failsafe_http_reply_json_alloc(response, 200, json, json);
}

static void uconfig_backup_free(struct uconfig_backup_session *session)
{
	if (!session)
		return;
	if (session->image) {
		memset(session->image, 0, NR3053_UCONFIG_IMAGE_SIZE);
		free(session->image);
	}
	memset(session, 0, sizeof(*session));
	free(session);
}

static void uconfig_backup_handler(enum httpd_uri_handler_status cb_status,
	struct httpd_request *request, struct httpd_response *response)
{
	struct uconfig_backup_session *session = response->session_data;
	enum nr3053_uconfig_slot slot;
	const char *filename;
	size_t remaining, chunk;
	int ret;

	if (cb_status == HTTP_CB_CLOSED) {
		uconfig_backup_free(session);
		response->session_data = NULL;
		return;
	}
	if (cb_status == HTTP_CB_RESPONDING) {
		if (!session || session->offset >= NR3053_UCONFIG_IMAGE_SIZE) {
			response->status = HTTP_RESP_NONE;
			return;
		}
		remaining = NR3053_UCONFIG_IMAGE_SIZE - session->offset;
		chunk = min_t(size_t, remaining, UCONFIG_BACKUP_CHUNK);
		response->status = HTTP_RESP_CUSTOM;
		response->data = (const char *)session->image + session->offset;
		response->size = chunk;
		session->offset += chunk;
		return;
	}
	if (cb_status != HTTP_CB_NEW)
		return;
	if (!request || request->method != HTTP_POST) {
		failsafe_http_reply_json(response, 405,
			"{\"ok\":false,\"error\":\"method\"}\n");
		return;
	}
	if (uconfig_form_equals(request, "slot", "active")) {
		slot = NR3053_UCONFIG_ACTIVE;
		filename = "NR3053_UConfig_active.bin";
	} else if (uconfig_form_equals(request, "slot", "recovery")) {
		slot = NR3053_UCONFIG_RECOVERY;
		filename = "NR3053_UConfig_res.bin";
	} else {
		failsafe_http_reply_json(response, 400,
			"{\"ok\":false,\"error\":\"bad-slot\"}\n");
		return;
	}

	session = calloc(1, sizeof(*session));
	if (!session)
		goto oom;
	session->image = malloc(NR3053_UCONFIG_IMAGE_SIZE);
	if (!session->image)
		goto oom;
	ret = nr3053_uconfig_read_slot(slot, session->image,
					NR3053_UCONFIG_IMAGE_SIZE);
	if (ret) {
		uconfig_backup_free(session);
		failsafe_http_reply_json(response, 500,
			"{\"ok\":false,\"error\":\"read-failed\"}\n");
		return;
	}
	session->header_len = snprintf(session->header, sizeof(session->header),
		"HTTP/1.1 200 OK\r\n"
		"Content-Type: application/octet-stream\r\n"
		"Content-Length: %u\r\n"
		"Content-Disposition: attachment; filename=\"%s\"\r\n"
		"Cache-Control: no-store\r\nConnection: close\r\n\r\n",
		NR3053_UCONFIG_IMAGE_SIZE, filename);
	response->session_data = session;
	response->status = HTTP_RESP_CUSTOM;
	response->data = session->header;
	response->size = session->header_len;
	return;

oom:
	uconfig_backup_free(session);
	failsafe_http_reply_json(response, 500,
		"{\"ok\":false,\"error\":\"oom\"}\n");
}

static void uconfig_restore_handler(enum httpd_uri_handler_status cb_status,
	struct httpd_request *request, struct httpd_response *response)
{
	struct httpd_form_value *file;
	struct nr3053_uconfig_status status;
	enum nr3053_uconfig_action action;
	char *json;
	int ret;

	if (cb_status == HTTP_CB_CLOSED) {
		failsafe_free_session(cb_status, response);
		return;
	}
	if (cb_status != HTTP_CB_NEW)
		return;
	if (!request || request->method != HTTP_POST ||
	    !uconfig_form_equals(request, "confirm", "RESTORE")) {
		failsafe_http_reply_json(response, 400,
			"{\"ok\":false,\"error\":\"confirmation-required\"}\n");
		return;
	}
	file = httpd_request_find_value(request, "uconfig");
	if (!file || !file->data || file->size != NR3053_UCONFIG_IMAGE_SIZE) {
		failsafe_http_reply_json(response, 400,
			"{\"ok\":false,\"error\":\"invalid-size\"}\n");
		return;
	}
	ret = nr3053_uconfig_restore(file->data, file->size, &action);
	if (ret) {
		failsafe_http_reply_json(response, ret == -EINVAL ? 400 : 500,
			"{\"ok\":false,\"error\":\"validation-or-write-failed\"}\n");
		return;
	}
	ret = nr3053_uconfig_get_status(&status);
	if (ret || !status.healthy) {
		failsafe_http_reply_json(response, 500,
			"{\"ok\":false,\"error\":\"post-verify-failed\"}\n");
		return;
	}
	json = malloc(160);
	if (!json) {
		failsafe_http_reply_json(response, 500,
			"{\"ok\":false,\"error\":\"oom\"}\n");
		return;
	}
	snprintf(json, 160,
		 "{\"ok\":true,\"action\":\"%s\",\"healthy\":true,"
		 "\"values_redacted\":true}\n",
		 nr3053_uconfig_action_name(action));
	failsafe_http_reply_json_alloc(response, 200, json, json);
}

void uconfig_register_handlers(struct httpd_instance *instance)
{
	httpd_register_uri_handler(instance, "/uconfig.html", &html_handler, NULL);
	httpd_register_uri_handler(instance, "/uconfig_js.js", &js_handler, NULL);
	httpd_register_uri_handler(instance, "/uconfig/status",
				  &uconfig_status_json_handler, NULL);
	httpd_register_uri_handler(instance, "/uconfig/action",
				  &uconfig_action_handler, NULL);
	httpd_register_uri_handler(instance, "/uconfig/backup",
				  &uconfig_backup_handler, NULL);
	httpd_register_uri_handler(instance, "/uconfig/restore",
				  &uconfig_restore_handler, NULL);
}
