#pragma once

#include <stdint.h>

/**
 * Должен быть сконфигурирован так, что бы прирывания случались раз в 1 ms
 */
uint64_t sys_clock_get_ms(void);
