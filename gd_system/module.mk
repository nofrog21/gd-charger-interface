ifeq ($(GD_TARGET),)
$(error GD_TARGET not specified)
endif

include gd_system/$(GD_TARGET)/module.mk
