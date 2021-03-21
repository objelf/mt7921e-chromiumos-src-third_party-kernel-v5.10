/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#ifndef __MTK_HDCP_H__
#define __MTK_HDCP_H__

#include <linux/types.h>


#ifdef DPTX_HDCP_ENABLE

struct HDCP1X_INFO {
	int MainStates;
	int SubStates;
	unsigned char uRetryCount;
	uint8_t bEnable:1;
	uint8_t bRepeater:1;
	uint8_t bR0Read:1;
	uint8_t bKSV_READY:1;
	uint8_t bMAX_CASCADE:1;
	uint8_t bMAX_DEVS:1;
	unsigned char ubBstatus;
	unsigned char ubBksv[5];
	unsigned char ubAksv[5];
	unsigned char ubV[20];
	unsigned char ubBinfo[2];
	unsigned char ubKSVFIFO[5*127];
	unsigned char ubDEVICE_COUNT;
};

struct HDCP2_INFO {
	uint8_t bEnable:1;
	uint8_t bRepeater:1;
	uint8_t bReadcertrx:1;
	uint8_t bReadHprime:1;
	uint8_t bReadPairing:1;
	uint8_t bReadLprime:1;
	uint8_t bksExchangeDone:1;
	uint8_t bReadVprime:1;
	uint8_t uRetryCount;
	uint8_t uDeviceCount;
	uint8_t uStreamIDType;
};

enum HDCP_RESULT {
	AUTH_ZERO     = 0,
	AUTH_INIT     = 1,
	AUTH_PASS     = 2,
	AUTH_FAIL     = 3,
};

#endif
#endif

