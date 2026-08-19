CPPFLAGS += -Igd_system/$(GD_TARGET)/CMSIS
CFLAGS += -mcpu=cortex-m3 -mthumb

include gd_system/$(GD_TARGET)/CMSIS/GD/module.mk
