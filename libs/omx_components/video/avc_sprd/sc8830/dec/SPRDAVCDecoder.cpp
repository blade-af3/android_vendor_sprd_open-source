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

//#define LOG_NDEBUG 0
#define LOG_TAG "SPRDAVCDecoder"
#include <utils/Log.h>

#include "SPRDAVCDecoder.h"

#include <media/stagefright/foundation/ADebug.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MediaErrors.h>
#include <media/IOMX.h>

#include <dlfcn.h>
#include <media/hardware/HardwareAPI.h>
#include <ui/GraphicBufferMapper.h>

#include "gralloc_priv.h"
#include "ion_sprd.h"
#include "avc_dec_api.h"

namespace android {

static const CodecProfileLevel kProfileLevels[] = {
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel1  },
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel1b },
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel11 },
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel12 },
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel13 },
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel2  },
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel21 },
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel22 },
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel3  },
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel31 },
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel32 },
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel4  },
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel41 },
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel42 },
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel5  },
    { OMX_VIDEO_AVCProfileBaseline, OMX_VIDEO_AVCLevel51 },

    { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel1  },
    { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel1b },
    { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel11 },
    { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel12 },
    { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel13 },
    { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel2  },
    { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel21 },
    { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel22 },
    { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel3  },
    { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel31 },
    { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel32 },
    { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel4  },
    { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel41 },
    { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel42 },
    { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel5  },
    { OMX_VIDEO_AVCProfileMain, OMX_VIDEO_AVCLevel51 },

    { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel1  },
    { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel1b },
    { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel11 },
    { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel12 },
    { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel13 },
    { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel2  },
    { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel21 },
    { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel22 },
    { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel3  },
    { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel31 },
    { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel32 },
    { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel4  },
    { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel41 },
    { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel42 },
    { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel5  },
    { OMX_VIDEO_AVCProfileHigh, OMX_VIDEO_AVCLevel51 },
};

template<class T>
static void InitOMXParams(T *params) {
    params->nSize = sizeof(T);
    params->nVersion.s.nVersionMajor = 1;
    params->nVersion.s.nVersionMinor = 0;
    params->nVersion.s.nRevision = 0;
    params->nVersion.s.nStep = 0;
}

SPRDAVCDecoder::SPRDAVCDecoder(
    const char *name,
    const OMX_CALLBACKTYPE *callbacks,
    OMX_PTR appData,
    OMX_COMPONENTTYPE **component)
    : SprdSimpleOMXComponent(name, callbacks, appData, component),
      mHandle(new tagAVCHandle),
      mInputBufferCount(0),
      mWidth(320),
      mHeight(240),
      mPictureSize(mWidth * mHeight * 3 / 2),
      mCropLeft(0),
      mCropTop(0),
      mCropWidth(mWidth),
      mCropHeight(mHeight),
      mPicId(0),
      mHeadersDecoded(false),
      mEOSStatus(INPUT_DATA_AVAILABLE),
      mOutputPortSettingsChange(NONE),
      mSignalledError(false),
      mLibHandle(NULL),
      mDecoderSwFlag(false),
      mChangeToSwDec(false),
      mNeedIVOP(true),
      mCodecInterBuffer(NULL),
      mCodecExtraBuffer(NULL),
      mPbuf_extra_v(NULL),
      mPbuf_extra_p(0),
      mPbuf_stream_v(NULL),
      mPbuf_stream_p(0),
      mH264DecInit(NULL),
      mH264DecGetInfo(NULL),
      mH264DecDecode(NULL),
      mH264DecRelease(NULL),
      mH264Dec_SetCurRecPic(NULL),
      mH264Dec_GetLastDspFrm(NULL),
      mH264Dec_ReleaseRefBuffers(NULL),
      mH264DecMemInit(NULL) {

    ALOGI("Construct SPRDAVCDecoder, this: %0x", (void *)this);

    bool ret = false;
    ret = openDecoder("libomx_avcdec_hw_sprd.so");
    if(ret == false) {
        ret = openDecoder("libomx_avcdec_sw_sprd.so");
        mDecoderSwFlag = true;
    }

    CHECK_EQ(ret, true);

    if(mDecoderSwFlag) {
        CHECK_EQ(initDecoder(), (status_t)OK);
    } else {
        if(initDecoder() != OK) {
            ret = openDecoder("libomx_avcdec_sw_sprd.so");
            mDecoderSwFlag = true;
            CHECK_EQ(ret, true);
            CHECK_EQ(initDecoder(), (status_t)OK);
        }
    }

    initPorts();

    iUseAndroidNativeBuffer[OMX_DirInput] = OMX_FALSE;
    iUseAndroidNativeBuffer[OMX_DirOutput] = OMX_FALSE;
}

SPRDAVCDecoder::~SPRDAVCDecoder() {
    ALOGI("Destruct SPRDAVCDecoder, this: %0x", (void *)this);

    releaseDecoder();

    delete mHandle;
    mHandle = NULL;

    List<BufferInfo *> &outQueue = getPortQueue(kOutputPortIndex);
    List<BufferInfo *> &inQueue = getPortQueue(kInputPortIndex);
    CHECK(outQueue.empty());
    CHECK(inQueue.empty());
}

void SPRDAVCDecoder::initPorts() {
    OMX_PARAM_PORTDEFINITIONTYPE def;
    InitOMXParams(&def);

    def.nPortIndex = kInputPortIndex;
    def.eDir = OMX_DirInput;
    def.nBufferCountMin = 1;
    def.nBufferCountActual = kNumInputBuffers;
    def.nBufferSize = 8192;
    def.bEnabled = OMX_TRUE;
    def.bPopulated = OMX_FALSE;
    def.eDomain = OMX_PortDomainVideo;
    def.bBuffersContiguous = OMX_FALSE;
    def.nBufferAlignment = 1;

    def.format.video.cMIMEType = const_cast<char *>(MEDIA_MIMETYPE_VIDEO_AVC);
    def.format.video.pNativeRender = NULL;
    def.format.video.nFrameWidth = mWidth;
    def.format.video.nFrameHeight = mHeight;
    def.format.video.nStride = def.format.video.nFrameWidth;
    def.format.video.nSliceHeight = def.format.video.nFrameHeight;
    def.format.video.nBitrate = 0;
    def.format.video.xFramerate = 0;
    def.format.video.bFlagErrorConcealment = OMX_FALSE;
    def.format.video.eCompressionFormat = OMX_VIDEO_CodingAVC;
    def.format.video.eColorFormat = OMX_COLOR_FormatUnused;
    def.format.video.pNativeWindow = NULL;

    addPort(def);

    def.nPortIndex = kOutputPortIndex;
    def.eDir = OMX_DirOutput;
    def.nBufferCountMin = 2;
    def.nBufferCountActual = kNumOutputBuffers;
    def.bEnabled = OMX_TRUE;
    def.bPopulated = OMX_FALSE;
    def.eDomain = OMX_PortDomainVideo;
    def.bBuffersContiguous = OMX_FALSE;
    def.nBufferAlignment = 2;

    def.format.video.cMIMEType = const_cast<char *>(MEDIA_MIMETYPE_VIDEO_RAW);
    def.format.video.pNativeRender = NULL;
    def.format.video.nFrameWidth = mWidth;
    def.format.video.nFrameHeight = mHeight;
    def.format.video.nStride = def.format.video.nFrameWidth;
    def.format.video.nSliceHeight = def.format.video.nFrameHeight;
    def.format.video.nBitrate = 0;
    def.format.video.xFramerate = 0;
    def.format.video.bFlagErrorConcealment = OMX_FALSE;
    def.format.video.eCompressionFormat = OMX_VIDEO_CodingUnused;
    def.format.video.eColorFormat = OMX_COLOR_FormatYUV420SemiPlanar;
    def.format.video.pNativeWindow = NULL;

    def.nBufferSize =
        (def.format.video.nFrameWidth * def.format.video.nFrameHeight * 3) / 2;

    addPort(def);
}

status_t SPRDAVCDecoder::initDecoder() {

    memset(mHandle, 0, sizeof(tagAVCHandle));

    mHandle->userdata = (void *)this;
    mHandle->VSP_bindCb = BindFrameWrapper;
    mHandle->VSP_unbindCb = UnbindFrameWrapper;
    mHandle->VSP_extMemCb = ExtMemAllocWrapper;

    int32 phy_addr = 0;
    int32 size = 0, size_stream;

    size_stream = H264_DECODER_STREAM_BUFFER_SIZE;
    mPmem_stream = new MemoryHeapIon(SPRD_ION_DEV, size_stream, MemoryHeapBase::NO_CACHING, ION_HEAP_CARVEOUT_MASK);
    if (mPmem_stream->getHeapID() < 0)
    {
        ALOGE("Failed to alloc bitstream pmem buffer\n");
    } else
    {
        int32 ret = mPmem_stream->get_phy_addr_from_ion(&phy_addr, &size);
        if (ret < 0)
        {
            ALOGE("Failed to alloc bitstream pmem buffer\n");
        } else
        {
            mPbuf_stream_v = (unsigned char*)mPmem_stream->base();
            mPbuf_stream_p = (uint32)phy_addr;
        }
    }

    int32 size_inter = H264_DECODER_INTERNAL_BUFFER_SIZE;
    mCodecInterBuffer = (uint8 *)malloc(size_inter);

    MMCodecBuffer codec_buf;
    MMDecVideoFormat video_format;

    codec_buf.common_buffer_ptr = (uint8 *)(mCodecInterBuffer);
    codec_buf.common_buffer_ptr_phy = 0;
    codec_buf.size = size_inter;
    codec_buf.int_buffer_ptr = NULL;
    codec_buf.int_size = 0;

    video_format.video_std = H264;
    video_format.frame_width = 0;
    video_format.frame_height = 0;
    video_format.p_extra = NULL;
    video_format.p_extra_phy = 0;
    video_format.i_extra = 0;
    video_format.uv_interleaved = 1;

    if ((*mH264DecInit)(mHandle, &codec_buf,&video_format) != MMDEC_OK)
    {
        ALOGE("Failed to init AVCDEC");
        return OMX_ErrorUndefined;
    }

    return OMX_ErrorNone;
}

void SPRDAVCDecoder::releaseDecoder()
{
    (*mH264DecRelease)(mHandle);

    if (mCodecInterBuffer != NULL)
    {
        free(mCodecInterBuffer);
        mCodecInterBuffer = NULL;
    }

    if (mCodecExtraBuffer != NULL)
    {
        free(mCodecExtraBuffer);
        mCodecExtraBuffer = NULL;
    }

    if (mPbuf_stream_v != NULL)
    {
        mPmem_stream.clear();
        mPbuf_stream_v = NULL;
        mPbuf_stream_p = 0;
    }

    if (mPbuf_extra_v != NULL)
    {
        mPmem_extra.clear();
        mPbuf_extra_v = NULL;
        mPbuf_extra_p = 0;
    }

    if(mLibHandle)
    {
        dlclose(mLibHandle);
        mLibHandle = NULL;
    }
}

OMX_ERRORTYPE SPRDAVCDecoder::internalGetParameter(
    OMX_INDEXTYPE index, OMX_PTR params) {
    switch (index) {
    case OMX_IndexParamVideoPortFormat:
    {
        OMX_VIDEO_PARAM_PORTFORMATTYPE *formatParams =
            (OMX_VIDEO_PARAM_PORTFORMATTYPE *)params;

        if (formatParams->nPortIndex > kOutputPortIndex) {
            return OMX_ErrorUndefined;
        }

        if (formatParams->nIndex != 0) {
            return OMX_ErrorNoMore;
        }

        if (formatParams->nPortIndex == kInputPortIndex) {
            formatParams->eCompressionFormat = OMX_VIDEO_CodingAVC;
            formatParams->eColorFormat = OMX_COLOR_FormatUnused;
            formatParams->xFramerate = 0;
        } else {
            CHECK(formatParams->nPortIndex == kOutputPortIndex);

            PortInfo *pOutPort = editPortInfo(OMX_DirOutput);
            ALOGI("internalGetParameter, OMX_IndexParamVideoPortFormat, eColorFormat: 0x%x",pOutPort->mDef.format.video.eColorFormat);
            formatParams->eCompressionFormat = OMX_VIDEO_CodingUnused;
            formatParams->eColorFormat = pOutPort->mDef.format.video.eColorFormat;
            formatParams->xFramerate = 0;
        }

        return OMX_ErrorNone;
    }

    case OMX_IndexParamVideoProfileLevelQuerySupported:
    {
        OMX_VIDEO_PARAM_PROFILELEVELTYPE *profileLevel =
            (OMX_VIDEO_PARAM_PROFILELEVELTYPE *) params;

        if (profileLevel->nPortIndex != kInputPortIndex) {
            ALOGE("Invalid port index: %ld", profileLevel->nPortIndex);
            return OMX_ErrorUnsupportedIndex;
        }

        size_t index = profileLevel->nProfileIndex;
        size_t nProfileLevels =
            sizeof(kProfileLevels) / sizeof(kProfileLevels[0]);
        if (index >= nProfileLevels) {
            return OMX_ErrorNoMore;
        }

        profileLevel->eProfile = kProfileLevels[index].mProfile;
        profileLevel->eLevel = kProfileLevels[index].mLevel;
        return OMX_ErrorNone;
    }

    case OMX_IndexParamEnableAndroidBuffers:
    {
        EnableAndroidNativeBuffersParams *peanbp = (EnableAndroidNativeBuffersParams *)params;
        peanbp->enable = iUseAndroidNativeBuffer[OMX_DirOutput];
        ALOGI("internalGetParameter, OMX_IndexParamEnableAndroidBuffers %d",peanbp->enable);
        return OMX_ErrorNone;
    }

    case OMX_IndexParamGetAndroidNativeBuffer:
    {
        GetAndroidNativeBufferUsageParams *pganbp;

        pganbp = (GetAndroidNativeBufferUsageParams *)params;
        if(mDecoderSwFlag) {
            pganbp->nUsage = GRALLOC_USAGE_SW_READ_OFTEN |GRALLOC_USAGE_SW_WRITE_OFTEN;
        } else {
            pganbp->nUsage = GRALLOC_USAGE_VIDEO_BUFFER | GRALLOC_USAGE_SW_READ_OFTEN |GRALLOC_USAGE_SW_WRITE_OFTEN;
        }
        ALOGI("internalGetParameter, OMX_IndexParamGetAndroidNativeBuffer %x",pganbp->nUsage);
        return OMX_ErrorNone;
    }

    default:
        return SprdSimpleOMXComponent::internalGetParameter(index, params);
    }
}

OMX_ERRORTYPE SPRDAVCDecoder::internalSetParameter(
    OMX_INDEXTYPE index, const OMX_PTR params) {
    switch (index) {
    case OMX_IndexParamStandardComponentRole:
    {
        const OMX_PARAM_COMPONENTROLETYPE *roleParams =
            (const OMX_PARAM_COMPONENTROLETYPE *)params;

        if (strncmp((const char *)roleParams->cRole,
                    "video_decoder.avc",
                    OMX_MAX_STRINGNAME_SIZE - 1)) {
            return OMX_ErrorUndefined;
        }

        return OMX_ErrorNone;
    }

    case OMX_IndexParamVideoPortFormat:
    {
        OMX_VIDEO_PARAM_PORTFORMATTYPE *formatParams =
            (OMX_VIDEO_PARAM_PORTFORMATTYPE *)params;

        if (formatParams->nPortIndex > kOutputPortIndex) {
            return OMX_ErrorUndefined;
        }

        if (formatParams->nIndex != 0) {
            return OMX_ErrorNoMore;
        }

        return OMX_ErrorNone;
    }

    case OMX_IndexParamEnableAndroidBuffers:
    {
        EnableAndroidNativeBuffersParams *peanbp = (EnableAndroidNativeBuffersParams *)params;
        PortInfo *pOutPort = editPortInfo(1);
        if (peanbp->enable == OMX_FALSE) {
            ALOGI("internalSetParameter, disable AndroidNativeBuffer");
            iUseAndroidNativeBuffer[OMX_DirOutput] = OMX_FALSE;

            pOutPort->mDef.format.video.eColorFormat = OMX_COLOR_FormatYUV420SemiPlanar;
        } else {
            ALOGI("internalSetParameter, enable AndroidNativeBuffer");
            iUseAndroidNativeBuffer[OMX_DirOutput] = OMX_TRUE;

            pOutPort->mDef.format.video.eColorFormat = OMX_COLOR_FormatYUV420SemiPlanar;
        }
        return OMX_ErrorNone;
    }

    case OMX_IndexParamPortDefinition:
    {
        OMX_PARAM_PORTDEFINITIONTYPE *defParams =
            (OMX_PARAM_PORTDEFINITIONTYPE *)params;

        if (defParams->nPortIndex > 1
                || defParams->nSize
                != sizeof(OMX_PARAM_PORTDEFINITIONTYPE)) {
            return OMX_ErrorUndefined;
        }

        PortInfo *port = editPortInfo(defParams->nPortIndex);

        if (defParams->nBufferSize != port->mDef.nBufferSize) {
            CHECK_GE(defParams->nBufferSize, port->mDef.nBufferSize);
            port->mDef.nBufferSize = defParams->nBufferSize;
        }

        if (defParams->nBufferCountActual
                != port->mDef.nBufferCountActual) {
            CHECK_GE(defParams->nBufferCountActual,
                     port->mDef.nBufferCountMin);

            port->mDef.nBufferCountActual = defParams->nBufferCountActual;
        }

        memcpy(&port->mDef.format.video, &defParams->format.video, sizeof(OMX_VIDEO_PORTDEFINITIONTYPE));
        if(defParams->nPortIndex == kOutputPortIndex) {
            port->mDef.format.video.nStride = port->mDef.format.video.nFrameWidth;
            port->mDef.format.video.nSliceHeight = port->mDef.format.video.nFrameHeight;
            mWidth = port->mDef.format.video.nFrameWidth;
            mHeight = port->mDef.format.video.nFrameHeight;
            mCropWidth = mWidth;
            mCropHeight = mHeight;
            port->mDef.nBufferSize =(((mWidth + 15) & -16)* ((mHeight + 15) & -16) * 3) / 2;
            mPictureSize = port->mDef.nBufferSize;
        }

        return OMX_ErrorNone;
    }

    default:
        return SprdSimpleOMXComponent::internalSetParameter(index, params);
    }
}

OMX_ERRORTYPE SPRDAVCDecoder::allocateBuffer(
    OMX_BUFFERHEADERTYPE **header,
    OMX_U32 portIndex,
    OMX_PTR appPrivate,
    OMX_U32 size) {
    switch(portIndex)
    {
    case OMX_DirInput:
        return SprdSimpleOMXComponent::allocateBuffer(header, portIndex, appPrivate, size);

    case OMX_DirOutput:
    {
        if(mDecoderSwFlag) {
            return SprdSimpleOMXComponent::allocateBuffer(header, portIndex, appPrivate, size);
        } else {
            MemoryHeapIon* pMem = NULL;
            int phyAddr = 0;
            int bufferSize = 0;
            unsigned char* pBuffer = NULL;
            OMX_U32 size64word = (size + 1024*4 - 1) & ~(1024*4 - 1);

            pMem = new MemoryHeapIon(SPRD_ION_DEV, size64word, MemoryHeapBase::NO_CACHING, ION_HEAP_CARVEOUT_MASK);

            if(pMem->getHeapID() < 0) {
                ALOGE("Failed to alloc outport pmem buffer");
                return OMX_ErrorInsufficientResources;
            }
            if(pMem->get_phy_addr_from_ion(&phyAddr, &bufferSize)) {
                ALOGE("get_phy_addr_from_ion fail");
                return OMX_ErrorInsufficientResources;
            }

            pBuffer = (unsigned char*)(pMem->base());
            BufferPrivateStruct* bufferPrivate = new BufferPrivateStruct();
            bufferPrivate->pMem = pMem;
            bufferPrivate->phyAddr = phyAddr;
            ALOGI("allocateBuffer, allocate buffer from pmem, pBuffer: 0x%x, phyAddr: 0x%x, size: %d", pBuffer, phyAddr, bufferSize);

            SprdSimpleOMXComponent::useBuffer(header, portIndex, appPrivate, bufferSize, pBuffer, bufferPrivate);
            delete bufferPrivate;

            return OMX_ErrorNone;
        }
    }

    default:
        return OMX_ErrorUnsupportedIndex;

    }
}

OMX_ERRORTYPE SPRDAVCDecoder::freeBuffer(
    OMX_U32 portIndex,
    OMX_BUFFERHEADERTYPE *header) {
    switch(portIndex)
    {
    case OMX_DirInput:
        return SprdSimpleOMXComponent::freeBuffer(portIndex, header);

    case OMX_DirOutput:
    {
        BufferCtrlStruct* pBufCtrl= (BufferCtrlStruct*)(header->pOutputPortPrivate);
        if(pBufCtrl != NULL) {
            if(pBufCtrl->pMem != NULL) {
                ALOGI("freeBuffer, phyAddr: 0x%x", pBufCtrl->phyAddr);
                pBufCtrl->pMem.clear();
            }
            return SprdSimpleOMXComponent::freeBuffer(portIndex, header);
        } else {
            ALOGE("freeBuffer, pBufCtrl==NULL");
            return OMX_ErrorUndefined;
        }
    }

    default:
        return OMX_ErrorUnsupportedIndex;
    }
}

OMX_ERRORTYPE SPRDAVCDecoder::getConfig(
    OMX_INDEXTYPE index, OMX_PTR params) {
    switch (index) {
    case OMX_IndexConfigCommonOutputCrop:
    {
        OMX_CONFIG_RECTTYPE *rectParams = (OMX_CONFIG_RECTTYPE *)params;

        if (rectParams->nPortIndex != 1) {
            return OMX_ErrorUndefined;
        }

        rectParams->nLeft = mCropLeft;
        rectParams->nTop = mCropTop;
        rectParams->nWidth = mCropWidth;
        rectParams->nHeight = mCropHeight;

        return OMX_ErrorNone;
    }

    default:
        return OMX_ErrorUnsupportedIndex;
    }
}

void dump_bs( uint8* pBuffer,int32 aInBufSize)
{
    FILE *fp = fopen("/data/video_es.m4v","ab");
    fwrite(pBuffer,1,aInBufSize,fp);
    fclose(fp);
}

void dump_yuv( uint8* pBuffer,int32 aInBufSize)
{
    FILE *fp = fopen("/data/video.yuv","ab");
    fwrite(pBuffer,1,aInBufSize,fp);
    fclose(fp);
}

void SPRDAVCDecoder::onQueueFilled(OMX_U32 portIndex) {
    if (mSignalledError || mOutputPortSettingsChange != NONE) {
        return;
    }

    if (mEOSStatus == OUTPUT_FRAMES_FLUSHED) {
        return;
    }

    if(mChangeToSwDec) {

        mChangeToSwDec = false;

        ALOGI("%s, %d, change to sw decoder", __FUNCTION__, __LINE__);

        releaseDecoder();

        if(!openDecoder("libomx_avcdec_sw_sprd.so")) {
            ALOGE("onQueueFilled, open  libomx_avcdec_sw_sprd.so failed.");
            notify(OMX_EventError, OMX_ErrorDynamicResourcesUnavailable, 0, NULL);
            mSignalledError = true;
            mDecoderSwFlag = false;
            return;
        }

        if(initDecoder() != OK) {
            ALOGE("onQueueFilled, init sw decoder failed.");
            notify(OMX_EventError, OMX_ErrorDynamicResourcesUnavailable, 0, NULL);
            mSignalledError = true;
            return;
        }
    }

    List<BufferInfo *> &inQueue = getPortQueue(kInputPortIndex);
    List<BufferInfo *> &outQueue = getPortQueue(kOutputPortIndex);

    while ((mEOSStatus != INPUT_DATA_AVAILABLE || !inQueue.empty())
            && outQueue.size() != 0) {

        if (mEOSStatus == INPUT_EOS_SEEN) {
            drainAllOutputBuffers();
            return;
        }

        BufferInfo *inInfo = *inQueue.begin();
        OMX_BUFFERHEADERTYPE *inHeader = inInfo->mHeader;

        List<BufferInfo *>::iterator itBuffer = outQueue.begin();
        OMX_BUFFERHEADERTYPE *outHeader = NULL;
        BufferCtrlStruct *pBufCtrl = NULL;
        uint32 count = 0;
        do
        {
            if(count >= outQueue.size()) {
                ALOGI("onQueueFilled, get outQueue buffer, return, count=%d, queue_size=%d",count, outQueue.size());
                return;
            }

            outHeader = (*itBuffer)->mHeader;
            pBufCtrl= (BufferCtrlStruct*)(outHeader->pOutputPortPrivate);
            if(pBufCtrl == NULL) {
                ALOGE("onQueueFilled, pBufCtrl == NULL, fail");
                notify(OMX_EventError, OMX_ErrorUndefined, 0, NULL);
                mSignalledError = true;
                return;
            }

            itBuffer++;
            count++;
        }
        while(pBufCtrl->iRefCount > 0);

//        ALOGI("%s, %d, mBuffer=0x%x, outHeader=0x%x, iRefCount=%d", __FUNCTION__, __LINE__, *itBuffer, outHeader, pBufCtrl->iRefCount);
        ALOGI("%s, %d, outHeader:0x%x, inHeader: 0x%x, len: %d, nOffset: %d, time: %lld, EOS: %d",
              __FUNCTION__, __LINE__,outHeader,inHeader, inHeader->nFilledLen,inHeader->nOffset, inHeader->nTimeStamp,inHeader->nFlags & OMX_BUFFERFLAG_EOS);

        ++mPicId;
        if (inHeader->nFlags & OMX_BUFFERFLAG_EOS) {
            inQueue.erase(inQueue.begin());
            inInfo->mOwnedByUs = false;
            notifyEmptyBufferDone(inHeader);
            mEOSStatus = INPUT_EOS_SEEN;
            continue;
        }

        if(inHeader->nFilledLen == 0) {
            inInfo->mOwnedByUs = false;
            inQueue.erase(inQueue.begin());
            inInfo = NULL;
            notifyEmptyBufferDone(inHeader);
            inHeader = NULL;
            continue;
        }

        MMDecInput dec_in;
        MMDecOutput dec_out;

        uint8_t *bitstream = inHeader->pBuffer + inHeader->nOffset;
        int32_t bufferSize = inHeader->nFilledLen;

        if (mPbuf_stream_v != NULL)
        {
            memcpy(mPbuf_stream_v, bitstream, bufferSize);
        }
        dec_in.pStream = (uint8 *) mPbuf_stream_v;
        dec_in.pStream_phy = (uint32) mPbuf_stream_p;
        dec_in.dataLen = bufferSize;
        dec_in.beLastFrm = 0;
        dec_in.expected_IVOP = mNeedIVOP;
        dec_in.beDisplayed = 1;
        dec_in.err_pkt_num = 0;

        dec_out.frameEffective = 0;

        ALOGV("%s, %d, dec_in.dataLen: %d, mPicId: %d", __FUNCTION__, __LINE__, dec_in.dataLen, mPicId);

        outHeader->nTimeStamp = inHeader->nTimeStamp;
        outHeader->nFlags = inHeader->nFlags;

        unsigned int picPhyAddr = 0;
        if(!mDecoderSwFlag) {
            pBufCtrl= (BufferCtrlStruct*)(outHeader->pOutputPortPrivate);
            if(pBufCtrl->phyAddr != 0) {
                picPhyAddr = pBufCtrl->phyAddr;
            } else {
                native_handle_t *pNativeHandle = (native_handle_t *)outHeader->pBuffer;
                struct private_handle_t *private_h = (struct private_handle_t *)pNativeHandle;
                int bufferSize = 0;
                MemoryHeapIon::Get_phy_addr_from_ion(private_h->share_fd,(int*)&picPhyAddr, &bufferSize);
                pBufCtrl->phyAddr = picPhyAddr;
            }
        }

        ALOGV("%s, %d, outHeader: 0x%x, pBuffer: 0x%x, phyAddr: 0x%x",__FUNCTION__, __LINE__, outHeader, outHeader->pBuffer, picPhyAddr);
        GraphicBufferMapper &mapper = GraphicBufferMapper::get();
        if(iUseAndroidNativeBuffer[OMX_DirOutput]) {
            OMX_PARAM_PORTDEFINITIONTYPE *def = &editPortInfo(OMX_DirOutput)->mDef;
            int width = def->format.video.nStride;
            int height = def->format.video.nSliceHeight;
            Rect bounds(width, height);
            void *vaddr;
            int usage;

            usage = GRALLOC_USAGE_SW_READ_OFTEN|GRALLOC_USAGE_SW_WRITE_OFTEN;

            if(mapper.lock((const native_handle_t*)outHeader->pBuffer, usage, bounds, &vaddr)) {
                ALOGE("onQueueFilled, mapper.lock fail %x",outHeader->pBuffer);
                return ;
            }
            ALOGV("%s, %d, pBuffer: 0x%x, vaddr: 0x%x", __FUNCTION__, __LINE__, outHeader->pBuffer,vaddr);
            uint8 *yuv = (uint8 *)(vaddr + outHeader->nOffset);
            ALOGV("%s, %d, yuv: %0x, mPicId: %d, outHeader: %0x, outHeader->pBuffer: %0x, outHeader->nTimeStamp: %lld",
                  __FUNCTION__, __LINE__, yuv, mPicId,outHeader, outHeader->pBuffer, outHeader->nTimeStamp);
            (*mH264Dec_SetCurRecPic)(mHandle, yuv, (uint8 *)picPhyAddr, (void *)outHeader, mPicId);
        } else {
            (*mH264Dec_SetCurRecPic)(mHandle, outHeader->pBuffer, (uint8 *)picPhyAddr, (void *)outHeader, mPicId);
        }

//        dump_bs( mPbuf_stream_v, dec_in.dataLen);

        int64_t start_decode = systemTime();
        MMDecRet decRet = (*mH264DecDecode)(mHandle, &dec_in,&dec_out);
        int64_t end_decode = systemTime();
        ALOGI("%s, %d, decRet: %d, %dms, dec_out.frameEffective: %d, needIVOP: %d", __FUNCTION__, __LINE__, decRet, (unsigned int)((end_decode-start_decode) / 1000000L), dec_out.frameEffective, mNeedIVOP);

        if( decRet == MMDEC_OK) {
            mNeedIVOP = false;
        } else if (decRet == MMDEC_MEMORY_ERROR)
        {
            ALOGE("failed to allocate memory.");
            notify(OMX_EventError, OMX_ErrorInsufficientResources, 0, NULL);
        }

        if(iUseAndroidNativeBuffer[OMX_DirOutput]) {
            if(mapper.unlock((const native_handle_t*)outHeader->pBuffer)) {
                ALOGE("onQueueFilled, mapper.unlock fail %x",outHeader->pBuffer);
            }
        }

        H264SwDecInfo decoderInfo;
        MMDecRet ret;
        ret = (*mH264DecGetInfo)(mHandle, &decoderInfo);
        if(ret == MMDEC_OK) {
            if (handlePortSettingChangeEvent(&decoderInfo)) {
                return;
            } else if(mChangeToSwDec == true) {
                return;
            }

            if (decoderInfo.croppingFlag &&
                    handleCropRectEvent(&decoderInfo.cropParams)) {
                return;
            }
        } else {
            inInfo->mOwnedByUs = false;
            inQueue.erase(inQueue.begin());
            inInfo = NULL;
            notifyEmptyBufferDone(inHeader);
            inHeader = NULL;

            continue;
        }

        CHECK_LE(bufferSize, inHeader->nFilledLen);
        inHeader->nOffset += bufferSize;
        inHeader->nFilledLen -= bufferSize;

        if (inHeader->nFilledLen == 0) {
            inHeader->nOffset = 0;
            inInfo->mOwnedByUs = false;
            inQueue.erase(inQueue.begin());
            inInfo = NULL;
            notifyEmptyBufferDone(inHeader);
            inHeader = NULL;
        }

        while (!outQueue.empty() &&
                mHeadersDecoded &&
                dec_out.frameEffective) {

//dump_yuv( dec_out.pOutFrameY, dec_out.frame_height*dec_out.frame_width*3/2);

            ALOGI("%s, %d, dec_out.pBufferHeader: %0x, dec_out.mPicId: %d", __FUNCTION__, __LINE__, dec_out.pBufferHeader, dec_out.mPicId);
            int32_t picId = dec_out.mPicId;//decodedPicture.picId;
            drainOneOutputBuffer(picId, dec_out.pBufferHeader);
            dec_out.frameEffective = false;
        }
    }
}

bool SPRDAVCDecoder::handlePortSettingChangeEvent(const H264SwDecInfo *info) {
//    ALOGI("%s, %d, mWidth: %d, mHeight: %d,  info->picWidth: %d,info->picHeight:%d, mPictureSize:%d ",
//                __FUNCTION__, __LINE__,mWidth, mHeight,  info->picWidth, info->picHeight, mPictureSize);

#if 0
    if(!mDecoderSwFlag) {
        ALOGI("%s, %d, picWidth: %d, picHeight: %d, numRef: %d, profile: 0x%x",
              __FUNCTION__, __LINE__,info->picWidth, info->picHeight, info->numRefFrames, info->profile);
        if ((!((info->picWidth <= 720 && info->picHeight <= 576) || (info->picWidth <= 576 && info->picHeight <= 720))) || (info->profile == 0x64) || (info->profile == 0x4d)) {
            mDecoderSwFlag = true;
            mChangeToSwDec = true;
        }
    }
#endif

    OMX_PARAM_PORTDEFINITIONTYPE *def = &editPortInfo(kOutputPortIndex)->mDef;
    if ((mWidth != info->picWidth) || (mHeight != info->picHeight) ||
            (info->numRefFrames > def->nBufferCountActual-(2+1+info->has_b_frames))) {
        ALOGI("%s, %d, mWidth: %d, mHeight: %d, info->picWidth: %d, info->picHeight: %d", 
			__FUNCTION__, __LINE__,mWidth, mHeight, info->picWidth, info->picHeight);
        mWidth  = info->picWidth;
        mHeight = info->picHeight;
        mPictureSize = mWidth * mHeight * 3 / 2;
        mCropWidth = mWidth;
        mCropHeight = mHeight;

        if (info->numRefFrames > def->nBufferCountActual-(2+1+info->has_b_frames))
        {
            ALOGI("%s, %d, info->numRefFrames: %d, info->has_b_frames: %d, def->nBufferCountActual: %d", __FUNCTION__, __LINE__, info->numRefFrames, info->has_b_frames, def->nBufferCountActual);
            def->nBufferCountActual = info->numRefFrames + (2+1+info->has_b_frames);
        }

        updatePortDefinitions();
        notify(OMX_EventPortSettingsChanged, 1, 0, NULL);
        mOutputPortSettingsChange = AWAITING_DISABLED;
        return true;
    }

    return false;
}

bool SPRDAVCDecoder::handleCropRectEvent(const CropParams *crop) {
    if (mCropLeft != crop->cropLeftOffset ||
            mCropTop != crop->cropTopOffset ||
            mCropWidth != crop->cropOutWidth ||
            mCropHeight != crop->cropOutHeight) {
        mCropLeft = crop->cropLeftOffset;
        mCropTop = crop->cropTopOffset;
        mCropWidth = crop->cropOutWidth;
        mCropHeight = crop->cropOutHeight;

        notify(OMX_EventPortSettingsChanged, 1,
               OMX_IndexConfigCommonOutputCrop, NULL);

        return true;
    }
    return false;
}

void SPRDAVCDecoder::drainOneOutputBuffer(int32_t picId, void* pBufferHeader) {

    List<BufferInfo *> &outQueue = getPortQueue(kOutputPortIndex);

    List<BufferInfo *>::iterator it = outQueue.begin();
    while ((*it)->mHeader != (OMX_BUFFERHEADERTYPE*)pBufferHeader) {
        ++it;
    }

    BufferInfo *outInfo = *it;
    OMX_BUFFERHEADERTYPE *outHeader = outInfo->mHeader;

    outHeader->nFilledLen = mPictureSize;

    ALOGI("%s, %d, outHeader: %0x, outHeader->pBuffer: %0x, outHeader->nOffset: %d, outHeader->nFlags: %d, outHeader->nTimeStamp: %lld",
          __FUNCTION__, __LINE__, outHeader , outHeader->pBuffer, outHeader->nOffset, outHeader->nFlags, outHeader->nTimeStamp);

//    LOGI("%s, %d, outHeader->nTimeStamp: %d, outHeader->nFlags: %d, mPictureSize: %d", __FUNCTION__, __LINE__, outHeader->nTimeStamp, outHeader->nFlags, mPictureSize);
//   LOGI("%s, %d, out: %0x", __FUNCTION__, __LINE__, outHeader->pBuffer + outHeader->nOffset);

//    dump_yuv(data, mPictureSize);
    outInfo->mOwnedByUs = false;
    outQueue.erase(it);
    outInfo = NULL;

    BufferCtrlStruct* pOutBufCtrl= (BufferCtrlStruct*)(outHeader->pOutputPortPrivate);
    pOutBufCtrl->iRefCount++;
    notifyFillBufferDone(outHeader);
}

bool SPRDAVCDecoder::drainAllOutputBuffers() {
    ALOGI("%s, %d", __FUNCTION__, __LINE__);

    List<BufferInfo *> &outQueue = getPortQueue(kOutputPortIndex);

    int32_t picId;
    uint8 *yuv;

    while (!outQueue.empty()) {
        BufferInfo *outInfo = *outQueue.begin();
        outQueue.erase(outQueue.begin());
        OMX_BUFFERHEADERTYPE *outHeader = outInfo->mHeader;
        if (mHeadersDecoded &&
                MMDEC_OK == (*mH264Dec_GetLastDspFrm)(mHandle, &yuv, &picId) ) {
            outHeader->nFilledLen = mPictureSize;
        } else {
            outHeader->nTimeStamp = 0;
            outHeader->nFilledLen = 0;
            outHeader->nFlags = OMX_BUFFERFLAG_EOS;
            mEOSStatus = OUTPUT_FRAMES_FLUSHED;
        }

        outInfo->mOwnedByUs = false;
        BufferCtrlStruct* pOutBufCtrl= (BufferCtrlStruct*)(outHeader->pOutputPortPrivate);
        pOutBufCtrl->iRefCount++;
        notifyFillBufferDone(outHeader);
    }

    return true;
}

void SPRDAVCDecoder::onPortFlushCompleted(OMX_U32 portIndex) {
    if (portIndex == kInputPortIndex) {
        mEOSStatus = INPUT_DATA_AVAILABLE;
        mNeedIVOP = true;
    }
}

void SPRDAVCDecoder::onPortEnableCompleted(OMX_U32 portIndex, bool enabled) {
    switch (mOutputPortSettingsChange) {
    case NONE:
        break;

    case AWAITING_DISABLED:
    {
        CHECK(!enabled);
        mOutputPortSettingsChange = AWAITING_ENABLED;
        break;
    }

    default:
    {
        CHECK_EQ((int)mOutputPortSettingsChange, (int)AWAITING_ENABLED);
        CHECK(enabled);
        mOutputPortSettingsChange = NONE;
        break;
    }
    }
}

void SPRDAVCDecoder::onPortFlushPrepare(OMX_U32 portIndex) {
    if(portIndex == OMX_DirOutput) {
        (*mH264Dec_ReleaseRefBuffers)(mHandle);
    }
}

void SPRDAVCDecoder::updatePortDefinitions() {
    OMX_PARAM_PORTDEFINITIONTYPE *def = &editPortInfo(0)->mDef;
    def->format.video.nFrameWidth = mWidth;
    def->format.video.nFrameHeight = mHeight;
    def->format.video.nStride = def->format.video.nFrameWidth;
    def->format.video.nSliceHeight = def->format.video.nFrameHeight;

    def = &editPortInfo(1)->mDef;
    def->format.video.nFrameWidth = mWidth;
    def->format.video.nFrameHeight = mHeight;
    def->format.video.nStride = def->format.video.nFrameWidth;
    def->format.video.nSliceHeight = def->format.video.nFrameHeight;

    def->nBufferSize =
        (def->format.video.nFrameWidth
         * def->format.video.nFrameHeight * 3) / 2;
}


// static
int32_t SPRDAVCDecoder::ExtMemAllocWrapper(
    void* aUserData, unsigned int width,unsigned int height, unsigned int numBuffers) {
    return static_cast<SPRDAVCDecoder *>(aUserData)->VSP_malloc_cb(width, height, numBuffers);
}

// static
int32_t SPRDAVCDecoder::BindFrameWrapper(void *aUserData, void *pHeader) {
    return static_cast<SPRDAVCDecoder *>(aUserData)->VSP_bind_cb(pHeader);
}

// static
int32_t SPRDAVCDecoder::UnbindFrameWrapper(void *aUserData, void *pHeader) {
    return static_cast<SPRDAVCDecoder *>(aUserData)->VSP_unbind_cb(pHeader);
}

int SPRDAVCDecoder::VSP_malloc_cb(unsigned int width,unsigned int height, unsigned int numBuffers) {

    ALOGI("%s, %d, mDecoderSwFlag: %d, mPictureSize: %d, width: %d, height: %d, numBuffers: %d", __FUNCTION__, __LINE__, mDecoderSwFlag, mPictureSize, width, height, numBuffers);

    int32 Frm_width_align = ((width + 15) & (~15));
    int32 Frm_height_align = ((height + 15) & (~15));
    int32 mb_num_x = Frm_width_align/16;
    int32 mb_num_y = Frm_height_align/16;
    int32 mb_num_total = mb_num_x * mb_num_y;
    MMCodecBuffer extra_mem[MAX_MEM_TYPE];
    uint32 size_extra;

    if (mDecoderSwFlag)
    {
        size_extra = (2*+mb_num_y)*mb_num_x*8 /*MB_INFO*/
                     + (mb_num_total*16) /*i4x4pred_mode_ptr*/
                     + (mb_num_total*16) /*direct_ptr*/
                     + (mb_num_total*24) /*nnz_ptr*/
                     + (mb_num_total*2*16*2*2) /*mvd*/
                     + 3*4*17 /*fs, fs_ref, fs_ltref*/
                     + 17*(7*4+(23+150*2*17)*4+mb_num_total*16*(2*2*2 + 1 + 1 + 4 + 4)+((mb_num_x*16+48)*(mb_num_y*16+48)*3/2)) /*dpb_ptr*/
                     + mb_num_total /*g_MbToSliceGroupMap*/
                     +10*1024; //rsv
        if (mCodecExtraBuffer != NULL)
        {
            free(mCodecExtraBuffer);
            mCodecExtraBuffer = NULL;
        }
        mCodecExtraBuffer = (uint8 *)malloc(size_extra);

        extra_mem[SW_CACHABLE].common_buffer_ptr = mCodecExtraBuffer;
        extra_mem[SW_CACHABLE].common_buffer_ptr_phy = 0;
        extra_mem[SW_CACHABLE].size = size_extra;
    } else
    {
        size_extra = mb_num_total * 80 * numBuffers + 1024; //384 for tmp YUV.
        size_extra += sizeof(uint32)*69;

        mPmem_extra = new MemoryHeapIon(SPRD_ION_DEV, size_extra, MemoryHeapBase::NO_CACHING, ION_HEAP_CARVEOUT_MASK);
        int fd = mPmem_extra->getHeapID();
        if(fd >= 0)
        {
            int ret,phy_addr, buffer_size;

            ret = mPmem_extra->get_phy_addr_from_ion(&phy_addr, &buffer_size);
            if(ret < 0)
            {
                ALOGE ("mPmem_extra: get_phy_addr_from_ion fail %d",ret);
                return -1;
            }

            mPbuf_extra_p =(OMX_U32)phy_addr;
            mPbuf_extra_v = (uint8 *)mPmem_extra->base();

            extra_mem[HW_NO_CACHABLE].common_buffer_ptr =(uint8 *) mPbuf_extra_v;
            extra_mem[HW_NO_CACHABLE].common_buffer_ptr_phy = (uint32)mPbuf_extra_p;
            extra_mem[HW_NO_CACHABLE].size = size_extra;
        } else
        {
            return -1;
        }
    }

    (*mH264DecMemInit)(((SPRDAVCDecoder *)this)->mHandle, extra_mem);

    mHeadersDecoded = true;

    return 0;
}

int SPRDAVCDecoder::VSP_bind_cb(void *pHeader)
{
    BufferCtrlStruct *pBufCtrl = (BufferCtrlStruct *)(((OMX_BUFFERHEADERTYPE *)pHeader)->pOutputPortPrivate);
    ALOGI("VSP_bind_cb, ref frame: 0x%x, %x; iRefCount=%d",
          ((OMX_BUFFERHEADERTYPE *)pHeader)->pBuffer, pHeader,pBufCtrl->iRefCount);
    pBufCtrl->iRefCount++;
    return 0;
}

int SPRDAVCDecoder::VSP_unbind_cb(void *pHeader)
{
    BufferCtrlStruct *pBufCtrl = (BufferCtrlStruct *)(((OMX_BUFFERHEADERTYPE *)pHeader)->pOutputPortPrivate);

    ALOGI("VSP_unbind_cb, ref frame: 0x%x, %x; iRefCount=%d",
          ((OMX_BUFFERHEADERTYPE *)pHeader)->pBuffer, pHeader,pBufCtrl->iRefCount);

    if (pBufCtrl->iRefCount  > 0)
    {
        pBufCtrl->iRefCount--;
    }

    return 0;
}

OMX_ERRORTYPE SPRDAVCDecoder::getExtensionIndex(
    const char *name, OMX_INDEXTYPE *index) {

    ALOGI("getExtensionIndex, name: %s",name);
    if(strcmp(name, SPRD_INDEX_PARAM_ENABLE_ANB) == 0)
    {
        ALOGI("getExtensionIndex:%s",SPRD_INDEX_PARAM_ENABLE_ANB);
        *index = (OMX_INDEXTYPE) OMX_IndexParamEnableAndroidBuffers;
        return OMX_ErrorNone;
    } else if (strcmp(name, SPRD_INDEX_PARAM_GET_ANB) == 0)
    {
        ALOGI("getExtensionIndex:%s",SPRD_INDEX_PARAM_GET_ANB);
        *index = (OMX_INDEXTYPE) OMX_IndexParamGetAndroidNativeBuffer;
        return OMX_ErrorNone;
    }	else if (strcmp(name, SPRD_INDEX_PARAM_USE_ANB) == 0)
    {
        ALOGI("getExtensionIndex:%s",SPRD_INDEX_PARAM_USE_ANB);
        *index = OMX_IndexParamUseAndroidNativeBuffer2;
        return OMX_ErrorNone;
    }

    return OMX_ErrorNotImplemented;
}

bool SPRDAVCDecoder::openDecoder(const char* libName)
{
    if(mLibHandle) {
        dlclose(mLibHandle);
    }

    ALOGI("openDecoder, lib: %s", libName);

    mLibHandle = dlopen(libName, RTLD_NOW);
    if(mLibHandle == NULL) {
        ALOGE("openDecoder, can't open lib: %s",libName);
        return false;
    }

    mH264DecGetNALType = (FT_H264DecGetNALType)dlsym(mLibHandle, "H264DecGetNALType");
    if(mH264DecGetNALType == NULL) {
        ALOGE("Can't find H264DecGetNALType in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    mH264DecGetInfo = (FT_H264DecGetInfo)dlsym(mLibHandle, "H264DecGetInfo");
    if(mH264DecGetInfo == NULL) {
        ALOGE("Can't find H264DecGetInfo in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    mH264DecInit = (FT_H264DecInit)dlsym(mLibHandle, "H264DecInit");
    if(mH264DecInit == NULL) {
        ALOGE("Can't find H264DecInit in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    mH264DecDecode = (FT_H264DecDecode)dlsym(mLibHandle, "H264DecDecode");
    if(mH264DecDecode == NULL) {
        ALOGE("Can't find H264DecDecode in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    mH264DecRelease = (FT_H264DecRelease)dlsym(mLibHandle, "H264DecRelease");
    if(mH264DecRelease == NULL) {
        ALOGE("Can't find H264DecRelease in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    mH264Dec_SetCurRecPic = (FT_H264Dec_SetCurRecPic)dlsym(mLibHandle, "H264Dec_SetCurRecPic");
    if(mH264Dec_SetCurRecPic == NULL) {
        ALOGE("Can't find H264Dec_SetCurRecPic in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    mH264Dec_GetLastDspFrm = (FT_H264Dec_GetLastDspFrm)dlsym(mLibHandle, "H264Dec_GetLastDspFrm");
    if(mH264Dec_GetLastDspFrm == NULL) {
        ALOGE("Can't find H264Dec_GetLastDspFrm in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    mH264Dec_ReleaseRefBuffers = (FT_H264Dec_ReleaseRefBuffers)dlsym(mLibHandle, "H264Dec_ReleaseRefBuffers");
    if(mH264Dec_ReleaseRefBuffers == NULL) {
        ALOGE("Can't find H264Dec_ReleaseRefBuffers in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    mH264DecMemInit = (FT_H264DecMemInit)dlsym(mLibHandle, "H264DecMemInit");
    if(mH264DecMemInit == NULL) {
        ALOGE("Can't find H264DecMemInit in %s",libName);
        dlclose(mLibHandle);
        mLibHandle = NULL;
        return false;
    }

    return true;
}

}  // namespace android

android::SprdOMXComponent *createSprdOMXComponent(
    const char *name, const OMX_CALLBACKTYPE *callbacks,
    OMX_PTR appData, OMX_COMPONENTTYPE **component) {
    return new android::SPRDAVCDecoder(name, callbacks, appData, component);
}