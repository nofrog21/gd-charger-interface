#include "autoconf.h"

#if defined(CONFIG_GD_TARGET_GD32F303) || defined(CONFIG_GD_TARGET_GD32F305)
#include "gd32f30x.h"
#elif defined(CONFIG_GD_TARGET_GD32F103)
#include "gd32f10x.h"
#endif // CONFIG_GD_TARGET_GD32

void app_main(void);

int __io_putchar(int ch)
{
	(void) ch;
	return 0;
}

int __io_getchar(void)
{
	return 0;
}

int main(void)
{
	SystemCoreClockUpdate();
	if (SysTick_Config(SystemCoreClock / 1000)) {
		__builtin_trap();
	}
	app_main();
}
