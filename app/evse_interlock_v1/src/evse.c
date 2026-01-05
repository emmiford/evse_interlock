/*
 * EVSE sensing + state machine (J1772)
 */
#include "evse.h"

#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/util.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#if defined(CONFIG_ADC_NRFX_SAADC)
#include <hal/nrf_saadc.h>
#endif

LOG_MODULE_REGISTER(evse, CONFIG_SIDEWALK_LOG_LEVEL);

#define ADC_RESOLUTION 12
#define ADC_GAIN ADC_GAIN_1_6
#define ADC_REFERENCE ADC_REF_INTERNAL
#define EVSE_ADC_ACQ_TIME ADC_ACQ_TIME_DEFAULT

#define EVSE_PWM_PORT CONFIG_SID_END_DEVICE_EVSE_PWM_GPIO_PORT
#define EVSE_PWM_PIN CONFIG_SID_END_DEVICE_EVSE_PWM_GPIO_PIN
#define EVSE_PROX_PORT CONFIG_SID_END_DEVICE_EVSE_PROX_GPIO_PORT
#define EVSE_PROX_PIN CONFIG_SID_END_DEVICE_EVSE_PROX_GPIO_PIN

#define EVSE_PILOT_CH CONFIG_SID_END_DEVICE_EVSE_PILOT_ADC_CHANNEL
#define EVSE_CURRENT_CH CONFIG_SID_END_DEVICE_EVSE_CURRENT_ADC_CHANNEL

#define EVSE_PILOT_SCALE_NUM CONFIG_SID_END_DEVICE_EVSE_PILOT_SCALE_NUM
#define EVSE_PILOT_SCALE_DEN CONFIG_SID_END_DEVICE_EVSE_PILOT_SCALE_DEN
#define EVSE_PILOT_BIAS_MV CONFIG_SID_END_DEVICE_EVSE_PILOT_BIAS_MV

#define EVSE_CURRENT_SCALE_NUM CONFIG_SID_END_DEVICE_EVSE_CURRENT_SCALE_NUM
#define EVSE_CURRENT_SCALE_DEN CONFIG_SID_END_DEVICE_EVSE_CURRENT_SCALE_DEN

#define EVSE_NOMINAL_VOLTAGE_V CONFIG_SID_END_DEVICE_EVSE_NOMINAL_VOLTAGE_V
#define EVSE_PILOT_TOL_MV CONFIG_SID_END_DEVICE_EVSE_PILOT_TOLERANCE_MV

static const struct device *adc_dev;
static const struct device *pwm_gpio_dev;
static const struct device *prox_gpio_dev;
static struct gpio_callback pwm_cb;

static volatile int64_t pwm_last_rise_us;
static volatile int64_t pwm_last_high_us;
static volatile int64_t pwm_last_period_us;

static enum evse_pilot_state last_pilot_state = EVSE_PILOT_UNKNOWN;
static bool last_prox_state;
static float energy_kwh;
static char session_id[37];
static bool session_active;
static int64_t last_energy_ts_ms;

static int64_t cycles_to_us(uint32_t cycles)
{
	return (int64_t)k_cyc_to_us_floor64(cycles);
}

static void pwm_isr(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

	uint32_t now_cycles = k_cycle_get_32();
	int val = gpio_pin_get(pwm_gpio_dev, EVSE_PWM_PIN);
	int64_t now_us = cycles_to_us(now_cycles);

	if (val > 0) {
		if (pwm_last_rise_us > 0) {
			pwm_last_period_us = now_us - pwm_last_rise_us;
		}
		pwm_last_rise_us = now_us;
	} else {
		if (pwm_last_rise_us > 0) {
			pwm_last_high_us = now_us - pwm_last_rise_us;
		}
	}
}

static const struct device *gpio_dev_from_port(int port)
{
	switch (port) {
	case 0:
		return DEVICE_DT_GET(DT_NODELABEL(gpio0));
#if DT_NODE_HAS_STATUS(DT_NODELABEL(gpio1), okay)
	case 1:
		return DEVICE_DT_GET(DT_NODELABEL(gpio1));
#endif
	default:
		return NULL;
	}
}

static void log_gpio_mapping(const char *label, int port, int pin)
{
	LOG_INF("%s GPIO: P%d.%02d", label, port, pin);
}

static float pwm_get_duty_cycle(void)
{
	int64_t period = pwm_last_period_us;
	int64_t high = pwm_last_high_us;
	if (period <= 0 || high <= 0 || high > period) {
		return 0.0f;
	}
	return (float)high * 100.0f / (float)period;
}

static int adc_read_channel(int channel, int16_t *out)
{
	struct adc_channel_cfg cfg = {
		.gain = ADC_GAIN,
		.reference = ADC_REFERENCE,
		.acquisition_time = EVSE_ADC_ACQ_TIME,
		.channel_id = channel,
		.differential = 0,
	};
#if defined(CONFIG_ADC_NRFX_SAADC)
	cfg.input_positive = NRF_SAADC_INPUT_AIN0 + channel;
#endif
	int ret = adc_channel_setup(adc_dev, &cfg);
	if (ret) {
		return ret;
	}

	int16_t buf = 0;
	struct adc_sequence seq = {
		.channels = BIT(channel),
		.buffer = &buf,
		.buffer_size = sizeof(buf),
		.resolution = ADC_RESOLUTION,
	};
	ret = adc_read(adc_dev, &seq);
	if (ret) {
		return ret;
	}
	*out = buf;
	return 0;
}

static int adc_raw_to_mv(int16_t raw)
{
	int32_t mv = raw;
	(void)adc_raw_to_millivolts(ADC_REFERENCE, ADC_GAIN, ADC_RESOLUTION, &mv);
	return (int)mv;
}

static int pilot_mv_from_adc(void)
{
	int16_t raw = 0;
	if (adc_read_channel(EVSE_PILOT_CH, &raw)) {
		return 0;
	}
	int mv = adc_raw_to_mv(raw);
	/* scale and remove bias to recover negative range */
	int scaled = (mv * EVSE_PILOT_SCALE_NUM) / EVSE_PILOT_SCALE_DEN;
	return scaled - EVSE_PILOT_BIAS_MV;
}

static float current_a_from_adc(void)
{
	int16_t raw = 0;
	if (adc_read_channel(EVSE_CURRENT_CH, &raw)) {
		return 0.0f;
	}
	int mv = adc_raw_to_mv(raw);
	int scaled = (mv * EVSE_CURRENT_SCALE_NUM) / EVSE_CURRENT_SCALE_DEN;
	return (float)scaled / 1000.0f;
}

static enum evse_pilot_state pilot_state_from_mv(int mv)
{
	if (mv >= 12000 - EVSE_PILOT_TOL_MV) {
		return EVSE_PILOT_A;
	}
	if (mv >= 9000 - EVSE_PILOT_TOL_MV) {
		return EVSE_PILOT_B;
	}
	if (mv >= 6000 - EVSE_PILOT_TOL_MV) {
		return EVSE_PILOT_C;
	}
	if (mv >= 3000 - EVSE_PILOT_TOL_MV) {
		return EVSE_PILOT_D;
	}
	if (mv >= -1000 - EVSE_PILOT_TOL_MV) {
		return EVSE_PILOT_E;
	}
	return EVSE_PILOT_F;
}

char evse_pilot_state_to_char(enum evse_pilot_state state)
{
	switch (state) {
	case EVSE_PILOT_A:
		return 'A';
	case EVSE_PILOT_B:
		return 'B';
	case EVSE_PILOT_C:
		return 'C';
	case EVSE_PILOT_D:
		return 'D';
	case EVSE_PILOT_E:
		return 'E';
	case EVSE_PILOT_F:
		return 'F';
	default:
		return '?';
	}
}

static void session_id_new(void)
{
	uint32_t r[4] = {
		sys_rand32_get(),
		sys_rand32_get(),
		sys_rand32_get(),
		sys_rand32_get(),
	};
	snprintf(session_id, sizeof(session_id),
		 "%08x-%04x-%04x-%04x-%04x%04x%04x",
		 r[0],
		 (r[1] >> 16) & 0xFFFF,
		 r[1] & 0xFFFF,
		 (r[2] >> 16) & 0xFFFF,
		 r[2] & 0xFFFF,
		 (r[3] >> 16) & 0xFFFF,
		 r[3] & 0xFFFF);
}

int evse_init(void)
{
	adc_dev = DEVICE_DT_GET_ANY(nordic_nrf_saadc);
	if (!adc_dev || !device_is_ready(adc_dev)) {
		LOG_ERR("ADC not ready");
		return -ENODEV;
	}

	pwm_gpio_dev = gpio_dev_from_port(EVSE_PWM_PORT);
	prox_gpio_dev = gpio_dev_from_port(EVSE_PROX_PORT);
	if (!pwm_gpio_dev || !prox_gpio_dev || !device_is_ready(pwm_gpio_dev) ||
	    !device_is_ready(prox_gpio_dev)) {
		LOG_ERR("GPIO not ready");
		return -ENODEV;
	}

	log_gpio_mapping("EVSE PWM", EVSE_PWM_PORT, EVSE_PWM_PIN);
	log_gpio_mapping("EVSE PROX", EVSE_PROX_PORT, EVSE_PROX_PIN);
	LOG_INF("EVSE ADC channels: pilot=%d current=%d", EVSE_PILOT_CH, EVSE_CURRENT_CH);

	if (gpio_pin_configure(pwm_gpio_dev, EVSE_PWM_PIN, GPIO_INPUT)) {
		return -EINVAL;
	}
	if (gpio_pin_interrupt_configure(pwm_gpio_dev, EVSE_PWM_PIN, GPIO_INT_EDGE_BOTH)) {
		return -EINVAL;
	}
	gpio_init_callback(&pwm_cb, pwm_isr, BIT(EVSE_PWM_PIN));
	gpio_add_callback(pwm_gpio_dev, &pwm_cb);

	if (gpio_pin_configure(prox_gpio_dev, EVSE_PROX_PIN, GPIO_INPUT)) {
		return -EINVAL;
	}

	last_prox_state = gpio_pin_get(prox_gpio_dev, EVSE_PROX_PIN) > 0;
	last_pilot_state = EVSE_PILOT_UNKNOWN;
	energy_kwh = 0.0f;
	session_active = false;
	last_energy_ts_ms = 0;
	memset(session_id, 0, sizeof(session_id));
	return 0;
}

bool evse_poll(struct evse_event *evt, int64_t timestamp_ms)
{
	if (!evt) {
		return false;
	}

	int pilot_mv = pilot_mv_from_adc();
	enum evse_pilot_state state = pilot_state_from_mv(pilot_mv);
	bool prox = gpio_pin_get(prox_gpio_dev, EVSE_PROX_PIN) > 0;
	float current_a = current_a_from_adc();
	float duty = pwm_get_duty_cycle();

	/* energy accumulation during charge states */
	if (last_energy_ts_ms > 0 && (state == EVSE_PILOT_C || state == EVSE_PILOT_D)) {
		float dt_h = (float)(timestamp_ms - last_energy_ts_ms) / 3600000.0f;
		float power_kw = (current_a * (float)EVSE_NOMINAL_VOLTAGE_V) / 1000.0f;
		if (dt_h > 0) {
			energy_kwh += power_kw * dt_h;
		}
	}
	last_energy_ts_ms = timestamp_ms;

	evt->send = false;
	evt->pilot_state = state;
	evt->proximity_detected = prox;
	evt->pwm_duty_cycle = duty;
	evt->current_draw_a = current_a;
	evt->energy_kwh = energy_kwh;
	evt->session_id = session_id[0] ? session_id : NULL;
	evt->event_type = "state_change";

	if (state != last_pilot_state || prox != last_prox_state) {
		evt->send = true;
		if (last_pilot_state == EVSE_PILOT_A && state == EVSE_PILOT_B) {
			session_id_new();
			session_active = true;
			energy_kwh = 0.0f;
			evt->event_type = "session_start";
		} else if (session_active && state == EVSE_PILOT_A) {
			evt->event_type = "session_end";
			session_active = false;
		}
	}

	last_pilot_state = state;
	last_prox_state = prox;
	return evt->send;
}

int evse_read_raw(struct evse_raw *raw)
{
	if (!raw || !adc_dev || !pwm_gpio_dev || !prox_gpio_dev) {
		return -EINVAL;
	}
	raw->pilot_mv = pilot_mv_from_adc();
	raw->pilot_state = pilot_state_from_mv(raw->pilot_mv);
	raw->proximity_detected = gpio_pin_get(prox_gpio_dev, EVSE_PROX_PIN) > 0;
	raw->pwm_duty_cycle = pwm_get_duty_cycle();
	raw->current_draw_a = current_a_from_adc();
	return 0;
}
