include components/utils/module.mk
include components/rs485/module.mk

CWD := components/charger_driver

CPPFLAGS += -I$(CWD)

ifdef CONFIG_CHARGER_TARGET_MDX
SRCS += $(CWD)/charger_mdx.c
endif
ifdef CONFIG_CHARGER_TARGET_DX1230
SRCS += $(CWD)/charger_nemo.c
endif
