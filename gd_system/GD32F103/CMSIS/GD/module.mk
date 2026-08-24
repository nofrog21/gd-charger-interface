CPPFLAGS += -DGD32F10X_MD
OBJS += $(BUILD_DIR)/gd_system/$(GD_TARGET)/CMSIS/GD/startup_gd32f10x_md.o
LDFLAGS += -T gd_system/$(GD_TARGET)/CMSIS/GD/gd32f103xB_flash.ld
SRCS += gd_system/$(GD_TARGET)/CMSIS/GD/syscalls.c

$(BUILD_DIR)/gd_system/$(GD_TARGET)/CMSIS/GD/startup_gd32f10x_md.o: \
gd_system/$(GD_TARGET)/CMSIS/GD/startup_gd32f10x_md.S
	mkdir -p $(dir $@)
	$(CC) -c $< -o $@
