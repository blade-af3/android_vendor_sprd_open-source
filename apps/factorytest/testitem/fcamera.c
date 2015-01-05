#include "testitem.h"
#include <dlfcn.h>
#include <cutils/properties.h>


static void *sprd_handle_camera_dl;
static int eng_front_camera =0;

int test_fcamera_start(void)
{
	int rtn = RL_NA;
	SPRD_DBG("%s enter", __FUNCTION__);
	char lib_full_name[60] = { 0 };
	char prop[PROPERTY_VALUE_MAX] = { 0 };


	//eng_front_camera = 1;
	ui_clear_rows(0,20);
	ui_set_color(CL_GREEN);//++++++++++
	ui_show_text(0, 0, CAMERA_START);//++++++
	gr_flip();
	sleep(1);
	
    LOGD("mmitest before open lib\n");
	property_get("ro.hardware", prop, NULL);
	sprintf(lib_full_name, "%scamera.%s.so", LIBRARY_PATH, prop);
	sprd_handle_camera_dl = dlopen(lib_full_name,RTLD_NOW);

	LOGD("mmitest %s\n",lib_full_name);
	if(sprd_handle_camera_dl == NULL)
	{
		LOGD("mmitest %s fail dlopen ", __FUNCTION__);
		rtn = RL_FAIL;
		goto go_exit;
	}

	LOGD("mmitest after open lib\n");
	typedef int (*pf_eng_test_camera_init)(int32_t camera_id);
	pf_eng_test_camera_init eng_test_camera_init = (pf_eng_test_camera_init)dlsym(sprd_handle_camera_dl,"eng_test_camera_init" );
	sleep(1);
	if(eng_test_camera_init)
	{
		if(eng_test_camera_init(1))   //init front camera and start preview
		{
			LOGD("mmitest %s grh fail to call eng_test_camera_init ", __FUNCTION__);
			rtn = RL_FAIL;
			goto go_exit;
		}
	}


	LOGD("mmitest %s start preview with front camera", __FUNCTION__);
	//eng_draw_handle_softkey(ENG_ITEM_FCAMERA);
	rtn = ui_handle_button(TEXT_PASS, TEXT_FAIL);//, TEXT_GOBACK

	typedef void (*pf_eng_test_camera_close)(void);
	pf_eng_test_camera_close eng_test_camera_close = (pf_eng_test_camera_close)dlsym(sprd_handle_camera_dl,"eng_test_camera_close" );
	if(eng_test_camera_close)
	{
		eng_test_camera_close();   //init back camera and start preview
	}

go_exit:
	//if(sprd_handle_camera_dl)
//		dlclose(sprd_handle_camera_dl);
	ui_fill_locked();
	ui_show_title(MENU_TEST_FCAMERA);
	if(rtn == RL_FAIL) {
		ui_set_color(CL_RED);
		ui_show_text(3, 0, TEXT_TEST_FAIL);
	} else if(rtn == RL_PASS) {
		ui_set_color(CL_GREEN);
		ui_show_text(3, 0, TEXT_TEST_PASS);
	} else {
		ui_set_color(CL_WHITE);
		ui_show_text(3, 0, TEXT_TEST_NA);
	}
	gr_flip();
	sleep(1);

	return rtn;
}


