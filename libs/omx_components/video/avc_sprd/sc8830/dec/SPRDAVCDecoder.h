/*
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef SPRD_AVC_DECODER_H_
#define SPRD_AVC_DECODER_H_

#include "SprdSimpleOMXComponent.h"
#include <utils/KeyedVector.h>
#include <binder/MemoryHeapIon.h>
#include "avc_dec_api.h"

#define SPRD_ION_DEV "/dev/ion"

#define H264_DECODER_INTERNAL_BUFFER_SIZE (0x200000)
#define H264_DECODER_STREAM_BUFFER_SIZE (1024*1024*2)

struct tagAVCHandle;

namespace android {

struct SPRDAVCDecoder : public SprdSimpleOMXComponent {
    SPRDAVCDecoder(const char *name,
                   const OMX_CALLBACKTYPE *callbacks,
                   OMX_PTR appData,
                   OMX_COMPONENTTYPE **component);

protected:
    virtual ~SPRDAVCDecoder();

    virtual OMX_ERRORTYPE internalGetParameter(
        OMX_INDEXTYPE index, OMX_PTR params);

    virtual OMX_ERRORTYPE internalSetParameter(
        OMX_INDEXTYPE index, const OMX_PTR params);

    virtual OMX_ERRORTYPE allocateBuffer(
        OMX_BUFFERHEADERTYPE **header,
        OMX_U32 portIndex,
        OMX_PTR appPrivate,
        OMX_U32 size);

    virtual OMX_ERRORTYPE freeBuffer(
        OMX_U32 portIndex,
        OMX_BUFFERHEADERTYPE *header);

    virtual OMX_ERRORTYPE getConfig(OMX_INDEXTYPE index, OMX_PTR params);

    virtual void onQueueFilled(OMX_U32 portIndex);
    virtual void onPortFlushCompleted(OMX_U32 portIndex);
    virtual void onPortEnableCompleted(OMX_U32 portIndex, bool enabled);
    virtual void onPortFlushPrepare(OMX_U32 portIndex);
    virtual OMX_ERRORTYPE getExtensionIndex(const char *name, OMX_INDEXTYPE *index);

private:
    enum {
        kInputPortIndex   = 0,
        kOutputPortIndex  = 1,
        kNumInputBuffers  = 8,
        kNumOutputBuffers = 5,
    };

    enum EOSStatus {
        INPUT_DATA_AVAILABLE,
        INPUT_EOS_SEEN,
        OUTPUT_FRAMES_FLUSHED,
    };

    tagAVCHandle *mHandle;

    size_t mInputBufferCount;

    bool mIOMMUEnabled;
    uint8_t *mCodecInterBuffer;
    uint8_t *mCodecExtraBuffer;

    sp<MemoryHeapIon> mPmem_stream;
    unsigned char* mPbuf_stream_v;
    int32 mPbuf_stream_p;
    int32 mPbuf_stream_size;

    sp<MemoryHeapIon> mPmem_extra;
    unsigned char*  mPbuf_extra_v;
    int32  mPbuf_extra_p;
    int32  mPbuf_extra_size;

    uint32_t mWidth, mHeight, mPictureSize;
    uint32_t mCropLeft, mCropTop;
    uint32_t mCropWidth, mCropHeight;

    int32 mMaxWidth, mMaxHeight;

    OMX_BOOL iUseAndroidNativeBuffer[2];

    void* mLibHandle;
    bool mDecoderSwFlag;
    bool mChangeToSwDec;
    FT_H264DecGetNALType mH264DecGetNALType;
    FT_H264DecGetInfo mH264DecGetInfo;
    FT_H264GetCodecCapability mH264GetCodecCapability;
    FT_H264DecInit mH264DecInit;
    FT_H264DecDecode mH264DecDecode;
    FT_H264DecRelease mH264DecRelease;
    FT_H264Dec_SetCurRecPic  mH264Dec_SetCurRecPic;
    FT_H264Dec_GetLastDspFrm  mH264Dec_GetLastDspFrm;
    FT_H264Dec_ReleaseRefBuffers  mH264Dec_ReleaseRefBuffers;
    FT_H264DecMemInit mH264DecMemInit;

    int32_t mPicId;  // Which output picture is for which input buffer?

    // OMX_BUFFERHEADERTYPE may be overkill, but it is convenient
    // for tracking the following fields: nFlags, nTimeStamp, etc.
    KeyedVector<int32_t, OMX_BUFFERHEADERTYPE *> mPicToHeaderMap;
    bool mHeadersDecoded;

    EOSStatus mEOSStatus;
    bool mNeedIVOP;

    enum OutputPortSettingChange {
        NONE,
        AWAITING_DISABLED,
        AWAITING_ENABLED
    };
    OutputPortSettingChange mOutputPortSettingsChange;

    bool mSignalledError;

    void initPorts();
    status_t initDecoder();
    void releaseDecoder();
    void updatePortDefinitions();
    bool drainAllOutputBuffers();
    void drainOneOutputBuffer(int32_t picId, void* pBufferHeader);
    bool handleCropRectEvent(const CropParams* crop);
    bool handlePortSettingChangeEvent(const H264SwDecInfo *info);

    static int32_t ExtMemAllocWrapper(void* aUserData, unsigned int width,unsigned int height, unsigned int numBuffers) ;
    static int32_t BindFrameWrapper(void *aUserData, void *pHeader);
    static int32_t UnbindFrameWrapper(void *aUserData, void *pHeader);

    int VSP_malloc_cb(unsigned int width,unsigned int height, unsigned int numBuffers);
    int VSP_bind_cb(void *pHeader);
    int VSP_unbind_cb(void *pHeader);
    bool openDecoder(const char* libName);

    DISALLOW_EVIL_CONSTRUCTORS(SPRDAVCDecoder);
};

}  // namespace android

#endif  // SPRD_AVC_DECODER_H_

