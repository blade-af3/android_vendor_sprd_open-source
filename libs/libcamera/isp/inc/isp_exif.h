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
#ifndef _ISP_EXIF_H_
#define _ISP_EXIF_H_

struct exit_blc_param{
	uint32_t mode;
	uint16_t r;
	uint16_t gr;
	uint16_t gb;
	uint16_t b;
};

struct exit_nlc_param{
	uint16_t r_node[29];
	uint16_t g_node[29];
	uint16_t b_node[29];
	uint16_t l_node[29];
};

struct exit_lnc_param{
	uint16_t grid;
	uint16_t r_pec;
	uint16_t g_pec;
	uint16_t b_pec;
};

struct exit_awb_map{
	uint16_t *addr;
	uint32_t len;		//by bytes
};


struct exit_ae_param{
	uint8_t iso;
	uint8_t exposure;
	uint8_t gain;
};

struct exit_awb_param{
	uint16_t alg_id;
	uint16_t r_gain;
	uint16_t g_gain;
	uint16_t b_gain;
};

struct exit_bpc_param{
	uint16_t flat_thr;
	uint16_t std_thr;
	uint16_t texture_thr;
	uint16_t reserved;
};

struct exit_denoise_param{
	uint32_t write_back;
	uint16_t r_thr;
	uint16_t g_thr;
	uint16_t b_thr;
	uint8_t diswei[19];
	uint8_t ranwei[31];
	uint8_t reserved1;
	uint8_t reserved0;
};

struct exit_grgb_param{
	uint16_t edge_thr;
	uint16_t diff_thr;
};

struct exit_cfa_param{
	uint16_t edge_thr;
	uint16_t diff_thr;
};

struct exit_cmc_param{
	uint16_t matrix[9];
	uint16_t reserved;
};

struct exit_cce_parm{
	uint16_t matrix[9];
	uint16_t y_shift;
	uint16_t u_shift;
	uint16_t v_shift;
};

struct exit_gamma_param{
	uint16_t axis[2][26];
};

struct exit_cce_uvdiv{
	uint8_t thrd[7];
	uint8_t t[2];
	uint8_t m[3];
};

struct exit_pref_param{
	uint8_t write_back;
	uint8_t y_thr;
	uint8_t u_thr;
	uint8_t v_thr;
};

struct exit_bright_param{
	uint8_t factor;
};

struct exit_contrast_param{
	uint8_t factor;
};

struct exit_hist_param
{
	uint16_t low_ratio;
	uint16_t high_ratio;
	uint8_t mode;
	uint8_t reserved2;
	uint8_t reserved1;
	uint8_t reserved0;
};

struct exit_auto_contrast_param{
	uint8_t mode;
	uint8_t reserved2;
	uint8_t reserved1;
	uint8_t reserved0;
};

struct exit_saturation_param{
	uint8_t factor;
};

struct exit_af_param{
	uint16_t alg_id;
	uint16_t cur_step;
	uint16_t edge_info[32];
};

struct exit_emboss_param{
	uint8_t step;
	uint8_t reserved2;
	uint8_t reserved1;
	uint8_t reserved0;
};

struct exit_edge_info{
	uint8_t detail_thr;
	uint8_t smooth_thr;
	uint8_t strength;
	uint8_t reserved;
};

struct exit_global_gain_param{
	uint32_t gain;
};

struct exit_chn_gain_param{
	uint8_t r_gain;
	uint8_t g_gain;
	uint8_t b_gain;
	uint8_t reserved0;
	uint16_t r_offset;
	uint16_t g_offset;
	uint16_t b_offset;
	uint16_t reserved1;
};

struct exit_flash_cali_param{
	uint16_t effect;
	uint16_t lum_ratio;
	uint16_t r_ratio;
	uint16_t g_ratio;
	uint16_t b_ratio;
};

struct exit_css_param{
	uint8_t low_thr[7];
	uint8_t lum_thr;
	uint8_t low_sum_thr[7];
	uint8_t chr_thr;
};

typedef struct {
	uint32_t is_exif_validate;
	uint32_t tool_version;
	uint32_t version_id;
	uint32_t info_len;
	uint32_t blc_bypass;
	uint32_t nlc_bypass;
	uint32_t lnc_bypass;
	uint32_t ae_bypass;
	uint32_t awb_bypass;
	uint32_t bpc_bypass;
	uint32_t denoise_bypass;
	uint32_t grgb_bypass;
	uint32_t cmc_bypass;
	uint32_t gamma_bypass;
	uint32_t uvdiv_bypass;
	uint32_t pref_bypass;
	uint32_t bright_bypass;
	uint32_t contrast_bypass;
	uint32_t hist_bypass;
	uint32_t auto_contrast_bypass;
	uint32_t af_bypass;
	uint32_t edge_bypass;
	uint32_t fcs_bypass;
	uint32_t css_bypass;
	uint32_t saturation_bypass;
	uint32_t hdr_bypass;
	uint32_t glb_gain_bypass;
	uint32_t chn_gain_bypass;

	struct exit_blc_param blc;
	struct exit_nlc_param nlc;
	struct exit_lnc_param lnc;
	struct exit_ae_param ae;
	struct exit_awb_param awb;
	struct exit_bpc_param bpc;
	struct exit_denoise_param denoise;
	struct exit_grgb_param grgb;
	struct exit_cfa_param cfa;
	struct exit_cmc_param cmc;
	struct exit_gamma_param gamma;
	struct exit_cce_parm cce;
	struct exit_cce_uvdiv uv_div;
	struct exit_pref_param pref;
	struct exit_bright_param bright;
	struct exit_contrast_param contrast;
	struct exit_hist_param hist;
	struct exit_auto_contrast_param auto_contrast;
	struct exit_saturation_param saturation;
	struct exit_css_param css;
	struct exit_af_param af;
	struct exit_edge_info edge;
	struct exit_emboss_param emboss;
	struct exit_global_gain_param global;
	struct exit_chn_gain_param chn;
	struct exit_flash_cali_param flash;
 }EXIF_ISP_INFO_T;

#endif //_ISP_EXIF_H_