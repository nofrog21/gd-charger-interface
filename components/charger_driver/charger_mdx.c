#include "gd32f30x_gpio.h"
#include "charger.h"
#define LOG_ENABLE
#include "log.h"

#include <string.h>

#define UNREACHABLE __asm__ ("BKPT")

#define HEADER_SZ         6
#define TOGGLE_POWER_GPIO 0

const uint8_t CHRG_MAX_CURRENT = 60;
const uint8_t CHRG_MIN_CURRENT = 5;
const uint8_t INSTANCE = 2;

#ifndef ARRAY_SIZE
#  define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

static uint8_t settings_msg[255];
static uint16_t cached_settings[4];
static uint16_t cached_settings_regs[4];
static size_t cached_settings_sz = 0;
static size_t cached_settings_epos = 0;

#define CHRG_START_36V_REG         96
#define CHRG_CURRENT_36V_REG       100
#define CHRG_DISCHARGE_END_36V_REG 102
#define CHRG_CURRENT_12V_REG       132

static uint8_t is_in_reverse = 0;

/*
 * Compute CRC-16/Modbus checksum
 * Should be reversed for big endian order
 */
static uint16_t calc_checksum(const uint8_t *data, int sz)
{
	uint16_t crc = 0xFFFF;
	for (int i = 0; i < sz; ++i) {
		crc ^= (uint16_t) data[i];
		for (size_t j = 0; j < 8; ++j) {
			crc = ((crc & 0x0001) ? ((crc >> 1) ^ 0xA001)
			                      : (crc >> 1));
		}
	}
	return crc;
}

static int check_message_checksum(const uint8_t *data, int size)
{
#define LOG_TAG "check_message_checksum"
	if (calc_checksum(data, size - 2) ==
	    ((uint16_t) (data[size - 1] << 8) | data[size - 2]))
	{
		return 0;
	}
	ERROR("Wrong checksum");
#ifdef LOG_ENABLE
	ESP_LOG_BUFFER_HEX(LOG_TAG, data, size);
#endif
	return -1;
#undef LOG_TAG
}

static inline uint16_t bytetos(uint8_t high_byte, uint8_t low_byte)
{
	return ((uint16_t) high_byte << 8) | low_byte;
}

// Will wait for responce and check it
static int request_msg(uint8_t *mmsg,
    uint32_t mlen,
    uint8_t *smsg,
    uint32_t *slen,
    uint64_t delay)
{
#define LOG_TAG "request_msg"
#define TIME_PER_BYTE 2 /* ms */
	int err = 0;
	uint16_t crc = calc_checksum(mmsg, mlen - 2);
	mmsg[mlen - 2] = crc & 0xff;
	mmsg[mlen - 1] = crc >> 8;
	if ((err = rs485_request_msg(mmsg, mlen))) {
		ERROR("failed to send request");
		return -1;
	}
	uint8_t message_sz;
	size_t header_size = 5 + sizeof (message_sz);
	err = rs485_receive_msg(smsg,
	    header_size,
	    delay + (header_size) * TIME_PER_BYTE);
	switch (err) {
	case 0:
		break;
	case -RS485_ETIMEOUT:
		return -2;
	case -RS485_EPERIPH: {
		volatile uint32_t rs485_err_mask = rs485_get_errors();
		// TODO:
		(void) rs485_err_mask;
		return -1;
	}
	default:
		UNREACHABLE;
	}
	size_t offset = header_size - sizeof (message_sz);
	message_sz = smsg[offset];
	if (message_sz > *slen) {
		return -1;
	}
	err = rs485_receive_msg(smsg + header_size,
	    message_sz - header_size,
	    (message_sz - header_size) * TIME_PER_BYTE);
	switch (err) {
	case 0:
		break;
	case -RS485_ETIMEOUT:
		return -2;
	case -RS485_EPERIPH:
		return -1;
	default:
		UNREACHABLE;
	}
	*slen = message_sz;
	if ((err = check_message_checksum(smsg, *slen))) {
		ERROR("received message checksum is incorrect");
		return -1;
	}
	if ((smsg[0] == 0x7e) && (smsg[1] == mmsg[2]) &&
	    (smsg[2] == mmsg[1]) && (smsg[3] == mmsg[3]) &&
	    (smsg[4] == mmsg[4]))
	{
		return 0;
	}
	ERROR("received message is formated incorrectly");
	return -1;
#undef LOG_TAG
}

int chrg_get_rt_data(struct chrg_runtime_data *rt_data)
{
#define LOG_TAG "chrg_get_rt_data"
#ifndef swap
#  define swap(first, last)               \
	  do {                            \
		  typeof(first) t = last; \
		  last = first;           \
		  first = t;              \
	  } while (0)
#endif
	static uint8_t m_message[12] = { 0x7E,
		0xFF,
		0x3A,
		0x03,
		0x00,
		0x0C,
		0x00,
		0x00,
		0x00,
		0xFF };
	static uint8_t responce[255];
	uint32_t sz = ARRAY_SIZE(responce);
	int err =
	    request_msg(m_message, ARRAY_SIZE(m_message), responce, &sz, 150);
	if (err)
		return -1;
	if (!rt_data)
		return 0;
	uint16_t curr = bytetos(responce[18], responce[19]);
	is_in_reverse = (curr >> 15) & 1;

	rt_data->state = CHRG_UNKNOWN;
	rt_data->in_volt = bytetos(responce[12], responce[13]) * 0.01f;
	rt_data->out_volt = bytetos(responce[14], responce[15]) * 0.01f;
	curr = bytetos(responce[16], responce[17]);
	if (curr >> 15 & 1)
		curr = ~(curr - 1);
	rt_data->in_curr = curr * 0.01f;
	curr = bytetos(responce[18], responce[19]);
	if (curr >> 15 & 1)
		curr = ~(curr - 1);
	rt_data->out_curr = curr * 0.01f;
	if (is_in_reverse) {
		swap(rt_data->out_volt, rt_data->in_volt);
		swap(rt_data->out_curr, rt_data->in_curr);
	}
	return 0;
#undef LOG_TAG
}

void chrg_get_config(struct chrg_config *config)
{
	config->algo = THREE_STAGE;
	config->bt_type = LFP;
	config->max_chrg_current =
	    bytetos(settings_msg[HEADER_SZ + CHRG_CURRENT_36V_REG],
	        settings_msg[HEADER_SZ + CHRG_CURRENT_36V_REG + 1]);
}

int chrg_set_max_chrg_current(uint8_t value)
{
	if (value < CHRG_MIN_CURRENT || value > CHRG_MAX_CURRENT) {
		return -1;
	}
	cached_settings[cached_settings_sz] = value;
	cached_settings_regs[cached_settings_sz++] = CHRG_CURRENT_36V_REG;
	if (cached_settings_sz == ARRAY_SIZE(cached_settings)) {
		return chrg_flush_settings();
	}
	return 0;
}

int chrg_set_battery_type(enum chrg_battery_type value)
{
	(void) value;
	return -1;
}

int chrg_find()
{
#define LOG_TAG "init_charger"
	//uart_setup(config.lock_handle);
	gpio_init(GPIOA,
	    GPIO_MODE_OUT_PP,
	    GPIO_OSPEED_50MHZ,
	    TOGGLE_POWER_GPIO);

	memset(settings_msg, 0, sizeof(settings_msg));
	chrg_toggle_power(1);
	/* Receive data from charger */
	static uint8_t m_message[12] = { 0x7e,
		0xff,
		0x3a,
		0x03,
		0x03,
		0x0c,
		0x00,
		0x00,
		0x00,
		0x46 };
	uint32_t sz = ARRAY_SIZE(settings_msg);
	return request_msg(m_message,
	    ARRAY_SIZE(m_message),
	    settings_msg,
	    &sz,
	    250);
#undef LOG_TAG
}

int chrg_flush_settings()
{
#define LOG_TAG "chrg_settings_flush"
	int err;
	static uint8_t r_message[12];
	static uint8_t
	    master_message[12] = { 0x7e, 0xff, 0x3a, 0x06, 0x06, 0x0c, 0x00 };
	uint32_t sz = ARRAY_SIZE(r_message);
	for (size_t i = cached_settings_epos; i < cached_settings_sz; ++i) {
		master_message[7] = cached_settings_regs[i] >> 1;
		master_message[8] = cached_settings[i] >> 8;
		master_message[9] = cached_settings[i] & 0xFF;
		err = request_msg(master_message,
		    ARRAY_SIZE(master_message),
		    r_message,
		    &sz,
		    150);
		if (err) {
			cached_settings_epos = i;
			return -1;
		}
		err = memcmp(master_message + 6, r_message + 6, 4);
		if (err) {
			cached_settings_epos = i;
			return -1;
		}
		settings_msg[HEADER_SZ + cached_settings_regs[i]] =
		    cached_settings[i] >> 8;
		settings_msg[HEADER_SZ + cached_settings_regs[i] + 1] =
		    cached_settings[i] & 0xFF;
	}
	chrg_clear_settings();
	return 0;
#undef LOG_TAG
}

void chrg_clear_settings()
{
	cached_settings_sz = 0;
	cached_settings_epos = 0;
}

void chrg_toggle_power(int status)
{
#define LOG_TAG "chrg_toggle_power"
	if (status) {
		gpio_bit_set(GPIOA, TOGGLE_POWER_GPIO);
	} else {
		gpio_bit_reset(GPIOA, TOGGLE_POWER_GPIO);
	}
	LOG("toggle power to %d", status);
#undef LOG_TAG
}
