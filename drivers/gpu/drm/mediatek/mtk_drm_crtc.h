/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015 MediaTek Inc.
 */

#ifndef MTK_DRM_CRTC_H
#define MTK_DRM_CRTC_H

#include <drm/drm_crtc.h>
#include "mtk_drm_ddp_comp.h"
#include "mtk_drm_plane.h"
#include "mtk_panel_ext.h"

#define MTK_LUT_SIZE	512
#define MTK_MAX_BPC	10
#define MTK_MIN_BPC	3

#define _MASK_SHIFT(val, width, shift)                                         \
	(((val) >> (shift)) & ((1 << (width)) - 1))

#define REG_FLD(width, shift)                                                  \
	((unsigned int)((((width)&0xff) << 16) | ((shift)&0xff)))

#define REG_FLD_MSB_LSB(msb, lsb) REG_FLD((msb) - (lsb) + 1, (lsb))

#define REG_FLD_WIDTH(field) ((unsigned int)(((field) >> 16) & 0xff))

#define REG_FLD_SHIFT(field) ((unsigned int)((field)&0xff))

#define REG_FLD_MASK(field)                                                    \
	((unsigned int)((1ULL << REG_FLD_WIDTH(field)) - 1)                    \
	 << REG_FLD_SHIFT(field))

#define REG_FLD_VAL(field, val)                                                \
	(((val) << REG_FLD_SHIFT(field)) & REG_FLD_MASK(field))

#define REG_FLD_VAL_GET(field, regval)                                         \
	(((regval)&REG_FLD_MASK(field)) >> REG_FLD_SHIFT(field))

#define DISP_REG_GET_FIELD(field, reg32)                                       \
	REG_FLD_VAL_GET(field, __raw_readl((unsigned long *)(reg32)))

#define SET_VAL_MASK(o_val, o_mask, i_val, i_fld)                              \
	do {                                                                   \
		o_val |= (i_val << REG_FLD_SHIFT(i_fld));                      \
		o_mask |= (REG_FLD_MASK(i_fld));                               \
	} while (0)

#define for_each_comp_in_crtc_target_mode(comp, mtk_crtc, __i, __j, ddp_mode)  \
		for ((__i) = 0; (__i) < DDP_PATH_NR; (__i)++)						   \
			for ((__j) = 0; 					  \
				(__j) < 						  \
				(mtk_crtc)->ddp_ctx[ddp_mode].ddp_comp_nr[__i] &&  \
				((comp) = (mtk_crtc)->ddp_ctx[ddp_mode].ddp_comp[__i]  \
				[__j],							  \
				1); 							  \
				(__j)++)						  \
				for_each_if(comp)

enum CRTC_DDP_MODE {
	DDP_MAJOR,
	DDP_MINOR,
	DDP_MODE_NR,
	DDP_NO_USE,
};

enum CRTC_DDP_PATH {
	DDP_FIRST_PATH,
	DDP_SECOND_PATH,
	DDP_PATH_NR,
};

struct mtk_crtc_path_data {
	const enum mtk_ddp_comp_id *path[DDP_MODE_NR][DDP_PATH_NR];
	unsigned int path_len[DDP_MODE_NR][DDP_PATH_NR];
	bool path_req_hrt[DDP_MODE_NR][DDP_PATH_NR];
	const enum mtk_ddp_comp_id *wb_path[DDP_MODE_NR];
	unsigned int wb_path_len[DDP_MODE_NR];
	//const struct mtk_addon_scenario_data *addon_data;
	//for dual path
	const enum mtk_ddp_comp_id *dual_path[DDP_PATH_NR];
	unsigned int dual_path_len[DDP_PATH_NR];
};

/**
 * struct mtk_crtc_ddp_ctx - MediaTek specific ddp structure for crtc path
 * control.
 * @mutex: handle to one of the ten disp_mutex streams
 * @ddp_comp_nr: number of components in ddp_comp
 * @ddp_comp: array of pointers the mtk_ddp_comp structures used by this crtc
 * @wb_comp_nr: number of components in 1to2 path
 * @wb_comp: array of pointers the mtk_ddp_comp structures used for 1to2 path
 * @wb_fb: temp wdma output buffer in 1to2 path
 * @dc_fb: frame buffer for decouple mode
 * @dc_fb_idx: the index of latest used fb
 */
struct mtk_crtc_ddp_ctx {
	struct mtk_mutex *mutex;
	unsigned int ddp_comp_nr[DDP_PATH_NR];
	struct mtk_ddp_comp **ddp_comp[DDP_PATH_NR];
	bool req_hrt[DDP_PATH_NR];
	unsigned int wb_comp_nr;
	struct mtk_ddp_comp **wb_comp;
	struct drm_framebuffer *wb_fb;
	struct drm_framebuffer *dc_fb;
	unsigned int dc_fb_idx;
};

/**
 * struct mtk_drm_crtc - MediaTek specific crtc structure.
 * @base: crtc object.
 * @enabled: records whether crtc_enable succeeded
 * @planes: array of 4 drm_plane structures, one for each overlay plane
 * @pending_planes: whether any plane has pending changes to be applied
 * @mmsys_dev: pointer to the mmsys device for configuration registers
 * @mutex: handle to one of the ten disp_mutex streams
 * @ddp_comp_nr: number of components in ddp_comp
 * @ddp_comp: array of pointers the mtk_ddp_comp structures used by this crtc
 */
struct mtk_drm_crtc {
	struct drm_crtc 		base;
	bool				enabled;

	bool				pending_needs_vblank;
	struct drm_pending_vblank_event *event;

	struct drm_plane		*planes;
	unsigned int			layer_nr;
	bool				pending_planes;
	bool				pending_async_planes;

#if IS_REACHABLE(CONFIG_MTK_CMDQ)
	struct cmdq_client		*cmdq_client;
	u32 			cmdq_event;
#endif

	struct device			*mmsys_dev;
	struct mtk_mutex		*mutex;
	struct mtk_crtc_ddp_ctx ddp_ctx[DDP_MODE_NR];
	unsigned int			ddp_comp_nr;
	struct mtk_ddp_comp 	**ddp_comp;
	unsigned int ddp_mode;

	const struct mtk_crtc_path_data *path_data;
	struct mtk_crtc_ddp_ctx dual_pipe_ddp_ctx;
	bool is_dual_pipe;
	struct mtk_panel_ext *panel_ext;

	/* lock for display hardware access */
	struct mutex			hw_lock;
	bool				config_updating;
};

void mtk_drm_crtc_commit(struct drm_crtc *crtc);
int mtk_drm_crtc_create(struct drm_device *drm_dev,
			const enum mtk_ddp_comp_id *path,
			unsigned int path_len);
int mtk_drm_crtc_plane_check(struct drm_crtc *crtc, struct drm_plane *plane,
			     struct mtk_plane_state *state);
void mtk_drm_crtc_async_update(struct drm_crtc *crtc, struct drm_plane *plane,
			       struct drm_atomic_state *plane_state);

#endif /* MTK_DRM_CRTC_H */
