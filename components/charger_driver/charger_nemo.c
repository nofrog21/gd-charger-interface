#include "autoconf.h"
#include "charger.h"
#include "sys_clock.h"
#define LOG_ENABLE
#include "log.h"

#if defined(CONFIG_GD_TARGET_GD32F303)
#include "gd32f30x_gpio.h"
#elif defined(CONFIG_GD_TARGET_GD32F103)
#include "gd32f10x_gpio.h"
#endif

#include <assert.h>
#include <string.h>

#ifndef UNREACHABLE
#define UNREACHABLE __asm__ ("BKPT")
#endif

const uint8_t CHRG_MAX_CURRENT = 30;
const uint8_t CHRG_MIN_CURRENT = 5;
const uint8_t INSTANCE = 1;

#define TOGGLE_POWER_GPIO BIT(0)

#ifndef ARRAY_SIZE
#  define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

// Настройки зарядного устройства
#define DB_START 147
// "Имя" зарядного устройства, по умолчанию с0
static uint8_t CHRG_NAME = 0xc0;
#define STATUS_MSG_SZ 0x1E + 6
static uint8_t status_msg[STATUS_MSG_SZ];
#define SETTINGS_MSG_SZ 0xFF + 6
// Не использовать вне вспомогательных define
static uint8_t charger_settings[SETTINGS_MSG_SZ];
static uint16_t cached_settings[9];
static uint16_t cached_settings_regs[9];
static size_t cached_settings_sz = 0;
static size_t cached_settings_epos = 0;
// Адреса регистров, используются для получения и установки настроек
#define BATTERY_TYPE_REG             0x0c
#define BATTERY_CAP_REG              0x0d
#define MAX_CHARGE_RATE_REG          0x11
#define ABS_VOLTAGE_REG              0x13
#define FLOAT_VOLTAGE_REG            0x14
// Необходимо добавлять 1 к адресам
#define SMART_DISCONNECT_VOLTAGE_REG 0x29
#define SMART_CONNECT_DIFF_REG       0x2a
#define CONV_DISCONNECT_VOLTAGE_REG  0x2b
#define CONV_CONNECT_DIFF_REG        0x2c

#define DEFAULT_TURNOFF_V 1280
#define TIME_PER_BYTE 2 /* ms */

/**
 * @breif Вспомогательные define, используются для обращения к массиву charger_settings
 */
#define charger_settings_high(i)                                         \
	charger_settings[DB_START + ((i) >= SMART_DISCONNECT_VOLTAGE_REG \
	                                    ? (i) + 1                    \
					    : (i)) *                     \
	                                2]
#define charger_settings_low(i) \
	charger_settings        \
	    [DB_START +         \
	        ((i) >= SMART_DISCONNECT_VOLTAGE_REG ? (i) + 1 : (i)) * 2 + 1]
#define charger_settings_u16(i) \
	(uint16_t) (charger_settings_high((i)) << 8 | charger_settings_low((i)))

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

/**
 * Returns:
 *     0  - success
 *     -1 - request_failed
 *     -2 - timeout
 */
static inline int handle_rs485_receive_err(int err)
{
	switch (err) {
	case 0:
		break;
	case -RS485_ETIMEOUT:
		return -2;
	case -RS485_EPERIPH: {
		volatile uint32_t rs485_err_mask =
		    rs485_get_errors();
		(void) rs485_err_mask;
		return -1;
	}
	default:
		UNREACHABLE;
	}
	return 0;
}

/**
 * Requests charger address
 * Returns:
 *     0 - success
 *     -1 - request failed
 *     -2 - timeout
 */
static int request_address(uint8_t *address)
{
	int err;
	static uint8_t find_charger_msg[10] =
	    { 0xf7, 0xff, 0x00, 0x07, 0x00, 0x02, 0x00, 0x01, 0xd4, 0x66};
	static uint8_t responce_msg[10];
	rs485_clear();
	if ((err = rs485_request_msg(find_charger_msg, ARRAY_SIZE(find_charger_msg))) < 0) {
		ERROR("failed to send request");
		return -1;
	}
	err = rs485_receive_msg(responce_msg, ARRAY_SIZE(responce_msg),
	    10 + ARRAY_SIZE(responce_msg) * TIME_PER_BYTE);
	if ((err = handle_rs485_receive_err(err)) < 0) {
		return err;
	}
	if ((err = check_message_checksum(responce_msg, ARRAY_SIZE(responce_msg)))) {
		return -1;
	}
	const uint8_t *expected_msg =
	    (uint8_t []) {responce_msg[0], 0xff, 0x80, 0x07, 0x00, 0x02, 0x00, responce_msg[0]};
	if (memcmp(expected_msg, responce_msg, ARRAY_SIZE(responce_msg) - 2)) {
		return -1;
	}
	*address = responce_msg[0];
	return 0;
}

/**
 * Requests status message (code 0x03) from charger
 * Returns:
 *     0  - success
 *     -1 - request failed
 *     -2 - timeout
 */
static int request_status_msg(uint8_t chrg_address,
    uint16_t start_reg,
    uint8_t responce_data_sz,
    uint8_t *responce_msg)
{
	int err;
	static uint8_t request_msg[8] = {0x00, 0x03};
	// prepare and send request
	request_msg[0] = chrg_address;
	request_msg[2] = start_reg >> 8;
	request_msg[3] = start_reg & 0xff;
	request_msg[5] = responce_data_sz;
	uint16_t crc = calc_checksum(request_msg,
	    ARRAY_SIZE(request_msg) - 2);
	request_msg[ARRAY_SIZE(request_msg) - 2] = crc >> 8;
	request_msg[ARRAY_SIZE(request_msg) - 1] = crc & 0xff;
	rs485_clear();
	err = rs485_request_msg(request_msg, ARRAY_SIZE(request_msg));
	if (err < 0) return -1;

	// receive header
	size_t header_size = 3 + sizeof (responce_data_sz);
	err = rs485_receive_msg(responce_msg,
	    header_size,
	    10 + header_size * TIME_PER_BYTE);
	if ((err = handle_rs485_receive_err(err)) < 0) {
		return err;
	}
	// match header
	if (chrg_address == responce_msg[0] && 0x03 == responce_msg[1]) return -1;
	if (responce_msg[3] != responce_data_sz) return -1;

	// receive data
	err = rs485_receive_msg(responce_msg + header_size,
	    responce_data_sz + 2,
	    (responce_data_sz + 2) * TIME_PER_BYTE);
	if ((err = handle_rs485_receive_err(err)) < 0) {
		return err;
	}
	if ((err = check_message_checksum(responce_msg, header_size + responce_data_sz + 2)) < 0) {
		ERROR("received message with incorrect checksum");
		return -1;
	}
	return 0;
}

/**
 * Requests set setting (0x06)
 * Returns:
 *     0  - success
 *     -1 - request failed
 *     -2 - timeout
 */
static int request_setting_msg(uint8_t chrg_address,
    uint16_t reg_address,
    const uint8_t *data,
    uint16_t data_sz)
{
	int err;
	static uint8_t request_msg[11] = {0x00, 0x06};
	static uint8_t responce_msg[11];
	// prepare and send request
	size_t request_msg_sz = data_sz + 2 + sizeof (reg_address) + 2;
	assert(ARRAY_SIZE(request_msg) < request_msg_sz);
	request_msg[0] = chrg_address;
	request_msg[2] = reg_address >> 8;
	request_msg[3] = reg_address & 0xff;
	memcpy(request_msg + 3, data, data_sz);
	uint16_t crc = calc_checksum(request_msg, request_msg_sz - 2);
	request_msg[request_msg_sz - 2] = crc >> 8;
	request_msg[request_msg_sz - 1] = crc & 0xff;
	rs485_clear();
	err = rs485_request_msg(request_msg, request_msg_sz);
	if (err < 0) {
		return -1;
	}

	// receive responce
	err = rs485_receive_msg(responce_msg,
	    request_msg_sz,
	    50 + request_msg_sz * TIME_PER_BYTE);
	if ((err = handle_rs485_receive_err(err)) < 0) {
		return err;
	}
	if (memcmp(request_msg, responce_msg, request_msg_sz) != 0) {
		return -1;
	}
	return 0;
}

int chrg_get_rt_data(struct chrg_runtime_data *rt_data)
{
#define LOG_TAG "chrg_get_rt_data"
	static uint8_t charger_manual_turnoff = 0;
	static int64_t charger_manual_turnoff_time = 0;
	int err;
	if (charger_manual_turnoff &&
	    (sys_clock_get_ms() - charger_manual_turnoff_time) > 5000)
	{
		chrg_toggle_power(1);
		charger_manual_turnoff = 0;
	}
	err = request_status_msg(CHRG_NAME, 0x0100, 0x1e, status_msg);
	if (err < 0)
		return -1;
	if (bytetos(status_msg[8], status_msg[9]) < DEFAULT_TURNOFF_V) {
		chrg_toggle_power(0);
		charger_manual_turnoff = 1;
		charger_manual_turnoff_time = sys_clock_get_ms();
		return -1;
	}
	if (!rt_data)
		return 0;
	rt_data->in_volt = bytetos(status_msg[8], status_msg[9]) * 0.01f;
	rt_data->in_curr = bytetos(status_msg[10], status_msg[11]) * 0.01f;
	rt_data->out_volt = bytetos(status_msg[12], status_msg[13]) * 0.01f;
	rt_data->out_curr = bytetos(status_msg[14], status_msg[15]) * 0.01f;
	switch (status_msg[30] << 8 | status_msg[31]) {
	case 0x2008:
		rt_data->state = CHRG_BULK;
		break;
	case 0x2009:
		rt_data->state = CHRG_ABSORPTION;
		break;
	case 0x200A:
		rt_data->state = CHRG_FLOW_LEVEL;
		break;
	case 0x200F:
		rt_data->state = CHRG_NO_CHARGING;
		break;
	case 0x201F:
		rt_data->state = CHRG_OUT_VOLTAGE_LOW_ERR;
		break;
	case 0x202F:
		rt_data->state = CHRG_OUT_VOLTAGE_HIGH_ERR;
		break;
	case 0xA008:
		rt_data->state = CHRG_INPUT_VOLTAGE_HIGH_ERR;
		break;
	default:
		rt_data->state = CHRG_UNKNOWN;
	}
	return 0;
#undef LOG_TAG
}

void chrg_get_config(struct chrg_config *config)
{
	config->algo = THREE_STAGE;
	config->bt_type = LFP;
	config->max_chrg_current =
	    charger_settings_u16(BATTERY_CAP_REG) * 0.01f;
}

int chrg_find()
{
	int err = 0;
	gpio_init(GPIOA,
	    GPIO_MODE_OUT_PP,
	    GPIO_OSPEED_50MHZ,
	    TOGGLE_POWER_GPIO);
	chrg_toggle_power(1);
	err = request_address(&CHRG_NAME);
	if (err < 0) return -1;
	err = request_status_msg(CHRG_NAME, 0x0000, 0xFF, charger_settings);
	if (err < 0) return -1;
	uint16_t smart_connect_val =
	    charger_settings_u16(SMART_CONNECT_DIFF_REG);
	uint16_t smart_disconnect_val =
	    charger_settings_u16(SMART_DISCONNECT_VOLTAGE_REG);
	if (smart_connect_val + smart_disconnect_val < 1320) {
		// Напряжение включения меньше 1320В
		cached_settings[cached_settings_sz] =
		    1320 - charger_settings_u16(SMART_DISCONNECT_VOLTAGE_REG);
		cached_settings_regs[cached_settings_sz] =
		    charger_settings_u16(SMART_DISCONNECT_VOLTAGE_REG);
		cached_settings_sz += 1;
		err = chrg_flush_settings();
		if (err) {
			return -1;
		}
	}
	if (charger_settings_u16(MAX_CHARGE_RATE_REG) != 10) {
		// Настройки установятся при первой попытке установить их пользователем
		uint16_t new_battery_bank_sz =
		    charger_settings_u16(BATTERY_CAP_REG) *
		    charger_settings_u16(MAX_CHARGE_RATE_REG) * 0.1f;
		cached_settings[cached_settings_sz] = new_battery_bank_sz;
		cached_settings_regs[cached_settings_sz] = BATTERY_CAP_REG;
		cached_settings[cached_settings_sz + 1] = 0x000a;
		cached_settings_regs[cached_settings_sz + 1] =
		    MAX_CHARGE_RATE_REG;
		cached_settings_sz += 2;
	}
	return 0;
}

int chrg_set_max_chrg_current(uint8_t value)
{
	if (value < CHRG_MIN_CURRENT || value > CHRG_MAX_CURRENT) {
		return -1;
	}
	cached_settings[cached_settings_sz] = value * 100;
	cached_settings_regs[cached_settings_sz++] = BATTERY_CAP_REG;
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

int chrg_flush_settings()
{
	int err;
	for (size_t i = cached_settings_epos; i < cached_settings_sz; ++i) {
		err = request_setting_msg(CHRG_NAME,
		    cached_settings_regs[i],
		    (uint8_t []) {cached_settings[i] >> 8,
				cached_settings[i] & 0xff},
		    sizeof (cached_settings[i]));
		if (err) {
			cached_settings_epos = i;
			return -1;
		}
		// Обновляем значения параметров после успешной записи
		charger_settings_high(cached_settings_regs[i]) =
		    cached_settings[i] >> 8;
		charger_settings_low(cached_settings_regs[i]) =
		    cached_settings[i] & 0xff;
	}
	chrg_clear_settings();
	return 0;
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
