#Enables the listed display HAL modules

ifeq ($(BOARD_USES_QCOM_HARDWARE),true)
ifeq ($(BOARD_USES_LEGACY_QCOM),true)
    display-hals := libgralloc-legacy libcopybit
else
	display-hals := libhwcomposer liboverlay libgralloc libcopybit libgenlock libtilerenderer
	display-hals += libqcomui
endif
	include $(call all-named-subdir-makefiles,$(display-hals))
endif
