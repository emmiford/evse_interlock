/*
 * Sidewalk message helper
 */
#ifndef SIDEWALK_MSG_H
#define SIDEWALK_MSG_H

#include <stddef.h>

#include <sid_api.h>

int sidewalk_send_notify_json(const char *json, size_t len);
int sidewalk_send_msg_copy(const struct sid_msg_desc *desc, const void *payload, size_t len);

#endif /* SIDEWALK_MSG_H */
