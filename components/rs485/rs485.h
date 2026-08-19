#ifndef RS485_H
#define RS485_H
#include <stdint.h>
#include <stddef.h>

void rs485_init(int pin_tx, int pin_rx, int pin_de, int pin_re);
int rs485_request_msg(const uint8_t *rmsg, size_t sz);
int rs485_receive_msg(uint8_t *msg, uint32_t len, uint64_t ms);
size_t rs485_get_rx_data_len();
#endif
