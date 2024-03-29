LOCAL_PATH := $(call my-dir)
include $(LOCAL_PATH)/../common.mk
include $(CLEAR_VARS)

LOCAL_MODULE                  := libqservice
LOCAL_LICENSE_KINDS           := SPDX-license-identifier-Apache-2.0 SPDX-license-identifier-BSD legacy_not_a_contribution
LOCAL_LICENSE_CONDITIONS      := by_exception_only not_allowed notice
LOCAL_MODULE_PATH             := $(TARGET_OUT_SHARED_LIBRARIES)
LOCAL_MODULE_TAGS             := optional
LOCAL_C_INCLUDES              := $(common_includes)
LOCAL_SHARED_LIBRARIES        := $(common_libs) libbinder
LOCAL_CFLAGS                  := $(common_flags) -DLOG_TAG=\"qdqservice\"
LOCAL_CFLAGS                  += -Wno-error
LOCAL_HEADER_LIBRARIES        := display_headers generated_kernel_headers
LOCAL_SRC_FILES               := QService.cpp \
                                 IQService.cpp \
                                 IQClient.cpp \
                                 IQHDMIClient.cpp

include $(BUILD_SHARED_LIBRARY)
