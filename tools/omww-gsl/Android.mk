
#*********************************************************************************
#
# Copyright (c) 2014 OpenMobile Worldwide Inc.  All rights reserved.
# Confidential and Proprietary to OpenMobile Worldwide, Inc.
# This software is provided under nondisclosure agreement for evaluation purposes only.
# Not for distribution.
#
#********************************************************************************

LOCAL_DEBUG := 1

LOCAL_PATH := $(call my-dir)

# ---- libnss3 -----
include $(CLEAR_VARS)
ifeq (true,true)
include $(CLEAR_VARS)
LOCAL_MODULE := libnss3
LOCAL_MODULE_CLASS := SHARED_LIBRARIES
LOCAL_SRC_FILES := nss/libnss3.so
LOCAL_MODULE_SUFFIX := .so
LOCAL_MODULE_TAGS := optional
include $(BUILD_PREBUILT)
endif

# ---- installer -----
include $(CLEAR_VARS)

LOCAL_MODULE := acl-installer

LOCAL_CFLAGS := \
          -DLOG_TAG=\"INSTALLER\" \
          -DDBG=$(LOCAL_DEBUG) \

LOCAL_LDFLAGS := \
          -Wl,--no-fatal-warnings \

LOCAL_MODULE_TAGS := optional

LOCAL_CFLAGS += -DUSE_FILE32API=1

LOCAL_SRC_FILES := \
          src/installer.c \
          src/mar_utils.c \
          src/unzip.c \

LOCAL_STATIC_LIBRARIES := \
          libmar          \

#LOCAL_LDLIBS += -Lout/target/product/msm8610/system/b2g -lnss3

LOCAL_SHARED_LIBRARIES := \
          libcutils \
          libnss3 \
          libz \

LOCAL_C_INCLUDES := \
          external/zlib \
          gecko/security/nss/lib/nss/ \
          gecko/security/nss/lib/util/ \
          gaia/xulrunner-sdk-26/xulrunner-sdk/include/nspr/ \
          $(LOCAL_PATH)/libmar/src/ \
          objdir-gecko/dist/include/ \
          objdir-gecko/dist/include/nspr/ \

include $(BUILD_EXECUTABLE)


# ---- installer script -----

include $(CLEAR_VARS)

LOCAL_MODULE       := acl-installer.sh

LOCAL_MODULE_TAGS  := optional

LOCAL_MODULE_CLASS := DATA
#LOCAL_MODULE_CLASS := EXECUTABLES

LOCAL_SRC_FILES    := acl-installer.sh
#LOCAL_SRC_FILES    := $(LOCAL_MODULE)

LOCAL_MODULE_PATH  := $(TARGET_OUT_EXECUTABLES)

include $(BUILD_PREBUILT)


# ---- keys -----
include $(CLEAR_VARS)

LOCAL_MODULE := acl_pubkey.der

LOCAL_MODULE_CLASS := ETC

# This will install the file in /system/etc/keys
LOCAL_MODULE_PATH := $(TARGET_OUT_ETC)/keys

LOCAL_SRC_FILES := keys/acl_pubkey.der

include $(BUILD_PREBUILT)

CUSTOM_MODULES += libnss3
CUSTOM_MODULES += acl-installer
CUSTOM_MODULES += acl-installer.sh
CUSTOM_MODULES += acl_pubkey.der
include $(LOCAL_PATH)/libmar/Android.mk

