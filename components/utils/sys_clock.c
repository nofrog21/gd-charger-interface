#include "sys_clock.h"

static volatile uint64_t tick_time = 0;

void SysTick_Handler(void)
{
	++tick_time;
}

uint64_t sys_clock_get_ms(void)
{
	return tick_time;
}
