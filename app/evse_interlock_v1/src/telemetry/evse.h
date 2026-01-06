/*
 * EVSE sensing + state machine (J1772)
 */
#ifndef EVSE_H
#define EVSE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

enum evse_pilot_state {
	EVSE_PILOT_A = 0,
	EVSE_PILOT_B,
	EVSE_PILOT_C,
	EVSE_PILOT_D,
	EVSE_PILOT_E,
	EVSE_PILOT_F,
	EVSE_PILOT_UNKNOWN,
};

struct evse_event {
	bool send;
	enum evse_pilot_state pilot_state;
	bool proximity_detected;
	float pwm_duty_cycle;
	float current_draw_a;
	float energy_kwh;
	const char *event_type;
	const char *session_id;
};

struct evse_raw {
	enum evse_pilot_state pilot_state;
	bool proximity_detected;
	float pwm_duty_cycle;
	float current_draw_a;
	int pilot_mv;
};

int evse_init(void);
bool evse_poll(struct evse_event *evt, int64_t timestamp_ms);
int evse_read_raw(struct evse_raw *raw);
char evse_pilot_state_to_char(enum evse_pilot_state state);

#endif /* EVSE_H */
