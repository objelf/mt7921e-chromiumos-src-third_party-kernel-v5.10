/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#ifndef __MTK_DISP_ETHDR_H__
#define __MTK_DISP_ETHDR_H__

#include <linux/uaccess.h>
#include <drm/mediatek_drm.h>

enum mtk_ethdr_comp_id {
	ETHDR_DISP_MIXER,
	ETHDR_VDO_FE0,
	ETHDR_VDO_FE1,
	ETHDR_GFX_FE0,
	ETHDR_GFX_FE1,
	ETHDR_VDO_BE,
	ETHDR_ADL_DS,
	ETHDR_ID_MAX
};

enum mtk_ethdr_async_id {
	ETHDR_ASYNC_VDO_FE0,
	ETHDR_ASYNC_VDO_FE1,
	ETHDR_ASYNC_GFX_FE0,
	ETHDR_ASYNC_GFX_FE1,
	ETHDR_ASYNC_VDO_BE,
	ETHDR_ASYNC_ID_MAX
};

void mtk_ethdr_layer_config(unsigned int idx,
				 struct mtk_plane_state *state,
				 struct cmdq_pkt *cmdq_pkt);
void mtk_ethdr_layer_on(unsigned int idx, struct cmdq_pkt *cmdq_pkt);
void mtk_ethdr_layer_off(unsigned int idx, struct cmdq_pkt *cmdq_pkt);

#endif

