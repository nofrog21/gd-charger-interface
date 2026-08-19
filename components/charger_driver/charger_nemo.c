#include "esp_timer.h"
#include "charger.h"
#include "driver/gpio.h"
#define LOG_ENABLE
#include "log.h"

#include <string.h>

const uint8_t CHRG_MAX_CURRENT = 30;
const uint8_t CHRG_MIN_CURRENT = 5;
const uint8_t INSTANCE = 1;

#define TOGGLE_POWER_GPIO     0
#define DRY_CTRL_GPIO 5

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof (x) / sizeof ((x)[0]))
#endif

// Настройки зарядного устройства
#define DB_START 147
// "Имя" зарядного устройства, по умолчанию с0
static uint8_t CHRG_NAME = 0xc0;
#define STATUS_MSG_SZ 36
static uint8_t status_msg[STATUS_MSG_SZ];
#define SETTINGS_MSG_SZ 262
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

static uint8_t find_charger_msg[10] =
{0xF7, 0xFF, 0x00, 0x07, 0x00, 0x02, 0x00, 0x01};

/**
 * @breif Вспомогательные define, используются для обращения к массиву charger_settings
 */
#define charger_settings_high(i)  \
	charger_settings[DB_START +       \
	((i) >= SMART_DISCONNECT_VOLTAGE_REG ? (i) + 1 : (i)) * 2]
#define charger_settings_low(i)   \
	charger_settings[DB_START +       \
	((i) >= SMART_DISCONNECT_VOLTAGE_REG ? (i) + 1 : (i)) * 2 + 1]
#define charger_settings_u16(i)   \
	(uint16_t) (charger_settings_high((i)) << 8 | \
	    charger_settings_low((i)))

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
			crc = ((crc & 0x0001) ? ((crc >> 1) ^ 0xA001) : (crc >> 1));
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
    return ((uint16_t)high_byte << 8) | low_byte;
}

static int request_msg(uint8_t *mmsg, uint32_t mlen,
    uint8_t *smsg, uint32_t *slen, uint64_t ms)
{
#define LOG_TAG "request_msg"
	int err = 0;
	int sz;
	uint16_t crc = calc_checksum(mmsg, mlen - 2);
	mmsg[mlen - 2] = crc & 0xff;
	mmsg[mlen - 1] = crc >> 8;
	if ((err = rs485_request_msg(mmsg, mlen))) {
		ERROR("failed to send request");
		return -1;
	}
	if ((sz = rs485_receive_msg(smsg, *slen, ms)) <= 0) {
		ERROR("received message size is <= 0");
		return -1;
	}
	if ((err = check_message_checksum(smsg, sz))) {
		ERROR("received message checksum is incorrect");
		return -1;
	}
	// TODO: check received message for correctness
	return 0;
#undef LOG_TAG
}

int chrg_get_rt_data(struct chrg_runtime_data *rt_data)
{
#define LOG_TAG "chrg_get_rt_data"
	static uint8_t charger_manual_turnoff = 0;
	static int64_t charger_manual_turnoff_time = 0;
	static uint8_t master_message[8] = {0x00, 0x03, 0x01, 0x00, 0x00, 0x1E};
	int err;
	uint32_t sz = STATUS_MSG_SZ;
	if (charger_manual_turnoff &&
	    (esp_timer_get_time() - charger_manual_turnoff_time) > 5000000) {
		chrg_toggle_power(1);
		charger_manual_turnoff = 0;
	}
	master_message[0] = CHRG_NAME;
	err = request_msg(
		master_message,
		ARRAY_SIZE(master_message),
		status_msg,
		&sz,
		150);
	if (err) return -1;
	if (bytetos(status_msg[8], status_msg[9]) < DEFAULT_TURNOFF_V) {
		chrg_toggle_power(0);
		charger_manual_turnoff = 1;
		charger_manual_turnoff_time = esp_timer_get_time();
		return -1;
	}
	if (!rt_data) return 0;
	rt_data->in_volt =
	    bytetos(status_msg[8], status_msg[9]) * 0.01f;
	rt_data->in_curr =
	    bytetos(status_msg[10], status_msg[11]) * 0.01f;
	rt_data->out_volt =
	    bytetos(status_msg[12], status_msg[13]) * 0.01f;
	rt_data->out_curr =
	    bytetos(status_msg[14], status_msg[15]) * 0.01f;
	switch (status_msg[30] << 8 | status_msg[31]) {
	case 0x2008: rt_data->state = CHRG_BULK; break;
	case 0x2009: rt_data->state = CHRG_ABSORPTION; break;
	case 0x200A: rt_data->state = CHRG_FLOW_LEVEL; break;
	case 0x200F: rt_data->state = CHRG_NO_CHARGING; break;
	case 0x201F: rt_data->state = CHRG_OUT_VOLTAGE_LOW_ERR; break;
	case 0x202F: rt_data->state = CHRG_OUT_VOLTAGE_HIGH_ERR; break;
	case 0xA008: rt_data->state = CHRG_INPUT_VOLTAGE_HIGH_ERR; break;
	default: rt_data->state = CHRG_UNKNOWN;
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
	gpio_set_direction(TOGGLE_POWER_GPIO, GPIO_MODE_OUTPUT);
	chrg_toggle_power(1);
	uint32_t sz = ARRAY_SIZE(status_msg);
	err = request_msg(
		find_charger_msg,
		ARRAY_SIZE(find_charger_msg),
		status_msg,
		&sz,
		150);
	CHRG_NAME = status_msg[0];
	if (err) return -1;
	static uint8_t master_message[8] = {0x00, 0x03, 0x00, 0x00, 0x00, 0xFF};
	master_message[0] = CHRG_NAME;
	sz = ARRAY_SIZE(charger_settings);
	err = request_msg(
		master_message,
		ARRAY_SIZE(master_message),
		charger_settings,
		&sz,
		250);
	if (err) return -1;
	// Напряжение включения меньше 1320В
	if (charger_settings_u16(SMART_CONNECT_DIFF_REG) +
	    charger_settings_u16(SMART_DISCONNECT_VOLTAGE_REG) < 1320) {
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
		cached_settings_regs[cached_settings_sz + 1] = MAX_CHARGE_RATE_REG;
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
	static uint8_t r_message[12];
	static uint8_t master_message[10] = {0x00, 0x06, 0x00, 0x00, 0x00, 0x02};
	uint32_t sz = ARRAY_SIZE(r_message);
	master_message[0] = CHRG_NAME;
	for (size_t i = cached_settings_epos; i < cached_settings_sz; ++i) {
		master_message[3] = cached_settings_regs[i];
		master_message[6] = cached_settings[i] >> 8;
		master_message[7] = cached_settings[i] & 0xFF;
		// TODO: write settings to charger
		err = request_msg(
			master_message,
			ARRAY_SIZE(master_message),
			r_message,
			&sz,
			150);
		if (err) {
			cached_settings_epos = i;
			return -1;
		}
		// Обновляем значения параметров после успешной записи
		charger_settings_high(cached_settings_regs[i]) = master_message[6];
		charger_settings_low(cached_settings_regs[i]) = master_message[7];
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
	gpio_set_level(TOGGLE_POWER_GPIO, status);
	LOG("toggle power to %d", status);
#undef LOG_TAG
}

#if 0 // deprecated

esp_err_t init_charger(ChargerInitConfig config)
{
#define LOG_TAG "init_charger"
	uart_setup(config.lock_handle);
#if 0
	gpio_config_t io_conf = {
		.intr_type = GPIO_INTR_DISABLE,
		.mode = GPIO_MODE_OUTPUT,
		.pin_bit_mask = TOGGLE_POWER_GPIO_SEL,
		.pull_down_en = 0,
		.pull_up_en = 0,
	};
	gpio_config(&io_conf);
#endif // Временно выключен
	// Подключение датчика температуры
#if 0
	gpio_set_direction(TOGGLE_POWER_GPIO, GPIO_MODE_OUTPUT);
	gpio_set_pull_mode(TEMP_PIN_GPIO, GPIO_PULLUP_ONLY);
	gpio_input_enable(TEMP_PIN_GPIO);
	size_t found;
	ds18x20_scan_devices(TEMP_PIN_GPIO, &temp_addr, 1, &found);
	if (!found) {
		ERROR("Temperature device not found");
	}
#endif // depricated
	toggle_power(1);
	// Ищем зарядное устройство
	wait_for_charger(status_msg, ARRAY_SIZE(status_msg));
	CHRG_NAME = *status_msg;
	// Получение первоначальных данных конфигурации
	{
		esp_err_t res = ESP_FAIL;
		while (1) {
			res = req_conf_msg();
			if (res != ESP_OK) {
				WARNING("Charger is disconnected");
			} else {
				break;
			}
			vTaskDelay(pdMS_TO_TICKS(1000));
		}
	}
	// Устанавливаем max_charger_rate в 0.10 для простой настройки тока
	if (charger_settings_u16(MAX_CHARGE_RATE) != 10) {
		// BUG: if set settings failed we have new values instead of old ones
		uint16_t new_battery_bank_sz =
		    charger_settings_u16(BATTERY_CAP) *
		    charger_settings_u16(MAX_CHARGE_RATE) * 0.1f;
		charger_settings_high(BATTERY_CAP) = new_battery_bank_sz & 0xFF;

		charger_settings_low(BATTERY_CAP) = new_battery_bank_sz >> 8;
		charger_settings_high(MAX_CHARGE_RATE) = 0x00;
		charger_settings_low(MAX_CHARGE_RATE) = 0x0A;
		uint16_t data_to_send[] = {
			charger_settings_u16(BATTERY_CAP),
			charger_settings_u16(MAX_CHARGE_RATE)
		};

		LOG("Battery cap: %hu; Max charge rate: %hu",
		    charger_settings_u16(BATTERY_CAP),
		    charger_settings_u16(MAX_CHARGE_RATE));
		int data_ids[] = {BATTERY_CAP, MAX_CHARGE_RATE};
		int err = set_settings(data_to_send,
		    ARRAY_SIZE(data_to_send), data_ids);
		if (err != -1) {
			// Попытаемся установить настройки еще раз, иначе вернем ошибку
			err = set_settings(data_to_send + err,
			    ARRAY_SIZE(data_to_send) - err, data_ids + err);
			if (err != -1)
				return ESP_FAIL;
		}
	}
	/* if (charger_settings_u16(SMART_CONNECT_DIFF) + */
	/*     charger_settings_u16(SMART_DISCONNECT_VOLTAGE) <= 1310) { */
	// Если напряжение включения стоит меньше 13.20В меняем его
	uint16_t data_to_send[] =
	    {1310 - charger_settings_u16(SMART_DISCONNECT_VOLTAGE)};
	int data_ids[] = {SMART_CONNECT_DIFF};
	int err = set_settings(data_to_send,
	    ARRAY_SIZE(data_to_send), data_ids);
	if (err != -1) {
		err = set_settings(data_to_send + err,
		    ARRAY_SIZE(data_to_send) - err, data_ids + err);
		if (err != -1)
			return ESP_FAIL;
	}
	return ESP_OK;
#undef LOG_TAG
}

int set_max_charging_current(uint8_t current)
{
#define LOG_TAG "set_max_charging_current"
	if (current >= 5) {
		if (charger_manual_turnoff) {
			LOG("Charger is turned off, enabling...");
			toggle_power(1);
			return 1;
		}
		uint16_t data_to_send[] = {current * 100};
		int data_ids[] = {BATTERY_CAP};
		return set_settings(data_to_send, ARRAY_SIZE(data_to_send), data_ids);
	}
	WARNING("Charger is turning off, current is %hhu", current);
	// Зарядное устройство выключается
	toggle_power(0);
	return ESP_OK;
#undef LOG_TAG
}

esp_err_t get_charger_state(ChargerState *state)
{
	if (!get_charger_status_msg()) {
		return ESP_FAIL;
	}
	state->input_voltage = bytetos(status_msg[8], status_msg[9]) / 100;
	state->input_current = bytetos(status_msg[10], status_msg[11]) / 100;
	state->output_voltage = bytetos(status_msg[12], status_msg[13]) / 100;
	state->output_current = bytetos(status_msg[14], status_msg[15]) / 100;
	switch (status_msg[30] << 8 | status_msg[31]) {
	case 0x2008: state->status = BULK; break;
	case 0x2009: state->status = ABSORPTION; break;
	case 0x200A: state->status = FLOW_LEVEL; break;
	case 0x200F: state->status = NO_CHARGING; break;
	case 0x201F: state->status = OUT_VOLTAGE_LOW_ERR; break;
	case 0x202F: state->status = OUT_VOLTAGE_HIGH_ERR; break;
	case 0xA008: state->status = INPUT_VOLTAGE_HIGH_ERR; break;
	}

	return ESP_OK;
}

void get_charger_config(ChargerConfig *config)
{
	config->alg = 2;
	config->mode = 0;
	// TODO: proper battery type deduction
	switch(charger_settings_u16(BATTERY_TYPE)) {
	case 4: config->battery_type = 3; break;
	default: config->battery_type = 2; break;
	}
	config->battery_bank_sz = charger_settings_u16(BATTERY_CAP) * 0.1f;
	config->max_current = charger_settings_u16(BATTERY_CAP) *
	    charger_settings_u16(MAX_CHARGE_RATE) * 0.001f;
}

void rv_c_assign_instance(uint8_t instance)
{
	charger_instance = instance;
}

/*!
 * Запрос конфигурационного сообщения
 */
static esp_err_t req_conf_msg()
{
	static uint8_t master_message[8] = {0x00, 0x03, 0x00, 0x00, 0x00, 0xFF};
	static uint8_t exp_header_data[] = {0x00, 0x03, 0x00, 0xFF};
	master_message[0] = CHRG_NAME;
	exp_header_data[0] = CHRG_NAME;
	MessageHeader exp_header = {
		.size = 4,
		.data = exp_header_data,
	};
	int sz = request_message(master_message,
	    ARRAY_SIZE(master_message), charger_settings, ARRAY_SIZE(charger_settings));
	return check_message(charger_settings, sz, &exp_header);
}

/*!
 * @breif Установить настройки зарядного устройства
 */
static int set_settings(const uint16_t *data_to_send, int sz,
    const int *data_ids)
{
#define LOG_TAG "set_settings"
	esp_err_t err;
	static uint8_t r_message[12];
	static uint8_t master_message[10] = {0x00, 0x06, 0x00, 0x00, 0x00, 0x02};
	master_message[0] = CHRG_NAME;
	for (size_t i = 0; i < sz; ++i) {
		if ((data_to_send[i] == 0xFFFF)) continue;
		master_message[3] = data_ids[i];
		master_message[6] = data_to_send[i] >> 8;
		master_message[7] = data_to_send[i] & 0xFF;
		MessageHeader exp_header = {
			.size = 6,
			.data = master_message,
		};

		int rm_sz = request_message(master_message,
		    ARRAY_SIZE(master_message), r_message, ARRAY_SIZE(r_message));
		err = check_message(r_message, rm_sz, &exp_header);
		if (err != ESP_OK) {
			ERROR("0x%.2X failed", data_ids[i]);
			return i;
		}
		// Обновляем значения параметров после успешной записи
		charger_settings_high(data_ids[i]) = master_message[6];
		charger_settings_low(data_ids[i]) = master_message[7];
	}
	return -1;
#undef LOG_TAG
}

/*!
 * @brief Проверка сообщения на корректность полей заголовка
 *
 * @return
 *     - ESP_OK: Все хорошо
 *     - ESP_ERR_INVALID_RESPONSE: Некорректный ответ от устройства
 *     - ESP_FAIL: Не верный формат ответа
 */
static esp_err_t check_message(const uint8_t *data, int size,
    const MessageHeader *exp_header)
{
#define LOG_TAG "check_message"
	esp_err_t res = check_message_checksum(data, size);
	if (res != ESP_OK) {
		return ESP_FAIL;
	}
	bool broadcast = (*exp_header->data == 0);
	if ((size == exp_header->size +
		exp_header->data[exp_header->size - 1] + 2) &&
	    compare_messages(exp_header->data + broadcast,
		data + broadcast,
		exp_header->size - broadcast))
	{
		return ESP_OK;
	}
	ERROR("Headers are not equal");
	return ESP_ERR_INVALID_RESPONSE;
#undef LOG_TAG
}

static void toggle_power(uint8_t status)
{
	gpio_set_level(TOGGLE_POWER_GPIO, status);
	charger_manual_turnoff = !status;
}

/**
 * @breif Чтение сообщения состояния зарядного устройства и запись в глобальный буфер
 *
 * @return
 *     - true: Состояние зарядного устройства обновлено
 *     - false: Зарядное устройство не отвечает на команды или ошибка при чтении ответа
 */
static bool get_charger_status_msg()
{
#define LOG_TAG "get_charger_status_msg"
	static uint8_t master_message[8] = {0x00, 0x03, 0x01, 0x00, 0x00, 0x1E};
	static uint8_t exp_header_data[] = {0x00, 0x03, 0x00, 0x1E};
	static uint64_t last_time_called = 0;
	static bool ok = false;
	static int cnt = 0;
	static bool should_enable = 1;
	uint64_t time_diff = esp_timer_get_time() - last_time_called;
	// Если вызов раньше чем периуд отправки статуса вернуть последний результат
	if (time_diff < 1ll * STATUS_MESSAGE_PERIOD * 1000) {
		return ok;
	}
	last_time_called = esp_timer_get_time();
	master_message[0] = CHRG_NAME;
	exp_header_data[0] = CHRG_NAME;
	MessageHeader exp_header = {
		.size = 4,
		.data = exp_header_data,
	};
	int sz = request_message(master_message, ARRAY_SIZE(master_message),
	    status_msg,
	    ARRAY_SIZE(status_msg));
	esp_err_t res = check_message(status_msg, sz, &exp_header);

	if (res != ESP_OK) {
		charger_state = 0;
		charger_errors = 0xFFFF;
		if (should_enable) {
			toggle_power(1);
			should_enable = 0;
		}
		ok = false;
		return ok;
	}

	charger_state = 1;
	uint16_t inv = bytetos(status_msg[8], status_msg[9]);
	if (inv < DEFAULT_TURNOFF_V) {
		++cnt;
		if (cnt > 3) {
			should_enable = 1;
			WARNING("Charger is turning off, voltage: %f", inv * 0.01f);
			toggle_power(0);
			cnt = 0;
		}
	} else {
		cnt = 0;
	}
	charger_errors = bytetos(status_msg[30], status_msg[31]);
	ok = true;
	return ok;
#undef LOG_TAG
}

/*!
 * @breif Ждем включения зарядного устройства
 *
 */
static void wait_for_charger(uint8_t *data, size_t length)
{
#define LOG_TAG "wait_for_charger"
	MessageHeader exp_header = {
		.size = 6,
		.data = (uint8_t []) {0x00, 0xFF, 0x80, 0x07, 0x00, 0x02},
	};
	esp_err_t res = ESP_FAIL;
	while (1) {
		int sz = request_message(find_charger_msg,
		    ARRAY_SIZE(find_charger_msg),
		    data, length);
		res = check_message(data, sz, &exp_header);
		if (res != ESP_OK) {
			WARNING("Charger is not found");
			vTaskDelay(pdMS_TO_TICKS(1000));
		} else {
			break;
		}
	}
#undef LOG_TAG
}

#endif
