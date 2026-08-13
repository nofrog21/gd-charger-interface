CPPFLAGS += -ICMSIS
CFLAGS += -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard

include CMSIS/GD/GD32F30x/module.mk
