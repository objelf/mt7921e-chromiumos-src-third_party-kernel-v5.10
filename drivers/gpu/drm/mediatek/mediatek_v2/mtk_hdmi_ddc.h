/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#ifndef _MTK_DDC_H
#define _MTK_DDC_H

#include <linux/clk.h>
#include <linux/i2c.h>

struct mtk_hdmi_ddc {
	struct i2c_adapter adap;
	struct clk *clk;
	void __iomem *regs;
};

enum SIF_BIT_T_HDMI {
	SIF_8_BIT_HDMI,		/* /< [8 bits data address.] */
	SIF_16_BIT_HDMI,	/* /< [16 bits data address.] */
};

enum SIF_BIT_T {
	SIF_8_BIT,		/* /< [8 bits data address.] */
	SIF_16_BIT,		/* /< [16 bits data address.] */
};

void DDC_WR_ONE(struct mtk_hdmi_ddc *ddc,
	unsigned int addr_id, unsigned int offset_id,
	unsigned char wr_data);

unsigned char _DDCMRead_hdmi(struct mtk_hdmi_ddc *ddc,
	unsigned char ucCurAddrMode, unsigned int u4ClkDiv,
	unsigned char ucDev, unsigned int u4Addr,
	enum SIF_BIT_T_HDMI ucAddrType,
	unsigned char *pucValue, unsigned int u4Count);

unsigned int DDCM_RanAddr_Write(struct mtk_hdmi_ddc *ddc,
	unsigned int u4ClkDiv, unsigned char ucDev, unsigned int u4Addr,
	enum SIF_BIT_T ucAddrType, const unsigned char *pucValue,
	unsigned int u4Count);

unsigned char DDCM_RanAddr_Read(struct mtk_hdmi_ddc *ddc,
	unsigned int u4ClkDiv, unsigned char ucDev,
	unsigned int u4Addr, enum SIF_BIT_T ucAddrType,
	unsigned char *pucValue, unsigned int u4Count);

unsigned int DDCM_CurAddr_Read(struct mtk_hdmi_ddc *ddc,
	unsigned int u4ClkDiv, unsigned char ucDev,
	unsigned char *pucValue, unsigned int u4Count);

unsigned char vDDCRead(struct mtk_hdmi_ddc *ddc,
	unsigned int u4ClkDiv, unsigned char ucDev,
	unsigned int u4Addr, enum SIF_BIT_T_HDMI ucAddrType,
	unsigned char *pucValue, unsigned int u4Count);

extern unsigned char fgDDCDataRead(struct mtk_hdmi_ddc *ddc,
	unsigned char bDevice, unsigned char bData_Addr,
	unsigned char bDataCount, unsigned char *prData);
extern unsigned char fgDDCDataWrite(struct mtk_hdmi_ddc *ddc,
	unsigned char bDevice, unsigned char bData_Addr,
	unsigned char bDataCount, unsigned char *prData);

#endif /* _MTK_DDC_H */
