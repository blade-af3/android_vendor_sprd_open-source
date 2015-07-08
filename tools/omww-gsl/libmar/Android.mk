
#*********************************************************************************
#
# Copyright (c) 2014 OpenMobile Worldwide Inc.  All rights reserved.
# Confidential and Proprietary to OpenMobile Worldwide, Inc.
# This software is provided under nondisclosure agreement for evaluation purposes only.
# Not for distribution.
#
#********************************************************************************

LOCAL_PATH := $(call my-dir)

# ---- Libmar static -----
include $(CLEAR_VARS)

LOCAL_MODULE := libmar

LOCAL_SRC_FILES := $(call all-c-files-under,sign src verify)

LOCAL_C_INCLUDES := \
          gecko/security/nss/lib/nss/ \
          gecko/security/nss/lib/util/ \
          gecko/security/nss/lib/cryptohi/ \
          gecko/security/nss/lib/pk11wrap/ \
          gecko/security/nss/lib/pkcs7/ \
          gecko/security/nss/lib/dev/ \
          gecko/security/nss/lib/certdb/ \
          gecko/security/nss/lib/freebl/ \
          gecko/security/nss/lib/freebl/ecl \
          gecko/security/nss/lib/smime/ \
          gaia/xulrunner-sdk-26/xulrunner-sdk/include/nspr/ \
          objdir-gecko/dist/include/ \
          objdir-gecko/dist/include/nspr/ \

LOCAL_C_INCLUDES += \
          $(LOCAL_PATH)/src/ \
          $(LOCAL_PATH)/sign/ \
          $(LOCAL_PATH)/verify/ \

LOCAL_CFLAGS := \
          -DLOG_TAG=\"TDB_INSTALLER\" \
          -DMAR_NSS=1 \
          -DDBG=$(LOCAL_DEBUG) \
          -DSEC_DEBUG=0 \

include $(BUILD_STATIC_LIBRARY)

