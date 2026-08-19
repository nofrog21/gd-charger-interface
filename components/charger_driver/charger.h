#ifndef UART_H
#define UART_H
#include <stdint.h>
#include <stdbool.h>
#include "sdkconfig.h"
#include "rs485.h"

#if defined(CONFIG_CHARGER_TARGET_MDX)
#define CHRG_BIDIRECTIONAL
#endif

extern const uint8_t CHRG_MAX_CURRENT;
extern const uint8_t CHRG_MIN_CURRENT;
extern const uint8_t INSTANCE;

enum chrg_state {
	CHRG_OFF,
	CHRG_BULK,
	CHRG_ABSORPTION,
	CHRG_FLOW_LEVEL,
	CHRG_NO_CHARGING,
	CHRG_OUT_VOLTAGE_LOW_ERR,
	CHRG_OUT_VOLTAGE_HIGH_ERR,
	CHRG_INPUT_VOLTAGE_HIGH_ERR,
	CHRG_UNKNOWN,
};

enum chrg_algo {
	CONST_CC,
	CONST_V,
	TWO_STAGE,
	THREE_STAGE,
};

enum chrg_battery_type {
	AGM,
	LFP,
};

struct chrg_runtime_data {
	float in_volt;
	float out_volt;
	float in_curr;
	float out_curr;
	float temp;
	enum chrg_state state;
};

struct chrg_config {
	enum chrg_algo algo;
	enum chrg_battery_type bt_type;
	uint8_t max_chrg_current;
#ifdef CHRG_BIDIRECTIONAL
	uint8_t max_chrg_current_bi;
#endif
};

/**
 * Call in the begining of work with charger
 *
 * Returns 0 if found, does 1 request
 */
int chrg_find();

/**
 * Returns 0 on success, -1 on failure
 */
int chrg_get_rt_data(struct chrg_runtime_data *);
void chrg_get_config(struct chrg_config *);
int chrg_set_max_chrg_current(uint8_t);
int chrg_set_battery_type(enum chrg_battery_type);
int chrg_flush_settings();
void chrg_clear_settings();
void chrg_toggle_power(int);

#ifdef CHRG_BIDIRECTIONAL
int chrg_set_max_chrg_current_bi(uint8_t);
#endif

#endif
