CPPFLAGS += -ICMSIS/GD/GD32F30x/Include -DGD32F30X_CL
SRCS += $(wildcard CMSIS/GD/GD32F30x/Source/*.c)
OBJS += CMSIS/GD/GD32F30x/Source/GCC/startup_gd32f30x_cl.o
LDFLAGS += -T CMSIS/GD/GD32F30x/Source/GCC/Ld/gd32f303xB_flash.ld
SRCS += CMSIS/GD/GD32F30x/Source/GCC/newlib/syscalls.c

$(BUILD_DIR)/CMSIS/GD/GD32F30x/Source/GCC/startup_gd32f30x_cl.o: CMSIS/GD/GD32F30x/Source/GCC/startup_gd32f30x_cl.S
	mkdir -p $(dir $@)
	$(CC) -c $< -o $@
