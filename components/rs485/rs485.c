#include "autoconf.h"
#include "rs485.h"
#include "ring_buf.h"
#include "sys_clock.h"

#if defined(CONFIG_GD_TARGET_GD32F303) || defined(CONFIG_GD_TARGET_GD32F305)
#include "gd32f30x_usart.h"
#include "gd32f30x_gpio.h"
#include "gd32f30x_rcu.h"
#include "gd32f30x_misc.h"
#elif defined(CONFIG_GD_TARGET_GD32F103)
#include "gd32f10x_usart.h"
#include "gd32f10x_gpio.h"
#include "gd32f10x_rcu.h"
#include "gd32f10x_misc.h"
#endif // CONFIG_GD_TARGET check

#include <stdint.h>
#include <string.h>

#if defined (CONFIG_RS485_USART0)
#  define USART_PORT          USART0
#  define GPIO_USART_PORT     GPIOA
#  define RCU_USART_PORT      RCU_USART0
#  define RCU_GPIO_USART_PORT RCU_GPIOA
#  define USART_PORT_IRQn     USART0_IRQn
#  define PIN_TX              GPIO_PIN_9
#  define PIN_RX              GPIO_PIN_10
#elif defined(CONFIG_RS485_USART1)
#  define USART_PORT          USART1
#  define GPIO_USART_PORT     GPIOA
#  define RCU_USART_PORT      RCU_USART1
#  define RCU_GPIO_USART_PORT RCU_GPIOA
#  define USART_PORT_IRQn     USART1_IRQn
#  define PIN_TX              GPIO_PIN_2
#  define PIN_RX              GPIO_PIN_3
#else
#  error "Unsupported UART num. Select different one using make menuconfig"
#endif

#define PIN_DE BIT(CONFIG_RS485_PIN_DE)
#define PIN_RE BIT(CONFIG_RS485_PIN_RE)

#define RS485_RX_RING_BUF_SZ 2048
static struct {
	volatile unsigned head; // written by ISR
	volatile unsigned tail; // read by app
	uint8_t data[RS485_RX_RING_BUF_SZ];
} rx_buffer;

#define RS485_TX_RING_BUF_SZ 1024
static struct {
	volatile unsigned head; // written by app
	volatile unsigned tail; // read by ISR
	uint8_t data[RS485_TX_RING_BUF_SZ];
} tx_buffer;

// RS485_RXERR_* flags latched by the ISR, consumed by rs485_get_errors()
static volatile uint32_t rx_errors;

/**
 * Latch the USART reception error flags reported for the byte currently in the
 * data register. On GD32F30x ORERR/FERR/NERR/PERR share the "read STAT0 then
 * read DATA" clear sequence with RBNE, so reading them here and then calling
 * usart_data_receive() clears them in hardware.
 */
static void rs485_latch_rx_errors(void)
{
	if (RESET != usart_flag_get(USART_PORT, USART_FLAG_ORERR)) {
		rx_errors |= RS485_RX_EOVERRUN;
	}
	if (RESET != usart_flag_get(USART_PORT, USART_FLAG_FERR)) {
		rx_errors |= RS485_RX_EFRAME;
	}
	if (RESET != usart_flag_get(USART_PORT, USART_FLAG_NERR)) {
		rx_errors |= RS485_RX_ENOISE;
	}
	if (RESET != usart_flag_get(USART_PORT, USART_FLAG_PERR)) {
		rx_errors |= RS485_RX_EPARITY;
	}
}

static volatile uint8_t last_received_byte;

#if defined(CONFIG_RS485_USART0)
void USART0_IRQHandler(void)
#elif defined(CONFIG_RS485_USART1)
void USART1_IRQHandler(void)
#endif
{
	// Receive data
	if (RESET != usart_interrupt_flag_get(USART_PORT, USART_INT_FLAG_RBNE))
	{
		rs485_latch_rx_errors();
		uint8_t byte = (uint8_t) usart_data_receive(USART_PORT);
		last_received_byte = byte;
		unsigned head = rx_buffer.head;
		unsigned tail = rx_buffer.tail;
		if (ring_buf_space(head, tail, RS485_RX_RING_BUF_SZ) != 0) {
			rx_buffer.data[head] = byte;
			__DMB();
			rx_buffer.head =
			    (head + 1) & (RS485_RX_RING_BUF_SZ - 1);
		} else {
			rx_errors |= RS485_RX_EOVERRUN;
		}
	}

	// Transmit data
	if (RESET != usart_interrupt_flag_get(USART_PORT, USART_INT_FLAG_TBE)) {
		unsigned head = tx_buffer.head;
		unsigned tail = tx_buffer.tail;
		if (ring_buf_cnt(head, tail, RS485_TX_RING_BUF_SZ) != 0) {
			usart_data_transmit(USART_PORT, tx_buffer.data[tail]);
			__DMB();
			tx_buffer.tail =
			    (tail + 1) & (RS485_TX_RING_BUF_SZ - 1);
		} else {
			usart_interrupt_disable(USART_PORT, USART_INT_TBE);
		}
	}
}

void rs485_init(void)
{
	// determed by gpio port and usart number
	rcu_periph_clock_enable(RCU_GPIO_USART_PORT);
	rcu_periph_clock_enable(RCU_USART_PORT);
	gpio_init(GPIO_USART_PORT, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, PIN_TX);
	gpio_init(GPIO_USART_PORT,
	    GPIO_MODE_IPU,
	    GPIO_OSPEED_50MHZ,
	    PIN_RX);
	gpio_init(GPIO_USART_PORT, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, PIN_DE);
	gpio_init(GPIO_USART_PORT, GPIO_MODE_OUT_OD, GPIO_OSPEED_50MHZ, PIN_RE);
	gpio_bit_reset(GPIO_USART_PORT, PIN_DE);
	gpio_bit_reset(GPIO_USART_PORT, PIN_RE);
	// TODO: add config structure
	usart_stop_bit_set(USART_PORT, USART_STB_1BIT);
	usart_word_length_set(USART_PORT, USART_WL_8BIT);
	usart_baudrate_set(USART_PORT, 9600);
	usart_parity_config(USART_PORT, USART_PM_NONE);
	usart_invert_config(USART_PORT, USART_TXPIN_DISABLE);
	usart_invert_config(USART_PORT, USART_RXPIN_DISABLE);
	usart_invert_config(USART_PORT, USART_DINV_DISABLE);
	usart_receive_config(USART_PORT, USART_RECEIVE_ENABLE);
	usart_transmit_config(USART_PORT, USART_TRANSMIT_ENABLE);
	rx_errors = 0;
	nvic_irq_enable(USART_PORT_IRQn, 0, 0);
	usart_interrupt_enable(USART_PORT, USART_INT_RBNE);
	usart_enable(USART_PORT);
}

int rs485_request_msg(const uint8_t *rmsg, size_t sz)
{
	// Drop any error/status flags left over from a previous session so the
	// first rs485_receive_msg() does not report a stale error. ORERR/FERR/
	// NERR/PERR are read-only in STAT0 and can't be cleared by writing 0;
	// they clear only on the "read STAT0 then read DATA" sequence
	// (GD32F30x User Manual, 17.4.1).
	(void) rs485_get_errors();
	(void) USART_STAT0(USART_PORT);
	(void) usart_data_receive(USART_PORT);
	unsigned head = tx_buffer.head;
	unsigned tail = tx_buffer.tail;
	if (ring_buf_space(head, tail, RS485_TX_RING_BUF_SZ) >= sz) {
		size_t rb_space_to_end =
		    ring_buf_space_to_end(head, tail, RS485_TX_RING_BUF_SZ);
		size_t first_copy_sz =
		    rb_space_to_end > sz ? sz : rb_space_to_end;
		memcpy(tx_buffer.data + head, rmsg, first_copy_sz);
		head = (head + first_copy_sz) & (RS485_TX_RING_BUF_SZ - 1);

		size_t second_copy_sz = sz - first_copy_sz;
		memcpy(tx_buffer.data + head,
		    rmsg + first_copy_sz,
		    second_copy_sz);
		head = (head + second_copy_sz) & (RS485_TX_RING_BUF_SZ - 1);
		__DMB();
		tx_buffer.head = head;
	} else {
		// not enough space
		return -RS485_ENOSPACE;
	}
	gpio_bit_set(GPIO_USART_PORT, PIN_DE);
	gpio_bit_set(GPIO_USART_PORT, PIN_RE);
	usart_interrupt_enable(USART_PORT, USART_INT_TBE);
	// wait until the ISR has pushed every queued byte into the data
	// register, otherwise the USART_FLAG_TC test below can pass on a stale
	// flag and cut the frame short when DE/RE are switched back.
	while (ring_buf_cnt(tx_buffer.head,
	           tx_buffer.tail,
	           RS485_TX_RING_BUF_SZ) != 0)
	{
		__WFI();
	}
	// wait until the last byte has left the shift register
	while (RESET == usart_flag_get(USART_PORT, USART_FLAG_TC));
	// The transmit path of this USART has no error flags to poll (ORERR/
	// FERR/NERR/PERR are receive-side only); nothing to check here.
	gpio_bit_reset(GPIO_USART_PORT, PIN_DE);
	gpio_bit_reset(GPIO_USART_PORT, PIN_RE);
	return 0;
}

int rs485_receive_msg(uint8_t *msg, size_t sz, uint64_t timeout_ms)
{
	uint64_t start = sys_clock_get_ms();
	while (sys_clock_get_ms() - start < timeout_ms) {
		unsigned tail = rx_buffer.tail;
		unsigned head = rx_buffer.head;
		if (ring_buf_cnt(head, tail, RS485_RX_RING_BUF_SZ) >= sz) {
			size_t rg_cnt_to_end =
			    ring_buf_cnt_to_end(head, tail, RS485_RX_RING_BUF_SZ);
			size_t first_copy_sz =
			    rg_cnt_to_end > sz ? sz : rg_cnt_to_end;
			memcpy(msg, rx_buffer.data + tail, first_copy_sz);
			tail =
			    (tail + first_copy_sz) & (RS485_RX_RING_BUF_SZ - 1);

			size_t second_copy_sz = sz - first_copy_sz;
			memcpy(msg + first_copy_sz,
			    rx_buffer.data + tail,
			    second_copy_sz);
			tail = (tail + second_copy_sz) &
			       (RS485_RX_RING_BUF_SZ - 1);
			__DMB();
			rx_buffer.tail = tail;
			return 0;
		}
		if (rx_errors != 0 && rx_errors != RS485_RX_EOVERRUN) {
			return -RS485_EPERIPH;
		}
		__WFI();
	}
	return -RS485_ETIMEOUT;
}

size_t rs485_get_rx_data_sz()
{
	unsigned head = rx_buffer.head;
	unsigned tail = rx_buffer.tail;
	return ring_buf_cnt(head, tail, RS485_RX_RING_BUF_SZ);
}

uint32_t rs485_get_errors(void)
{
	// Read-and-clear. The ISR only ever sets bits in rx_errors, so at worst
	// an error raised between the read and the clear is reported on the
	// next call instead of this one.
	uint32_t errs = rx_errors;
	rx_errors &= ~errs;
	return errs;
}

void rs485_clear(void)
{
	unsigned head = rx_buffer.head;
	unsigned tail = rx_buffer.tail;
	tail = (tail + ring_buf_cnt(head, tail, RS485_RX_RING_BUF_SZ)) &
	    (RS485_RX_RING_BUF_SZ - 1);
	__DMB();
	rx_buffer.tail = tail;
}
