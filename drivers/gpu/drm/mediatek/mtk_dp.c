// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/arm-smccc.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/of_gpio.h>
#include <linux/of_graph.h>
#include <linux/phy/phy.h>
#include <linux/regmap.h>
#include <linux/component.h>
#include <linux/of_device.h>
#include <linux/extcon.h>
#include <linux/extcon-provider.h>
#include <linux/kthread.h>
#include <linux/errno.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_bridge.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_edid.h>
#include <drm/drm_dp_helper.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/mediatek_drm.h>
#include <drm/drm_of.h>
#include <drm/drm_panel.h>
#include "mtk_drm_ddp_comp.h"
#include <video/videomode.h>

#include "mtk_dp.h"
#include "mtk_dp_hal.h"
#include "mtk_drm_drv.h"
#include "mtk_dp_api.h"
#include "mtk_dp_reg.h"

static inline struct mtk_dp *dp_from_conn(struct drm_connector *c)
{
	return container_of(c, struct mtk_dp, conn);
}

static inline struct mtk_dp *dp_from_bridge(struct drm_bridge *b)
{
	return container_of(b, struct mtk_dp, bridge);
}

static bool mdrv_DPTx_AuxWrite_Bytes(struct mtk_dp *mtk_dp, u8 ubCmd,
	u32  usDPCDADDR, size_t ubLength, BYTE *pData)
{
	bool bReplyStatus = false;
	u8 ubRetryLimit = 0x7;

	if (!mtk_dp->training_info.bCablePlugIn ||
		((mtk_dp->training_info.usPHY_STS & (HPD_DISCONNECT)) != 0x0)) {

		if (!mtk_dp->training_info.bDPTxAutoTest_EN)
			mtk_dp->training_state = DPTX_NTSTATE_CHECKCAP;
		return false;
	}

	do {
		bReplyStatus = mhal_DPTx_AuxWrite_Bytes(mtk_dp, ubCmd,
			usDPCDADDR, ubLength, pData);
		ubRetryLimit--;
		if (!bReplyStatus) {
			udelay(50);
		} else
			return true;
	} while (ubRetryLimit > 0);

	pr_err("Aux Write Fail: cmd = %d, addr = 0x%x, len = %ld\n",
		ubCmd, usDPCDADDR, ubLength);

	return false;
}

static bool mdrv_DPTx_AuxWrite_DPCD(struct mtk_dp *mtk_dp, u8 ubCmd,
	u32 usDPCDADDR, size_t ubLength, BYTE *pData)
{
	bool bRet = true;
	size_t times = 0;
	size_t remain = 0;
	size_t loop = 0;

	if (ubLength > DP_AUX_MAX_PAYLOAD_BYTES) {
		times = ubLength / DP_AUX_MAX_PAYLOAD_BYTES;
		remain = ubLength % DP_AUX_MAX_PAYLOAD_BYTES;

		for (loop = 0; loop < times; loop++)
			bRet &= mdrv_DPTx_AuxWrite_Bytes(mtk_dp,
				ubCmd,
				usDPCDADDR + (loop * DP_AUX_MAX_PAYLOAD_BYTES),
				DP_AUX_MAX_PAYLOAD_BYTES,
				pData + (loop * DP_AUX_MAX_PAYLOAD_BYTES));

		if (remain > 0)
			bRet &= mdrv_DPTx_AuxWrite_Bytes(mtk_dp,
				ubCmd,
				usDPCDADDR + (times * DP_AUX_MAX_PAYLOAD_BYTES),
				remain,
				pData + (times * DP_AUX_MAX_PAYLOAD_BYTES));
	} else {
		bRet &= mdrv_DPTx_AuxWrite_Bytes(mtk_dp,
				ubCmd,
				usDPCDADDR,
				ubLength,
				pData);
	}

	return bRet;
}


static bool mdrv_DPTx_AuxRead_Bytes(struct mtk_dp *mtk_dp, u8 ubCmd,
	u32 usDPCDADDR, size_t ubLength, BYTE *pData)
{
	bool bReplyStatus = false;
	u8 ubRetryLimit = 7;

	if (!mtk_dp->training_info.bCablePlugIn ||
		((mtk_dp->training_info.usPHY_STS & (HPD_DISCONNECT)) != 0x0)) {
		if (!mtk_dp->training_info.bDPTxAutoTest_EN)
			mtk_dp->training_state = DPTX_NTSTATE_CHECKCAP;
		return false;
	}


	do {
		bReplyStatus = mhal_DPTx_AuxRead_Bytes(mtk_dp, ubCmd,
					usDPCDADDR, ubLength, pData);
		if (!bReplyStatus) {
			udelay(50);
		} else
			return true;

		ubRetryLimit--;
	} while (ubRetryLimit > 0);

	pr_err("Aux Read Fail: cmd = %d, addr = 0x%x, len = %ld\n",
		ubCmd, usDPCDADDR, ubLength);

	return false;
}

static bool mdrv_DPTx_AuxRead_DPCD(struct mtk_dp *mtk_dp, u8 ubCmd,
	u32 usDPCDADDR, size_t ubLength, BYTE *pRxBuf)
{
	bool bRet = true;
	size_t times = 0;
	size_t remain = 0;
	size_t loop = 0;

	if (ubLength > DP_AUX_MAX_PAYLOAD_BYTES) {
		times = ubLength / DP_AUX_MAX_PAYLOAD_BYTES;
		remain = ubLength % DP_AUX_MAX_PAYLOAD_BYTES;

		for (loop = 0; loop < times; loop++)
			bRet &= mdrv_DPTx_AuxRead_Bytes(mtk_dp,
				ubCmd,
				usDPCDADDR + (loop * DP_AUX_MAX_PAYLOAD_BYTES),
				DP_AUX_MAX_PAYLOAD_BYTES,
				pRxBuf + (loop * DP_AUX_MAX_PAYLOAD_BYTES));

		if (remain > 0)
			bRet &= mdrv_DPTx_AuxRead_Bytes(mtk_dp,
				ubCmd,
				usDPCDADDR + (times * DP_AUX_MAX_PAYLOAD_BYTES),
				remain,
				pRxBuf + (times * DP_AUX_MAX_PAYLOAD_BYTES));
	} else
		bRet &= mdrv_DPTx_AuxRead_Bytes(mtk_dp,
				ubCmd,
				usDPCDADDR,
				ubLength,
				pRxBuf);

	return bRet;
}

static void mdrv_DPTx_InitVariable(struct mtk_dp *mtk_dp)
{
	mtk_dp->training_info.ubDPSysVersion = DP_VERSION_14;
	mtk_dp->training_info.ubLinkRate = DP_LINKRATE_HBR2;
	mtk_dp->training_info.ubLinkLaneCount = DP_LANECOUNT_4;
	mtk_dp->training_info.bSinkEXTCAP_En = false;
	mtk_dp->training_info.bSinkSSC_En = false;
	mtk_dp->training_info.bTPS3 = true;
	mtk_dp->training_info.bTPS4 = true;
	mtk_dp->training_info.usPHY_STS = HPD_INITIAL_STATE;
	mtk_dp->training_info.bCablePlugIn = false;
	mtk_dp->training_info.bCableStateChange = false;
	mtk_dp->training_state = DPTX_NTSTATE_STARTUP;
	mtk_dp->training_state_pre = DPTX_NTSTATE_STARTUP;
	mtk_dp->state = DPTXSTATE_INITIAL;
	mtk_dp->state_pre = DPTXSTATE_INITIAL;

	mtk_dp->info.input_src = DPTX_SRC_DPINTF;
	mtk_dp->info.format = DP_COLOR_FORMAT_RGB_444;
	mtk_dp->info.depth = DP_COLOR_DEPTH_8BIT;
	if (!mtk_dp->info.bPatternGen)
		mtk_dp->info.resolution = SINK_3840_2160;
	mtk_dp->info.bSetAudioMute = false;
	mtk_dp->info.bSetVideoMute = false;
	memset(&mtk_dp->info.DPTX_OUTBL, 0,
		sizeof(struct DPTX_TIMING_PARAMETER));
	mtk_dp->info.DPTX_OUTBL.FrameRate = 60;

	mtk_dp->bPowerOn = false;
	mtk_dp->video_enable = false;
	mtk_dp->dp_ready = false;
	mtk_dp->has_dsc = false;
	mtk_dp->has_fec = false;
	mtk_dp->dsc_enable = false;

	mdrv_DPTx_CheckMaxLinkRate(mtk_dp);
}

static void mdrv_DPTx_SetSDP_DownCntinit(struct mtk_dp *mtk_dp,
	u16 Sram_Read_Start)
{
	u16 SDP_Down_Cnt_Init = 0x0000;
	u8 ucDCOffset;

	if (mtk_dp->info.DPTX_OUTBL.PixRateKhz > 0)
		SDP_Down_Cnt_Init = (Sram_Read_Start *
			mtk_dp->training_info.ubLinkRate * 2700 * 8)
			/ (mtk_dp->info.DPTX_OUTBL.PixRateKhz * 4);

	switch (mtk_dp->training_info.ubLinkLaneCount) {
	case DP_LANECOUNT_1:
		SDP_Down_Cnt_Init = (SDP_Down_Cnt_Init > 0x1A) ?
			SDP_Down_Cnt_Init : 0x1A;  //26
		break;
	case DP_LANECOUNT_2:
		// case for LowResolution && High Audio Sample Rate
		ucDCOffset = (mtk_dp->info.DPTX_OUTBL.Vtt <= 525) ?
			0x04 : 0x00;
		SDP_Down_Cnt_Init = (SDP_Down_Cnt_Init > 0x10) ?
			SDP_Down_Cnt_Init : 0x10 + ucDCOffset; //20 or 16
		break;
	case DP_LANECOUNT_4:
		SDP_Down_Cnt_Init = (SDP_Down_Cnt_Init > 0x06) ?
			SDP_Down_Cnt_Init : 0x06; //6
		break;
	default:
		SDP_Down_Cnt_Init = (SDP_Down_Cnt_Init > 0x06) ?
			SDP_Down_Cnt_Init : 0x06;
		break;
	}

	pr_info("PixRateKhz = %ld SDP_DC_Init = %x\n",
		mtk_dp->info.DPTX_OUTBL.PixRateKhz, SDP_Down_Cnt_Init);

	mhal_DPTx_SetSDP_DownCntinit(mtk_dp, SDP_Down_Cnt_Init);
}

static void mdrv_DPTx_SetSDP_DownCntinitInHblanking(struct mtk_dp *mtk_dp)
{
	int PixClkMhz;
	u8 ucDCOffset;

	PixClkMhz = (mtk_dp->info.format == DP_COLOR_FORMAT_YUV_420) ?
		mtk_dp->info.DPTX_OUTBL.PixRateKhz/2000 :
		mtk_dp->info.DPTX_OUTBL.PixRateKhz/1000;

	switch (mtk_dp->training_info.ubLinkLaneCount) {
	case DP_LANECOUNT_1:
		mhal_DPTx_SetSDP_DownCntinitInHblanking(mtk_dp, 0x0020);
		break;
	case DP_LANECOUNT_2:
		ucDCOffset = (mtk_dp->info.DPTX_OUTBL.Vtt <= 525) ? 0x14 : 0x00;
		mhal_DPTx_SetSDP_DownCntinitInHblanking(mtk_dp,
			0x0018 + ucDCOffset);
		break;
	case DP_LANECOUNT_4:
		ucDCOffset = (mtk_dp->info.DPTX_OUTBL.Vtt <= 525) ? 0x08 : 0x00;
		if (PixClkMhz > (mtk_dp->training_info.ubLinkRate * 27)) {
			mhal_DPTx_SetSDP_DownCntinitInHblanking(mtk_dp, 0x0008);
			pr_info("Pixclk > LinkRateChange\n");
		} else {
			mhal_DPTx_SetSDP_DownCntinitInHblanking(mtk_dp,
				0x0010 + ucDCOffset);
		}
		break;
	}

}

static void mdrv_DPTx_SetTU(struct mtk_dp *mtk_dp)
{
	int TU_size = 0;
	int NValue = 0;
	int FValue = 0;
	int PixRateMhz = 0;
	u8 ColorBpp;
	u16 Sram_Read_Start = DPTX_TBC_BUF_ReadStartAdrThrd;

	ColorBpp = mhal_DPTx_GetColorBpp(mtk_dp);
	PixRateMhz = mtk_dp->info.DPTX_OUTBL.PixRateKhz/1000;
	TU_size = (640*(PixRateMhz)*ColorBpp)/
			(mtk_dp->training_info.ubLinkRate * 27 *
				mtk_dp->training_info.ubLinkLaneCount * 8);

	NValue = TU_size / 10;
	FValue = TU_size-NValue * 10;

	pr_info("TU_size %d, FValue %d\n", TU_size, FValue);
	if (mtk_dp->training_info.ubLinkLaneCount > 0) {
		Sram_Read_Start = mtk_dp->info.DPTX_OUTBL.Hde /
			(mtk_dp->training_info.ubLinkLaneCount*4*2*2);
		Sram_Read_Start =
			(Sram_Read_Start < DPTX_TBC_BUF_ReadStartAdrThrd) ?
			Sram_Read_Start : DPTX_TBC_BUF_ReadStartAdrThrd;
		mhal_DPTx_SetTU_SramRdStart(mtk_dp, Sram_Read_Start);
	}

	mhal_DPTx_SetTU_SetEncoder(mtk_dp);
	mdrv_DPTx_SetSDP_DownCntinitInHblanking(mtk_dp);
	mdrv_DPTx_SetSDP_DownCntinit(mtk_dp, Sram_Read_Start);
}

static void mdrv_DPTx_CalculateMN(struct mtk_dp *mtk_dp)
{
	int ubTargetFrameRate = 60;
	int ulTargetPixelclk = 148500000; // default set FHD

	if (mtk_dp->info.DPTX_OUTBL.FrameRate > 0) {
		ubTargetFrameRate = mtk_dp->info.DPTX_OUTBL.FrameRate;
		ulTargetPixelclk = (int)mtk_dp->info.DPTX_OUTBL.Htt*
			(int)mtk_dp->info.DPTX_OUTBL.Vtt*ubTargetFrameRate;
	} else if (mtk_dp->info.DPTX_OUTBL.PixRateKhz > 0) {
		ulTargetPixelclk = mtk_dp->info.DPTX_OUTBL.PixRateKhz * 1000;
	} else {
		ulTargetPixelclk = (int)mtk_dp->info.DPTX_OUTBL.Htt *
			(int)mtk_dp->info.DPTX_OUTBL.Vtt * ubTargetFrameRate;
	}

	if (ulTargetPixelclk > 0)
		mtk_dp->info.DPTX_OUTBL.PixRateKhz = ulTargetPixelclk / 1000;
}

static void mdrv_DPTx_Set_MISC(struct mtk_dp *mtk_dp)
{
	u8 format, depth;
	union MISC_T DPTX_MISC;

	format = mtk_dp->info.format;
	depth = mtk_dp->info.depth;

	// MISC 0/1 refernce to spec 1.4a p143 Table 2-96
	// MISC0[7:5] color depth
	switch (depth) {
	case DP_COLOR_DEPTH_6BIT:
	case DP_COLOR_DEPTH_8BIT:
	case DP_COLOR_DEPTH_10BIT:
	case DP_COLOR_DEPTH_12BIT:
	case DP_COLOR_DEPTH_16BIT:
	default:
		DPTX_MISC.dp_misc.color_depth = depth;
		break;
	}

	// MISC0[3]: 0->RGB, 1->YUV
	// MISC0[2:1]: 01b->4:2:2, 10b->4:4:4
	switch (format) {
	case DP_COLOR_FORMAT_YUV_444:
		DPTX_MISC.dp_misc.color_format = 0x1;  //01'b
		break;

	case DP_COLOR_FORMAT_YUV_422:
		DPTX_MISC.dp_misc.color_format = 0x2;  //10'b
		break;

	case DP_COLOR_FORMAT_YUV_420:
		//not support
		break;

	case DP_COLOR_FORMAT_RAW:
		DPTX_MISC.dp_misc.color_format = 0x1;
		DPTX_MISC.dp_misc.spec_def2 = 0x1;
		break;
	case DP_COLOR_FORMAT_YONLY:
		DPTX_MISC.dp_misc.color_format = 0x0;
		DPTX_MISC.dp_misc.spec_def2 = 0x1;
		break;

	case DP_COLOR_FORMAT_RGB_444:
	default:
		break;
	}

	mhal_DPTx_SetMISC(mtk_dp, DPTX_MISC.ucMISC);
}

static void mdrv_DPTx_SetDPTXOut(struct mtk_dp *mtk_dp)
{
	mhal_DPTx_EnableBypassMSA(mtk_dp, false);
	mdrv_DPTx_CalculateMN(mtk_dp);

	switch (mtk_dp->info.input_src) {
	case DPTX_SRC_PG:
		mhal_DPTx_PGEnable(mtk_dp, true);
		mhal_DPTx_Set_MVIDx2(mtk_dp, false);
		pr_info("Set Pattern Gen output\n");
		break;

	case DPTX_SRC_DPINTF:
		mhal_DPTx_PGEnable(mtk_dp, false);
		pr_info("Set dpintf output\n");
		break;

	default:
		mhal_DPTx_PGEnable(mtk_dp, true);
		break;
	}

	mdrv_DPTx_SetTU(mtk_dp);
}

static bool mdrv_DPTx_CheckSinkLock(struct mtk_dp *mtk_dp, u8 *pDPCD20x, u8 *pDPCD200C)
{
	bool bLocked = true;

	if (mtk_dp->training_info.bSinkEXTCAP_En) {
		switch (mtk_dp->training_info.ubLinkLaneCount) {
		case DP_LANECOUNT_1:
			if ((pDPCD200C[0] & 0x07) != 0x07) {
				bLocked = false;
				pr_info("2L Lose LCOK\n");
			}
			break;
		case DP_LANECOUNT_2:
			if ((pDPCD200C[0] & 0x77) != 0x77) {
				bLocked = false;
				pr_info("2L Lose LCOK\n");
			}
			break;
		case DP_LANECOUNT_4:
			if ((pDPCD200C[0] != 0x77) || (pDPCD200C[1] != 0x77)) {
				bLocked = false;
				pr_info("4L Lose LCOK\n");
			}
			break;
		}

		if ((pDPCD200C[2]&BIT0) == 0) {
			bLocked = false;
			pr_info("Interskew Lose LCOK\n");
		}
	} else {
		switch (mtk_dp->training_info.ubLinkLaneCount) {
		case DP_LANECOUNT_1:
			if ((pDPCD20x[2] & 0x07) != 0x07) {
				bLocked = false;
				pr_info("1L Lose LCOK\n");
			}
			break;
		case DP_LANECOUNT_2:
			if ((pDPCD20x[2] & 0x77) != 0x77) {
				bLocked = false;
				pr_info("2L Lose LCOK\n");
			}
			break;
		case DP_LANECOUNT_4:
			if (((pDPCD20x[2] != 0x77) || (pDPCD20x[3] != 0x77))) {
				bLocked = false;
				pr_info("4L Lose LCOK\n");
			}
			break;
		}

		if ((pDPCD20x[4]&BIT0) == 0) {
			bLocked = false;
			pr_info("Interskew Lose LCOK\n");
		}
	}


	if (!bLocked) {
		if (mtk_dp->training_state > DPTX_NTSTATE_TRAINING_PRE)
			mtk_dp->training_state = DPTX_NTSTATE_TRAINING_PRE;
	}

	return bLocked;
}

static void mdrv_DPTx_CheckSinkESI(struct mtk_dp *mtk_dp, u8 *pDPCD20x, u8 *pDPCD2002)
{
	u8 ubTempValue;

	if (pDPCD20x[0x1]&BIT0) { // not support, clrear it.
		ubTempValue = BIT0;
		drm_dp_dpcd_write(&mtk_dp->aux, DPCD_00201, &ubTempValue, 0x1);
	}
}

static void mdrv_DPTx_CheckSinkHPDEvent(struct mtk_dp *mtk_dp)
{
	u8 ubDPCD20x[6];
	u8 ubDPCD2002[2];
	u8 ubDPCD200C[4];
	bool ret;

	memset(ubDPCD20x, 0x0, sizeof(ubDPCD20x));
	memset(ubDPCD2002, 0x0, sizeof(ubDPCD2002));
	memset(ubDPCD200C, 0x0, sizeof(ubDPCD200C));

	ret = drm_dp_dpcd_read(&mtk_dp->aux, DPCD_02002, ubDPCD2002, 0x2);
	if (!ret) {
		pr_info("Read DPCD_02002 Fail\n");
		return;
	}

	ret = drm_dp_dpcd_read(&mtk_dp->aux, DPCD_0200C, ubDPCD200C, 0x4);
	if (!ret) {
		pr_info("Read DPCD_0200C Fail\n");
		return;
	}

	ret = drm_dp_dpcd_read(&mtk_dp->aux, DPCD_00200, ubDPCD20x, 0x6);
	if (!ret) {
		pr_info("Read DPCD200 Fail\n");
		return;
	}

	mdrv_DPTx_CheckSinkLock(mtk_dp, ubDPCD20x, ubDPCD200C);
	mdrv_DPTx_CheckSinkESI(mtk_dp, ubDPCD20x, ubDPCD2002);

	if ((ubDPCD2002[0x0] & 0x1F || ubDPCD20x[0x0] & 0x1F) &&
		(ubDPCD200C[0x2] & BIT6 || ubDPCD20x[0x4] & BIT6)) {
		pr_info("New Branch Device Detection!!\n");

		mtk_dp->training_info.ucCheckCapTimes = 0;
		kfree(mtk_dp->edid);
		mtk_dp->edid = NULL;
		mtk_dp->training_state = DPTX_NTSTATE_CHECKEDID;
		mdelay(20);
	}
}

void mdrv_DPTx_CheckMaxLinkRate(struct mtk_dp *mtk_dp)
{
	switch (mtk_dp->training_info.ubDPSysVersion) {
	case DP_VERSION_11:
		mtk_dp->training_info.ubSysMaxLinkRate = DP_LINKRATE_HBR;
		break;
	case DP_VERSION_12:
		mtk_dp->training_info.ubSysMaxLinkRate = DP_LINKRATE_HBR2;
		break;
	case DP_VERSION_14:
		mtk_dp->training_info.ubSysMaxLinkRate = DP_LINKRATE_HBR3;
		break;
	default:
		mtk_dp->training_info.ubSysMaxLinkRate = DP_LINKRATE_HBR2;
		break;
	}
}

/*void mdrv_DPTx_DisconnetInit(struct mtk_dp *mtk_dp)
{
	mdrv_DPTx_CheckMaxLinkRate(mtk_dp);
}*/

void mdrv_DPTx_SPKG_SDP(struct mtk_dp *mtk_dp, bool bEnable, u8 ucSDPType,
	u8 *pHB, u8 *pDB)
{
	mhal_DPTx_SPKG_SDP(mtk_dp, bEnable, ucSDPType, pHB, pDB);
}

static void mdrv_DPTx_StopSentSDP(struct mtk_dp *mtk_dp)
{
	u8 ubPkgType;

	for (ubPkgType = DPTx_SDPTYP_ACM ; ubPkgType < DPTx_SDPTYP_MAX_NUM;
		ubPkgType++)
		mhal_DPTx_SPKG_SDP(mtk_dp, false, ubPkgType, NULL, NULL);

	mhal_DPTx_SPKG_VSC_EXT_VESA(mtk_dp, false, 0x00, NULL);
	mhal_DPTx_SPKG_VSC_EXT_CEA(mtk_dp, false, 0x00, NULL);
}

int mdrv_DPTx_HPD_HandleInThread(struct mtk_dp *mtk_dp)
{
	int ret = DPTX_NOERR;

	if (mtk_dp->training_info.bCableStateChange) {
		bool ubCurrentHPD = mhal_DPTx_GetHPDPinLevel(mtk_dp);

		mtk_dp->training_info.bCableStateChange = false;

		if (mtk_dp->training_info.bCablePlugIn && ubCurrentHPD) {
			pr_info("HPD_CON\n");
		} else {
			pr_info("HPD_DISCON\n");
			mdrv_DPTx_VideoMute(mtk_dp, true);
			mdrv_DPTx_AudioMute(mtk_dp, true);

			if (mtk_dp->bUeventToHwc) {
				mtk_dp->bUeventToHwc = false;
				mtk_dp->disp_status = DPTX_DISP_NONE;
			} else
				pr_info("Skip Uevent(0)\n");

			mdrv_DPTx_InitVariable(mtk_dp);
			mhal_DPTx_PHY_SetIdlePattern(mtk_dp, true);
			if (mtk_dp->has_fec)
				mhal_DPTx_EnableFEC(mtk_dp, false);
			mdrv_DPTx_StopSentSDP(mtk_dp);
			mhal_DPTx_AnalogPowerOnOff(mtk_dp, false);

			pr_info("Power OFF %d", mtk_dp->bPowerOn);
			if (mtk_dp->dp_tx_clk)
				clk_disable_unprepare(mtk_dp->dp_tx_clk);

			kfree(mtk_dp->edid);
			mtk_dp->edid = NULL;
			ret = DPTX_PLUG_OUT;
		}
	}

	if (mtk_dp->training_info.usPHY_STS & HPD_INT_EVNET) {
		pr_info("HPD_INT_EVNET\n");
		mtk_dp->training_info.usPHY_STS &= ~HPD_INT_EVNET;
		mdrv_DPTx_CheckSinkHPDEvent(mtk_dp);
	}

	return ret;
}

void mdrv_DPTx_VideoMute(struct mtk_dp *mtk_dp, bool bENABLE)
{
	mtk_dp->info.bVideoMute = (mtk_dp->info.bSetVideoMute) ?
		true : bENABLE;
	mhal_DPTx_VideoMute(mtk_dp, mtk_dp->info.bVideoMute);
}

void mdrv_DPTx_AudioMute(struct mtk_dp *mtk_dp, bool bENABLE)
{
	mtk_dp->info.bAudioMute = (mtk_dp->info.bSetAudioMute) ?
		true : bENABLE;
	mhal_DPTx_AudioMute(mtk_dp, mtk_dp->info.bAudioMute);
}

static bool mdrv_DPTx_TrainingCheckSwingPre(struct mtk_dp *mtk_dp,
	u8 ubTargetLaneCount, u8 *ubDPCP202_x, u8 *ubDPCP_Buffer1)
{
	u8 ubSwingValue;
	u8 ubPreemphasis;

	if (ubTargetLaneCount >= 0x1) { //lane0
		ubSwingValue = (ubDPCP202_x[0x4]&0x3);
		ubPreemphasis = ((ubDPCP202_x[0x4]&0x0C)>>2);
		//Adjust the swing and pre-emphasis
		mhal_DPTx_SetSwingtPreEmphasis(mtk_dp, DPTx_LANE0,
			ubSwingValue, ubPreemphasis);
		//Adjust the swing and pre-emphasis done, notify Sink Side
		ubDPCP_Buffer1[0x0] = ubSwingValue | (ubPreemphasis << 3);
		if (ubSwingValue == DPTx_SWING3) { //MAX_SWING_REACHED
			ubDPCP_Buffer1[0x0] |= BIT2;
		}
		//MAX_PRE-EMPHASIS_REACHED
		if (ubPreemphasis == DPTx_PREEMPHASIS3)
			ubDPCP_Buffer1[0x0] |= BIT5;

	}

	if (ubTargetLaneCount >= 0x2) { //lane1
		ubSwingValue = (ubDPCP202_x[0x4]&0x30) >> 4;
		ubPreemphasis = ((ubDPCP202_x[0x4]&0xC0)>>6);
		//Adjust the swing and pre-emphasis
		mhal_DPTx_SetSwingtPreEmphasis(mtk_dp, DPTx_LANE1,
			ubSwingValue, ubPreemphasis);
		//Adjust the swing and pre-emphasis done, notify Sink Side
		ubDPCP_Buffer1[0x1] = ubSwingValue | (ubPreemphasis << 3);
		if (ubSwingValue == DPTx_SWING3) { //MAX_SWING_REACHED
			ubDPCP_Buffer1[0x1] |= BIT2;
		}
		//MAX_PRE-EMPHASIS_REACHED
		if (ubPreemphasis == DPTx_PREEMPHASIS3)
			ubDPCP_Buffer1[0x1] |= BIT5;

	}

	if (ubTargetLaneCount == 0x4) { //lane 2,3
		ubSwingValue = (ubDPCP202_x[0x5]&0x3);
		ubPreemphasis = ((ubDPCP202_x[0x5]&0x0C)>>2);

		//Adjust the swing and pre-emphasis
		mhal_DPTx_SetSwingtPreEmphasis(mtk_dp, DPTx_LANE2,
			(ubDPCP202_x[0x5]&0x3), ((ubDPCP202_x[0x5]&0x0C)>>2));
		//Adjust the swing and pre-emphasis done, notify Sink Side
		ubDPCP_Buffer1[0x2] = ubSwingValue | (ubPreemphasis << 3);
		if (ubSwingValue == DPTx_SWING3) { //MAX_SWING_REACHED
			ubDPCP_Buffer1[0x2] |= BIT2;
		}
		//MAX_PRE-EMPHASIS_REACHED
		if (ubPreemphasis == DPTx_PREEMPHASIS3)
			ubDPCP_Buffer1[0x2] |= BIT5;


		ubSwingValue = (ubDPCP202_x[0x5]&0x30) >> 4;
		ubPreemphasis = ((ubDPCP202_x[0x5]&0xC0)>>6);

		//Adjust the swing and pre-emphasis
		mhal_DPTx_SetSwingtPreEmphasis(mtk_dp, DPTx_LANE3,
			((ubDPCP202_x[0x5]&0x30)>>4),
			((ubDPCP202_x[0x5]&0xC0)>>6));
		//Adjust the swing and pre-emphasis done, notify Sink Side
		ubDPCP_Buffer1[0x3] = ubSwingValue | (ubPreemphasis << 3);
		if (ubSwingValue == DPTx_SWING3) { //MAX_SWING_REACHED
			ubDPCP_Buffer1[0x3] |= BIT2;
		}
		//MAX_PRE-EMPHASIS_REACHED
		if (ubPreemphasis == DPTx_PREEMPHASIS3)
			ubDPCP_Buffer1[0x3] |= BIT5;
	}

	//Wait signal stable enough
	mdelay(2);
	return true;
}

static void mdrv_DPTx_Print_TrainingState(u8 state)
{
	switch (state) {
	case DPTX_NTSTATE_STARTUP:
		pr_info("NTSTATE_STARTUP!\n");
		break;
	case DPTX_NTSTATE_CHECKCAP:
		pr_info("NTSTATE_CHECKCAP!\n");
		break;
	case DPTX_NTSTATE_CHECKEDID:
		pr_info("NTSTATE_CHECKEDID!\n");
		break;
	case DPTX_NTSTATE_TRAINING_PRE:
		pr_info("NTSTATE_TRAINING_PRE!\n");
		break;
	case DPTX_NTSTATE_TRAINING:
		pr_info("NTSTATE_TRAINING!\n");
		break;
	case DPTX_NTSTATE_CHECKTIMING:
		pr_info("NTSTATE_CHECKTIMING!\n");
		break;
	case DPTX_NTSTATE_NORMAL:
		pr_info("NTSTATE_NORMAL!\n");
		break;
	case DPTX_NTSTATE_POWERSAVE:
		pr_info("NTSTATE_POWERSAVE!\n");
		break;
	case DPTX_NTSTATE_DPIDLE:
		pr_info("NTSTATE_DPIDLE!\n");
		break;
	}
}

int mdrv_DPTx_set_reTraining(struct mtk_dp *mtk_dp)
{
	int ret = 0;

	if (mtk_dp->training_state > DPTX_NTSTATE_STARTUP)
		mtk_dp->training_state = DPTX_NTSTATE_STARTUP;

	if (mtk_dp->state > DPTXSTATE_INITIAL)
		mtk_dp->state = DPTXSTATE_INITIAL;

	mhal_DPTx_DigitalSwReset(mtk_dp);

	return ret;
}

static int mdrv_DPTx_TrainingFlow(struct mtk_dp *mtk_dp, u8 ubLaneRate, u8 ubLaneCount)
{
	u8  ubTempValue[0x6];
	u8  ubDPCD200C[0x3];
	u8  ubTargetLinkRate = ubLaneRate;
	u8  ubTargetLaneCount = ubLaneCount;
	u8  ubDPCP_Buffer1[0x4];
	u8  bPassTPS1 = false;
	u8  bPassTPS2_3 = false;
	u8  ubTrainRetryTimes;
	u8  ubStatusControl;
	u8  ubIterationCount;
	u8  ubDPCD206;

	memset(ubTempValue, 0x0, sizeof(ubTempValue));
	memset(ubDPCP_Buffer1, 0x0, sizeof(ubDPCP_Buffer1));

	drm_dp_dpcd_read(&mtk_dp->aux, DPCD_00600, ubTempValue, 0x1);
	if (ubTempValue[0] != 0x01) {
		ubTempValue[0] = 0x01;
		drm_dp_dpcd_write(&mtk_dp->aux, DPCD_00600, ubTempValue, 0x1);
		mdelay(1);
	}

	ubTempValue[0] = ubTargetLinkRate;
	ubTempValue[1] = ubTargetLaneCount | DPTX_AUX_SET_ENAHNCED_FRAME;
	drm_dp_dpcd_write(&mtk_dp->aux, DPCD_00100, ubTempValue, 0x2);

	if (mtk_dp->training_info.bSinkSSC_En) {
		ubTempValue[0x0] = 0x10;
		drm_dp_dpcd_write(&mtk_dp->aux, DPCD_00107, ubTempValue, 0x1);
	}

	ubTrainRetryTimes = 0x0;
	ubStatusControl = 0x0;
	ubIterationCount = 0x1;
	ubDPCD206 = 0xFF;

	mhal_DPTx_SetTxLane(mtk_dp, ubTargetLaneCount/2);
	mhal_DPTx_SetTxRate(mtk_dp, ubTargetLinkRate);

	do {
		ubTrainRetryTimes++;
		if (!mtk_dp->training_info.bCablePlugIn ||
			((mtk_dp->training_info.usPHY_STS & HPD_DISCONNECT)
				!= 0x0)) {
			pr_info("Training Abort! HPD is low\n");
			return DPTX_PLUG_OUT;
		}

		if (mtk_dp->training_info.usPHY_STS & HPD_INT_EVNET)
			mdrv_DPTx_HPD_HandleInThread(mtk_dp);

		if (mtk_dp->training_state < DPTX_NTSTATE_TRAINING)
			return DPTX_TRANING_STATE_CHANGE;

		if (!bPassTPS1)	{
			pr_info("CR Training START\n");
			mhal_DPTx_SetScramble(mtk_dp, false);

			if (ubStatusControl == 0x0)	{
				mhal_DPTx_SetTxTrainingPattern(mtk_dp, BIT4);
				ubStatusControl = 0x1;
				ubTempValue[0] = 0x21;
				drm_dp_dpcd_write(&mtk_dp->aux, DPCD_00102,
					ubTempValue, 0x1);
				drm_dp_dpcd_read(&mtk_dp->aux, DPCD_00206,
					(ubTempValue+4), 0x2);
				ubIterationCount++;

				mdrv_DPTx_TrainingCheckSwingPre(mtk_dp,
					ubTargetLaneCount, ubTempValue,
					ubDPCP_Buffer1);
			}
			drm_dp_dpcd_write(&mtk_dp->aux, DPCD_00103,
				ubDPCP_Buffer1, ubTargetLaneCount);

			drm_dp_link_train_clock_recovery_delay(mtk_dp->rx_cap);
			drm_dp_dpcd_read(&mtk_dp->aux, DPCD_00202,
				ubTempValue, 0x6);

			if (mtk_dp->training_info.bSinkEXTCAP_En) {
				drm_dp_dpcd_read(&mtk_dp->aux, DPCD_0200C,
					ubDPCD200C, 0x3);
				ubTempValue[0] = ubDPCD200C[0];
				ubTempValue[1] = ubDPCD200C[1];
				ubTempValue[2] = ubDPCD200C[2];
			}

			if (drm_dp_clock_recovery_ok(ubTempValue,
				ubTargetLaneCount)) {
				pr_info("CR Training Success\n");
				mtk_dp->training_info.cr_done = true;
				bPassTPS1 = true;
				ubTrainRetryTimes = 0x0;
				ubIterationCount = 0x1;
			} else {
				//request swing & emp is the same eith last time
				if (ubDPCD206 == ubTempValue[0x4]) {
					ubIterationCount++;
					if (ubDPCD206&0x3)
						ubIterationCount =
						DPTX_TRAIN_MAX_ITERATION;
				} else {
					ubDPCD206 = ubTempValue[0x4];
				}

				pr_info("CR Training Fail\n");
			}
		} else if (bPassTPS1 && !bPassTPS2_3) {
			pr_info("EQ Training START\n");
			if (ubStatusControl == 0x1) {
				if (mtk_dp->training_info.bTPS4) {
					mhal_DPTx_SetTxTrainingPattern(mtk_dp,
						BIT7);
					pr_info("LT TPS4\n");
				} else if (mtk_dp->training_info.bTPS3) {
					mhal_DPTx_SetTxTrainingPattern(mtk_dp,
						BIT6);
					pr_info("LT TP3\n");
				} else {
					mhal_DPTx_SetTxTrainingPattern(mtk_dp,
						BIT5);
					pr_info("LT TPS2\n");
				}

				if (mtk_dp->training_info.bTPS4) {
					ubTempValue[0] = 0x07;
					drm_dp_dpcd_write(&mtk_dp->aux,
						DPCD_00102, ubTempValue, 0x1);
				} else if (mtk_dp->training_info.bTPS3) {
					ubTempValue[0] = 0x23;
					drm_dp_dpcd_write(&mtk_dp->aux,
						DPCD_00102, ubTempValue, 0x1);
				} else {
					ubTempValue[0] = 0x22;
					drm_dp_dpcd_write(&mtk_dp->aux,
						DPCD_00102, ubTempValue, 0x1);
				}

				ubStatusControl = 0x2;
				drm_dp_dpcd_read(&mtk_dp->aux, DPCD_00206,
					(ubTempValue+4), 0x2);

				ubIterationCount++;
				mdrv_DPTx_TrainingCheckSwingPre(mtk_dp,
					ubTargetLaneCount, ubTempValue,
					ubDPCP_Buffer1);
			}

			drm_dp_dpcd_write(&mtk_dp->aux, DPCD_00103,
				ubDPCP_Buffer1, ubTargetLaneCount);
			drm_dp_link_train_channel_eq_delay(mtk_dp->rx_cap);

			drm_dp_dpcd_read(&mtk_dp->aux, DPCD_00202,
				ubTempValue, 0x6);
			if (mtk_dp->training_info.bSinkEXTCAP_En) {
				drm_dp_dpcd_read(&mtk_dp->aux, DPCD_0200C,
					ubDPCD200C, 0x3);
				ubTempValue[0] |= ubDPCD200C[0];
				ubTempValue[1] |= ubDPCD200C[1];
				ubTempValue[2] |= ubDPCD200C[2];
			}

			if (!drm_dp_clock_recovery_ok(ubTempValue,
				ubTargetLaneCount)) {
				mtk_dp->training_info.cr_done = false;
				mtk_dp->training_info.eq_done = false;
				break;
			}

			if (drm_dp_channel_eq_ok(ubTempValue,
				ubTargetLaneCount)) {
				mtk_dp->training_info.eq_done = true;
				bPassTPS2_3 = true;
				pr_info("EQ Training Success\n");
				break;
			}
			pr_info("EQ Training Fail\n");

			if (ubDPCD206 == ubTempValue[0x4])
				ubIterationCount++;
			else
				ubDPCD206 = ubTempValue[0x4];
		}

		mdrv_DPTx_TrainingCheckSwingPre(mtk_dp, ubTargetLaneCount,
			ubTempValue, ubDPCP_Buffer1);
		pr_info("ubTrainRetryTimes = %d, ubIterationCount = %d\n",
			ubTrainRetryTimes, ubIterationCount);

	} while ((ubTrainRetryTimes < DPTX_TRAIN_RETRY_LIMIT) &&
		(ubIterationCount < DPTX_TRAIN_MAX_ITERATION));

	ubTempValue[0] = 0x0;
	drm_dp_dpcd_write(&mtk_dp->aux, DPCD_00102, ubTempValue, 0x1);
	mhal_DPTx_SetTxTrainingPattern(mtk_dp, 0);

	if (bPassTPS2_3) {
		mtk_dp->training_info.ubLinkRate = ubTargetLinkRate;
		mtk_dp->training_info.ubLinkLaneCount = ubTargetLaneCount;

		mhal_DPTx_SetScramble(mtk_dp, true);

		ubTempValue[0] = ubTargetLaneCount
			| DPTX_AUX_SET_ENAHNCED_FRAME;
		drm_dp_dpcd_write(&mtk_dp->aux, DPCD_00101, ubTempValue, 0x1);
		mhal_DPTx_SetEF_Mode(mtk_dp, ENABLE_DPTX_EF_MODE);

		pr_info("Link Training PASS\n");
		return DPTX_NOERR;
	}

	pr_info("Link Training Fail\n");
	return DPTX_TRANING_FAIL;
}

static bool mdrv_DPTx_CheckSinkCap(struct mtk_dp *mtk_dp)
{
	u8 bTempBuffer[0x10];

	if (!mhal_DPTx_GetHPDPinLevel(mtk_dp)) {
		pr_info("HPD is low\n");
		return false;
	}
	memset(bTempBuffer, 0x0, sizeof(bTempBuffer));

	bTempBuffer[0x0] = 0x1;
	drm_dp_dpcd_write(&mtk_dp->aux, DPCD_00600, bTempBuffer, 0x1);
	mdelay(2);

	drm_dp_dpcd_read(&mtk_dp->aux, DPCD_00000, bTempBuffer, 0x10);

	mtk_dp->training_info.bSinkEXTCAP_En = (bTempBuffer[0x0E]&BIT7) ?
		true : false;

	mtk_dp->training_info.bSinkEXTCAP_En = false;

	if (mtk_dp->training_info.bSinkEXTCAP_En)
		drm_dp_dpcd_read(&mtk_dp->aux, DPCD_02200, bTempBuffer, 0x10);

	mtk_dp->training_info.ubDPCD_REV = bTempBuffer[0x0];
	pr_info("SINK DPCD version:0x%x\n", mtk_dp->training_info.ubDPCD_REV);

	memcpy(mtk_dp->rx_cap, bTempBuffer, 0x10);
	mtk_dp->rx_cap[0xe] &= 0x7F;

	if (mtk_dp->training_info.ubDPCD_REV >= 0x14)
		mdrv_DPTx_FEC_Ready(mtk_dp, FEC_BIT_ERROR_COUNT);

#if !ENABLE_DPTX_FIX_LRLC
	mtk_dp->training_info.ubLinkRate =
		(bTempBuffer[0x1] >= mtk_dp->training_info.ubSysMaxLinkRate) ?
		mtk_dp->training_info.ubSysMaxLinkRate : bTempBuffer[0x1];
	mtk_dp->training_info.ubLinkLaneCount =
		((bTempBuffer[0x2]&0x1F) >= MAX_LANECOUNT) ?
		MAX_LANECOUNT : (bTempBuffer[0x2]&0x1F);
#endif

#if ENABLE_DPTX_FIX_TPS2
	mtk_dp->training_info.bTPS3 = 0;
	mtk_dp->training_info.bTPS4 = 0;
#else
	mtk_dp->training_info.bTPS3 = (bTempBuffer[0x2]&BIT6)>>0x6;
	mtk_dp->training_info.bTPS4 = (bTempBuffer[0x3]&BIT7)>>0x7;
#endif
	mtk_dp->training_info.bDWN_STRM_PORT_PRESENT
			= (bTempBuffer[0x5] & BIT0);

#if (ENABLE_DPTX_SSC_OUTPUT == 0x1)
	if ((bTempBuffer[0x3] & BIT0) == 0x1) {
		mtk_dp->training_info.bSinkSSC_En = true;
		pr_info("SINK SUPPORT SSC!\n");
	} else {
		mtk_dp->training_info.bSinkSSC_En = false;
		pr_info("SINK NOT SUPPORT SSC!\n");
	}
#endif

#if ENABLE_DPTX_SSC_FORCEON
	pr_info("FORCE SSC ON !!!\n");
	mtk_dp->training_info.bSinkSSC_En = true;
#endif

	drm_dp_dpcd_read(&mtk_dp->aux, DPCD_00021, bTempBuffer, 0x1);
	mtk_dp->training_info.bDPMstCAP = (bTempBuffer[0x0] & BIT0);
	mtk_dp->training_info.bDPMstBranch = false;

	if (mtk_dp->training_info.bDPMstCAP == BIT0) {
		if (mtk_dp->training_info.bDWN_STRM_PORT_PRESENT == 0x1)
			mtk_dp->training_info.bDPMstBranch = true;

		drm_dp_dpcd_read(&mtk_dp->aux, DPCD_02003, bTempBuffer, 0x1);

		if (bTempBuffer[0x0] != 0x0)
			drm_dp_dpcd_write(&mtk_dp->aux, DPCD_02003,
				bTempBuffer, 0x1);
	}

	drm_dp_dpcd_read(&mtk_dp->aux, DPCD_00600, bTempBuffer, 0x1);
	if (bTempBuffer[0x0] != 0x1) {
		bTempBuffer[0x0] = 0x1;
		drm_dp_dpcd_write(&mtk_dp->aux, DPCD_00600, bTempBuffer, 0x1);
	}

	drm_dp_dpcd_read(&mtk_dp->aux, DPCD_00200, bTempBuffer, 0x2);

	return true;
}

static unsigned int mdrv_DPTx_getAudioCaps(struct mtk_dp *mtk_dp)
{
	struct cea_sad *sads;
	int sad_count, i, j;
	unsigned int caps = 0;

	if (mtk_dp->edid == NULL) {
		pr_err("EDID not found!\n");
		return 0;
	}

	sad_count = drm_edid_to_sad(mtk_dp->edid, &sads);
	if (sad_count <= 0) {
		pr_info("The SADs is NULL\n");
		return 0;
	}

	for (i = 0; i < sad_count; i++) {
		if (sads[i].format == 0x01)	{
			for (j = 0; j < sads[i].channels; j++)
				caps |= ((1 << j) <<
					DP_CAPABILITY_CHANNEL_SFT) &
					(DP_CAPABILITY_CHANNEL_MASK <<
					DP_CAPABILITY_CHANNEL_SFT);

			caps |= (sads[i].freq << DP_CAPABILITY_SAMPLERATE_SFT) &
				(DP_CAPABILITY_SAMPLERATE_MASK <<
				DP_CAPABILITY_SAMPLERATE_SFT);
			caps |= (sads[i].byte2 << DP_CAPABILITY_BITWIDTH_SFT) &
				(DP_CAPABILITY_BITWIDTH_MASK <<
				DP_CAPABILITY_BITWIDTH_SFT);
		}
	}

	pr_info("audio caps:0x%x", caps);
	return caps;
}

static bool mdrv_DPTx_TrainingChangeMode(struct mtk_dp *mtk_dp)
{
	mhal_DPTx_PHYD_Reset(mtk_dp);
	mhal_DPTx_ResetSwingtPreEmphasis(mtk_dp);
	mhal_DPTx_SSCOnOffSetting(mtk_dp, mtk_dp->training_info.bSinkSSC_En);

	mdelay(2);
	return true;
}

int mdrv_DPTx_SetTrainingStart(struct mtk_dp *mtk_dp)
{
	u8 ret = DPTX_NOERR;
	u8 ubLaneCount;
	u8 ubLinkRate;
	u8 ubTemp[16];
	u8 ubTrainTimeLimits;
#if !ENABLE_DPTX_FIX_LRLC
	u8 maxLinkRate;
#endif

	if (!mhal_DPTx_GetHPDPinLevel(mtk_dp)) {
		pr_info("Start Training Abort!=> HPD low !\n");

		ubTrainTimeLimits = 6;
		while (ubTrainTimeLimits > 6) {
			if (mhal_DPTx_GetHPDPinLevel(mtk_dp))
				break;

			ubTrainTimeLimits--;
			mdelay(1);
		}

		if (ubTrainTimeLimits == 0)	{
			mtk_dp->training_state = DPTX_NTSTATE_DPIDLE;
			return DPTX_PLUG_OUT;
		}
	}

	if (!mtk_dp->training_info.bDPTxAutoTest_EN) {
		ubTemp[0] = 0x1;
		drm_dp_dpcd_write(&mtk_dp->aux, DPCD_00600, ubTemp, 0x1);

		ubLinkRate = mtk_dp->rx_cap[1];
		ubLaneCount = mtk_dp->rx_cap[2] & 0x1F;
		pr_info("RX support ubLinkRate = 0x%x,ubLaneCount = %x",
			ubLinkRate, ubLaneCount);

#if !ENABLE_DPTX_FIX_LRLC
		mtk_dp->training_info.ubLinkRate =
			(ubLinkRate >= mtk_dp->training_info.ubSysMaxLinkRate) ?
			mtk_dp->training_info.ubSysMaxLinkRate : ubLinkRate;
		mtk_dp->training_info.ubLinkLaneCount =
			(ubLaneCount >= MAX_LANECOUNT) ?
			MAX_LANECOUNT : ubLaneCount;
#endif
		if (mtk_dp->training_info.bSinkEXTCAP_En)
			drm_dp_dpcd_read(&mtk_dp->aux, DPCD_02002, ubTemp, 0x1);
		else
			drm_dp_dpcd_read(&mtk_dp->aux, DPCD_00200, ubTemp, 0x1);

		if ((ubTemp[0x0] & 0xBF) != 0) {
			mtk_dp->training_info.ubSinkCountNum = ubTemp[0x0]&0xBF;
			pr_info("ExtSink Count = %d\n",
				mtk_dp->training_info.ubSinkCountNum);
		}
	}

	ubLinkRate = mtk_dp->training_info.ubLinkRate;
	ubLaneCount = mtk_dp->training_info.ubLinkLaneCount;

	switch (ubLinkRate) {
	case DP_LINKRATE_RBR:
	case DP_LINKRATE_HBR:
	case DP_LINKRATE_HBR2:
	case DP_LINKRATE_HBR25:
	case DP_LINKRATE_HBR3:
		break;
	default:
		mtk_dp->training_info.ubLinkRate = DP_LINKRATE_HBR3;
		break;
	};

#if !ENABLE_DPTX_FIX_LRLC
	maxLinkRate = ubLinkRate;
	ubTrainTimeLimits = 0x6;
#endif
	do {
		pr_info("LinkRate:0x%x, LaneCount:%x", ubLinkRate, ubLaneCount);

		mtk_dp->training_info.cr_done = false;
		mtk_dp->training_info.eq_done = false;

		mdrv_DPTx_TrainingChangeMode(mtk_dp);
		ret = mdrv_DPTx_TrainingFlow(mtk_dp, ubLinkRate, ubLaneCount);
		if (ret == DPTX_PLUG_OUT || ret == DPTX_TRANING_STATE_CHANGE)
			return ret;

		if (!mtk_dp->training_info.cr_done) {
#if !ENABLE_DPTX_FIX_LRLC
			switch (ubLinkRate) {
			case DP_LINKRATE_RBR:
				ubLaneCount = ubLaneCount/2;
				ubLinkRate = maxLinkRate;
				if (ubLaneCount == 0x0) {
					mtk_dp->training_state
						= DPTX_NTSTATE_DPIDLE;
					return DPTX_TRANING_FAIL;
				}
				break;
			case DP_LINKRATE_HBR:
				ubLinkRate = DP_LINKRATE_RBR;
				break;
			case DP_LINKRATE_HBR2:
				ubLinkRate = DP_LINKRATE_HBR;
				break;
			case DP_LINKRATE_HBR3:
				ubLinkRate = DP_LINKRATE_HBR2;
				break;
			default:
				return DPTX_TRANING_FAIL;
			};
#endif
			ubTrainTimeLimits--;
		} else if (!mtk_dp->training_info.eq_done) {
#if !ENABLE_DPTX_FIX_LRLC
			if (ubLaneCount == DP_LANECOUNT_4)
				ubLaneCount = DP_LANECOUNT_2;
			else if (ubLaneCount == DP_LANECOUNT_2)
				ubLaneCount = DP_LANECOUNT_1;
			else
				return DPTX_TRANING_FAIL;
#endif
			ubTrainTimeLimits--;
		} else
			return DPTX_NOERR;

	} while (ubTrainTimeLimits > 0);

	return DPTX_TRANING_FAIL;
}

static int mdrv_DPTx_Training_Handler(struct mtk_dp *mtk_dp)
{
	int ret = DPTX_NOERR;

	ret = mdrv_DPTx_HPD_HandleInThread(mtk_dp);

	if (!mtk_dp->training_info.bCablePlugIn)
		return DPTX_PLUG_OUT;

	if (mtk_dp->training_state == DPTX_NTSTATE_NORMAL)
		return ret;

	if (mtk_dp->training_state_pre != mtk_dp->training_state) {
		mdrv_DPTx_Print_TrainingState(mtk_dp->training_state);

		mtk_dp->training_state_pre = mtk_dp->training_state;
	}

	switch (mtk_dp->training_state) {
	case DPTX_NTSTATE_STARTUP:
		mtk_dp->training_state = DPTX_NTSTATE_CHECKCAP;
		break;

	case DPTX_NTSTATE_CHECKCAP:
		if (mdrv_DPTx_CheckSinkCap(mtk_dp)) {
			mtk_dp->training_info.ucCheckCapTimes = 0;
			mtk_dp->training_state = DPTX_NTSTATE_CHECKEDID;
		} else {
			BYTE uaCheckTimes = 0;

			mtk_dp->training_info.ucCheckCapTimes++;
			uaCheckTimes = mtk_dp->training_info.ucCheckCapTimes;

			if (uaCheckTimes > DPTX_CheckSinkCap_TimeOutCnt) {
				mtk_dp->training_info.ucCheckCapTimes = 0;
				pr_info("CheckCap Fail %d times",
					DPTX_CheckSinkCap_TimeOutCnt);
				mtk_dp->training_state = DPTX_NTSTATE_DPIDLE;
				ret = DPTX_TIMEOUT;
			} else
				pr_info("CheckCap Fail %d times", uaCheckTimes);
		}
		break;

	case DPTX_NTSTATE_CHECKEDID:
		mtk_dp->edid = mtk_dp_handle_edid(mtk_dp);
		if (mtk_dp->edid) {
			pr_info("READ EDID done!\n");
			if (0){//mtk_dp_debug_get()) {
				u8 *raw_edid = (u8 *)mtk_dp->edid;

				pr_info("Raw EDID:\n");
				print_hex_dump(KERN_NOTICE,
						"\t", DUMP_PREFIX_NONE, 16, 1,
						raw_edid, EDID_LENGTH, false);
				if ((raw_edid[0x7E] & 0x01) == 0x01) {
					print_hex_dump(KERN_NOTICE,
						"\t", DUMP_PREFIX_NONE, 16, 1,
						(raw_edid + 128), EDID_LENGTH,
						false);
				}
			}

			mtk_dp->info.audio_caps
				= mdrv_DPTx_getAudioCaps(mtk_dp);
		} else
			pr_info("Read EDID Fail!\n");

		mtk_dp->training_state = DPTX_NTSTATE_TRAINING_PRE;
		break;

	case DPTX_NTSTATE_TRAINING_PRE:
		mtk_dp->training_state = DPTX_NTSTATE_TRAINING;
		break;

	case DPTX_NTSTATE_TRAINING:
		ret = mdrv_DPTx_SetTrainingStart(mtk_dp);
		if (ret == DPTX_NOERR) {
			mdrv_DPTx_VideoMute(mtk_dp, true);
			mdrv_DPTx_AudioMute(mtk_dp, true);
			mtk_dp->training_state = DPTX_NTSTATE_CHECKTIMING;
			mtk_dp->dp_ready = true;
			mhal_DPTx_EnableFEC(mtk_dp, mtk_dp->has_fec);
		}  else if (ret != DPTX_TRANING_STATE_CHANGE)
			mtk_dp->training_state = DPTX_NTSTATE_DPIDLE;

		ret = DPTX_NOERR;
		break;

	case DPTX_NTSTATE_CHECKTIMING:
		mtk_dp->training_state = DPTX_NTSTATE_NORMAL;
		break;
	case DPTX_NTSTATE_NORMAL:
		break;
	case DPTX_NTSTATE_POWERSAVE:
		break;
	case DPTX_NTSTATE_DPIDLE:
		break;
	default:
		break;
	}

	return ret;
}

static int mdrv_DPTx_Handle(struct mtk_dp *mtk_dp)
{
	int ret = DPTX_NOERR;

	if (!mtk_dp->training_info.bCablePlugIn)
		return DPTX_PLUG_OUT;

	if (mtk_dp->state != mtk_dp->state_pre) {
		pr_info("m_DPTXState %d, m_DPTXStateTemp %d\n",
			mtk_dp->state, mtk_dp->state_pre);
		mtk_dp->state_pre = mtk_dp->state;
	}

	switch (mtk_dp->state) {
	case DPTXSTATE_INITIAL:
		mdrv_DPTx_VideoMute(mtk_dp, true);
		mdrv_DPTx_AudioMute(mtk_dp, true);
		mtk_dp->state = DPTXSTATE_IDLE;
		break;

	case DPTXSTATE_IDLE:
		if (mtk_dp->training_state == DPTX_NTSTATE_NORMAL)
			mtk_dp->state = DPTXSTATE_PREPARE;
		break;

	case DPTXSTATE_PREPARE:
		if (mtk_dp->bUeventToHwc) {
			mtk_dp->bUeventToHwc = false;
		} else
			pr_info("Skip Uevent(1)\n");

		if (mtk_dp->info.bPatternGen) {
			mtk_dp->video_enable = true;
			mtk_dp->info.input_src = DPTX_SRC_PG;
		}

		if (mtk_dp->video_enable) {
			mtk_dp_video_config(mtk_dp);
			mdrv_DPTx_Video_Enable(mtk_dp, true);
		}

		if (mtk_dp->audio_enable && (mtk_dp->info.audio_caps != 0)) {
			mdrv_DPTx_I2S_Audio_Config(mtk_dp);
			mdrv_DPTx_I2S_Audio_Enable(mtk_dp, true);
		}

		mtk_dp->state = DPTXSTATE_NORMAL;
		break;

	case DPTXSTATE_NORMAL:
		if (mtk_dp->training_state != DPTX_NTSTATE_NORMAL) {
			mdrv_DPTx_VideoMute(mtk_dp, true);
			mdrv_DPTx_AudioMute(mtk_dp, true);
			mdrv_DPTx_StopSentSDP(mtk_dp);
			mtk_dp->state = DPTXSTATE_IDLE;
			pr_info("DPTX Link Status Change!\n");
		}
		break;

	default:
		break;
	}

	return ret;
}

static void mdrv_DPTx_HPD_HandleInISR(struct mtk_dp *mtk_dp)
{
	bool ubCurrentHPD = mhal_DPTx_GetHPDPinLevel(mtk_dp);

	if (mtk_dp->training_info.usPHY_STS == HPD_INITIAL_STATE)
		return;

	if ((mtk_dp->training_info.usPHY_STS & (HPD_CONNECT|HPD_DISCONNECT))
		== (HPD_CONNECT|HPD_DISCONNECT)) {
		if (ubCurrentHPD)
			mtk_dp->training_info.usPHY_STS &= ~HPD_DISCONNECT;
		else
			mtk_dp->training_info.usPHY_STS &= ~HPD_CONNECT;
	}

	if ((mtk_dp->training_info.usPHY_STS & (HPD_INT_EVNET|HPD_DISCONNECT))
		== (HPD_INT_EVNET|HPD_DISCONNECT)) {
		if (ubCurrentHPD)
			mtk_dp->training_info.usPHY_STS &= ~HPD_DISCONNECT;
	}

	if (mtk_dp->training_info.bCablePlugIn)
		mtk_dp->training_info.usPHY_STS &= ~HPD_CONNECT;
	else
		mtk_dp->training_info.usPHY_STS &= ~HPD_DISCONNECT;

	if (mtk_dp->training_info.usPHY_STS & HPD_CONNECT) {
		mtk_dp->training_info.usPHY_STS &= ~HPD_CONNECT;
		mtk_dp->training_info.bCablePlugIn = true;
		mtk_dp->training_info.bCableStateChange = true;
		mtk_dp->bUeventToHwc = true;
		pr_info(" HPD_CON_ISR\n");
	}

	if (mtk_dp->training_info.usPHY_STS & HPD_DISCONNECT) {
		mtk_dp->training_info.usPHY_STS &= ~HPD_DISCONNECT;
		mtk_dp->training_info.bCablePlugIn = false;
		mtk_dp->training_info.bCableStateChange = true;
		mtk_dp->bUeventToHwc = true;
		pr_info("HPD_DISCON_ISR\n");
	}
}

static int DPTx_Int_Count;
void mdrv_DPTx_HPD_ISREvent(struct mtk_dp *mtk_dp)
{
	u8 ubIsrEnable = 0xF1;
	u16 ubSWStatus = mhal_DPTx_GetSWIRQStatus(mtk_dp);
	u8 ubHWStatus_undefine =  mhal_DPTx_GetHPDIRQStatus(mtk_dp);
	u8 ubHWStatus =  ubHWStatus_undefine&(~ubIsrEnable);

	DPTx_Int_Count++;

	mtk_dp->irq_status = (ubHWStatus | ubSWStatus);
	mtk_dp->training_info.usPHY_STS |= (ubHWStatus | ubSWStatus);
	pr_info("HW/SW status = 0x%x\n", (ubHWStatus | ubSWStatus));
	pr_info("ISR: Int Count = %d \r\n", DPTx_Int_Count);

	mdrv_DPTx_HPD_HandleInISR(mtk_dp);

	if (ubSWStatus)
		mhal_DPTx_SWInterruptClr(mtk_dp, ubSWStatus);

	if (ubHWStatus)
		mhal_DPTx_HPDInterruptClr(mtk_dp, ubHWStatus);
}

static void mdrv_DPTx_ISR(struct mtk_dp *mtk_dp)
{
	mhal_DPTx_ISR(mtk_dp);
}

static void mdrv_DPTx_InitPort(struct mtk_dp *mtk_dp)
{
	mhal_DPTx_PHY_SetIdlePattern(mtk_dp, true);
	mdrv_DPTx_InitVariable(mtk_dp);

	mhal_DPTx_InitialSetting(mtk_dp);
	mhal_DPTx_AuxSetting(mtk_dp);
	mhal_DPTx_DigitalSetting(mtk_dp);
	mhal_DPTx_PHYSetting(mtk_dp);
	mhal_DPTx_HPDDetectSetting(mtk_dp);

	mhal_DPTx_DigitalSwReset(mtk_dp);
	mhal_DPTx_Set_Efuse_Value(mtk_dp);
}

void mdrv_DPTx_Video_Enable(struct mtk_dp *mtk_dp, bool bEnable)
{
	pr_info("Output Video %s!\n", bEnable ? "enable" : "disable");

	if (bEnable) {
		mdrv_DPTx_SetDPTXOut(mtk_dp);
		mdrv_DPTx_VideoMute(mtk_dp, false);
		mhal_DPTx_Verify_Clock(mtk_dp);
	} else
		mdrv_DPTx_VideoMute(mtk_dp, true);
}

static void mdrv_DPTx_Set_Color_Format(struct mtk_dp *mtk_dp, u8 ucColorFormat)
{
	pr_info("Set Color Format = 0x%x\n", ucColorFormat);

	mtk_dp->info.format = ucColorFormat;
	mhal_DPTx_SetColorFormat(mtk_dp, ucColorFormat);
}

static void mdrv_DPTx_Set_Color_Depth(struct mtk_dp *mtk_dp, u8 ucColorDepth)
{
	pr_info("Set Color Depth = %d (1~4=6/8/10/12bpp)\n", ucColorDepth + 1);

	mtk_dp->info.depth = ucColorDepth;
	mhal_DPTx_SetColorDepth(mtk_dp, ucColorDepth);
}

void mdrv_DPTx_I2S_Audio_Enable(struct mtk_dp *mtk_dp, bool bEnable)
{
	if (bEnable) {
		mdrv_DPTx_AudioMute(mtk_dp, false);
		pr_info("I2S Audio Enable!\n");
	} else {
		mdrv_DPTx_AudioMute(mtk_dp, true);
		pr_info("I2S Audio Disable!\n");
	}
}

static void mdrv_DPTx_I2S_Audio_Set_MDiv(struct mtk_dp *mtk_dp, u8 ucDiv)
{
	char bTable[7][5] = {"X2", "X4", "X8", "N/A", "/2", "/4", "/8"};

	pr_info("I2S Set Audio M Divider = %s\n", bTable[ucDiv-1]);
	mhal_DPTx_Audio_M_Divider_Setting(mtk_dp, ucDiv);
}

void mdrv_DPTx_I2S_Audio_Config(struct mtk_dp *mtk_dp)
{
	u8 ucChannel, ucFs, ucWordlength;
	unsigned int tmp = mtk_dp->info.audio_config;

	if (!mtk_dp->dp_ready) {
		pr_err("%s, DP is not ready!\n", __func__);
		return;
	}

	ucChannel = (tmp >> DP_CAPABILITY_CHANNEL_SFT)
		& DP_CAPABILITY_CHANNEL_MASK;
	ucFs = (tmp >> DP_CAPABILITY_SAMPLERATE_SFT)
		& DP_CAPABILITY_SAMPLERATE_MASK;
	ucWordlength = (tmp >> DP_CAPABILITY_BITWIDTH_SFT)
			& DP_CAPABILITY_BITWIDTH_MASK;

	switch (ucChannel) {
	case DP_CHANNEL_2:
		ucChannel = 2;
		break;
	case DP_CHANNEL_8:
		ucChannel = 8;
		break;
	default:
		ucChannel = 2;
		break;
	}

	switch (ucFs) {
	case DP_SAMPLERATE_32:
		ucFs = FS_32K;
		break;
	case DP_SAMPLERATE_44:
		ucFs = FS_44K;
		break;
	case DP_SAMPLERATE_48:
		ucFs = FS_48K;
		break;
	case DP_SAMPLERATE_96:
		ucFs = FS_96K;
		break;
	case DP_SAMPLERATE_192:
		ucFs = FS_192K;
		break;
	default:
		ucFs = FS_48K;
		break;
	}

	switch (ucWordlength) {
	case DP_BITWIDTH_16:
		ucWordlength = WL_16bit;
		break;
	case DP_BITWIDTH_20:
		ucWordlength = WL_20bit;
		break;
	case DP_BITWIDTH_24:
		ucWordlength = WL_24bit;
		break;
	default:
		ucWordlength = WL_24bit;
		break;
	}

	mdrv_DPTx_I2S_Audio_SDP_Channel_Setting(mtk_dp, ucChannel,
		ucFs, ucWordlength);
	mdrv_DPTx_I2S_Audio_Ch_Status_Set(mtk_dp, ucChannel,
		ucFs, ucWordlength);

	mhal_DPTx_Audio_PG_EN(mtk_dp, ucChannel, ucFs, false);
	mdrv_DPTx_I2S_Audio_Set_MDiv(mtk_dp, 5);
}

void mdrv_DPTx_I2S_Audio_SDP_Channel_Setting(struct mtk_dp *mtk_dp,
	u8 ucChannel, u8 ucFs, u8 ucWordlength)
{
	u8 SDP_DB[32] = {0};
	u8 SDP_HB[4] = {0};

	SDP_HB[1] = DP_SPEC_SDPTYP_AINFO;
	SDP_HB[2] = 0x1B;
	SDP_HB[3] = 0x48;

	SDP_DB[0x0] = 0x10 | (ucChannel-1); //L-PCM[7:4], channel-1[2:0]
	SDP_DB[0x1] = ucFs << 2 | ucWordlength; // fs[4:2], len[1:0]
	SDP_DB[0x2] = 0x0;

	if (ucChannel == 8)
		SDP_DB[0x3] = 0x13;
	else
		SDP_DB[0x3] = 0x00;

	mhal_DPTx_Audio_SDP_Setting(mtk_dp, ucChannel);
	pr_info("I2S Set Audio Channel = %d\n", ucChannel);
	mdrv_DPTx_SPKG_SDP(mtk_dp, true, DPTx_SDPTYP_AUI, SDP_HB, SDP_DB);
}

void mdrv_DPTx_I2S_Audio_Ch_Status_Set(struct mtk_dp *mtk_dp, u8 ucChannel,
	u8 ucFs, u8 ucWordlength)
{
	mhal_DPTx_Audio_Ch_Status_Set(mtk_dp, ucChannel, ucFs, ucWordlength);
}

void mdrv_DPTx_FEC_Ready(struct mtk_dp *mtk_dp, u8 err_cnt_sel)
{
	u8 i, Data[3];

	drm_dp_dpcd_read(&mtk_dp->aux, 0x90, Data, 0x1);
	pr_info("FEC Capable[0], [0:3] should be 1: 0x%x\n", Data[0]);

	/* FEC error count select 120[3:1]:         *
	 * 000b: FEC_ERROR_COUNT_DIS                *
	 * 001b: UNCORRECTED_BLOCK_ERROR_COUNT      *
	 * 010b: CORRECTED_BLOCK_ERROR_COUNT        *
	 * 011b: BIT_ERROR_COUNT                    *
	 * 100b: PARITY_BLOCK_ERROR_COUNT           *
	 * 101b: PARITY_BIT_ERROR_COUNT             *
	 */
	if (Data[0] & BIT0) {
		mtk_dp->has_fec = true;
		Data[0] = (err_cnt_sel << 1) | 0x1;     //FEC Ready
		drm_dp_dpcd_write(&mtk_dp->aux, 0x120, Data, 0x1);
		drm_dp_dpcd_read(&mtk_dp->aux, 0x280, Data, 0x3);
		for (i = 0; i < 3; i++)
			pr_info("FEC status & error Count: 0x%x\n", Data[i]);
	}
}

void mtk_dp_video_config(struct mtk_dp *mtk_dp)
{
	struct DPTX_TIMING_PARAMETER *DPTX_TBL = &mtk_dp->info.DPTX_OUTBL;
	u32 mvid = 0;
	bool overwrite = false;
	struct videomode vm = { 0 };

	drm_display_mode_to_videomode(&mtk_dp->mode, &vm);

	if (!mtk_dp->dp_ready) {
		pr_err("%s, DP is not ready!\n", __func__);
		return;
	}

	if (mtk_dp->info.resolution >= SINK_MAX) {
		pr_err("DPTX doesn't support this resolution(%d)!\n",
			mtk_dp->info.resolution);
		return;
	}

	DPTX_TBL->FrameRate = mtk_dp->mode.clock * 1000 / mtk_dp->mode.htotal / mtk_dp->mode.vtotal;
	DPTX_TBL->Htt = mtk_dp->mode.htotal;
	DPTX_TBL->Hbp = vm.hback_porch;
	DPTX_TBL->Hsw = vm.hsync_len;
	DPTX_TBL->bHsp = 0;
	DPTX_TBL->Hfp = vm.hfront_porch;
	DPTX_TBL->Hde = vm.hactive;
	DPTX_TBL->Vtt = mtk_dp->mode.vtotal;
	DPTX_TBL->Vbp = vm.vback_porch;
	DPTX_TBL->Vsw = vm.vsync_len;
	DPTX_TBL->bVsp = 0;
	DPTX_TBL->Vfp = vm.vfront_porch;
	DPTX_TBL->Vde = vm.vactive;

	mhal_DPTx_OverWrite_MN(mtk_dp, overwrite, mvid, 0x8000);

	if (mtk_dp->has_dsc) {
		uint8_t Data[1];

		Data[0] = (u8) mtk_dp->dsc_enable;
		drm_dp_dpcd_write(&mtk_dp->aux, 0x160, Data, 0x1);
	}

	//interlace not support
	DPTX_TBL->Video_ip_mode = DPTX_VIDEO_PROGRESSIVE;
	mhal_DPTx_SetMSA(mtk_dp);

	mdrv_DPTx_Set_MISC(mtk_dp);

	mdrv_DPTx_Set_Color_Depth(mtk_dp, mtk_dp->info.depth);
	mdrv_DPTx_Set_Color_Format(mtk_dp, mtk_dp->info.format);
}

struct edid *mtk_dp_handle_edid(struct mtk_dp *mtk_dp)
{
	struct drm_connector *connector = &mtk_dp->conn;

	/* use cached edid if we have one */
	if (mtk_dp->edid) {
		/* invalid edid */
		if (IS_ERR(mtk_dp->edid))
			return NULL;

		pr_info("duplicate edid from mtk_dp->edid!\n");
		return drm_edid_duplicate(mtk_dp->edid);
	}

	pr_info("Get edid from RX!\n");
	return drm_get_edid(connector, &mtk_dp->aux.ddc);
}

static irqreturn_t mtk_dp_hpd_event(int hpd, void *dev)
{
	struct mtk_dp *mtk_dp = dev;

	mdrv_DPTx_ISR(mtk_dp);

	return IRQ_HANDLED;
}

static int mtk_dp_dt_parse_pdata(struct mtk_dp *mtk_dp,
		struct platform_device *pdev)
{
	struct resource regs;
	struct device *dev = &pdev->dev;
	int ret = 0;

	if (of_address_to_resource(dev->of_node, 0, &regs) != 0)
		dev_err(dev, "Missing reg in %s node\n",
		dev->of_node->full_name);

	mtk_dp->regs = of_iomap(dev->of_node, 0);
	mtk_dp->dp_tx_clk = devm_clk_get(dev, "dp_tx_faxi");
	if (IS_ERR(mtk_dp->dp_tx_clk)) {
		ret = PTR_ERR(mtk_dp->dp_tx_clk);
		dev_err(dev, "Failed to get dptx clock: %d\n", ret);
		mtk_dp->dp_tx_clk = NULL;
	}

	pr_info("reg and clock get success!\n");

	return 0;
}

static inline struct mtk_dp *mtk_dp_ctx_from_conn(struct drm_connector *c)
{
	return container_of(c, struct mtk_dp, conn);
}

static enum drm_connector_status mtk_dp_conn_detect(struct drm_connector *conn,
	bool force)
{
	struct mtk_dp *mtk_dp = mtk_dp_ctx_from_conn(conn);

	return ((mhal_DPTx_GetHPDPinLevel(mtk_dp)) ? connector_status_connected :
		connector_status_disconnected);
}

static enum drm_connector_status mtk_dp_bdg_detect(struct drm_bridge *bridge)
{
	struct mtk_dp *mtk_dp = dp_from_bridge(bridge);
	enum drm_connector_status ret;

	ret = mhal_DPTx_GetHPDPinLevel(mtk_dp) ? connector_status_connected :
			connector_status_disconnected;

	if (mtk_dp->driver_data && mtk_dp->driver_data->is_edp)
		return connector_status_connected;
	else
		return ret;
}

static void mtk_dp_conn_destroy(struct drm_connector *conn)
{
	drm_connector_cleanup(conn);
}

static const struct drm_connector_funcs mtk_dp_connector_funcs = {
	.detect = mtk_dp_conn_detect,
	.fill_modes = drm_helper_probe_single_connector_modes,
	.destroy = mtk_dp_conn_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
};

static int mtk_dp_conn_mode_valid(struct drm_connector *conn,
		struct drm_display_mode *mode)
{
	return MODE_OK;
}

static struct drm_encoder *mtk_dp_conn_best_enc(struct drm_connector *conn)
{
	struct mtk_dp *mtk_dp = dp_from_conn(conn);

	return mtk_dp->bridge.encoder;
}

static int mtk_dp_conn_get_modes(struct drm_connector *conn)
{
	struct mtk_dp *mtk_dp = dp_from_conn(conn);
	int ret;
	int num_modes = 0;

	if (mtk_dp->edid) {
		drm_connector_update_edid_property(&mtk_dp->conn,
			mtk_dp->edid);
		num_modes = drm_add_edid_modes(&mtk_dp->conn, mtk_dp->edid);
		pr_info("%s modes = %d\n", __func__, num_modes);
		return num_modes;
	} else {
		mtk_dp->edid = drm_get_edid(conn, &mtk_dp->aux.ddc);
		if (!mtk_dp->edid) {
			DRM_ERROR("Failed to read EDID\n");
			goto error;
		}

		ret = drm_connector_update_edid_property(conn, mtk_dp->edid);
		if (ret) {
			DRM_ERROR("Failed to update EDID property: %d\n", ret);
			goto error;
		}

		num_modes = drm_add_edid_modes(conn, mtk_dp->edid);
		return num_modes;
	}

error:
	return num_modes;
}

static int mtk_dp_bdg_get_modes(struct drm_bridge *bridge, struct drm_connector *conn)
{
	struct mtk_dp *mtk_dp = dp_from_bridge(bridge);
	int ret;
	int num_modes = 0;

	if (mtk_dp->edid) {
		drm_connector_update_edid_property(&mtk_dp->conn,
			mtk_dp->edid);
		num_modes = drm_add_edid_modes(&mtk_dp->conn, mtk_dp->edid);
		pr_info("%s modes = %d\n", __func__, num_modes);
		return num_modes;
	} else {
		mtk_dp->edid = drm_get_edid(conn, &mtk_dp->aux.ddc);
		if (!mtk_dp->edid) {
			DRM_ERROR("Failed to read EDID\n");
			goto error;
		}

		ret = drm_connector_update_edid_property(conn, mtk_dp->edid);
		if (ret) {
			DRM_ERROR("Failed to update EDID property: %d\n", ret);
			goto error;
		}

		num_modes = drm_add_edid_modes(conn, mtk_dp->edid);
		return num_modes;
	}

error:
	return num_modes;
}

static const struct drm_connector_helper_funcs
		mtk_dp_connector_helper_funcs = {
	.get_modes = mtk_dp_conn_get_modes,
	.mode_valid = mtk_dp_conn_mode_valid,
	.best_encoder = mtk_dp_conn_best_enc,
};

static struct edid *mtk_get_edid(struct drm_bridge *bridge,
			   struct drm_connector *connector)
{
	struct mtk_dp *mtk_dp = dp_from_bridge(bridge);
	bool poweroff = !mtk_dp->powered;
	struct edid *edid;

	drm_bridge_chain_pre_enable(bridge);

	edid = drm_get_edid(connector, &mtk_dp->aux.ddc);

	/*
	 * If we call the get_edid() function without having enabled the chip
	 * before, return the chip to its original power state.
	 */
	if (poweroff)
		drm_bridge_chain_post_disable(bridge);

	mtk_dp->edid = edid;

	return edid;
}


static ssize_t mtk_dp_aux_transfer(struct drm_dp_aux *mtk_aux,
	struct drm_dp_aux_msg *msg)
{
	u8 ubCmd;
	void *pData;
	size_t ubLength, ret = 0;
	u32 usADDR;
	bool ack = false;
	struct mtk_dp *mtk_dp;

	mtk_dp = container_of(mtk_aux, struct mtk_dp, aux);
	ubCmd = msg->request;
	usADDR = msg->address;
	ubLength = msg->size;
	pData = msg->buffer;

	switch (ubCmd) {
	case DP_AUX_I2C_MOT:
	case DP_AUX_I2C_WRITE:
	case DP_AUX_NATIVE_WRITE:
	case DP_AUX_I2C_WRITE_STATUS_UPDATE:
	case DP_AUX_I2C_WRITE_STATUS_UPDATE | DP_AUX_I2C_MOT:
		ubCmd &= ~DP_AUX_I2C_WRITE_STATUS_UPDATE;
		ack = mdrv_DPTx_AuxWrite_DPCD(mtk_dp, ubCmd,
			usADDR, ubLength, pData);
		break;

	case DP_AUX_I2C_READ:
	case DP_AUX_NATIVE_READ:
	case DP_AUX_I2C_READ | DP_AUX_I2C_MOT:
		ack = mdrv_DPTx_AuxRead_DPCD(mtk_dp, ubCmd,
			usADDR, ubLength, pData);
		break;

	default:
		pr_err("invalid aux cmd = %d\n", ubCmd);
		ret = -EINVAL;
		break;
	}

	if (ack) {
		msg->reply = DP_AUX_NATIVE_REPLY_ACK | DP_AUX_I2C_REPLY_ACK;
		ret = ubLength;
	} else {
		msg->reply = DP_AUX_NATIVE_REPLY_NACK | DP_AUX_I2C_REPLY_NACK;
		ret = -EAGAIN;
	}

	return ret;
}

static void mtk_dp_aux_init(struct mtk_dp *mtk_dp)
{
	drm_dp_aux_init(&mtk_dp->aux);
	pr_info("aux hw_mutex = 0x%p\n", &mtk_dp->aux.hw_mutex);

	mtk_dp->aux.name = kasprintf(GFP_KERNEL, "DPDDC-MTK");
	mtk_dp->aux.transfer = mtk_dp_aux_transfer;
}

static void mtk_dp_HPDInterruptSet(struct mtk_dp *mtk_dp, int bstatus)
{
	pr_info("status:%d[2:DISCONNECT, 4:CONNECT, 8:IRQ] Power:%d\n",
		bstatus, mtk_dp->bPowerOn);

	if ((bstatus == HPD_CONNECT && !mtk_dp->bPowerOn)
		|| (bstatus == HPD_DISCONNECT && mtk_dp->bPowerOn)
		|| (bstatus == HPD_INT_EVNET && mtk_dp->bPowerOn)) {

		if (bstatus == HPD_CONNECT) {
			int ret;

			if (mtk_dp->dp_tx_clk) {
				ret = clk_prepare_enable(mtk_dp->dp_tx_clk);
				if (ret < 0)
					pr_err("Fail to enable dptx clock: %d\n", ret);
			}
			mdrv_DPTx_InitPort(mtk_dp);
			mhal_DPTx_AnalogPowerOnOff(mtk_dp, true);
			/* mhal_DPTx_USBC_HPD(mtk_dp, true); */
			mtk_dp->bPowerOn = true;
		} else if (bstatus == HPD_DISCONNECT) {
			/* mhal_DPTx_USBC_HPD(mtk_dp, false); */
		}

		/*
		 * mhal_DPTx_SWInterruptEnable(mtk_dp, true);
		 * mhal_DPTx_SWInterruptSet(mtk_dp, bstatus);
		 */
		mhal_DPTx_HPDInterruptEnable(mtk_dp, true);
		return;
	}
}

void mtk_dp_poweroff(struct mtk_dp *mtk_dp)
{
	mutex_lock(&mtk_dp->dp_lock);
	if (mtk_dp->disp_status == DPTX_DISP_NONE) {
		pr_info("DPTX has been powered off\n");
		mutex_unlock(&mtk_dp->dp_lock);
		return;
	}

	mtk_dp->disp_status = DPTX_DISP_SUSPEND;
	mtk_dp_HPDInterruptSet(mtk_dp, HPD_DISCONNECT);
	mutex_unlock(&mtk_dp->dp_lock);
}

void mtk_dp_poweron(struct mtk_dp *mtk_dp)
{
	mutex_lock(&mtk_dp->dp_lock);
	mtk_dp->disp_status = DPTX_DISP_RESUME;
	if (mtk_dp->bPowerOn) {
		pr_info("DPTX has been powered on\n");
		mutex_unlock(&mtk_dp->dp_lock);
		return;
	}

	mtk_dp_HPDInterruptSet(mtk_dp, HPD_CONNECT);
	mutex_unlock(&mtk_dp->dp_lock);
}

static int mtk_dp_bridge_attach(struct drm_bridge *bridge,
			enum drm_bridge_attach_flags flags)
{
	struct mtk_dp *mtk_dp = dp_from_bridge(bridge);
	int ret;

	mtk_dp_poweron(mtk_dp);

	if (mtk_dp->next_bridge) {
		ret = drm_bridge_attach(bridge->encoder, mtk_dp->next_bridge, &mtk_dp->bridge, flags);
		if (ret) {
			dev_err(mtk_dp->dev,
				"Failed to attach external bridge: %d\n", ret);
			return ret;
		}
	}

	if (flags & DRM_BRIDGE_ATTACH_NO_CONNECTOR) {
		DRM_ERROR("Fix bridge driver to make connector optional!");
		return 0;
	}

	mtk_dp->drm_dev = bridge->dev;

	ret = drm_connector_init(bridge->dev, &mtk_dp->conn, &mtk_dp_connector_funcs,
				 DRM_MODE_CONNECTOR_DisplayPort);
	if (ret) {
		pr_info("Failed to initialize connector: %d\n", ret);
		return ret;
	}
	drm_connector_helper_add(&mtk_dp->conn, &mtk_dp_connector_helper_funcs);

	mtk_dp->conn.polled = DRM_CONNECTOR_POLL_HPD;
	mtk_dp->conn.interlace_allowed = false;
	mtk_dp->conn.doublescan_allowed = false;

	ret = drm_connector_attach_encoder(&mtk_dp->conn,
						bridge->encoder);
	if (ret) {
		pr_info("Failed to attach connector to encoder: %d\n", ret);
		return ret;
	}

	return 0;
}

static bool mtk_dp_bridge_mode_fixup(struct drm_bridge *bridge,
				       const struct drm_display_mode *mode,
				       struct drm_display_mode *adjusted_mode)
{
	return true;
}

static void mtk_dp_bridge_disable(struct drm_bridge *bridge)
{
	struct mtk_dp *mtk_dp = dp_from_bridge(bridge);

	if (!mtk_dp->enabled)
		return;

	mtk_dp->enabled = false;
}

static void mtk_dp_bridge_post_disable(struct drm_bridge *bridge)
{
	struct mtk_dp *mtk_dp = dp_from_bridge(bridge);

	if (!mtk_dp->powered)
		return;

	mtk_dp->powered = false;
}

static void mtk_dp_bridge_mode_set(struct drm_bridge *bridge,
				     const struct drm_display_mode *mode,
				     const struct drm_display_mode *adjusted_mode)
{
	struct mtk_dp *mtk_dp = dp_from_bridge(bridge);

	drm_mode_copy(&mtk_dp->mode, adjusted_mode);
}

static void mtk_dp_bridge_pre_enable(struct drm_bridge *bridge)
{
	struct mtk_dp *mtk_dp = dp_from_bridge(bridge);

	mdrv_DPTx_CheckSinkCap(mtk_dp);

	mtk_dp->powered = true;
}

static void mtk_dp_bridge_enable(struct drm_bridge *bridge)
{
	struct mtk_dp *mtk_dp = dp_from_bridge(bridge);
	int ret = DPTX_NOERR;
	int i;

	mdrv_DPTx_CheckSinkCap(mtk_dp);
	mtk_dp->edid = mtk_dp_handle_edid(mtk_dp);
	//training
	for (i = 0; i < 50; i++) {
		ret = mdrv_DPTx_Training_Handler(mtk_dp);
		if (ret != DPTX_NOERR)
			break;

		ret = mdrv_DPTx_Handle(mtk_dp);
		if (ret != DPTX_NOERR)
			break;
	}

	if (ret != DPTX_NOERR)
		pr_info("dptx handle fail!!!");


	mtk_dp_video_config(mtk_dp);
	mdrv_DPTx_Video_Enable(mtk_dp, true);

	if ((mtk_dp->info.audio_caps != 0)) {
		mdrv_DPTx_I2S_Audio_Config(mtk_dp);
		mdrv_DPTx_I2S_Audio_Enable(mtk_dp, true);
	}

	mtk_dp->enabled = true;
}

static const struct drm_bridge_funcs mtk_dp_bridge_funcs = {
	.attach = mtk_dp_bridge_attach,
	.mode_fixup = mtk_dp_bridge_mode_fixup,
	.disable = mtk_dp_bridge_disable,
	.post_disable = mtk_dp_bridge_post_disable,
	.mode_set = mtk_dp_bridge_mode_set,
	.pre_enable = mtk_dp_bridge_pre_enable,
	.enable = mtk_dp_bridge_enable,
	.get_edid = mtk_get_edid,
	.detect = mtk_dp_bdg_detect,
	.get_modes = mtk_dp_bdg_get_modes,
};

static int mtk_drm_dp_probe(struct platform_device *pdev)
{
	struct mtk_dp *mtk_dp;
	struct device *dev = &pdev->dev;
	int ret, irq_num = 0;
	struct mtk_drm_private *mtk_priv = dev_get_drvdata(dev);
	struct drm_panel *panel;

	dev_err(&pdev->dev, "%s", __func__);
	dev_err(&pdev->dev, "DP_TX PROBE START!!!");
	mtk_dp = devm_kmalloc(dev, sizeof(*mtk_dp), GFP_KERNEL | __GFP_ZERO);
	if (!mtk_dp)
		return -ENOMEM;

	memset(mtk_dp, 0, sizeof(struct mtk_dp));
	mtk_dp->id = 0x0;
	mtk_dp->dev = dev;
	mtk_dp->priv = mtk_priv;
	mtk_dp->bUeventToHwc = false;
	mtk_dp->disp_status = DPTX_DISP_NONE;

	irq_num = platform_get_irq(pdev, 0);
	if (irq_num < 0) {
		dev_err(&pdev->dev, "failed to request dp irq resource\n");
		return -EPROBE_DEFER;
	}

	mtk_dp->driver_data = of_device_get_match_data(dev);

	if (mtk_dp->driver_data && mtk_dp->driver_data->is_edp) {
		ret = drm_of_find_panel_or_bridge(dev->of_node, 1, 0,
						  &panel, &mtk_dp->next_bridge);
		if (ret) {
			dev_err(dev, "failed to drm_of_find_panel_or_bridge!ret=%d\n", ret);
			return -EPROBE_DEFER;
		}

		if (panel) {
			mtk_dp->next_bridge = devm_drm_panel_bridge_add(dev, panel);
			if (IS_ERR(mtk_dp->next_bridge)) {
				ret = PTR_ERR(mtk_dp->next_bridge);
				dev_err(dev, "failed to devm_drm_panel_bridge_add!\n");
				return -EPROBE_DEFER;
			}
		}
	}
	ret = mtk_dp_dt_parse_pdata(mtk_dp, pdev);
	if (ret)
		return ret;

	mtk_dp_aux_init(mtk_dp);

	dev_err(&pdev->dev, "type %d, irq %d\n", MTK_DISP_DPTX, irq_num);

	irq_set_status_flags(irq_num, IRQ_TYPE_LEVEL_HIGH);
	ret = devm_request_irq(&pdev->dev, irq_num, mtk_dp_hpd_event,
			IRQ_TYPE_LEVEL_HIGH, dev_name(&pdev->dev), mtk_dp);
	if (ret) {
		dev_err(&pdev->dev, "failed to request mediatek dptx irq\n");
		return -EPROBE_DEFER;
	}

	mutex_init(&mtk_dp->dp_lock);

	mtk_dp->bridge.funcs = &mtk_dp_bridge_funcs;
	mtk_dp->bridge.of_node = pdev->dev.of_node;
	if (mtk_dp->driver_data && mtk_dp->driver_data->is_edp)
		mtk_dp->bridge.type = DRM_MODE_CONNECTOR_eDP;
	else
		mtk_dp->bridge.type = DRM_MODE_CONNECTOR_DisplayPort;

	mtk_dp->bridge.ops = DRM_BRIDGE_OP_DETECT | DRM_BRIDGE_OP_EDID
			| DRM_BRIDGE_OP_HPD | DRM_BRIDGE_OP_MODES;
	drm_bridge_add(&mtk_dp->bridge);

	platform_set_drvdata(pdev, mtk_dp);

	dev_err(&pdev->dev, "probe done\n");

	return 0;
}

static int mtk_drm_dp_remove(struct platform_device *pdev)
{
	struct mtk_dp *mtk_dp = platform_get_drvdata(pdev);
	int ret;

	mdrv_DPTx_VideoMute(mtk_dp, true);
	mdrv_DPTx_AudioMute(mtk_dp, true);

	drm_connector_cleanup(&mtk_dp->conn);

	if (!IS_ERR(mtk_dp->task)) {
		ret = kthread_stop(mtk_dp->task);
		pr_info( "=====thread function has stop %ds======\n", ret);
	}

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int mtk_dp_suspend(struct device *dev)
{
	struct mtk_dp *mtk_dp = dev_get_drvdata(dev);

	mutex_lock(&mtk_dp->dp_lock);
	if (mtk_dp->bPowerOn) {
		mtk_dp->disp_status = DPTX_DISP_SUSPEND;
		mtk_dp_HPDInterruptSet(mtk_dp, HPD_DISCONNECT);
		mdelay(5);
	}
	mutex_unlock(&mtk_dp->dp_lock);

	return 0;
}

static int mtk_dp_resume(struct device *dev)
{
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(mtk_dp_pm_ops,
		mtk_dp_suspend, mtk_dp_resume);

static const struct mtk_dp_driver_data mt8195_edp_driver_data = {
	.is_edp = true,
};

static const struct of_device_id mtk_dp_of_match[] = {
	{ .compatible = "mediatek,mt8195-dp_tx", },
	{ .compatible = "mediatek,mt8195-edp_tx",
	  .data = &mt8195_edp_driver_data },
	{ },
};


struct platform_driver mtk_dp_tx_driver = {
	.probe = mtk_drm_dp_probe,
	.remove = mtk_drm_dp_remove,
	.driver = {
		.name = "mediatek-drm-dp",
		.of_match_table = mtk_dp_of_match,
		.pm = &mtk_dp_pm_ops,
	},
};

