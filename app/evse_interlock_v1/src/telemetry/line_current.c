/*
 * [LINE-CURRENT] ADC sampling + significant change detection.
 * [BOILERPLATE] Zephyr ADC configuration for SAADC.
 */
#include "telemetry/line_current.h"

#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <errno.h>
#if defined(CONFIG_ADC_NRFX_SAADC)
#include <hal/nrf_saadc.h>
#endif

LOG_MODULE_REGISTER(line_current, CONFIG_SIDEWALK_LOG_LEVEL);

#define ADC_RESOLUTION 12
#define ADC_GAIN ADC_GAIN_1_6
#define ADC_REFERENCE ADC_REF_INTERNAL
#define LINE_CURRENT_ADC_ACQ_TIME ADC_ACQ_TIME_DEFAULT

#define LINE_CURRENT_CH CONFIG_SID_END_DEVICE_LINE_CURRENT_ADC_CHANNEL
#define LINE_CURRENT_SCALE_NUM CONFIG_SID_END_DEVICE_LINE_CURRENT_SCALE_NUM
#define LINE_CURRENT_SCALE_DEN CONFIG_SID_END_DEVICE_LINE_CURRENT_SCALE_DEN
#define LINE_CURRENT_DELTA_MA CONFIG_SID_END_DEVICE_LINE_CURRENT_DELTA_MA

static const struct device *adc_dev;
static float last_current_a;
static bool current_initialized;

static int adc_read_channel(int channel, int16_t *out)
{
	struct adc_channel_cfg cfg = {
		.gain = ADC_GAIN,
		.reference = ADC_REFERENCE,
		.acquisition_time = LINE_CURRENT_ADC_ACQ_TIME,
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

static bool line_current_read_a(float *out)
{
	int16_t raw = 0;
	if (adc_read_channel(LINE_CURRENT_CH, &raw)) {
		return false;
	}
	int mv = adc_raw_to_mv(raw);
	int scaled = (mv * LINE_CURRENT_SCALE_NUM) / LINE_CURRENT_SCALE_DEN;
	*out = (float)scaled / 1000.0f;
	return true;
}

int line_current_init(void)
{
	adc_dev = DEVICE_DT_GET_ANY(nordic_nrf_saadc);
	if (!adc_dev || !device_is_ready(adc_dev)) {
		LOG_ERR("ADC not ready");
		return -ENODEV;
	}

	LOG_INF("Line current ADC channel: %d", LINE_CURRENT_CH);
	last_current_a = 0.0f;
	current_initialized = false;
	return 0;
}

bool line_current_poll(struct line_current_event *evt)
{
	if (!evt) {
		return false;
	}

	float current_a = 0.0f;
	if (!line_current_read_a(&current_a)) {
		return false;
	}

	evt->send = false;
	evt->current_a = current_a;
	evt->event_type = "current_change";

	if (current_initialized) {
		float delta_a = current_a - last_current_a;
		if (delta_a < 0.0f) {
			delta_a = -delta_a;
		}
		if (delta_a >= ((float)LINE_CURRENT_DELTA_MA / 1000.0f)) {
			evt->send = true;
		}
	} else {
		current_initialized = true;
	}

	last_current_a = current_a;
	return evt->send;
}
