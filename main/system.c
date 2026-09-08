#include "autoconf.h"
#ifdef CONFIG_GD_TARGET_GD32F303
#include "gd32f30x.h"
#include "gd32f30x_misc.h"
#endif // CONFIG_GD_TARGET_GD32F303

#ifdef CONFIG_GD_TARGET_GD32F103
#include "gd32f10x.h"
#include "system_gd32f10x.h"
#include "gd32f10x_misc.h"
#endif // CONFIG_GD_TARGET_GD32F103

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
	nvic_irq_enable(SysTick_IRQn, 0, 0);
	app_main();
}
