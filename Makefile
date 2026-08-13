.PHONY: all clean

BUILD_DIR := build

CC := arm-none-eabi-gcc
CFLAGS := -Wall -Wextra -ggdb -ffunction-sections -fdata-sections -specs=nano.specs -specs=nosys.specs
LDFLAGS := -Wl,--gc-sections -Wl,-Map=$(BUILD_DIR)/output.map
OBJCOPY := arm-none-eabi-objcopy
SRCS :=
OBJS :=

all: $(BUILD_DIR)/firmware.bin

# System includes
include GD32F30x_standard_peripheral/module.mk
include CMSIS/module.mk
# User main application
include main/module.mk
# User components
include components/module.mk

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
