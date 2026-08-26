#ifndef RS485_H
#define RS485_H
#include <stdint.h>
#include <stddef.h>

/**
 * Configures UART
 */
void rs485_init(void);

/**
 * Sends `rmsg` bytes to configured UART
 * Waits until transmition complites
 */
int rs485_request_msg(const uint8_t *rmsg, size_t sz);

/**
 * Copies data of size `sz` from rx buffer to `msg`
 * If size of data avalible in rx is less than `sz` will wait `ms` milliseconds for
 * size of rx data to reach `sz`
 */
int rs485_receive_msg(uint8_t *msg, uint32_t sz, uint64_t ms);
size_t rs485_get_rx_data_sz();
#endif
