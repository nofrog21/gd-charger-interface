#include "autoconf.h"
#include "gd32f30x_usart.h"
#include "gd32f30x_gpio.h"
#include "gd32f30x_rcu.h"
#include "gd32f30x_misc.h"
#include "rs485.h"
#include "ring_buf.h"
#include "sys_clock.h"

#include <stdint.h>
#include <string.h>

#ifdef CONFIG_RS485_USART0
#  define USART_PORT USART0
#  define GPIO_USART_PORT GPIOA
#  define RCU_USART_PORT RCU_USART0
#  define RCU_GPIO_USART_PORT RCU_GPIOA
#  define USART_PORT_IRQn USART0_IRQn
#  define PIN_TX GPIO_PIN_9
#  define PIN_RX GPIO_PIN_10
#elifdef CONFIG_RS485_USART1
#  define USART_PORT USART1
#  define GPIO_USART_PORT GPIOA
#  define RCU_USART_PORT RCU_USART1
#  define RCU_GPIO_USART_PORT RCU_GPIOA
#  define USART_PORT_IRQn USART1_IRQn
#  define PIN_TX GPIO_PIN_2
#  define PIN_RX GPIO_PIN_3
#else
#error "Unsupported UART num. Select different one using make menuconfig"
#endif

#define PIN_DE BIT(CONFIG_RS485_PIN_DE)
#define PIN_RE BIT(CONFIG_RS485_PIN_RE)

#define RS485_RX_RING_BUF_SZ 2048
static struct {
	volatile int head; // written by ISR
	volatile int tail; // read by app
	uint8_t data[RS485_RX_RING_BUF_SZ];
} rx_buffer;

#define RS485_TX_RING_BUF_SZ 1024
static struct {
	volatile int head; // written by app
	volatile int tail; // read by ISR
	uint8_t data[RS485_TX_RING_BUF_SZ];
} tx_buffer;

void USART0_IRQHandler(void)
{
	// Receive data
	if (RESET != usart_interrupt_flag_get(USART_PORT, USART_INT_FLAG_RBNE)) {
		int head = rx_buffer.head;
		int tail = rx_buffer.tail;
		if (ring_buf_space(head, tail, RS485_RX_RING_BUF_SZ) != 0) {
			rx_buffer.data[head] = (uint8_t) usart_data_receive(USART_PORT);
			__DMB();
			rx_buffer.head = (head + 1) & (RS485_RX_RING_BUF_SZ - 1);
		}
	}

	// Transmit data
	if (RESET != usart_interrupt_flag_get(USART_PORT, USART_INT_FLAG_TBE)) {
		int head = tx_buffer.head;
		int tail = tx_buffer.tail;
		if (ring_buf_cnt(head, tail, RS485_TX_RING_BUF_SZ) != 0) {
			usart_data_transmit(USART_PORT, tx_buffer.data[tail]);
			__DMB();
			tx_buffer.tail = (tail + 1) & (RS485_TX_RING_BUF_SZ - 1);
		}
	}
}

void rs485_init(void)
{
	// determed by gpio port and usart number
	rcu_periph_clock_enable(RCU_GPIO_USART_PORT);
	rcu_periph_clock_enable(RCU_USART_PORT);
	gpio_init(GPIO_USART_PORT, GPIO_MODE_AF_PP, GPIO_OSPEED_2MHZ, PIN_TX);
	gpio_init(GPIO_USART_PORT, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_2MHZ, PIN_RX);
	gpio_init(GPIO_USART_PORT, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, PIN_DE);
	gpio_init(GPIO_USART_PORT, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, PIN_RE);
	usart_stop_bit_set(USART_PORT, USART_STB_1BIT);
	usart_word_length_set(USART_PORT, USART_WL_8BIT);
	usart_transmit_config(USART_PORT, USART_TRANSMIT_ENABLE);
	usart_receive_config(USART_PORT, USART_RECEIVE_ENABLE);
	usart_baudrate_set(USART_PORT, 9600);
	nvic_irq_enable(USART_PORT_IRQn, 0, 0);
	usart_interrupt_enable(USART_PORT, USART_INT_RBNE);
	usart_interrupt_enable(USART_PORT, USART_INT_TBE);
	usart_enable(USART_PORT);
}

int rs485_request_msg(const uint8_t *rmsg, size_t sz)
{
	// TODO: gpio set level
	gpio_bit_reset(GPIO_USART_PORT, PIN_DE);
	gpio_bit_set(GPIO_USART_PORT, PIN_RE);
	int head = tx_buffer.head;
	int tail = tx_buffer.tail;
	if (ring_buf_space(head, tail, RS485_TX_RING_BUF_SZ) >= sz) {
		size_t rb_space_to_end = ring_buf_space_to_end(
			head,
			tail,
			RS485_TX_RING_BUF_SZ);
		size_t first_copy_sz = rb_space_to_end > sz ? sz : rb_space_to_end;
		memcpy(tx_buffer.data + head, rmsg, first_copy_sz);
		head = (head + first_copy_sz) & (RS485_TX_RING_BUF_SZ - 1);

		size_t second_copy_sz = sz - first_copy_sz;
		memcpy(tx_buffer.data + head, rmsg + first_copy_sz, second_copy_sz);
		head = (head + second_copy_sz) & (RS485_TX_RING_BUF_SZ - 1);
		__DMB();
		tx_buffer.head = head;
	} else {
		// not enough space
		return -1;
	}
	// wait until tx is done
	while (RESET == usart_flag_get(USART_PORT, USART_FLAG_TC)) {
		__WFI();
	}
	// TODO: check usart flag for error
	// TODO: gpio set level
	gpio_bit_set(GPIO_USART_PORT, PIN_DE);
	gpio_bit_reset(GPIO_USART_PORT, PIN_RE);
	return 0;
}

int rs485_receive_msg(uint8_t *msg, uint32_t sz, uint64_t ms)
{
	uint64_t start = sys_clock_get_ms();
	while (sys_clock_get_ms() - start < ms) {
		// TODO: check usart flags for error
		int tail = rx_buffer.tail;
		int head = rx_buffer.head;
		if (ring_buf_cnt(head, tail, RS485_RX_RING_BUF_SZ) >= sz) {
			size_t rg_cnt_to_end = ring_buf_cnt(
				head,
				tail,
				RS485_RX_RING_BUF_SZ);
			size_t first_copy_sz = rg_cnt_to_end > sz ? sz : rg_cnt_to_end;
			memcpy(msg, rx_buffer.data + tail, first_copy_sz);
			tail = (tail + first_copy_sz) & (RS485_RX_RING_BUF_SZ - 1);

			size_t second_copy_sz = sz - first_copy_sz;
			memcpy(msg + first_copy_sz, rx_buffer.data + tail, second_copy_sz);
			tail = (tail + second_copy_sz) & (RS485_RX_RING_BUF_SZ - 1);
			__DMB();
			rx_buffer.tail = tail;
			return sz;
		}
		__WFI();
	}
	return 0;
}

size_t rs485_get_rx_data_sz()
{
	return ring_buf_cnt(rx_buffer.head, rx_buffer.tail, RS485_RX_RING_BUF_SZ);
}
