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
	ui_clear_rows(0,20);

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
	rtn = ui_handle_button(NULL, NULL);//, TEXT_GOBACK

	typedef void (*pf_eng_test_camera_close)(void);
	pf_eng_test_camera_close eng_test_camera_close = (pf_eng_test_camera_close)dlsym(sprd_handle_camera_dl,"eng_test_camera_close" );
	if(eng_test_camera_close)
	{
		eng_test_camera_close();   //init back camera and start preview
	}

go_exit:
	save_result(CASE_TEST_FCAMERA,rtn);
	return rtn;
}


