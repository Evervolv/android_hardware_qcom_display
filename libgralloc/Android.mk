#gralloc module
LOCAL_PATH := $(call my-dir)
include $(LOCAL_PATH)/../common.mk
include $(CLEAR_VARS)

LOCAL_MODULE                  := gralloc.$(TARGET_BOARD_PLATFORM)
LOCAL_MODULE_PATH             := $(TARGET_OUT_SHARED_LIBRARIES)/hw
LOCAL_MODULE_TAGS             := optional
LOCAL_C_INCLUDES              := $(common_includes)
LOCAL_SHARED_LIBRARIES        := $(common_libs) libmemalloc libgenlock
LOCAL_SHARED_LIBRARIES        += libqdutils libGLESv1_CM
LOCAL_CFLAGS                  := $(common_flags) -DLOG_TAG=\"gralloc\"
LOCAL_ADDITIONAL_DEPENDENCIES := $(common_deps)
LOCAL_SRC_FILES               :=  gpu.cpp gralloc.cpp framebuffer.cpp mapper.cpp
include $(BUILD_SHARED_LIBRARY)

#MemAlloc Library
include $(CLEAR_VARS)
LOCAL_MODULE           := libmemalloc
LOCAL_MODULE_TAGS      := optional
LOCAL_C_INCLUDES       := $(common_includes)
LOCAL_SHARED_LIBRARIES := $(common_libs) libgenlock
LOCAL_CFLAGS           := $(common_flags) -DLOG_TAG=\"memalloc\"
LOCAL_SRC_FILES        :=  ionalloc.cpp alloc_controller.cpp
include $(BUILD_SHARED_LIBRARY)
