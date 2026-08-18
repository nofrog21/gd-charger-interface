CPPFLAGS += -Igd_system/$(GD_TARGET)/CMSIS
CFLAGS += -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard

include gd_system/$(GD_TARGET)/CMSIS/GD/module.mk
