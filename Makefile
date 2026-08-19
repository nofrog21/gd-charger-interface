.PHONY: all clean menuconfig

BUILD_DIR := build

CC := arm-none-eabi-gcc
CPPFLAGS += -I$(BUILD_DIR)/generated
CFLAGS := -Wall -Wextra -ggdb -ffunction-sections -fdata-sections \
-specs=nano.specs -specs=nosys.specs
LDFLAGS := -Wl,--gc-sections -Wl,-Map=$(BUILD_DIR)/output.map
OBJCOPY := arm-none-eabi-objcopy
SRCS :=
OBJS :=

all: $(BUILD_DIR)/firmware.bin

menuconfig:
	.venv/bin/menuconfig Kconfig

$(BUILD_DIR)/generated/autoconf.h .config: Kconfig
	@mkdir -p $(BUILD_DIR)/generated
	.venv/bin/genconfig --header-path $(BUILD_DIR)/generated/autoconf.h $<

-include .config

# System includes
include gd_system/module.mk
# User main application
include main/module.mk
# User components
#include components/module.mk

OBJS += $(patsubst %.c,$(BUILD_DIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

$(BUILD_DIR)/firmware.elf: $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -MMD -MP -c $< -o $@

$(BUILD_DIR)/firmware.bin: $(BUILD_DIR)/firmware.elf
	$(OBJCOPY) -O binary $^ $@

-include $(DEPS)

clean:
	rm -r $(BUILD_DIR)
