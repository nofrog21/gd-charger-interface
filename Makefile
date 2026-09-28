.PHONY: all clean menuconfig flash gdbinit

BUILD_DIR := build

CC := arm-none-eabi-gcc
CPPFLAGS := -I$(BUILD_DIR)/generated -DHXTAL_VALUE=24000000
CFLAGS := -Wall -Wextra -ggdb -ffunction-sections -fdata-sections \
-specs=nano.specs -specs=nosys.specs -Os
LDFLAGS := -Wl,--gc-sections -Wl,-Map=$(BUILD_DIR)/output.map
OBJCOPY := arm-none-eabi-objcopy
SRCS :=
OBJS :=

# Macros
define gen_gdbinit
mkdir -p $(BUILD_DIR)/gdbinit
echo "file $(BUILD_DIR)/firmware.elf" > $(BUILD_DIR)/gdbinit/symbols
printf "%s\n" \
"target extended-remote :3333" \
"monitor reset halt" \
"maintenance flush register-cache" \
"thbreak main" \
"continue" \
> $(BUILD_DIR)/gdbinit/connect
printf "%s\n" \
"source $(BUILD_DIR)/gdbinit/symbols" \
"source $(BUILD_DIR)/gdbinit/connect" \
"define reflash" \
"monitor reset halt" \
"monitor program $(BUILD_DIR)/firmware.bin 0x08000000 verify" \
"file $(BUILD_DIR)/firmware.elf" \
"monitor reset halt" \
"end" \
> $(BUILD_DIR)/gdbinit/gdbinit
endef

all: $(BUILD_DIR)/generated/autoconf.h $(BUILD_DIR)/firmware.bin

flash: $(BUILD_DIR)/firmware.bin
	openocd -f openocd.cfg -c "program $(BUILD_DIR)/firmware.bin 0x08000000 verify reset exit"

genconfig:
	@mkdir -p $(BUILD_DIR)/generated
	.venv/bin/genconfig --header-path $(BUILD_DIR)/generated/autoconf.h Kconfig

menuconfig:
	.venv/bin/menuconfig Kconfig

$(BUILD_DIR)/generated/autoconf.h: Kconfig .config
	@mkdir -p $(BUILD_DIR)/generated
	.venv/bin/genconfig --header-path $(BUILD_DIR)/generated/autoconf.h $<

-include .config

# System includes
include gd_system/module.mk
# User components
include components/module.mk
# User main application
include main/module.mk

OBJS += $(patsubst %.c,$(BUILD_DIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

$(BUILD_DIR)/firmware.elf: $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/firmware.bin: $(BUILD_DIR)/firmware.elf
	$(OBJCOPY) -O binary -R .bss -R .heap_stack $^ $@
$(BUILD_DIR)/firmware.hex: $(BUILD_DIR)/firmware.elf
	$(OBJCOPY) -O ihex -R .bss -R .heap_stack $^ $@
gdbinit:
	$(gen_gdbinit)


-include $(DEPS)

clean:
	rm -r $(BUILD_DIR)
