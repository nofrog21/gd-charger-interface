#include "autoconf.h"
#ifdef CONFIG_GD_TARGET_GD32F303
#include "gd32f30x.h"
#endif // CONFIG_GD_TARGET_GD32F303

#ifdef CONFIG_GD_TARGET_GD32F103
#include "gd32f10x.h"
#include "system_gd32f10x.h"
#endif // CONFIG_GD_TARGET_GD32F103

void app_main(void);

int __io_putchar(int ch)
{
	return 0;
}

int __io_getchar(void)
{
	return 0;
}

int main(void)
{
	SysTick_Config(SystemCoreClock / 1000);
	app_main();
}
