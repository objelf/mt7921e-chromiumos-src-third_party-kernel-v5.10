/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 MediaTek Inc.
 */


#ifndef __DRTX_TYPE_H__
#define __DRTX_TYPE_H__
#include "mtk_drm_ddp_comp.h"
#include <drm/drm_bridge.h>
#include <drm/drm_bridge_connector.h>
#include <drm/drm_device.h>
#include <drm/drm_dp_helper.h>
#include "drm/mediatek_drm.h"
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>

#include "mtk_dp_hdcp.h"
#include "mtk_dp_debug.h"


#ifndef BYTE
#define BYTE    unsigned char
#endif
#ifndef WORD
#define WORD    unsigned short
#endif
#ifndef DWORD
#define DWORD   unsigned long
#endif

#ifndef UINT32
#define UINT32  unsigned int
#endif

#ifndef UINT8
#define UINT8   unsigned char
#endif


#define EDID_SIZE 0x200
#define ENABLE_DPTX_SSC_FORCEON		0
#define ENABLE_DPTX_FIX_LRLC		0
#define ENABLE_DPTX_SSC_OUTPUT		1
#define ENABLE_DPTX_FIX_TPS2		0
#define AUX_WRITE_READ_WAIT_TIME        20 //us
#define DPTX_SUPPORT_DSC                0

enum DP_ATF_CMD {
	DP_ATF_DUMP = 0x20,
	DP_ATF_VIDEO_UNMUTE = 0x20,
	DP_ATF_EDP_VIDEO_UNMUTE,
	DP_ATF_REG_WRITE,
	DP_ATF_REG_READ,
	DP_ATF_CMD_COUNT
};

union MISC_T {
	struct {
		BYTE is_sync_clock : 1;
		BYTE color_format : 2;
		BYTE spec_def1 : 2;
		BYTE color_depth : 3;

		BYTE interlaced : 1;
		BYTE stereo_attr : 2;
		BYTE reserved : 3;
		BYTE is_vsc_sdp : 1;
		BYTE spec_def2 : 1;

	} dp_misc;
	BYTE ucMISC[2];
};

struct DPTX_TIMING_PARAMETER {
	WORD Htt;
	WORD Hde;
	WORD Hbk;
	WORD Hfp;
	WORD Hsw;

	bool bHsp;
	WORD Hbp;
	WORD Vtt;
	WORD Vde;
	WORD Vbk;
	WORD Vfp;
	WORD Vsw;

	bool bVsp;
	WORD Vbp;
	BYTE FrameRate;
	DWORD PixRateKhz;
	int Video_ip_mode;
};

struct DPTX_TRAINING_INFO {
	bool bSinkEXTCAP_En;
	bool bTPS3;
	bool bTPS4;
	bool bSinkSSC_En;
	bool bDPTxAutoTest_EN;
	bool bCablePlugIn;
	bool bCableStateChange;
	bool bDPMstCAP;
	bool bDPMstBranch;
	bool bDWN_STRM_PORT_PRESENT;
	bool cr_done;
	bool eq_done;

	BYTE ubDPSysVersion;
	BYTE ubSysMaxLinkRate;
	BYTE ubLinkRate;
	BYTE ubLinkLaneCount;
	WORD usPHY_STS;
	BYTE ubDPCD_REV;
	BYTE ubSinkCountNum;
	BYTE ucCheckCapTimes;
};

struct DPTX_INFO {
	uint8_t input_src;
	uint8_t depth;
	uint8_t format;
	uint8_t resolution;
	unsigned int audio_caps;
	unsigned int audio_config;
	struct DPTX_TIMING_PARAMETER DPTX_OUTBL;

	bool bPatternGen;
	bool bSinkSSC_En;
	bool bSetAudioMute;
	bool bSetVideoMute;
	bool bAudioMute;
	bool bVideoMute;
	bool bForceHDCP1x;

#ifdef DPTX_HDCP_ENABLE
	BYTE bAuthStatus;
	struct HDCP1X_INFO hdcp1x_info;
	struct HDCP2_INFO hdcp2_info;
#endif

};

struct mtk_dp_driver_data {
	bool is_edp;
};

struct mtk_dp {
	struct device *dev;
	struct mtk_ddp_comp ddp_comp;
	struct drm_device *drm_dev;
	struct drm_bridge bridge;
	struct drm_bridge *next_bridge;
	struct drm_connector conn;
	struct drm_encoder enc;
	struct work_struct work;
	struct workqueue_struct *workqueue;
	u8 bridge_attached;
	int id;
	struct edid *edid;
	struct drm_dp_aux aux;
	u8 rx_cap[16];
	struct drm_display_mode mode;
	struct DPTX_INFO info;
	int state;
	int state_pre;
	struct DPTX_TRAINING_INFO training_info;
	int training_state;
	int training_state_pre;

	wait_queue_head_t irq_wq;
	wait_queue_head_t control_wq;
	wait_queue_head_t notify_wq;
	u8 irq_status;
	struct task_struct *task;
	struct task_struct *control_task;
	struct task_struct *notify_task;

	u32 min_clock;
	u32 max_clock;
	u32 max_hdisplay;
	u32 max_vdisplay;

	void __iomem *regs;
	struct clk *dp_tx_clk;

	bool bUeventToHwc;
	int disp_status;  //for DDP
	bool bPowerOn;
	bool audio_enable;
	bool video_enable;
	bool dp_ready;
	bool has_dsc;
	bool has_fec;
	bool dsc_enable;
	bool enabled;
	bool powered;
	struct mtk_drm_private *priv;
	struct mutex dp_lock;
	const struct mtk_dp_driver_data *driver_data;
};
#endif /*__DRTX_TYPE_H__*/

