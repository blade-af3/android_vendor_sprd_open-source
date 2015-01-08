#include "testitem.h"


int test_tel_start(void)
{
	int cur_row=2;
	int ret,fd;
	int pos;

	ui_fill_locked();
	ui_show_title(MENU_TEST_TEL);
	ui_set_color(CL_GREEN);
	cur_row = ui_show_text(cur_row, 0, TEL_TEST_START);
	cur_row = ui_show_text(cur_row, 0, TEL_TEST_TIPS);
	gr_flip();

	fd=open(TEL_DEVICE_PATH,O_RDWR);

	if(fd<0)
	{
		LOGD("mmitest tel test is faild");
		return RL_FAIL;
	}

	tel_send_at(fd,"AT",NULL,0, 0);

	tel_send_at(fd,"AT+SFUN=2",NULL,0, 0);
    tel_send_at(fd,"AT+SFUN=4",NULL,0, 0);

    pos=tel_send_at(fd, "ATD112;", NULL,NULL, 0);

	ret = ui_handle_button(TEXT_PASS, TEXT_FAIL);


	tel_send_at(fd,"ATH",NULL,0, 0);
	save_result(CASE_TEST_TEL,ret);
	return ret;
}
