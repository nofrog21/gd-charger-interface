OBJS += $(BUILD_DIR)/gd_system/$(GD_TARGET)/CMSIS/GD/GCC/startup_gd32f10x_cl.o
LDFLAGS += -T gd_system/$(GD_TARGET)/CMSIS/GD/gd32f105vct6.ld
SRCS += gd_system/$(GD_TARGET)/CMSIS/GD/syscalls.c

gd_system/$(GD_TARGET)/CMSIS/GD/GCC/startup_gd32f10x_cl.S: \
gd_system/$(GD_TARGET)/CMSIS/GD/ARM/startup_gd32f10x_cl.s
	sed -Ef \
gd_system/$(GD_TARGET)/CMSIS/GD/arm2gas.sed $< > $@

$(BUILD_DIR)/gd_system/$(GD_TARGET)/CMSIS/GD/GCC/startup_gd32f10x_cl.o: \
gd_system/$(GD_TARGET)/CMSIS/GD/GCC/startup_gd32f10x_cl.S
	mkdir -p $(dir $@)
	$(CC) -c $< -o $@
