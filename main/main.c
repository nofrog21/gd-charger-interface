#define LOG_DISABLE
#include "log.h"
#include "charger.h"
#include "rvcbus.h"
#include "gd32f30x.h"
#include "sys_clock.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static bool charger_is_off = false;

#define UNREACHABLE __asm__ ("BKPT")

static int handle_rvcb_config_command(struct rvcb_chrg_cnfgc *rx_can_data)
{
	int err = 0;
	// parse configuration command
	if (charger_is_off) {
		return -1;
	}
	if (rx_can_data->max_chrg_current < CHRG_MIN_CURRENT) {
		chrg_toggle_power(0);
		charger_is_off = 1;
		return 0;
	}
	err = chrg_set_max_chrg_current(rx_can_data->max_chrg_current);
	if (err == -1)
		return err;
	err = chrg_flush_settings();
	if (err == -1)
		return err;
	return 0;
}

static int can_send_charger_is_off(struct rvcb_node *rvcb_node)
{
	int err = 0;
	struct rvcb_dm_rv tx_data;
	memset(&tx_data, 0xFF, sizeof(tx_data));
	tx_data.dsa = RVCB_CONVERTER_1;
	if (charger_is_off) {
		tx_data.op_st1 = 1;
		tx_data.op_st2 = 0;
	} else {
		tx_data.op_st1 = 0;
		tx_data.op_st2 = 0;
	}
	tx_data.yl_st = 0;
	tx_data.rl_st = 0;
	tx_data.SPN_ISB = INSTANCE;
	err = rvcb_send(rvcb_node, RVCB_DM_RV, &tx_data);
	if (err) {
		return 1;
	}
	return 0;
}

/**
 * Проверяет статус зарядного устройства на наличие
 * неисправностей и отправляет DM_RV если требуется
 * Возвращает 0 если отправка успешна или 1 если нет
 * Переменная is_fault, если она не NULL, будет true
 * если есть неисправность.
 */
static int can_send_charger_fault(struct rvcb_node *rvcb_node,
    enum chrg_state state,
    bool *is_fault)
{
	int err = 0;
	struct rvcb_dm_rv tx_data;
	memset(&tx_data, 0xFF, sizeof(tx_data));
	if (is_fault)
		*is_fault = false;
	tx_data.dsa = RVCB_CONVERTER_1;
	switch (state) {
	case CHRG_OUT_VOLTAGE_LOW_ERR:
	case CHRG_OUT_VOLTAGE_HIGH_ERR:
	case CHRG_INPUT_VOLTAGE_HIGH_ERR:
		tx_data.op_st1 = 0;
		tx_data.op_st2 = 1;
		tx_data.yl_st = 0;
		tx_data.rl_st = 1;
		tx_data.SPN_ISB = INSTANCE;
		if (is_fault)
			*is_fault = true;
		break;
	case CHRG_OFF:
		tx_data.op_st1 = 1;
		tx_data.op_st2 = 0;
		tx_data.yl_st = 0;
		tx_data.rl_st = 0;
		tx_data.SPN_ISB = INSTANCE;
		break;
	default:
		return 0;
	}
	err = rvcb_send(rvcb_node, RVCB_DM_RV, &tx_data);
	if (err) {
		return 1;
	}
	return 0;
}

/**
 * Возвращает ту же ошибку что и rvcb_send
 */
static int can_send_charger_status(struct rvcb_node *rvcb_node,
    const struct chrg_runtime_data *rt_data)
{
	int err;
	struct rvcb_chrg_status data;
	memset(&data, 0xFF, sizeof(data));
	data.instance = INSTANCE;
	switch (rt_data->state) {
	case CHRG_NO_CHARGING:
		data.state = 1;
		break;
	case CHRG_BULK:
		data.state = 2;
		break;
	case CHRG_ABSORPTION:
		data.state = 3;
		break;
	case CHRG_FLOW_LEVEL:
		data.state = 6;
		break;
	case CHRG_UNKNOWN:
		break;
	default:
		assert(false && "unreachable");
	}
	err = rvcb_send(rvcb_node, RVCB_CHARGER_STATUS, &data);
	return err;
}

/**
 * Возвращает ту же ошибку что и rvcb_send
 */
static int can_send_charger_status_2(struct rvcb_node *rvcb_node,
    const struct chrg_runtime_data *rt_data)
{
	int err;
	struct rvcb_chrg_status2 data;
	memset(&data, 0xFF, sizeof(data));
	data.instance = INSTANCE;
	data.voltage = rvcb_ftov16(rt_data->out_volt);
	data.current = rvcb_ftoc16(rt_data->out_curr);
	// TODO: get temperature from charger
	err = rvcb_send(rvcb_node, RVCB_CHARGER_STATUS_2, &data);
	return err;
}

/**
 * Возвращает ту же ошибку что и rvcb_send
 */
static int can_send_chrg_cnfgc(struct rvcb_node *rvcb_node)
{
	struct chrg_config chrg_config;
	chrg_get_config(&chrg_config);
	struct rvcb_chrg_cnfgs data;
	memset(&data, 0xFF, sizeof(data));
	data.instance = INSTANCE;
	data.alg = chrg_config.algo;
	data.chrg_mode = 1;
	data.battery_type = chrg_config.bt_type;
	data.max_chrg_current = rvcb_ftoc16(chrg_config.max_chrg_current);
	return rvcb_send(rvcb_node, RVCB_CHARGER_CONFIGURATION_STATUS, &data);
}

void can_rx_ev_handler(struct rvcb_node *rvcb_node, struct rvcb_msg *msg)
{
#define LOG_TAG "can_rx_ev_handler"
	// TODO: maybe parse dc source
	// to know if we should send generator voltage or not
	LOG("Received frame, id: %x", msg->id);
	switch (msg->dgn) {
	case RVCB_INFORMATION_REQUEST: {
		LOG("Received INFORMATION_REQUEST");
		struct rvcb_info_req *rx_data =
		    (struct rvcb_info_req *) msg->data;
		if (rx_data->instance != INSTANCE)
			break;
		switch (rvcb_get_field(rx_data->dgn)) {
		case RVCB_DM_RV: {
			int err;
			struct chrg_runtime_data rt_data;
			err = chrg_get_rt_data(&rt_data);
			if (err) {
				err = can_send_charger_is_off(rvcb_node);
				if (err) {
					ERROR("unable to queue DM_RV");
				}
				(void) err;
			} else {
				bool is_err_sent;
				err = can_send_charger_fault(rvcb_node,
				    rt_data.state,
				    &is_err_sent);
				if (err) {
					ERROR("unable to queue DM_RV");
				}
				if (!is_err_sent) {
					struct rvcb_dm_rv tx_data;
					tx_data.op_st1 = 1;
					tx_data.op_st2 = 1;
					tx_data.yl_st = 0;
					tx_data.rl_st = 0;
					tx_data.SPN_ISB = INSTANCE;
					err = rvcb_send(rvcb_node,
					    RVCB_DM_RV,
					    &tx_data);
					if (err) {
						ERROR("unable to queue DM_RV");
					}
				}
			}
			break;
		}
		case RVCB_CHARGER_CONFIGURATION_STATUS: {
			int err = can_send_chrg_cnfgc(rvcb_node);
			if (err) {
				ERROR("unable to queue "
				      "CHARGER_CONFIGURATION_STATUS");
			}
			break;
		}
		case RVCB_CHARGER_STATUS: {
			struct chrg_runtime_data rt_data;
			int err = chrg_get_rt_data(&rt_data);
			if (err) {
				break;
			}
			err = can_send_charger_status(rvcb_node, &rt_data);
			if (err) {
				ERROR("Could not queue CHARGER STATUS");
			}
			break;
		}
		case RVCB_CHARGER_STATUS_2: {
			struct chrg_runtime_data rt_data;
			int err = chrg_get_rt_data(&rt_data);
			if (err) {
				break;
			}
			err = can_send_charger_status_2(rvcb_node, &rt_data);
			if (err) {
				ERROR("Could not queue CHARGER STATUS 2");
			}
			break;
		}
		default:
			break;
		}
		break;
	}
	case RVCB_CHARGER_COMMAND: {
		struct rvcb_chrg_cmd *rx_data =
		    (struct rvcb_chrg_cmd *) msg->data;
		if (rx_data->instance != INSTANCE) {
			return;
		}
		if (rx_data->status == 0) {
			chrg_toggle_power(0);
			charger_is_off = 1;
		} else if (rx_data->status == 1) {
			chrg_toggle_power(1);
			charger_is_off = 0;
		}
		struct rvcb_chrg_status data;
		memset(&data, 0xFF, sizeof(data));
		data.instance = INSTANCE;
		data.state = charger_is_off ? 0 : 1;
		if (rvcb_send(rvcb_node, RVCB_CHARGER_STATUS, &data)) {
			ERROR("Could not queue CHARGER STATUS 1");
		}
		break;
	}
	case RVCB_CHARGER_CONFIGURATION_COMMAND: {
		struct rvcb_chrg_cnfgc *rx_data =
		    (struct rvcb_chrg_cnfgc *) msg->data;
		if (rx_data->instance != INSTANCE) {
			return;
		}
		int err =
		    handle_rvcb_config_command(rx_data);
		if (err == -1) {
			struct rvcb_ack data;
			memset(&data, 0xFF, sizeof(data));
			data.ack_code = 1;
			data.instance = INSTANCE;
			data.source_address = msg->sa;
			data.dgn =
			    rvcb_uto24(RVCB_CHARGER_CONFIGURATION_COMMAND);
			err = rvcb_send(rvcb_node, RVCB_ACKNOWLEDGMENT, &data);
			// TODO:
			assert(err == RVCB_OK);
		} else {
			err = can_send_chrg_cnfgc(rvcb_node);
			// TODO:
			assert(err == RVCB_OK);
		}
		break;
	}
	}
#undef LOG_TAG
}

void can_ev_handler(struct rvcb_node *node, int ev, void *data)
{
	if (ev == RVCB_EV_RX) {
		can_rx_ev_handler(node, (struct rvcb_msg *) data);
	}
}

/**
 * Отправляет сообщения статуса и возвращает через сколько нужно
 * отправить следующие
 */
static inline uint64_t send_status(struct rvcb_node *rvcb_node)
{
#define LOG_TAG "send_status"
	const uint64_t fault_delay = 100;
	const uint64_t normal_delay = 1000;
	int err;
	struct chrg_runtime_data rt_data;
	err = chrg_get_rt_data(&rt_data);
	if (err) {
		err = can_send_charger_is_off(rvcb_node);
		if (err) {
			ERROR("unable to queue DM_RV");
		}
		return normal_delay;
	} else {
		bool is_fault;
		err =
		    can_send_charger_fault(rvcb_node, rt_data.state, &is_fault);
		if (err) {
			ERROR("unable to queue DM_RV");
		}
		if (is_fault) {
			return fault_delay;
		}
	}
	err = can_send_charger_status(rvcb_node, &rt_data);
	if (err) {
		ERROR("Could not queue CHARGER STATUS");
	}
	err = can_send_charger_status_2(rvcb_node, &rt_data);
	if (err) {
		ERROR("Could not queue CHARGER STATUS 2");
	}
#if 0
	// dcs_status_1
	{
		struct rvcb_dcs_status1 data;
		// DC SOURCE instance 4 is starter battery
		data.instance = 4;
		data.priority = 80;
		data.voltage = rvcb_ftov16(rt_data.in_volt);
		data.current = rvcb_ftoc32(rt_data.in_curr);
		rvcb_send(rvcb_node, RVCB_DC_SOURCE_STATUS_1, data);
	}
#endif
	return normal_delay;
#undef LOG_TAG
}

void app_main()
{
#define LOG_TAG "app_main"
	rs485_init();
	int rerr = 0;
	struct rvcb_node rvcb_node;
	struct rvcb_node_config node_cnfg = {
		.address_claim = {
			.serial_number = 255,
			.manufacture_code = 0xFFFF,
		},
		.ev_handler = can_ev_handler,
		.sa_range_start = RVCB_POWER_COMPONENTS,
		.mempool = malloc(RVCB_MIN_MEMPOOL_SZ + 32),
		.mempool_sz = RVCB_MIN_MEMPOOL_SZ + 32,
	};
	rerr = rvcb_init(&rvcb_node, &node_cnfg);
	if (rerr) {
		ERROR("Failed to init RV-C bus");
		abort();
	}
	// TODO: move finding charger to main loop
	LOG("Searching for charger");
	while (1) {
		int err = chrg_find();
		if (err) {
			WARNING("Charger is not found");
			// TODO: on every check send dm_rv
#if 0  /* blocked because of rvcbus lib limits */
			err = can_send_charger_is_off(rvcb_node);
			if (err) {
				ERROR("unable to queue DM_RV");
			}
#endif /* if 0 */
			uint64_t start = sys_clock_get_ms();
			while (sys_clock_get_ms() - start <= 1000)
				__WFI();
		} else {
			break;
		}
	}
	LOG("Charger is found");
	chrg_set_max_chrg_current(CHRG_MAX_CURRENT);
	uint64_t next_status_send =
	    sys_clock_get_ms() + send_status(&rvcb_node) * 1000;
	for (;;) {
		rvcb_poll(&rvcb_node);
		if (sys_clock_get_ms() > next_status_send) {
			LOG("Sending status");
			next_status_send =
			    sys_clock_get_ms() + send_status(&rvcb_node) * 1000;
		}
	}
}
