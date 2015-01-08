#include "testitem.h"
#include"linux/videodev2.h"

/** The following define the IOCTL command values via the ioctl macros */
// ioctl command

static int test_result = -1;
static int fm_fd = -1;
extern char sdcard_fm_state;



static void fm_show_play_stat(int freq, int inpw)
{
    int row = 4;
    char text[128] = {0};

    ui_set_color(CL_GREEN);
    memset(text, 0, sizeof(text));
    sprintf(text, "%s:%d.%dMHz", TEXT_FM_FREQ, freq / 10, freq % 10);           /*show channel*/
    row = ui_show_text(row, 0, text);

    row = ui_show_text(row, 0, TEXT_FM_OK);

    gr_flip();
}


static void fm_show_headset(int state)
{
    int row = 3;
    if(SPRD_HEADSETOUT == state) {
        ui_clear_rows(row, 1);
        ui_set_color(CL_RED);
        ui_show_text(row, 0, TEXT_HD_UNINSERT);
    } else if (SPRD_HEADSETIN == state) {
        ui_clear_rows(row, 1);
        ui_set_color(CL_GREEN);
        ui_show_text(row, 0, TEXT_HD_INSERTED);
    }

    gr_flip();
}

static void fm_show_searching(int state)
{
    int row = 4;

    if (STATE_DISPLAY == state) {
        ui_clear_rows(row, 1);
        ui_set_color(CL_GREEN);
        ui_show_text(row, 0, TEXT_FM_SCANING);
    } else if (STATE_CLEAN== state) {
        ui_clear_rows(row, 1);
    }

    gr_flip();
}

static void fm_seek_timeout(void)
{
    int row = 6;

    ui_clear_rows(row, 2);
    ui_set_color(CL_RED);
    row = ui_show_text(row, 0, TEXT_FM_SEEK_TIMEOUT);
    row = ui_show_text(row, 0, TEXT_FM_FAIL);

    gr_flip();
}

static int fm_check_headset(int cmd, int* headset_state)
{
    int ret;
    char buf[8];
    static int fd = -1;

    if (HEADSET_CHECK == cmd) {
        memset(buf, 0, sizeof(buf));
        lseek(fd, 0, SEEK_SET);
        ret = read(fd, buf, sizeof(buf));
        if(ret < 0) {
            SPRD_DBG("%s: read fd fail error[%d]=%s",__func__,errno, strerror(errno));
            return -1;
        }
        *headset_state = atoi(buf);
    }
    else if (HEADSET_OPEN == cmd) {
        if (fd > 0) {
            SPRD_DBG("headset device already open !");
            return -1;
        }

        fd = open(SPRD_HEADSET_SWITCH_DEV, O_RDONLY);
        if (fd < 0) {
            fd = -1;
            SPRD_DBG("headset device open faile! errno[%d] = %s", errno, strerror(errno));
            return -1;
        }

        SPRD_DBG("open headset device success, fd =  %d", fd);
    }
    else if (HEADSET_CLOSE == cmd) {
        if (-1 == fd) {
            SPRD_DBG("headset device already close !");
            return -1;
        }

        close(fd);
        fd = -1;
    }
    else {
        SPRD_DBG("In %s error command: %d", __FUNCTION__, cmd);
    }

    return 0;
}

static int fm_open(void)
{
    int ret = 0;
    int value = 0;

    fm_fd = open(TROUT_FM_DEV_NAME, O_RDONLY);
    if (fm_fd < 0) {
        SPRD_DBG("mmitest FM open faile! errno[%d] = %s", errno, strerror(errno));
        return -1;
    }

    SPRD_DBG("mmitest FM open success ! fd = %d", fm_fd);

    return 0;

ENABLE_FAILE:
    ret = close(fm_fd);
    if (0 != ret) {
        SPRD_DBG("mmitest FM close faile! errno[%d] = %s", errno, strerror(errno));
    }

    fm_fd = -1;

    return -1;
}

static int fm_get_rssi(int* freq, int* rssi)
{
    int ret;
    int value;
    int buffer[4];      /*freq, dir, timeout, reserve*/

    value = 0;
    ret = ioctl(fm_fd, FM_IOCTL_SET_VOLUME, &value);
    if (0 != ret) {
        SPRD_DBG("FM set mute faile!");
        return -1;
    }
    do
	{
	    buffer[0] = *freq;
	    buffer[1] = 1;
	    buffer[2] = 3000;
	    buffer[3] = 0;

	    SPRD_DBG("(before seek)freq=%d direction=%d timeout=%d reserve=%d",
	            buffer[0], buffer[1], buffer[2], buffer[3]);

	    ret = ioctl(fm_fd, FM_IOCTL_SEARCH, buffer);
	    SPRD_DBG("(after seek)freq=%d direction=%d timeout=%d reserve=%d result=%d",
	            buffer[0], buffer[1], buffer[2], buffer[3], ret);
		LOGD("mmitest new freq=%d",buffer[3]);
	    if (0 != ret) {
	        LOGD("mmitest FM seek timeout!");
	        //return -1;
	    } else {
	        SPRD_DBG("mmitest FM seek success. freq = %d", buffer[3]);
	    }

		*freq=buffer[3];

	}while(ret!=0&&*freq<8700);

    ret = ioctl(fm_fd, FM_IOCTL_SET_TUNE, &buffer[3]);
    if (0 != ret) {
        SPRD_DBG("FM set tune faile!");
        return -1;
    }

    value = 1;
    ret = ioctl(fm_fd, FM_IOCTL_SET_VOLUME, &value);
    if (0 != ret) {
        SPRD_DBG("FM set unmute faile!");
        return -1;
    }

    ret = ioctl(fm_fd, Trout_FM_IOCTL_GET_RSSI, rssi);
    if (0 != ret) {
        SPRD_DBG("FM get rssi faile!");
        return -1;
    }

    *freq = buffer[3];
	sdcard_write_fm(freq);
	LOGD("mmitest new freq write=%d",*freq);

    SPRD_DBG("FM get freq = %d, rssi = %d", *freq, *rssi);

    return 0;
}

static int fm_set_tune(int freq)
{
    int rc;
    struct v4l2_frequency freqt = {0};
    freqt.type = V4L2_TUNER_RADIO;
    freqt.frequency = (freq * 100 * 10000) / 625;

    rc = ioctl(fm_fd, VIDIOC_S_FREQUENCY, &freqt);
    if (rc < 0){
        LOGD("mmitest mmitest Could not set radio frequency");
        return -1;
    }

    return 0;
}

static int fm_search(int direction)
{
    int rc;
    struct v4l2_hw_freq_seek seek = {0};
    seek.type = V4L2_TUNER_RADIO;
    seek.seek_upward = direction;


    rc = ioctl(fm_fd, VIDIOC_S_HW_FREQ_SEEK, &seek);

    return rc;
}


static int fm_get_freq(void)
{
    int rc;
    struct v4l2_frequency freq;
    rc = ioctl(fm_fd, VIDIOC_G_FREQUENCY, &freq);
    if (rc < 0)
    {
        LOGD("mmitest Could not get radio frequency");
        return 0;
    }

    return (freq.frequency * 625) / 10000;
}

static int fm_close(void)
{
    int ret;

    ret = close(fm_fd);
    if (0 != ret) {
        SPRD_DBG("FM close faile! errno[%d] = %s", errno, strerror(errno));
        return -1;
    }

    fm_fd = -1;

    SPRD_DBG("FM close success !");

    return 0;
}

int test_fm_start(void)
{
    int ret;
    int rc;
    int headset_in = 0;
    int freq ;//= 875


	if(sdcard_fm_state==0)
		sdcard_read_fm(&freq);
	else
	    freq=990;


	LOGD("mmitest freq=%d",freq);

    ui_fill_locked();
    ui_show_title(MENU_TEST_FM);

    system(FM_INSMOD_COMMEND);

    ret = fm_check_headset(HEADSET_OPEN, NULL);         /*open headset device*/
    if (ret < 0) {
         goto FM_TEST_FAIL;
    }

    fm_check_headset(HEADSET_CHECK, &headset_in);       /*checket headset state*/
    if (0 == headset_in) {
        SPRD_DBG("%s:%d headset out", __FUNCTION__, __LINE__);

        fm_show_headset(SPRD_HEADSETOUT);                   /*show headset state*/

        fm_check_headset(HEADSET_CLOSE, NULL);    /*close headset device*/

        goto FM_TEST_FAIL;
    }

    ret = fm_check_headset(HEADSET_CLOSE, NULL);        /*close headset device*/
    if (0 != ret) {
        goto FM_TEST_FAIL;
    }

    SPRD_DBG("%s:%d headset in", __FUNCTION__, __LINE__);

    fm_show_headset(SPRD_HEADSETIN);                /*show headset state*/
    fm_show_searching(STATE_DISPLAY);

    ret = fm_open();                            /*open fm*/
    if (0 != ret) {
        goto FM_TEST_FAIL;
    }


    fm_set_tune(freq);
    sleep(2);
    freq=fm_get_freq()/100;
    LOGD("mmitest freq=%d\n",freq);
    do
    {
        rc=fm_search(0);
        freq=fm_get_freq()/100;

        LOGD("mmitest freq=%d\n",freq);
    }while(freq<1080&&rc<0);

    freq=fm_get_freq()/100;

    if (rc<0) {
        test_result=0;
        fm_show_searching(STATE_CLEAN);

        fm_seek_timeout();

        fm_close();
        LOGD("mmitest search failed");
        goto FM_TEST_FAIL;
    }

    if(rc==0)
    {
        test_result=1;
        sdcard_write_fm(&freq);
        fm_show_searching(STATE_CLEAN);
        fm_show_play_stat(freq, -50);           /*display rssi*/
        LOGD("mmitest search success");
    }
    fm_close();                              /*close fm*/
    if(1 == test_result) {
        ret = RL_PASS;
    } else {
FM_TEST_FAIL:
        ret = RL_FAIL;
    }

    sleep(2);

	save_result(CASE_TEST_FM,ret);
    return ret;
}


