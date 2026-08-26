include components/utils/module.mk

CWD := components/rs485

CPPFLAGS += -I$(CWD)
SRCS += $(CWD)/rs485.c
