#include "autoconf.h"
#include "sys_clock.h"

#if defined(CONFIG_GD_TARGET_GD32F303) || defined(CONFIG_GD_TARGET_GD32F305)
#include "gd32f30x.h"
#elif defined(CONFIG_GD_TARGET_GD32F103)
#include "gd32f10x.h"
#endif // CONFIG_GD_TARGET_GD32

#include <errno.h>
#include <time.h>

static volatile uint64_t tick_time = 0;

void SysTick_Handler(void)
{
	++tick_time;
}

uint64_t sys_clock_get_ms(void)
{
	uint64_t now_ms;
	uint32_t primask = __get_PRIMASK();
	__disable_irq();
	now_ms = tick_time;
	__enable_irq();
	if (primask) __disable_irq();
	return now_ms;
}

int _gettimeofday(struct timeval *tp, struct timezone *tzp)
{
	(void) tzp;

	if (!tp) {
		errno = EFAULT;
		return -1;
	}
	uint64_t now_ms = sys_clock_get_ms();
	tp->tv_sec = (time_t) (now_ms / 1000ULL);
	tp->tv_usec = (suseconds_t) ((now_ms % 1000ULL) * 1000ULL);
	return 0;
}
