CPPFLAGS += -DGD32F30X_CL
OBJS += $(BUILD_DIR)/gd_system/$(GD_TARGET)/CMSIS/GD/startup_gd32f30x_cl.o
LDFLAGS += -T gd_system/$(GD_TARGET)/CMSIS/GD/gd32f303xC_flash.ld
SRCS += gd_system/$(GD_TARGET)/CMSIS/GD/syscalls.c

$(BUILD_DIR)/gd_system/$(GD_TARGET)/CMSIS/GD/startup_gd32f30x_cl.o: \
gd_system/$(GD_TARGET)/CMSIS/GD/startup_gd32f30x_cl.S
	mkdir -p $(dir $@)
	$(CC) -c $< -o $@
