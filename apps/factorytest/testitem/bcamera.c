#include "testitem.h"
#include <unistd.h>
#include <linux/input.h>
#include <fcntl.h>
#include <errno.h>
#include <dlfcn.h>
#include <cutils/properties.h>


/*
#include <utils/Log.h>
#include <utils/String16.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <linux/ion.h>
#include <binder/MemoryHeapIon.h>
#include <camera/Camera.h>
#include <semaphore.h>
#include <linux/fb.h>

using namespace android;


*/




enum cmr_flash_status {
	FLASH_CLOSE = 0x0,
	FLASH_OPEN = 0x1,
	FLASH_TORCH = 0x2,/*user only set flash to close/open/torch state */
	FLASH_AUTO = 0x3,
	FLASH_CLOSE_AFTER_OPEN = 0x10,/* following is set to sensor */
	FLASH_HIGH_LIGHT = 0x11,
	FLASH_OPEN_ON_RECORDING = 0x22,
	FLASH_CLOSE_AFTER_AUTOFOCUS = 0x30,
	FLASH_STATUS_MAX
};

static void *sprd_handle_camera_dl;

/*
int test_bcamera_start(void)
{
	volatile int  rtn = RL_FAIL;
	char lib_full_name[60] = { 0 };
	char prop[PROPERTY_VALUE_MAX] = { 0 };

	SPRD_DBG("%s enter", __FUNCTION__);

	ui_clear_rows(0,20);
	//ui_set_color(CL_GREEN);//++++++++++
	//ui_show_text(0, 0, CAMERA_START);//++++++
	//ui_show_text(3, 0, CAMERA_LIGHT_ON);//++++++
	//gr_flip();
	//sleep(2);


	property_get("ro.hardware", prop, NULL);
	sprintf(lib_full_name, "%scamera.%s.so", LIBRARY_PATH, prop);
	sprd_handle_camera_dl = dlopen(lib_full_name,RTLD_NOW);
	if(sprd_handle_camera_dl == NULL)
{
		SPRD_DBG("%s fail dlopen ", __FUNCTION__);
		rtn = RL_FAIL;
		goto go_exit;
	}


	typedef int (*pf_eng_test_camera_init)(int32_t camera_id);
	pf_eng_test_camera_init eng_test_camera_init = (pf_eng_test_camera_init)dlsym(sprd_handle_camera_dl,"eng_test_camera_init" );
	if(eng_test_camera_init)
	{
		if(eng_test_camera_init(0))   //init back camera and start preview
		{
			SPRD_DBG("%s fail to call eng_test_camera_init ", __FUNCTION__);
		}
	}
	else
	{
		SPRD_DBG("%s fail to find eng_test_camera_init() ", __FUNCTION__);
		rtn = RL_FAIL;
		goto go_exit;
	}




    


	SPRD_DBG("%s start preview with Back camera", __FUNCTION__);
	//eng_draw_handle_softkey(ENG_ITEM_BCAMERA);
	rtn = ui_handle_button(NULL, NULL);//, TEXT_GOBACK



	typedef void (*pf_eng_test_camera_close)(void);
	pf_eng_test_camera_close eng_test_camera_close = (pf_eng_test_camera_close)dlsym(sprd_handle_camera_dl,"eng_test_camera_close" );
	if(eng_test_camera_close)
	{
		eng_test_camera_close();   
	}
	else{
		SPRD_DBG("%s fail to find eng_test_camera_close ", __FUNCTION__);

	}

go_exit:


	ui_fill_locked();
	ui_show_title(MENU_TEST_BCAMERA);
	if(rtn == RL_FAIL) {
		ui_set_color(CL_RED);
		ui_show_text(3, 0, TEXT_TEST_FAIL);
	}
	else if(rtn == RL_PASS) {
		ui_set_color(CL_GREEN);
		ui_show_text(3, 0, TEXT_TEST_PASS);
	}
	else if(rtn==RL_NA)
	{
		ui_set_color(CL_WHITE);
		ui_show_text(3, 0, TEXT_TEST_NA);
	}
	gr_flip();
	sleep(1);
	save_result(CASE_TEST_BCAMERA,rtn);
	save_result(CASE_TEST_FLASH,rtn);
	return rtn;
}


*/



int test_bcamera_start(void)
{
	volatile int  rtn = RL_FAIL;
	char lib_full_name[60] = { 0 };
	char prop[PROPERTY_VALUE_MAX] = { 0 };

	SPRD_DBG("%s enter", __FUNCTION__);

	ui_clear_rows(0,20); 


	property_get("ro.hardware", prop, NULL);
	sprintf(lib_full_name, "%scamera.%s.so", LIBRARY_PATH, prop);
	sprd_handle_camera_dl = dlopen(lib_full_name,RTLD_NOW);
	if(sprd_handle_camera_dl == NULL)
{
		SPRD_DBG("%s fail dlopen ", __FUNCTION__);
		rtn = RL_FAIL;
		goto go_end;
	}

	
	typedef int (*pf_eng_test_camera_init)(int32_t camera_id);
	pf_eng_test_camera_init eng_test_camera_init = (pf_eng_test_camera_init)dlsym(sprd_handle_camera_dl,"eng_test_camera_init" );
	if(eng_test_camera_init)
	{
		if(eng_test_camera_init(0))   //init back camera and start preview
		{
			SPRD_DBG("%s fail to call eng_test_camera_init ", __FUNCTION__);
		}
	}
	else
	{
		SPRD_DBG("%s fail to find eng_test_camera_init() ", __FUNCTION__);
		rtn = RL_FAIL;
		goto go_end;
	}


	typedef int (*pf_eng_test_flashlight_ctrl)(int32_t flash_status);
	pf_eng_test_flashlight_ctrl eng_test_flashlight_ctrl = (pf_eng_test_flashlight_ctrl)dlsym(sprd_handle_camera_dl,"eng_test_flashlight_ctrl" );
	if(eng_test_flashlight_ctrl)
	{
		if(eng_test_flashlight_ctrl(FLASH_OPEN))
		{
			SPRD_DBG("%s fail to open flash light ", __FUNCTION__);
		}
	}else{
		SPRD_DBG("%s fail to find eng_test_flashlight_ctrl() ", __FUNCTION__);
		rtn = RL_FAIL;
		goto go_end;
	}

	//SPRD_DBG("%s start preview with Back camera", __FUNCTION__);
	//eng_draw_handle_softkey(ENG_ITEM_BCAMERA);
	rtn = ui_handle_button(NULL, NULL);//, TEXT_GOBACK

	if(eng_test_flashlight_ctrl){
		if(eng_test_flashlight_ctrl(FLASH_CLOSE)){
			SPRD_DBG("%s grh fail to close flash light ", __FUNCTION__);
		}
	}else{
		SPRD_DBG("%s fail to find eng_test_flashlight_ctrl() ", __FUNCTION__);
		//rtn = 1;
		goto go_end;
	}

	typedef void (*pf_eng_test_camera_close)(void);
	pf_eng_test_camera_close eng_test_camera_close = (pf_eng_test_camera_close)dlsym(sprd_handle_camera_dl,"eng_test_camera_close" );
	if(eng_test_camera_close)
	{
		eng_test_camera_close();   //init back camera and start preview
	}
	else{
		SPRD_DBG("%s fail to find eng_test_camera_close ", __FUNCTION__); 
	}

go_end:
 
  
	save_result(CASE_TEST_BCAMERA,rtn);
	save_result(CASE_TEST_FLASH,rtn);
	return rtn;
}


