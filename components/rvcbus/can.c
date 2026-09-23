#include "rvcbus.h"
#include "ring_buf.h"
#include "gd32f30x.h"
#include "gd32f30x_can.h"
#include "gd32f30x_gpio.h"
#include "gd32f30x_rcu.h"
#include "gd32f30x_misc.h"
#include "sys_clock.h"

#include <assert.h>
#include <string.h>

#define UNREACHABLE __builtin_trap()

#define CAN_PORT CAN0
#define CAN_FIFO CAN_FIFO0
#define RCU_CAN_PORT RCU_CAN0

#define CAN_TX_TIMEOUT_LOOPS 100000U
#ifdef CONFIG_GD_TARGET_GD32F305
#define NVIC_CAN_PORT CAN0_RX0IRQn
#else
#define NVIC_CAN_PORT USBD_LP_CAN0_RX0_IRQn
#endif

/* Accessed from IRQ handler only */
static can_receive_message_struct isr_receive_can_message;

/* Error counters, written from the RX ISR only, readable for diagnostics. */
static volatile uint32_t rx_can_hw_overrun_cnt;     /* frames dropped by CAN FIFO0 */
static volatile uint32_t rx_can_queue_overflow_cnt; /* frames dropped, SW ring buffer full */

#define CAN_RX_RING_BUF_SZ 2048
static struct {
	volatile int head;
	volatile int tail;
	uint8_t data[CAN_RX_RING_BUF_SZ];
} rx_can_queue;

#ifdef CONFIG_GD_TARGET_GD32F305
void CAN0_RX0_IRQHandler(void)
#else
void USBD_LP_CAN0_RX0_IRQHandler(void)
#endif
{
	if (can_flag_get(CAN_PORT, CAN_FLAG_RFO0) == SET) {
		rx_can_hw_overrun_cnt++;
		can_flag_clear(CAN_PORT, CAN_FLAG_RFO0);
	}

	while (can_receive_message_length_get(CAN_PORT, CAN_FIFO) != 0) {
		can_message_receive(CAN_PORT, CAN_FIFO, &isr_receive_can_message);
		if (isr_receive_can_message.rx_dlen != RVCB_CAN_DATA_SIZE ||
		    isr_receive_can_message.rx_ff != CAN_FF_EXTENDED)
		{
			continue;
		}
		int head = rx_can_queue.head;
		int tail = rx_can_queue.tail;
		size_t elem_sz = sizeof (uint32_t) + RVCB_CAN_DATA_SIZE;
		size_t space_to_end = ring_buf_space_to_end(head, tail, CAN_RX_RING_BUF_SZ);
		if (space_to_end < elem_sz) {
			head = (head + space_to_end + 1) & (CAN_RX_RING_BUF_SZ - 1);
		}
		if (ring_buf_space(head, tail, CAN_RX_RING_BUF_SZ) >= elem_sz) {
			memcpy(rx_can_queue.data + head,
			    &isr_receive_can_message.rx_efid,
			    sizeof (uint32_t));
			// will not overflow, we are either at the start or
			// space to end is enough
			head += sizeof (uint32_t);
			memcpy(rx_can_queue.data + head,
			    isr_receive_can_message.rx_data,
			    RVCB_CAN_DATA_SIZE);
			head += RVCB_CAN_DATA_SIZE;
			rx_can_queue.head = head;
		} else {
			rx_can_queue_overflow_cnt++;
		}
	}
}

int rvcb_can_init(struct rvcb_can_driver_data **can_driver_data,
    const struct rvcb_can_driver_config *can_driver_config)
{
	(void) can_driver_data;
	(void) can_driver_config;
	nvic_irq_enable(NVIC_CAN_PORT, 0, 0);

	rcu_periph_clock_enable(RCU_GPIOA);
	rcu_periph_clock_enable(RCU_CAN_PORT);
	rcu_periph_clock_enable(RCU_AF);

	gpio_init(GPIOA, GPIO_MODE_IPU, GPIO_OSPEED_50MHZ, GPIO_PIN_11);
	gpio_init(GPIOA, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_12);

	can_parameter_struct            can_parameter;
	can_filter_parameter_struct     can_filter;
	can_struct_para_init(CAN_INIT_STRUCT, &can_parameter);
	can_struct_para_init(CAN_FILTER_STRUCT, &can_filter);
	/* initialize CAN register */
	can_deinit(CAN_PORT);
	/* initialize CAN parameters */
	can_parameter.time_triggered = DISABLE;
	can_parameter.auto_bus_off_recovery = ENABLE;
	can_parameter.auto_wake_up = DISABLE;
	can_parameter.auto_retrans = DISABLE;
	can_parameter.rec_fifo_overwrite = DISABLE;
	can_parameter.trans_fifo_order = DISABLE;
	can_parameter.working_mode = CAN_NORMAL_MODE;
	/* correct for APB1 == 60MHz */
	can_parameter.resync_jump_width = CAN_BT_SJW_1TQ;
	can_parameter.time_segment_1 = CAN_BT_BS1_7TQ;
	can_parameter.time_segment_2 = CAN_BT_BS2_2TQ;
	can_parameter.prescaler = 24;
	if (can_init(CAN_PORT, &can_parameter) == ERROR) {
		return -1;
	}
	/* initialize CAN filter */
	can_filter.filter_number = 0;
	can_filter.filter_mode = CAN_FILTERMODE_MASK;
	can_filter.filter_bits = CAN_FILTERBITS_32BIT;
	can_filter.filter_list_high = 0x0000;
	can_filter.filter_list_low = 0x0000;
	can_filter.filter_mask_high = 0x0000;
	can_filter.filter_mask_low = 0x0000;
	can_filter.filter_fifo_number = CAN_FIFO0;
	can_filter.filter_enable = ENABLE;
	can_filter_init(&can_filter);
	can_interrupt_enable(CAN_PORT, CAN_INT_RFNE0 | CAN_INT_RFO0);
	return 0;
}

int rvcb_can_read(struct rvcb_can_driver_data *driver_data,
    uint32_t *id, uint8_t *data)
{
	(void) driver_data;

	if (can_flag_get(CAN_PORT, CAN_FLAG_RFO0) == SET) {
		can_flag_clear(CAN_PORT, CAN_FLAG_RFO0);
		return -1;
	}

	int head = rx_can_queue.head;
	int tail = rx_can_queue.tail;
	size_t elem_sz = sizeof (uint32_t) + RVCB_CAN_DATA_SIZE;
	size_t cnt_to_end = ring_buf_cnt_to_end(head, tail, CAN_RX_RING_BUF_SZ);
	if (cnt_to_end < elem_sz) {
		tail = (tail + cnt_to_end + 1) & (CAN_RX_RING_BUF_SZ - 1);
	}
	if (ring_buf_cnt(head, tail, CAN_RX_RING_BUF_SZ) >= elem_sz) {
		// here copy is the same as in IRQHandler
		memcpy(id, rx_can_queue.data + tail, sizeof (uint32_t));
		tail += sizeof (uint32_t);
		memcpy(data, rx_can_queue.data + tail, RVCB_CAN_DATA_SIZE);
		tail += RVCB_CAN_DATA_SIZE;
		rx_can_queue.tail = tail;
		return 0;
	}
	return -1;
}

#define RVCB_CAN_EBASE 0x100
#define RVCB_CAN_EGEN (RVCB_CAN_EBASE + 1)
#define RVCB_CAN_EFILL (RVCB_CAN_EBASE + 2)
#define RVCB_CAN_EFORMAT (RVCB_CAN_EBASE + 3)
#define RVCB_CAN_EACK (RVCB_CAN_EBASE + 4)
#define RVCB_CAN_EBITERCESSIVE (RVCB_CAN_EBASE + 5)
#define RVCB_CAN_EBITDOMINANT (RVCB_CAN_EBASE + 6)
#define RVCB_CAN_ECRC (RVCB_CAN_EBASE + 7)
#define RVCB_CAN_ESOFTWARECFG (RVCB_CAN_EBASE + 8)
#define RVCB_CAN_EBUSOFF (RVCB_CAN_EBASE + 9)

/**
 * Returns -RVCB_CAN_E* error set or 0 on success
 */
int rvcb_can_send(struct rvcb_can_driver_data *driver_data,
    uint32_t id, const uint8_t *data)
{
	(void) driver_data;
	int err = 0;
	can_transmit_message_struct transmit_message = {
		.tx_efid = id,
		.tx_ft = CAN_FT_DATA,
		.tx_ff = CAN_FF_EXTENDED,
		.tx_dlen = RVCB_CAN_DATA_SIZE,
	};
	memcpy(transmit_message.tx_data, data, RVCB_CAN_DATA_SIZE);

	uint8_t transmit_mailbox = can_message_transmit(CAN_PORT, &transmit_message);
	if (transmit_mailbox == CAN_NOMAILBOX) {
		return -RVCB_CAN_EGEN;
	}

	uint64_t start = sys_clock_get_ms();
	while (1) {
		can_transmit_state_enum transmit_state =
		    can_transmit_states(CAN_PORT, transmit_mailbox);
		switch (transmit_state) {
		case CAN_TRANSMIT_OK:
			goto exit;
		case CAN_TRANSMIT_PENDING:
			if (sys_clock_get_ms() - start >= 50) {
				can_transmission_stop(CAN_PORT, transmit_mailbox);
				err = -RVCB_CAN_EGEN;
				goto exit;
			}
			__WFI();
			break;
		case CAN_TRANSMIT_FAILED:
			can_transmission_stop(CAN_PORT, transmit_mailbox);
			if (CAN_ERR(CAN_PORT) & CAN_ERR_BOERR) {
				err =  -RVCB_CAN_EBUSOFF;
				goto exit;
			}
			switch (can_error_get(CAN_PORT)) {
			case CAN_ERROR_FILL:
				err = -RVCB_CAN_EFILL;
				break;
			case CAN_ERROR_FORMAT:
				err = -RVCB_CAN_EFORMAT;
				break;
			case CAN_ERROR_ACK:
				err = -RVCB_CAN_EACK;
				break;
			case CAN_ERROR_BITRECESSIVE:
				err = -RVCB_CAN_EBITERCESSIVE;
				break;
			case CAN_ERROR_BITDOMINANT:
				err = -RVCB_CAN_EBITDOMINANT;
				break;
			case CAN_ERROR_CRC:
				err = -RVCB_CAN_ECRC;
				break;
			case CAN_ERROR_SOFTWARECFG:
				err = -RVCB_CAN_ESOFTWARECFG;
				break;
			default:
				UNREACHABLE;
			}
			goto exit;
		default:
			UNREACHABLE;
			break;
		}
	}
exit:
	return err;
}

int rvcb_can_delete(struct rvcb_can_driver_data *driver_data)
{
	(void) driver_data;
	return -1;
}
