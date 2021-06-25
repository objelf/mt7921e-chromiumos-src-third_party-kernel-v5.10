// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#include <linux/clk.h>
#include <linux/component.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/soc/mediatek/mtk-cmdq.h>

#include "mtk_drm_crtc.h"
#include "mtk_drm_ddp_comp.h"
#include "mtk_drm_drv.h"
#include "mtk_disp_drv.h"

#define DISP_REG_MERGE_CTRL (0x000)
	#define FLD_MERGE_EN REG_FLD_MSB_LSB(0, 0)
	#define FLD_MERGE_RST REG_FLD_MSB_LSB(4, 4)
	#define FLD_MERGE_LR_SWAP REG_FLD_MSB_LSB(8, 8)
	#define FLD_MERGE_DCM_DIS REG_FLD_MSB_LSB(12, 12)
#define DISP_REG_MERGE_WIDTH (0x004)
	#define FLD_IN_WIDHT_L REG_FLD_MSB_LSB(15, 0)
	#define FLD_IN_WIDHT_R REG_FLD_MSB_LSB(31, 16)
#define DISP_REG_MERGE_HEIGHT (0x008)
	#define FLD_IN_HEIGHT REG_FLD_MSB_LSB(15, 0)
#define DISP_REG_MERGE_SHADOW_CRTL (0x00c)
#define DISP_REG_MERGE_DGB0 (0x010)
	#define FLD_PIXEL_CNT REG_FLD_MSB_LSB(15, 0)
	#define FLD_MERGE_STATE REG_FLD_MSB_LSB(17, 16)
#define DISP_REG_MERGE_DGB1 (0x014)
	#define FLD_LINE_CNT REG_FLD_MSB_LSB(15, 0)
#define DISP_REG_MERGE_CFG2_0 (0x160)
#define DISP_REG_MERGE_CFG2_2 (0x168)

#define MT8195_DISP_MERGE_RESET		0x004
#define MT8195_DISP_MERGE_CFG_0		0x010
#define MT8195_DISP_MERGE_CFG_1		0x014
#define MT8195_DISP_MERGE_CFG_4		0x020
#define MT8195_DISP_MERGE_CFG_5		0x024
#define MT8195_DISP_MERGE_CFG_8		0x030
#define MT8195_DISP_MERGE_CFG_9		0x034
#define MT8195_DISP_MERGE_CFG_10	0x038
#define MT8195_DISP_MERGE_CFG_11	0x03c
#define MT8195_DISP_MERGE_CFG_12	0x040
	#define CFG_11_11_1PI_1PO_BYPASS	1
	#define CFG_11_11_2PI_2PO_BYPASS	2
	#define CFG_10_10_2PI_1PO_BYPASS	3
	#define CFG_10_10_2PI_2PO_BYPASS	4
	#define CFG_10_10_1PI_1PO_BUF_MODE	5
	#define CFG_10_10_1PI_2PO_BUF_MODE	6
	#define CFG_10_10_2PI_1PO_BUF_MODE	7
	#define CFG_10_10_2PI_2PO_BUF_MODE	8
	#define CFG_10_01_1PI_1PO_BUF_MODE	9
	#define CFG_10_01_2PI_1PO_BUF_MODE	10
	#define CFG_01_10_1PI_1PO_BUF_MODE	11
	#define CFG_01_10_1PI_2PO_BUF_MODE	12
	#define CFG_01_01_1PI_1PO_BUF_MODE	13
	#define CFG_10_11_1PI_1PO_SPLIT		14
	#define CFG_10_11_2PI_1PO_SPLIT		15
	#define CFG_01_11_1PI_1PO_SPLIT		16
	#define CFG_11_10_1PI_1PO_MERGE		17
	#define CFG_11_10_1PI_2PO_MERGE		18
	#define CFG_10_10_1PI_1PO_TO422		19
	#define CFG_10_10_1PI_2PO_TO444		20
	#define CFG_10_10_2PI_2PO_TO444		21
#define MT8195_DISP_MERGE_CFG_13	0x044
#define MT8195_DISP_MERGE_CFG_14	0x048
#define MT8195_DISP_MERGE_CFG_15	0x04c
#define MT8195_DISP_MERGE_CFG_17	0x054
#define MT8195_DISP_MERGE_CFG_18	0x058
#define MT8195_DISP_MERGE_CFG_19	0x05c
#define MT8195_DISP_MERGE_CFG_20	0x060
#define MT8195_DISP_MERGE_CFG_21	0x064
#define MT8195_DISP_MERGE_CFG_22	0x068
#define MT8195_DISP_MERGE_CFG_23	0x06c
#define MT8195_DISP_MERGE_CFG_24	0x070
#define MT8195_DISP_MERGE_CFG_25	0x074
#define MT8195_DISP_MERGE_CFG_26	0x078
#define MT8195_DISP_MERGE_CFG_27	0x07c
#define MT8195_DISP_MERGE_CFG_28	0x080
#define MT8195_DISP_MERGE_CFG_29	0x084
#define MT8195_DISP_MERGE_CFG_36	0x0a0
	#define MT8195_DISP_MERGE_CFG_36_FLD_ULTRA_EN	REG_FLD(1, 0)
	#define MT8195_DISP_MERGE_CFG_36_FLD_PREULTRA_EN REG_FLD(1, 4)
	#define MT8195_DISP_MERGE_CFG_36_FLD_HALT_FOR_DVFS_EN REG_FLD(1, 8)
	#define MT8195_DISP_MERGE_CFG_36_VAL_ULTRA_EN(val)         \
		REG_FLD_VAL(MT8195_DISP_MERGE_CFG_36_FLD_ULTRA_EN, (val))
	#define MT8195_DISP_MERGE_CFG_36_VAL_PREULTRA_EN(val)		  \
		REG_FLD_VAL(MT8195_DISP_MERGE_CFG_36_FLD_PREULTRA_EN, (val))
	#define MT8195_DISP_MERGE_CFG_36_VAL_HALT_FOR_DVFS_EN(val)		  \
		REG_FLD_VAL(MT8195_DISP_MERGE_CFG_36_FLD_HALT_FOR_DVFS_EN, (val))
#define MT8195_DISP_MERGE_CFG_37	0x0a4
	#define MT8195_DISP_MERGE_CFG_37_FLD_BUFFER_MODE REG_FLD(2, 0)
	#define MT8195_DISP_MERGE_CFG_37_VAL_BUFFER_MODE(val)		  \
		REG_FLD_VAL(MT8195_DISP_MERGE_CFG_37_FLD_BUFFER_MODE, (val))
#define MT8195_DISP_MERGE_CFG_38	0x0a8
	#define MT8195_DISP_MERGE_CFG_38_FLD_VDE_BLOCK_ULTRA REG_FLD(1, 0)
	#define MT8195_DISP_MERGE_CFG_38_FLD_VALID_TH_BLOCK_ULTRA REG_FLD(1, 4)
	#define MT8195_DISP_MERGE_CFG_38_FLD_ULTRA_FIFO_VALID_TH REG_FLD(16, 16)
	#define MT8195_DISP_MERGE_CFG_38_VAL_VDE_BLOCK_ULTRA(val)		  \
		REG_FLD_VAL(MT8195_DISP_MERGE_CFG_38_FLD_VDE_BLOCK_ULTRA, (val))
	#define MT8195_DISP_MERGE_CFG_38_VAL_VALID_TH_BLOCK_ULTRA(val)	\
		REG_FLD_VAL(MT8195_DISP_MERGE_CFG_38_FLD_VALID_TH_BLOCK_ULTRA, (val))
	#define MT8195_DISP_MERGE_CFG_38_VAL_ULTRA_FIFO_VALID_TH(val)	\
		REG_FLD_VAL(MT8195_DISP_MERGE_CFG_38_FLD_ULTRA_FIFO_VALID_TH, (val))
#define MT8195_DISP_MERGE_CFG_39	0x0ac
	#define MT8195_DISP_MERGE_CFG_39_FLD_NVDE_FORCE_PREULTRA REG_FLD(1, 8)
	#define MT8195_DISP_MERGE_CFG_39_FLD_NVALID_TH_FORCE_PREULTRA REG_FLD(1, 12)
	#define MT8195_DISP_MERGE_CFG_39_FLD_PREULTRA_FIFO_VALID_TH REG_FLD(16, 16)
	#define MT8195_DISP_MERGE_CFG_39_VAL_NVDE_FORCE_PREULTRA(val) \
		REG_FLD_VAL(MT8195_DISP_MERGE_CFG_39_FLD_NVDE_FORCE_PREULTRA, (val))
	#define MT8195_DISP_MERGE_CFG_39_VAL_NVALID_TH_FORCE_PREULTRA(val)\
	REG_FLD_VAL(MT8195_DISP_MERGE_CFG_39_FLD_NVALID_TH_FORCE_PREULTRA, (val))
	#define MT8195_DISP_MERGE_CFG_39_VAL_PREULTRA_FIFO_VALID_TH(val)\
		REG_FLD_VAL(MT8195_DISP_MERGE_CFG_39_FLD_PREULTRA_FIFO_VALID_TH, (val))
#define MT8195_DISP_MERGE_CFG_40	0x0b0
	#define MT8195_DISP_MERGE_CFG_40_FLD_ULTRA_TH_LOW REG_FLD(16, 0)
	#define MT8195_DISP_MERGE_CFG_40_FLD_ULTRA_TH_HIGH REG_FLD(16, 16)
	#define MT8195_DISP_MERGE_CFG_40_VAL_ULTRA_TH_LOW(val)	\
		REG_FLD_VAL(MT8195_DISP_MERGE_CFG_40_FLD_ULTRA_TH_LOW, (val))
	#define MT8195_DISP_MERGE_CFG_40_VAL_ULTRA_TH_HIGH(val) \
		REG_FLD_VAL(MT8195_DISP_MERGE_CFG_40_FLD_ULTRA_TH_HIGH, (val))
#define MT8195_DISP_MERGE_CFG_41	0x0b4
	#define MT8195_DISP_MERGE_CFG_41_FLD_PREULTRA_TH_LOW REG_FLD(16, 0)
	#define MT8195_DISP_MERGE_CFG_41_FLD_PREULTRA_TH_HIGH REG_FLD(16, 16)
	#define MT8195_DISP_MERGE_CFG_41_VAL_PREULTRA_TH_LOW(val) \
		REG_FLD_VAL(MT8195_DISP_MERGE_CFG_41_FLD_PREULTRA_TH_LOW, (val))
	#define MT8195_DISP_MERGE_CFG_41_VAL_PREULTRA_TH_HIGH(val) \
		REG_FLD_VAL(MT8195_DISP_MERGE_CFG_41_FLD_PREULTRA_TH_HIGH, (val))

#define DISP_REG_MERGE_CFG2_0 (0x160)
#define DISP_REG_MERGE_CFG2_2 (0x168)

struct mtk_disp_merge_data {
	bool need_golden_setting;
	enum mtk_ddp_comp_id gs_comp_id;
};

struct mtk_merge_config_struct {
	unsigned short width_right;
	unsigned short width_left;
	unsigned int height;
	unsigned int fmt;
	unsigned int mode;
	unsigned int swap;
};

struct mtk_disp_merge {
	struct mtk_ddp_comp ddp_comp;
	struct drm_crtc *crtc;
	struct clk *clk;
	struct clk *async_clk;
	void __iomem *regs;
	struct cmdq_client_reg		cmdq_reg;
	int irq;
	void (*vblank_cb)(void *data);
	void *vblank_cb_data;
	const struct mtk_disp_merge_data *data;
};

static struct mtk_ddp_comp *_Merge5Comp;

static inline struct mtk_disp_merge *comp_to_merge(struct mtk_ddp_comp *comp)
{
	return container_of(comp, struct mtk_disp_merge, ddp_comp);
}

void mtk_merge_start(struct device *dev)
{
	struct mtk_disp_merge *priv = dev_get_drvdata(dev);

	pr_debug("%s\n", __func__);

	mtk_ddp_write_mask(NULL, 0x1, &priv->cmdq_reg, priv->regs, DISP_REG_MERGE_CTRL, ~0);
}

void mtk_merge_stop(struct device *dev)
{
	struct mtk_disp_merge *priv = dev_get_drvdata(dev);

	pr_debug("%s\n", __func__);

	mtk_ddp_write_mask(NULL, 0x0, &priv->cmdq_reg, priv->regs, DISP_REG_MERGE_CTRL, ~0);
}

static int mtk_merge_check_params(struct mtk_merge_config_struct *merge_config)
{
	if (!merge_config->height || !merge_config->width_left
		|| !merge_config->width_right) {
		pr_err("%s:merge input width l(%u) w(%u) h(%u)\n",
			  __func__, merge_config->width_left,
			  merge_config->width_right, merge_config->height);
		return -EINVAL;
	}
	pr_info("%s:merge input width l(%u) r(%u) height(%u)\n",
			  __func__, merge_config->width_left,
			  merge_config->width_right, merge_config->height);
	return 0;
}

static int mtk_merge_golden_setting(struct mtk_disp_merge *priv,
				  struct cmdq_pkt *handle)
{
	int ultra_en = 1;
	int preultra_en = 1;
	int halt_for_dvfs_en = 0;
	int buffer_mode = 3;
	int vde_block_ultra = 0;
	int valid_th_block_ultra = 0;
	int ultra_fifo_valid_th = 0;
	int nvde_force_preultra = 0;
	int nvalid_th_force_preultra = 0;
	int preultra_fifo_valid_th = 0;
	int ultra_th_low = 0xe10;
	int ultra_th_high = 0x12c0;
	int preultra_th_low = 0x12c0;
	int preultra_th_high = 0x1518;

	pr_debug("%s\n", __func__);

	mtk_ddp_write_mask(NULL,
		MT8195_DISP_MERGE_CFG_36_VAL_ULTRA_EN(ultra_en)|
		MT8195_DISP_MERGE_CFG_36_VAL_PREULTRA_EN(preultra_en)|
		MT8195_DISP_MERGE_CFG_36_VAL_HALT_FOR_DVFS_EN(halt_for_dvfs_en),
		&priv->cmdq_reg, priv->regs, MT8195_DISP_MERGE_CFG_36,
		REG_FLD_MASK(MT8195_DISP_MERGE_CFG_36_FLD_ULTRA_EN)|
		REG_FLD_MASK(MT8195_DISP_MERGE_CFG_36_FLD_PREULTRA_EN)|
		REG_FLD_MASK(MT8195_DISP_MERGE_CFG_36_FLD_HALT_FOR_DVFS_EN));

	mtk_ddp_write_mask(NULL,
		MT8195_DISP_MERGE_CFG_37_VAL_BUFFER_MODE(buffer_mode),
		&priv->cmdq_reg, priv->regs, MT8195_DISP_MERGE_CFG_37,
		REG_FLD_MASK(MT8195_DISP_MERGE_CFG_37_FLD_BUFFER_MODE));

	mtk_ddp_write_mask(NULL,
		MT8195_DISP_MERGE_CFG_38_VAL_VDE_BLOCK_ULTRA(vde_block_ultra)|
		MT8195_DISP_MERGE_CFG_38_VAL_VALID_TH_BLOCK_ULTRA(valid_th_block_ultra)|
		MT8195_DISP_MERGE_CFG_38_VAL_ULTRA_FIFO_VALID_TH(ultra_fifo_valid_th),
		&priv->cmdq_reg, priv->regs, MT8195_DISP_MERGE_CFG_38,
		REG_FLD_MASK(MT8195_DISP_MERGE_CFG_38_FLD_VDE_BLOCK_ULTRA)|
		REG_FLD_MASK(MT8195_DISP_MERGE_CFG_38_FLD_VALID_TH_BLOCK_ULTRA)|
		REG_FLD_MASK(MT8195_DISP_MERGE_CFG_38_FLD_ULTRA_FIFO_VALID_TH));

	mtk_ddp_write_mask(NULL,
		MT8195_DISP_MERGE_CFG_39_VAL_NVDE_FORCE_PREULTRA(nvde_force_preultra)|
		MT8195_DISP_MERGE_CFG_39_VAL_NVALID_TH_FORCE_PREULTRA
			(nvalid_th_force_preultra)|
		MT8195_DISP_MERGE_CFG_39_VAL_PREULTRA_FIFO_VALID_TH
			(preultra_fifo_valid_th),
		&priv->cmdq_reg, priv->regs, MT8195_DISP_MERGE_CFG_39,
		REG_FLD_MASK(MT8195_DISP_MERGE_CFG_39_FLD_NVDE_FORCE_PREULTRA)|
		REG_FLD_MASK(MT8195_DISP_MERGE_CFG_39_FLD_NVALID_TH_FORCE_PREULTRA)|
		REG_FLD_MASK(MT8195_DISP_MERGE_CFG_39_FLD_PREULTRA_FIFO_VALID_TH));

	mtk_ddp_write_mask(NULL,
		MT8195_DISP_MERGE_CFG_40_VAL_ULTRA_TH_LOW(ultra_th_low)|
		MT8195_DISP_MERGE_CFG_40_VAL_ULTRA_TH_HIGH(ultra_th_high),
		&priv->cmdq_reg, priv->regs, MT8195_DISP_MERGE_CFG_40,
		REG_FLD_MASK(MT8195_DISP_MERGE_CFG_40_FLD_ULTRA_TH_LOW)|
		REG_FLD_MASK(MT8195_DISP_MERGE_CFG_40_FLD_ULTRA_TH_HIGH));

	mtk_ddp_write_mask(NULL,
		MT8195_DISP_MERGE_CFG_41_VAL_PREULTRA_TH_LOW(preultra_th_low)|
		MT8195_DISP_MERGE_CFG_41_VAL_PREULTRA_TH_HIGH(preultra_th_high),
		&priv->cmdq_reg, priv->regs, MT8195_DISP_MERGE_CFG_41,
		REG_FLD_MASK(MT8195_DISP_MERGE_CFG_41_FLD_PREULTRA_TH_LOW)|
		REG_FLD_MASK(MT8195_DISP_MERGE_CFG_41_FLD_PREULTRA_TH_HIGH));

	return 0;
}

void mtk_merge_config(struct device *dev, unsigned int w,
				  unsigned int h, unsigned int vrefresh,
				  unsigned int bpc, struct cmdq_pkt *handle)
{
	struct mtk_merge_config_struct merge_config;
	struct mtk_disp_merge *priv = dev_get_drvdata(dev);
	struct mtk_ddp_comp *comp = &priv->ddp_comp;

	/*golden setting*/
	if (priv->data != NULL) {
		if (priv->data->need_golden_setting &&
			priv->data->gs_comp_id == comp->id)
			mtk_merge_golden_setting(priv, handle);
	}

	switch (comp->id) {
	case DDP_COMPONENT_MERGE0:
		merge_config.mode = CFG_10_10_1PI_2PO_BUF_MODE;
		merge_config.width_left = w;
		merge_config.width_right = w;
		merge_config.height = h;
		merge_config.swap = 0;
		break;
	case DDP_COMPONENT_MERGE5:
		merge_config.mode = CFG_10_10_2PI_2PO_BUF_MODE;
		merge_config.width_left = w;
		merge_config.width_right = w;
		merge_config.height = h;
		merge_config.swap = 0;
		break;
	default:
		pr_err("No find component merge %d\n", comp->id);
		return;
	}

	mtk_merge_check_params(&merge_config);

	switch (merge_config.mode) {
	case CFG_10_10_1PI_1PO_BUF_MODE:
	case CFG_10_10_1PI_2PO_BUF_MODE:
	case CFG_10_10_2PI_2PO_BUF_MODE:
		mtk_ddp_write_mask(handle,
			(merge_config.height << 16 | merge_config.width_left),
			&priv->cmdq_reg, priv->regs,
			MT8195_DISP_MERGE_CFG_0, ~0);

		mtk_ddp_write_mask(handle,
			(merge_config.height << 16 | merge_config.width_left),
			&priv->cmdq_reg, priv->regs,
			MT8195_DISP_MERGE_CFG_4, ~0);

		mtk_ddp_write_mask(handle,
			(merge_config.height << 16 | merge_config.width_left),
			&priv->cmdq_reg, priv->regs,
			MT8195_DISP_MERGE_CFG_24, ~0);

		mtk_ddp_write_mask(handle,
			(merge_config.height << 16 | merge_config.width_left),
			&priv->cmdq_reg, priv->regs,
			MT8195_DISP_MERGE_CFG_25, ~0);

		mtk_ddp_write_mask(handle,
			merge_config.swap,
			&priv->cmdq_reg, priv->regs,
			MT8195_DISP_MERGE_CFG_10, 0x1f);
	break;
	case CFG_11_10_1PI_2PO_MERGE:
		mtk_ddp_write_mask(handle,
			(merge_config.height << 16 | merge_config.width_left),
			&priv->cmdq_reg, priv->regs,
			MT8195_DISP_MERGE_CFG_0, ~0);

		mtk_ddp_write_mask(handle,
			(merge_config.height << 16 | merge_config.width_right),
			&priv->cmdq_reg, priv->regs,
			MT8195_DISP_MERGE_CFG_1, ~0);

		mtk_ddp_write_mask(handle,
			(merge_config.height << 16 | w),
			&priv->cmdq_reg, priv->regs,
			MT8195_DISP_MERGE_CFG_4, ~0);

		mtk_ddp_write_mask(handle,
			(merge_config.height << 16 | merge_config.width_left),
			&priv->cmdq_reg, priv->regs,
			MT8195_DISP_MERGE_CFG_24, ~0);

		mtk_ddp_write_mask(handle,
			(merge_config.height << 16 | merge_config.width_right),
			&priv->cmdq_reg, priv->regs,
			MT8195_DISP_MERGE_CFG_25, ~0);

		mtk_ddp_write_mask(handle,
			(merge_config.height << 16 | merge_config.width_left),
			&priv->cmdq_reg, priv->regs,
			MT8195_DISP_MERGE_CFG_26, ~0);

		mtk_ddp_write_mask(handle,
			(merge_config.height << 16 | merge_config.width_right),
			&priv->cmdq_reg, priv->regs,
			MT8195_DISP_MERGE_CFG_27, ~0);

		mtk_ddp_write_mask(handle,
			merge_config.swap,
			&priv->cmdq_reg, priv->regs,
			MT8195_DISP_MERGE_CFG_10, 0x1f);
	break;
	default:
	break;
	}
	mtk_ddp_write_mask(handle, merge_config.mode,
		&priv->cmdq_reg, priv->regs, MT8195_DISP_MERGE_CFG_12, 0x1f);
	mtk_ddp_write_mask(handle, 0x1,
		&priv->cmdq_reg, priv->regs, DISP_REG_MERGE_CTRL, 0x1);
}

int mtk_merge_clk_enable(struct device *dev)
{
	int ret = 0;
	struct mtk_disp_merge *priv = dev_get_drvdata(dev);

	pr_debug("%s\n", __func__);

	ret = pm_runtime_get_sync(dev);

	ret = pm_runtime_get_sync(dev);

	if (priv->clk != NULL) {
		ret = clk_prepare_enable(priv->clk);
		if (ret)
			pr_err("merge clk prepare enable failed\n");
	}

	if (priv->async_clk != NULL) {
		ret = clk_prepare_enable(priv->async_clk);
		if (ret)
			pr_err("async clk prepare enable failed\n");
	}

	return ret;
}

void mtk_merge_clk_disable(struct device *dev)
{
	struct mtk_disp_merge *priv = dev_get_drvdata(dev);

	pr_debug("%s\n", __func__);

	if (priv->async_clk != NULL)
		clk_disable_unprepare(priv->async_clk);

	if (priv->async_clk != NULL)
		clk_disable_unprepare(priv->async_clk);

	if (priv->clk != NULL)
		clk_disable_unprepare(priv->clk);

	pm_runtime_put_sync(dev);
}

void mtk_merge_enable_vblank(struct device *dev,
			    void (*vblank_cb)(void *),
			    void *vblank_cb_data)
{
	struct mtk_disp_merge *priv = dev_get_drvdata(dev);

	pr_debug("s\n", __func__);

	priv->vblank_cb = vblank_cb;
	priv->vblank_cb_data = vblank_cb_data;

	writel(0x10000, priv->regs+ DISP_REG_MERGE_CFG2_0);
}

void mtk_merge_disable_vblank(struct device *dev)
{
	struct mtk_disp_merge *priv = dev_get_drvdata(dev);

	pr_debug("%s\n", __func__);

	priv->vblank_cb = NULL;
	priv->vblank_cb_data = NULL;

	writel(0x0, priv->regs+ DISP_REG_MERGE_CFG2_0);
}

static int mtk_disp_merge_bind(struct device *dev, struct device *master,
			       void *data)
{
	pr_debug("%s\n", __func__);

	return 0;
}

static void mtk_disp_merge_unbind(struct device *dev, struct device *master,
				  void *data)
{
	pr_debug("%s\n", __func__);
}

static irqreturn_t mtk_disp_merge_irq_handler(int irq, void *dev_id)
{
	struct mtk_disp_merge *priv = dev_id;

	/* Clear frame completion interrupt */
	writel(0x1, priv->regs + DISP_REG_MERGE_CFG2_2);
	writel(0x0, priv->regs + DISP_REG_MERGE_CFG2_2);

	if (!priv->vblank_cb)
		return IRQ_NONE;

	priv->vblank_cb(priv->vblank_cb_data);

	return IRQ_HANDLED;
}

static const struct component_ops mtk_disp_merge_component_ops = {
	.bind	= mtk_disp_merge_bind,
	.unbind = mtk_disp_merge_unbind,
};

static int mtk_disp_merge_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *res;
	struct mtk_disp_merge *priv;
	enum mtk_ddp_comp_id comp_id;
	int ret;

	pr_debug("%s+\n", __func__);

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	comp_id = mtk_ddp_comp_get_id(dev->of_node, MTK_DISP_MERGE);
	if ((int)comp_id < 0) {
		dev_err(dev, "Failed to identify by alias: %d\n", comp_id);
		return comp_id;
	}

	priv->ddp_comp.id = comp_id;

	if (comp_id == DDP_COMPONENT_MERGE5)
		_Merge5Comp = &priv->ddp_comp;

	priv->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_err(dev, "failed to get merge clk\n");
		return PTR_ERR(priv->clk);
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(priv->regs)) {
		dev_err(dev, "failed to ioremap merge\n");
		return PTR_ERR(priv->regs);
	}

#if IS_REACHABLE(CONFIG_MTK_CMDQ)
	ret = cmdq_dev_get_client_reg(dev, &priv->cmdq_reg, 0);
	if (ret)
		dev_dbg(dev, "get mediatek,gce-client-reg fail!\n");
#endif

	priv->irq = platform_get_irq(pdev, 0);
	if (priv->irq < 0)
		priv->irq = 0;

	priv->async_clk = of_clk_get(dev->of_node, 1);
	if (IS_ERR(priv->async_clk)) {
		ret = PTR_ERR(priv->async_clk);
		dev_dbg(dev, "No merge async clock: %d\n", ret);
		priv->async_clk = NULL;
	}

	priv->data = of_device_get_match_data(dev);

	if (priv->irq) {
		ret = devm_request_irq(dev, priv->irq, mtk_disp_merge_irq_handler,
				       IRQF_TRIGGER_NONE, dev_name(dev), priv);
		if (ret < 0) {
			dev_err(dev, "Failed to request irq %d: %d\n", priv->irq, ret);
			return ret;
		}
	}

	platform_set_drvdata(pdev, priv);

	pm_runtime_enable(dev);

	ret = component_add(dev, &mtk_disp_merge_component_ops);
	if (ret != 0) {
		dev_err(dev, "Failed to add component: %d\n", ret);
		pm_runtime_disable(dev);
	}

	pr_debug("%s- \n", __func__);

	return ret;
}

static int mtk_disp_merge_remove(struct platform_device *pdev)
{
	component_del(&pdev->dev, &mtk_disp_merge_component_ops);

	pm_runtime_disable(&pdev->dev);
	return 0;
}

static const struct mtk_disp_merge_data mt8195_merge_driver_data = {
	.need_golden_setting = true,
	.gs_comp_id = DDP_COMPONENT_MERGE5,
};

static const struct of_device_id mtk_disp_merge_driver_dt_match[] = {
	{
		.compatible = "mediatek,mt8195-disp-merge",
		.data = &mt8195_merge_driver_data
	},
	{},
};

MODULE_DEVICE_TABLE(of, mtk_disp_merge_driver_dt_match);

struct platform_driver mtk_disp_merge_driver = {
	.probe = mtk_disp_merge_probe,
	.remove = mtk_disp_merge_remove,
	.driver = {
		.name = "mediatek-disp-merge",
		.owner = THIS_MODULE,
		.of_match_table = mtk_disp_merge_driver_dt_match,
	},
};

