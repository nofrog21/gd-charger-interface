ifdef CONFIG_GD_TARGET_GD32F303
GD_TARGET := GD32F303
endif
ifdef CONFIG_GD_TARGET_GD32F305
GD_TARGET := GD32F305
endif
ifdef CONFIG_GD_TARGET_GD32F103
GD_TARGET := GD32F103
endif

ifneq ($(GD_TARGET),)
include gd_system/$(GD_TARGET)/module.mk
endif
