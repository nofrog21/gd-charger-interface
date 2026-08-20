#include "autoconf.h"
#include "gd32f30x_usart.h"
#include "gd32f30x_gpio.h"
#include "gd32f30x_rcu.h"
#include "gd32f30x_misc.h"
#include "rs485.h"
#include "ring_buf.h"
#include "timer.h"

#include <stdint.h>

#ifdef CONFIG_RS485_UART0
#define USART_PORT USART0
#define GPIO_USART_PORT GPIOA
#else
#define USART_PORT USART1
#define GPIO_USART_PORT GPIOA
#endif

#define RS485_RX_RING_BUF_SZ 2048
struct {
	volatile int head;
	volatile int tail;
	uint8_t data[RS485_RX_RING_BUF_SZ];
} rx_buffer;

void USART0_IRQHandler(void)
{
	// Receive data
	if(RESET != usart_interrupt_flag_get(USART_PORT, USART_INT_FLAG_RBNE)) {
		if (ring_buf_space(rx_buffer.head, rx_buffer.tail, RS485_RX_RING_BUF_SZ) != 0) {
			rx_buffer.data[rx_buffer.head] = (uint8_t) usart_data_receive(USART_PORT);
			rx_buffer.head = (rx_buffer.head + 1) & (RS485_RX_RING_BUF_SZ - 1)
		}
	}
}

void rs485_init(int pin_tx, int pin_rx, int pin_de, int pin_re)
{
	// TODO: Determine GPIO mask based on uart pins
#define RCU_USART_PORT RCU_##USART_PORT
#define RCU_GPIO_USART_PORT RCU_##GPIO_USART_PORT
	rcu_periph_clock_enable(RCU_GPIO_USART_PORT);
	rcu_periph_clock_enable(RCU_USART_PORT);
	gpio_init(GPIO_USART_PORT, GPIO_MODE_AF_PP, GPIO_OSPEED_2MHZ, pin_tx);
	gpio_init(GPIO_USART_PORT, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_2MHZ, pin_rx);
	gpio_init(GPIO_USART_PORT, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, pin_de);
	gpio_init(GPIO_USART_PORT, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, pin_re);
	usart_stop_bit_set(USART_PORT, USART_STB_1BIT);
	// TODO:
	usart_transmit_config(USART_PORT, USART_TRANSMIT_ENABLE);
	usart_receive_config(USART_PORT, USART_RECEIVE_ENABLE);
	usart_baudrate_set(USART_PORT, 9600);
#define USART_PORT_IRQn USART_PORT##_IRQn
	nvic_irq_enable(USART_PORT_IRQn, 0, 0);
	usart_interrupt_enable(USART_PORT, USART_INT_RBNE);
}

int rs485_request_msg(const uint8_t *rmsg, size_t sz)
{
	// TODO: gpio set level
	for (size_t i = 0; i < sz; ++i) {
		usart_data_transmit(rmsg[i]);
	}
	// TODO: check for error
	while (RESET == usart_flag_get(USART_PORT, USART_FLAG_TC));
	// TODO: gpio set level
	return 0;
}

int rs485_receive_msg(uint8_t *msg, uint32_t len, uint64_t ms)
{
	int64_t start = timer_get_time();
	while (1) {
		// TODO: check for error
		if (ring_buf_space(rx_buffer.head, rx_buffer.tail, RS485_RX_RING_BUF_SZ) >= len) {
			memcpy(msg, rx_buffer.tail, len);
			rx_buffer.tail = (rx_buffer.tail + len) & (RS485_RX_RING_BUF_SZ - 1);
			return len;
		}
		if (timer_get_time() - start >= ms) {
			break;
		}
	}
	return 0;
}

size_t rs485_get_rx_data_len()
{
	return rx_data_cnt;
}
