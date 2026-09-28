#ifndef RS485_H
#define RS485_H
#include <stdint.h>
#include <stddef.h>

#define RS485_ENOSPACE 1
#define RS485_EPERIPH  2
#define RS485_ETIMEOUT 3

/**
 * Bit flags accumulated from the USART status register, reported by
 * rs485_get_errors().
 * EOVERRUN: byte lost (hw or sw fifo overflow)
 * EFRAME: framing error / break
 * ENOISE: noise detected on a received bit
 * EPARITY: parity check failed
 */
#define RS485_RX_EOVERRUN (1u << 0)
#define RS485_RX_EFRAME   (1u << 1)
#define RS485_RX_ENOISE   (1u << 2)
#define RS485_RX_EPARITY  (1u << 3)

/**
 * Configures UART
 */
void rs485_init(void);

/**
 * Sends `rmsg` bytes to configured UART
 * Waits until transmition complites
 * Returns:
 *     0 - success
 *     -RS485_ENOSPACE - no space in tx buffer left
 */
int rs485_request_msg(const uint8_t *rmsg, size_t sz);

/**
 * Copies data of size `sz` from rx buffer to `msg`
 * Returns:
 *     0 - success
 *     -RS485_EPERIPH - reception error, call `rs485_get_errors()` for error mask
 *     -RS485_ETIMEOUT
 */
int rs485_receive_msg(uint8_t *msg, uint32_t sz, uint64_t timeout_ms);
size_t rs485_get_rx_data_sz();

/**
 * Returns the RS485_RX_E* flags latched by the receive ISR since the last
 * call and clears them. Returns 0 when no reception error has occurred.
 */
uint32_t rs485_get_errors(void);
#endif
