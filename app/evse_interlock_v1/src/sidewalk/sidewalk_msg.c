/*
 * [3P-GLUE] Sidewalk message helper (alloc/copy/free around SDK APIs).
 * [BOILERPLATE] Typical ownership pattern for SDK message buffers.
 */
#include "sidewalk/sidewalk_msg.h"

#include "sidewalk.h"

#include <sid_hal_memory_ifc.h>
#include <zephyr/logging/log.h>
#include <errno.h>
#include <string.h>

LOG_MODULE_REGISTER(sidewalk_msg, CONFIG_SIDEWALK_LOG_LEVEL);

static void sidewalk_msg_free_ctx(void *ctx)
{
	/* [BOILERPLATE] Free payload/context allocated for SDK send. */
	sidewalk_msg_t *msg = (sidewalk_msg_t *)ctx;
	if (!msg) {
		return;
	}
	if (msg->msg.data) {
		sid_hal_free(msg->msg.data);
	}
	sid_hal_free(msg);
}

int sidewalk_send_msg_copy(const struct sid_msg_desc *desc, const void *payload, size_t len)
{
	/* THIRD-PARTY BOUNDARY - DO NOT MODIFY: uses Sidewalk SDK memory APIs. */
	if (!desc || !payload || len == 0) {
		return -EINVAL;
	}

	sidewalk_msg_t *msg = sid_hal_malloc(sizeof(*msg));
	if (!msg) {
		LOG_ERR("Failed to alloc msg context");
		return -ENOMEM;
	}
	memset(msg, 0x0, sizeof(*msg));

	msg->msg.size = len;
	msg->msg.data = sid_hal_malloc(msg->msg.size);
	if (!msg->msg.data) {
		LOG_ERR("Failed to alloc msg data");
		sid_hal_free(msg);
		return -ENOMEM;
	}
	memcpy(msg->msg.data, payload, msg->msg.size);
	msg->desc = *desc;

	int err = sidewalk_event_send(sidewalk_event_send_msg, msg, sidewalk_msg_free_ctx);
	if (err) {
		sidewalk_msg_free_ctx(msg);
	}
	return err;
}

int sidewalk_send_notify_json(const char *json, size_t len)
{
	/* [3P-GLUE] SDK message descriptor for uplink notify payloads. */
	struct sid_msg_desc desc = {
		.type = SID_MSG_TYPE_NOTIFY,
		.link_type = SID_LINK_TYPE_ANY,
		.link_mode = SID_LINK_MODE_CLOUD,
	};

	return sidewalk_send_msg_copy(&desc, json, len);
}
