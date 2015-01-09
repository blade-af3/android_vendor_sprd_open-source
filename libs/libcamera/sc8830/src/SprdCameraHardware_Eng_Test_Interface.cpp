/*
 * Copyright (C) 2012 The Android Open Source Project
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
#include <utils/Log.h>
#include <utils/String16.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <cutils/properties.h>
#include <linux/ion.h>
#include <binder/MemoryHeapIon.h>
#include <camera/Camera.h>
#include <semaphore.h>
#include "cmr_oem.h"
#include "SprdOEMCamera.h"
#include "isp_cali_interface.h"
#include "sensor_drv_u.h"
#include "sensor_cfg.h"
#include "ion_sprd.h"

#include <linux/fb.h>
#include "sprd_rot_k.h"

#define DEBUG_EN 1
#if DEBUG_EN
#define SPRD_DBG CMR_LOGE
#else
#define SPRD_DBG
#endif
using namespace android;

#define ENGTEST_PREVIEW_BUF_NUM 2
#define ENGTEST_PREVIEW_WIDTH 640
#define ENGTEST_PREVIEW_HEIGHT 480
#define ENGTEST_MAX_MISCHEAP_NUM 10

#define USE_PHYSICAL_ADD 0
#define USE_IOMM_ADD 1
static int g_mem_method = USE_PHYSICAL_ADD;/*0: physical address, 1: iommu  address*/

#define SPRD_FB_DEV					"/dev/graphics/fb0"
static int fb_fd = -1;
static struct fb_fix_screeninfo fix;
static struct fb_var_screeninfo var;
static int fcamera_bcamera=0;

struct frame_buffer_t {
    uint32_t phys_addr;
	uint32_t virt_addr;
    uint32_t length;								 //buffer's length is different from cap_image_size
};

#define SPRD_LCD_WIDTH				480
#define SPRD_LCD_HEIGHT				854
#define SPRD_MAX_PREVIEW_BUF 		ENGTEST_PREVIEW_BUF_NUM
static struct frame_buffer_t fb_buf[SPRD_MAX_PREVIEW_BUF+1];
static uint8_t tmpbuf[SPRD_LCD_WIDTH*SPRD_LCD_HEIGHT*4];
static uint8_t tmpbuf1[SPRD_LCD_WIDTH*SPRD_LCD_HEIGHT*4];
static uint32_t post_preview_buf[ENGTEST_PREVIEW_WIDTH*ENGTEST_PREVIEW_WIDTH*2];
static uint32_t post_preview_buf1[ENGTEST_PREVIEW_WIDTH*ENGTEST_PREVIEW_WIDTH*2];
static uint32_t rot_buf[ENGTEST_PREVIEW_WIDTH*ENGTEST_PREVIEW_WIDTH*2];

#define RGB565(r,g,b)       ((unsigned short)((((unsigned char)(r)>>3)|((unsigned short)(((unsigned char)(g)>>2))<<5))|(((unsigned short)((unsigned char)(b>>3)))<<11)))

static int rot_fd = -1;
static char rot_dev_name[50] = "/dev/sprd_rotation";



struct eng_test_cmr_context {
	uint32_t sensor_id;
	uint32_t capture_width;
	uint32_t capture_height;

	sp<MemoryHeapIon> preview_pmem_hp[ENGTEST_PREVIEW_BUF_NUM];
	uint32_t preview_pmemory_size[ENGTEST_PREVIEW_BUF_NUM];
	int preview_physical_addr[ENGTEST_PREVIEW_BUF_NUM];
	unsigned char* preview_virtual_addr[ENGTEST_PREVIEW_BUF_NUM];
};

static struct eng_test_cmr_context eng_test_cmr_cxt;
static struct eng_test_cmr_context *g_eng_test_cmr_cxt_ptr = &eng_test_cmr_cxt;


void RGBRotate90_anticlockwise(uint8_t *des,uint8_t *src,int width,int height, int bits)
{
	if ((!des)||(!src))
	{
		return;
	}

	int n = 0;
	int linesize;
	int i,j;
	int m = bits/8;

	SPRD_DBG("%s: bits=%d; m=%d",__FUNCTION__, bits, m);
	linesize = width*m;

	for(j = 0;j < width ;j++)
	{
		for(i= height;i>0;i--)
		{
			memcpy(&des[n],&src[linesize*(i-1)+j*m],m);
			n+=m;
		}
	}
}

void data_mirror(uint8_t *des,uint8_t *src,int width,int height, int bits)
{
    if ((!des)||(!src))
    {
        return;
    }

    int n = 0;
    int linesize;
    int i,j;
    int num;
    int lineunm;
    int m = bits/8;

    linesize = width*m;

    for(j=0;j<height;j++)
    {
            for(i= 0;i< width;i++)
            {
                    memcpy(&des[n],&src[linesize-(i+1)*m+j*linesize],m);
                    n+=m;
            }
    }
}


void YUVRotate90(uint8_t *des,uint8_t *src,int width,int height)
{
	int i=0,j=0,n=0;
	int hw=width/2,hh=height/2;

	for(j=width;j>0;j--)
		for(i=0;i<height;i++)
		{
			des[n++] = src[width*i+j];
		}
	unsigned char *ptmp = src+width*height;
	for(j=hw;j>0;j--)
		for(i=0;i<hh;i++)
		{
			des[n++] = ptmp[hw*i+j];
		}

	ptmp = src+width*height*5/4;
	for(j=hw;j>0;j--)
		for(i=0;i<hh;i++)
		{
			des[n++] = ptmp[hw*i+j];
		}
}
void  StretchColors(void* pDest, int nDestWidth, int nDestHeight, int nDestBits, void* pSrc, int nSrcWidth, int nSrcHeight, int nSrcBits)
{
	double dfAmplificationX = ((double)nDestWidth)/nSrcWidth;
	double dfAmplificationY = ((double)nDestHeight)/nSrcHeight;

	const int nSrcColorLen = nSrcBits/8;
	const int nDestColorLen = nDestBits/8;
	int i = 0;
	int j = 0;
	SPRD_DBG("%s",__FUNCTION__);
	for(i = 0; i<nDestHeight; i++)
		for(j = 0; j<nDestWidth; j++)
		{
			double tmp = i/dfAmplificationY;
			int nLine = (int)tmp;

			if(tmp - nLine > 0.5)
			++nLine;

			if(nLine >= nSrcHeight)
			--nLine;

			tmp = j/dfAmplificationX;
			int nRow = (int)tmp;

			if(tmp - nRow > 0.5)
			++nRow;

			if(nRow >= nSrcWidth)
			--nRow;

			unsigned char *pSrcPos = (unsigned char*)pSrc + (nLine*nSrcWidth + nRow)*nSrcColorLen;
			unsigned char *pDestPos = (unsigned char*)pDest + (i*nDestWidth + j)*nDestColorLen;

			*pDestPos++ = *pSrcPos++;
			*pDestPos++ = *pSrcPos++;
			*pDestPos++ = *pSrcPos++;

			if(nDestColorLen == 4)
			*pDestPos = 0;
		}
}


void yuv420_to_rgb(int width, int height, unsigned char *src, unsigned int *dst)
{
    int frameSize = width * height;
	int j = 0, yp = 0, i = 0;
		unsigned short *dst16 = (unsigned short *)dst;

    unsigned char *yuv420sp = src;
    for (j = 0, yp = 0; j < height; j++) {
        int uvp = frameSize + (j >> 1) * width, u = 0, v = 0;
        for (i = 0; i < width; i++, yp++) {
            int y = (0xff & ((int) yuv420sp[yp])) - 16;
            if (y < 0) y = 0;
            if ((i & 1) == 0) {
                u = (0xff & yuv420sp[uvp++]) - 128;
                v = (0xff & yuv420sp[uvp++]) - 128;
            }

            int y1192 = 1192 * y;
            int r = (y1192 + 1634 * v);
            int g = (y1192 - 833 * v - 400 * u);
            int b = (y1192 + 2066 * u);

            if (r < 0) r = 0; else if (r > 262143) r = 262143;
            if (g < 0) g = 0; else if (g > 262143) g = 262143;
            if (b < 0) b = 0; else if (b > 262143) b = 262143;

			if(var.bits_per_pixel == 32) {
        		dst[yp] = ((((r << 6) & 0xff0000)>>16)<<16)|(((((g >> 2) & 0xff00)>>8))<<8)|((((b >> 10) & 0xff))<<0);
			} else {
				dst16[yp] = RGB565((((r << 6) & 0xff0000)>>16), (((g >> 2) & 0xff00)>>8), (((b >> 10) & 0xff)));
			}
		}
    }
}


static void eng_dcamtest_switchTB(uint8_t *buffer, uint16_t width, uint16_t height, uint8_t pixel)
{
	uint32_t i,j,tmp;
	uint32_t linesize;
	int m = pixel/8;
	uint8_t *dst, *src;
	linesize = width*m;

	SPRD_DBG("DCAM: %s: width=%d; height=%d",__func__, width, height);

	uint8_t *tmpBuf = (uint8_t *)malloc(linesize);
	if(NULL == tmpBuf){
		SPRD_DBG("DCAM: fail to alloc temp buffer.");
		return;
	}

	for(j=0; j<height/2; j++) {
		src = buffer + j * linesize;
		dst = buffer + height * linesize - (j + 1) * linesize;
		memcpy(tmpBuf,src,linesize);
		memcpy(src,dst,linesize);
		memcpy(dst,tmpBuf,linesize);
	}

	free(tmpBuf);
}

static int eng_test_rotation(uint32_t agree, uint32_t width, uint32_t height, uint32_t in_addr, uint32_t out_addr)
{
	struct _rot_cfg_tag rot_params;
	SPRD_DBG("%s",__FUNCTION__);
	rot_params.format = ROT_YUV420;
	switch(agree){
		case 90:
			rot_params.angle = ROT_90;
			break;
		case 180:
			rot_params.angle = ROT_180;
			break;
		case 270:
			rot_params.angle = ROT_270;
			break;
		default:
			rot_params.angle = ROT_ANGLE_MAX;
			break;
	}
	rot_params.img_size.w = width;
	rot_params.img_size.h = height;
	rot_params.src_addr.y_addr = in_addr;
	rot_params.src_addr.u_addr = rot_params.src_addr.y_addr + rot_params.img_size.w * rot_params.img_size.h;
	rot_params.src_addr.v_addr = rot_params.src_addr.u_addr + rot_params.img_size.w * rot_params.img_size.h/4;
	rot_params.dst_addr.y_addr = out_addr;
	rot_params.dst_addr.u_addr = rot_params.dst_addr.y_addr + rot_params.img_size.w * rot_params.img_size.h;
	rot_params.dst_addr.v_addr = rot_params.dst_addr.u_addr + rot_params.img_size.w * rot_params.img_size.h/4;

	rot_fd = open(rot_dev_name, O_RDWR, 0);
	if (-1 == rot_fd)
	{
		SPRD_DBG("Fail to open rotation device.");
		return -1;
	}

	if (-1 == ioctl(rot_fd, ROT_IO_START, &rot_params))
	{
		SPRD_DBG("Fail to SC8800G_ROTATION_DONE");
		return -1;
	}

	return 0;
}


static int eng_test_fb_open(void)
{
	int i;
	void *bits;
	int offset_page_align;

	if(fb_fd==-1)
		fb_fd = open(SPRD_FB_DEV,O_RDWR);

	if(fb_fd<0) {
        SPRD_DBG("DCAM: %s Cannot open '%s': %d, %s", __func__, SPRD_FB_DEV, errno,  strerror(errno));
		return -1;
	}

    if(ioctl(fb_fd, FBIOGET_FSCREENINFO,&fix))
    {
        SPRD_DBG("DCAM: %s failed to get fix\n",__func__);
        close(fb_fd);
        return -1;
    }

    if(ioctl(fb_fd, FBIOGET_VSCREENINFO, &var))
    {
        SPRD_DBG("DCAM: %s failed to get var\n",__func__);
        close(fb_fd);
        return -1;
    }

	SPRD_DBG("%s: fix.smem_len=%d\n",__func__, fix.smem_len);

	bits = mmap(0, fix.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    if (bits == MAP_FAILED) {
        SPRD_DBG("DCAM: failed to mmap framebuffer");
        return -1;
    }

	SPRD_DBG("%s: var.yres=%d; var.xres=%d\n",__func__, var.yres, var.xres);

#if 0

	memset(&sprd_pmem_region, 0, sizeof(sprd_pmem_region));
	sprd_pmem_base=eng_dcamtest_pmem(SPRD_DCAM_DISPLAY_WIDTH*SPRD_DCAM_DISPLAY_WIDTH*SPRD_TYPE*4, &sprd_pmem_region);


	offset_page_align = eng_dcamtest_align_page(SPRD_DCAM_DISPLAY_WIDTH * SPRD_DCAM_DISPLAY_WIDTH * SPRD_TYPE);
	memset(&preview_buf, 0, sizeof(preview_buf));
	preview_buf[0].virt_addr= (uint32_t)sprd_pmem_base;
	preview_buf[0].phys_addr = sprd_pmem_region.offset;
	preview_buf[0].length = offset_page_align;

	preview_buf[1].virt_addr= (uint32_t)(((unsigned) sprd_pmem_base) + offset_page_align);
	preview_buf[1].phys_addr = sprd_pmem_region.offset + offset_page_align;
	preview_buf[1].length = offset_page_align;

	preview_buf[2].virt_addr= (uint32_t)(((unsigned) sprd_pmem_base) + offset_page_align * 2);
	preview_buf[2].phys_addr = sprd_pmem_region.offset + offset_page_align * 2;
	preview_buf[2].length = offset_page_align;


	for(i=0; i<3; i++){
		SPRD_DBG("DCAM: preview_buf[%d] virt_addr=0x%x, phys_addr=0x%x, length=%d", \
			i, preview_buf[i].virt_addr,preview_buf[i].phys_addr,preview_buf[i].length);
	}
#endif



	//set framebuffer address
	memset(&fb_buf, 0, sizeof(fb_buf));
	fb_buf[0].virt_addr = (uint32_t)bits;
    fb_buf[0].phys_addr = fix.smem_start;
	fb_buf[0].length = var.yres * var.xres * (var.bits_per_pixel/8);

    fb_buf[1].virt_addr = (uint32_t)(((unsigned) bits) + var.yres * var.xres * (var.bits_per_pixel/8));
	fb_buf[1].phys_addr = fix.smem_start+ var.yres * var.xres * (var.bits_per_pixel/8);
	fb_buf[1].length = var.yres * var.xres * (var.bits_per_pixel/8);

    fb_buf[2].virt_addr = (uint32_t)tmpbuf;
	fb_buf[2].length = var.yres * var.xres * (var.bits_per_pixel/8);

    fb_buf[3].virt_addr = (uint32_t)tmpbuf1;
    fb_buf[3].length = var.yres * var.xres * (var.bits_per_pixel/8);

	for(i=0; i<3; i++){
		SPRD_DBG("DCAM: buf[%d] virt_addr=0x%x, phys_addr=0x%x, length=%d", \
			i, fb_buf[i].virt_addr,fb_buf[i].phys_addr,fb_buf[i].length);
	}

	return 0;

}

static void eng_test_fb_update(unsigned n)
{
	SPRD_DBG("DCAM: active framebuffer[%d], bits_per_pixel=%d",n, var.bits_per_pixel);
    if (n > 1) return;

    var.yres_virtual = var.yres * 2;
    var.yoffset = n * var.yres;
    if (ioctl(fb_fd, FBIOPUT_VSCREENINFO, &var) < 0) {
        SPRD_DBG("DCAM: active fb swap failed");
    }
}

static int eng_test_dcam_preview_mem_alloc(void)
{
	uint32_t i =0;
	uint32_t buf_size =0;

	struct eng_test_cmr_context *cmr_cxt_ptr = g_eng_test_cmr_cxt_ptr;
	SPRD_DBG("%s g_mem_method %d line = %d\n ",__func__,g_mem_method,__LINE__);

	buf_size = (ENGTEST_PREVIEW_WIDTH * ENGTEST_PREVIEW_HEIGHT * 3) >> 1;
	buf_size = camera_get_size_align_page(buf_size);

	for (i = 0; i < ENGTEST_PREVIEW_BUF_NUM; i++) {

		if (g_mem_method == USE_PHYSICAL_ADD)
			cmr_cxt_ptr->preview_pmem_hp[i] = new MemoryHeapIon("/dev/ion", buf_size , MemoryHeapBase::NO_CACHING, ION_HEAP_ID_MASK_MM);
		else
			cmr_cxt_ptr->preview_pmem_hp[i] = new MemoryHeapIon("/dev/ion", buf_size ,MemoryHeapBase::NO_CACHING, ION_HEAP_ID_MASK_SYSTEM);

		if (cmr_cxt_ptr->preview_pmem_hp[i]->getHeapID() < 0) {
			SPRD_DBG("failed to alloc preview pmem buffer.\n");
			return -1;
		}

		if (g_mem_method == USE_PHYSICAL_ADD) {
			cmr_cxt_ptr->preview_pmem_hp[i]->get_phy_addr_from_ion((int *)(&cmr_cxt_ptr->preview_physical_addr[i]),
				(int *)(&cmr_cxt_ptr->preview_pmemory_size[i]));
		} else {
			cmr_cxt_ptr->preview_pmem_hp[i]->get_mm_iova((int *)(&cmr_cxt_ptr->preview_physical_addr[i]),
				(int *)(&cmr_cxt_ptr->preview_pmemory_size[i]));
		}

		cmr_cxt_ptr->preview_virtual_addr[i] = (unsigned char*)cmr_cxt_ptr->preview_pmem_hp[i]->base();
		if (!cmr_cxt_ptr->preview_physical_addr[i]) {
			SPRD_DBG("failed to alloc preview pmem buffer:addr is null.\n");
			return -1;
		}
	}

	if (camera_set_preview_mem((uint32_t)cmr_cxt_ptr->preview_physical_addr,
								(uint32_t)cmr_cxt_ptr->preview_virtual_addr,
								buf_size,
								ENGTEST_PREVIEW_BUF_NUM))
		return -1;

	return 0;
}

static void eng_test_dcam_preview_mem_release(void)
{
	uint32_t i =0;
	SPRD_DBG("%s g_mem_method %d line = %d\n ",__func__,g_mem_method,__LINE__);
 	struct eng_test_cmr_context *cmr_cxt_ptr = g_eng_test_cmr_cxt_ptr;
	for (i = 0; i < ENGTEST_PREVIEW_BUF_NUM; i++) {
		if(g_mem_method == USE_PHYSICAL_ADD) {
			if (cmr_cxt_ptr->preview_physical_addr[i])
				cmr_cxt_ptr->preview_pmem_hp[i].clear();
		} else {
			cmr_cxt_ptr->preview_pmem_hp[i]->free_mm_iova(cmr_cxt_ptr->preview_physical_addr[i], cmr_cxt_ptr->preview_pmemory_size[i]);
		}
	}

}

static void eng_test_dcam_preview_cb(camera_cb_type cb,
			const void *client_data,
			camera_func_type func,
			int32_t parm4)
{
	struct eng_test_cmr_context *cmr_cxt_ptr = g_eng_test_cmr_cxt_ptr;

	if (CAMERA_FUNC_START_PREVIEW == func) {

		switch(cb) {
		case CAMERA_EVT_CB_FRAME:
			{
				//here first display Preview Frame ,then free this frame displayed
				camera_frame_type *frame = (camera_frame_type *)parm4;
				SPRD_DBG("here should display camera frame index : 0x%x \n", frame->buf_id);

				SPRD_DBG("%s: var.yres=%d, var.xres=%d, var.bits_per_pixel=%d",__func__, var.yres, var.xres,var.bits_per_pixel);

				yuv420_to_rgb(ENGTEST_PREVIEW_WIDTH,ENGTEST_PREVIEW_HEIGHT, cmr_cxt_ptr->preview_virtual_addr[frame->buf_id], \
					post_preview_buf);
				

                if(fcamera_bcamera==0)
                {
                    StretchColors((void *)(fb_buf[2].virt_addr), var.yres, var.xres, var.bits_per_pixel, \
                                            post_preview_buf, ENGTEST_PREVIEW_WIDTH, ENGTEST_PREVIEW_HEIGHT, var.bits_per_pixel);
                    RGBRotate90_anticlockwise((uint8_t *)(fb_buf[frame->buf_id].virt_addr), (uint8_t *)(fb_buf[2].virt_addr),
					var.yres, var.xres, var.bits_per_pixel);
                    SPRD_DBG("mmitest i am backcamera");
                }
                else
                {
                  /*  RGBRotate90_anticlockwise((uint8_t *)post_preview_buf1 ,(uint8_t *)post_preview_buf,
                                                                     ENGTEST_PREVIEW_HEIGHT, ENGTEST_PREVIEW_WIDTH, var.bits_per_pixel);
                   */
                    StretchColors((void *)(fb_buf[2].virt_addr), var.yres, var.xres, var.bits_per_pixel, \
                                            post_preview_buf, ENGTEST_PREVIEW_WIDTH, ENGTEST_PREVIEW_HEIGHT, var.bits_per_pixel);
                    data_mirror((uint8_t *)(fb_buf[3].virt_addr), (uint8_t *)(fb_buf[2].virt_addr),
                                                 var.yres,var.xres, var.bits_per_pixel);
                    StretchColors((void *)(fb_buf[frame->buf_id].virt_addr), var.xres, var.yres, var.bits_per_pixel, \
                                              (void *) (fb_buf[3].virt_addr) , var.yres, var.xres, var.bits_per_pixel);


                   /* RGBRotate90_anticlockwise((uint8_t *)(fb_buf[2].virt_addr), (uint8_t *)(fb_buf[3].virt_addr),
                                                                     var.yres, var.xres, var.bits_per_pixel);
                     RGBRotate90_anticlockwise((uint8_t *)(fb_buf[3].virt_addr), (uint8_t *)(fb_buf[2].virt_addr),
                                                                                              var.xres, var.yres, var.bits_per_pixel);
                     RGBRotate90_anticlockwise((uint8_t *)(fb_buf[frame->buf_id].virt_addr), (uint8_t *)(fb_buf[2].virt_addr),
                                            var.yres, var.xres, var.bits_per_pixel);*/
                    SPRD_DBG("mmitest i am frontcamera");
                }
				eng_test_fb_update(frame->buf_id);

				camera_release_frame(frame->buf_id);
			}
			break;
		default:
			break;
		}
	}
}

static int32_t eng_test_dcam_preview(void)
{
	if (camerea_set_preview_format(1))
		return -1;

	if (eng_test_dcam_preview_mem_alloc())
		return -1;

	if (camera_start_preview(eng_test_dcam_preview_cb, NULL,CAMERA_NORMAL_MODE))
		return -1;

	return 0;
}

extern "C" {

void eng_test_camera_close(void)
{
	SPRD_DBG("%s: fb_fd : %d\n",__func__, fb_fd);

	eng_test_dcam_preview_mem_release();
	camera_stop(NULL, NULL);

	if(fb_fd >= 0) {
		close(fb_fd);
		fb_fd = -1;
	}

	if(rot_fd >=0) {
		close(rot_fd);
		rot_fd = -1;
	}

}

int eng_test_camera_init(int32_t camera_id)
{
	int rtn = 0;
	struct eng_test_cmr_context *cmr_cxt_ptr = g_eng_test_cmr_cxt_ptr;

    if(camera_id==1)
        fcamera_bcamera=1;
    else
        fcamera_bcamera=0;
	//first open fb for display
	eng_test_fb_open();

	cmr_cxt_ptr->sensor_id = camera_id;
	cmr_cxt_ptr->capture_width = ENGTEST_PREVIEW_WIDTH;
	cmr_cxt_ptr->capture_height = ENGTEST_PREVIEW_HEIGHT;

	g_mem_method = MemoryHeapIon::Mm_iommu_is_enabled();

	if (CAMERA_SUCCESS != camera_init(cmr_cxt_ptr->sensor_id))
		return -1;

	camera_set_dimensions(cmr_cxt_ptr->capture_width,
					cmr_cxt_ptr->capture_height,
					ENGTEST_PREVIEW_WIDTH,/*cmr_cxt_ptr->capture_width,*/
					ENGTEST_PREVIEW_HEIGHT,/*cmr_cxt_ptr->capture_height,*/
					NULL,
					NULL,
					0);

	if (eng_test_dcam_preview()) {
		rtn = -1;
	}

	return rtn;
}


int eng_test_flashlight_ctrl(uint32_t flash_status)
{
	int                      ret = 0;
	ret = camera_set_flashdevice(flash_status);

	return ret;
}

}

