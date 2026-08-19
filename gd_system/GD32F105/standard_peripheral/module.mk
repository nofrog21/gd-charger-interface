SRCS += $(wildcard gd_system/$(GD_TARGET)/standard_peripheral/Source/*.c)
CPPFLAGS += -Igd_system/$(GD_TARGET)/standard_peripheral/Include -DGD32F10X_CL
