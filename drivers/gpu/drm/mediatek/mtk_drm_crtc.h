/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2015 MediaTek Inc.
 */

#ifndef MTK_DRM_CRTC_H
#define MTK_DRM_CRTC_H

#include <drm/drm_crtc.h>
#include "mtk_drm_ddp_comp.h"
#include "mtk_drm_plane.h"

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

void mtk_drm_crtc_commit(struct drm_crtc *crtc);
int mtk_drm_crtc_create(struct drm_device *drm_dev,
			const enum mtk_ddp_comp_id *path,
			unsigned int path_len);
int mtk_drm_crtc_plane_check(struct drm_crtc *crtc, struct drm_plane *plane,
			     struct mtk_plane_state *state);
void mtk_drm_crtc_async_update(struct drm_crtc *crtc, struct drm_plane *plane,
			       struct drm_atomic_state *plane_state);

#endif /* MTK_DRM_CRTC_H */
