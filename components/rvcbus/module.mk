include components/utils/module.mk

CWD := components/rvcbus

SRCS += $(CWD)/can.c \
thirdparty/rvcbus/rvcbus.c
CPPFLAGS += -I$(CWD) -Ithirdparty/rvcbus/
