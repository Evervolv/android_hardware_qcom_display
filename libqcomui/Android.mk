LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

LOCAL_SRC_FILES := \
        qcom_ui.cpp \
        utils/profiler.cpp \
        utils/IdleTimer.cpp

LOCAL_SHARED_LIBRARIES := \
        libutils \
        libcutils \
        libmemalloc \
        libui \
        libEGL \
        libskia

LOCAL_C_INCLUDES := $(TOP)/hardware/qcom/display/libgralloc \
                    $(TOP)/frameworks/base/services/surfaceflinger \
                    $(TOP)/external/skia/include/core \
                    $(TOP)/external/skia/include/images

LOCAL_CFLAGS := -DLOG_TAG=\"libQcomUI\"
LOCAL_CFLAGS += -DDEBUG_CALC_FPS

# Hacks for broken mdp versions
ifeq ($(BOARD_ADRENO_DECIDE_TEXTURE_TARGET),true)
    LOCAL_CFLAGS += -DDECIDE_TEXTURE_TARGET
    ifeq ($(BOARD_ADRENO_AVOID_EXTERNAL_TEXTURE),true)
        LOCAL_CFLAGS += -DCHECK_FOR_EXTERNAL_FORMAT
    endif
endif

LOCAL_MODULE := libQcomUI
LOCAL_MODULE_TAGS := optional
include $(BUILD_SHARED_LIBRARY)
