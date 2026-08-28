#include "rvcbus.h"
#include "ring_buf.h"
#include "gd32f30x.h"
#include "gd32f30x_can.h"
#include "gd32f30x_gpio.h"
#include "gd32f30x_rcu.h"
#include "gd32f30x_misc.h"

#include <assert.h>
#include <string.h>

#define CAN_PORT CAN0
#define CAN_FIFO CAN_FIFO0
#define RCU_CAN_PORT RCU_CAN0
#ifdef CONFIG_GD_TARGET_GD32F305
#define NVIC_CAN_PORT CAN0_RX0IRQn
#else
#define NVIC_CAN_PORT USBD_LP_CAN0_RX0_IRQn
#endif

/* Accessed from IRQ handler only */
static can_receive_message_struct isr_receive_can_message;

#define CAN_RX_RING_BUF_SZ 2048
static struct {
	volatile int head;
	volatile int tail;
	uint8_t data[CAN_RX_RING_BUF_SZ];
} rx_can_queue;

#ifdef CONFIG_GD_TARGET_GD32F305
void CAN0_RX0_IRQHandler(void)
#else
void USBD_LP_CAN0RX0_IRQHandler(void)
#endif
{
	can_message_receive(CAN_PORT, CAN_FIFO, &isr_receive_can_message);
	if (isr_receive_can_message.rx_dlen == RVCB_MESSAGE_SZ &&
	    isr_receive_can_message.rx_ff == CAN_FF_EXTENDED)
	{
		int head = rx_can_queue.head;
		int tail = rx_can_queue.tail;
		size_t elem_sz = sizeof (uint32_t) + RVCB_MESSAGE_SZ;
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
			    RVCB_MESSAGE_SZ);
			head += RVCB_MESSAGE_SZ;
			rx_can_queue.head = head;
		}
	}
}

int rvcb_can_init(struct rvcb_can_driver_data **,
    const struct rvcb_can_driver_config *)
{
#ifdef CONFIG_GD_TARGET_GD32F305
	nvic_irq_enable(NVIC_CAN_PORT, 0, 0);
#else
	nvic_irq_enable(NVIC_CAN_PORT, 0, 0);
#endif
	rcu_periph_clock_enable(RCU_CAN_PORT);
	rcu_periph_clock_enable(RCU_AF);

	gpio_init(GPIOA, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_50MHZ, GPIO_PIN_11);
	gpio_init(GPIOA, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_12);
#ifdef CONFIG_GD_TARGET_GD32F305
	gpio_pin_remap_config(GPIO_CAN0_FULL_REMAP,ENABLE);
#else
	gpio_pin_remap_config(GPIO_CAN_FULL_REMAP,ENABLE);
#endif

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
	can_parameter.auto_retrans = ENABLE;
	can_parameter.rec_fifo_overwrite = DISABLE;
	can_parameter.trans_fifo_order = DISABLE;
	can_parameter.working_mode = CAN_NORMAL_MODE;
	can_parameter.resync_jump_width = CAN_BT_SJW_1TQ;
	can_parameter.time_segment_1 = CAN_BT_BS1_7TQ;
	can_parameter.time_segment_2 = CAN_BT_BS2_2TQ;
	can_parameter.prescaler = 24;
	can_init(CAN_PORT, &can_parameter);
	// TODO: init filter?
#if 0
	/* initialize filter */
	can_filter.filter_number=0;
	can_filter.filter_mode = CAN_FILTERMODE_MASK;
	can_filter.filter_bits = CAN_FILTERBITS_32BIT;
	can_filter.filter_list_high = 0x0000;
	can_filter.filter_list_low = 0x0000;
	can_filter.filter_mask_high = 0x0000;
	can_filter.filter_mask_low = 0x0000;
	can_filter.filter_fifo_number = CAN_FIFO0;
	can_filter.filter_enable = ENABLE;
	can_filter_init(&can_filter);
#endif
	can_interrupt_enable(CAN_PORT, CAN_INT_RFNE0);
}

int rvcb_can_read(struct rvcb_can_driver_data *driver_data,
    uint32_t *id, uint8_t *data)
{
	(void) driver_data;
	int head = rx_can_queue.head;
	int tail = rx_can_queue.tail;
	size_t elem_sz = sizeof (uint32_t) + RVCB_MESSAGE_SZ;
	size_t cnt_to_end = ring_buf_cnt_to_end(head, tail, CAN_RX_RING_BUF_SZ);
	if (cnt_to_end < elem_sz) {
		tail = (tail + cnt_to_end + 1) & (CAN_RX_RING_BUF_SZ - 1);
	}
	if (ring_buf_cnt(head, tail, CAN_RX_RING_BUF_SZ) >= elem_sz) {
		// here copy is the same as in IRQHandler
		memcpy(id, rx_can_queue.data + tail, sizeof (uint32_t));
		tail += sizeof (uint32_t);
		memcpy(data, rx_can_queue.data + tail, RVCB_MESSAGE_SZ);
		tail += RVCB_MESSAGE_SZ;
		return 0;
	}
	return -1;
}

int rvcb_can_send(struct rvcb_can_driver_data *driver_data,
    uint32_t id, const uint8_t *data)
{
	(void) driver_data;
	int err = 0;
	can_transmit_message_struct transmit_message = {
		.tx_efid = id,
		.tx_ft = CAN_FT_DATA,
		.tx_ff = CAN_FF_EXTENDED,
		.tx_dlen = RVCB_MESSAGE_SZ,
	};
	memcpy(transmit_message.tx_data, data, RVCB_MESSAGE_SZ);
	uint8_t transmit_mailbox = can_message_transmit(CAN_PORT, &transmit_message);
	while (1) {
		can_transmit_state_enum transmit_state =
		    can_transmit_states(CAN_PORT, transmit_mailbox);
		switch (transmit_state) {
		case CAN_TRANSMIT_OK:
			break;
		case CAN_TRANSMIT_FAILED:
			err = -1;
			break;
		case CAN_TRANSMIT_PENDING:
			__WFI();
			break;
		case CAN_TRANSMIT_NOMAILBOX:
			err = -1;
			break;
		}
	}
	return err;
}

int rvcb_can_delete(struct rvcb_can_driver_data *driver_data)
{
	(void) driver_data;
	return -1;
}
