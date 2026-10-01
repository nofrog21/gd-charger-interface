SRCS += $(wildcard gd_system/$(GD_TARGET)/standard_peripheral/Source/*.c)
CPPFLAGS += -I gd_system/$(GD_TARGET)/standard_peripheral/Include -DHXTAL_VALUE=24000000
