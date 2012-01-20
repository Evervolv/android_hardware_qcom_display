LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
        qcom_ui.cpp

#ifeq ($(TARGET_BOARD_PLATFORM),qsd8k) # these are originally for 7x27a
#    LOCAL_CFLAGS += -DCHECK_FOR_EXTERNAL_FORMAT
#    LOCAL_CFLAGS += -DBYPASS_EGLIMAGE
#endif

LOCAL_SHARED_LIBRARIES := \
        libutils \
        libcutils \
        libmemalloc \
        libui \
        libEGL

LOCAL_C_INCLUDES := $(TOP)/hardware/qcom/display/libgralloc \
LOCAL_CFLAGS := -DLOG_TAG=\"libQcomUI\"
LOCAL_CFLAGS += -DQCOM_HARDWARE
LOCAL_CFLAGS += -DDEBUG_CALC_FPS
LOCAL_MODULE := libQcomUI
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)
