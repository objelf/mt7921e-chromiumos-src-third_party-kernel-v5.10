# SPDX-License-Identifier: GPL-2.0

ifeq (y, $(filter y, $(CONFIG_DRM_MEDIATEK_V2)))
obj-$(CONFIG_DRM_MEDIATEK) += mediatek_v2/
else
mediatek-drm-y := mtk_disp_ccorr.o \
		  mtk_disp_color.o \
		  mtk_disp_gamma.o \
		  mtk_disp_ovl.o \
		  mtk_disp_rdma.o \
		  mtk_disp_merge.o \
		  mtk_disp_dsc.o \
		  mtk_drm_crtc.o \
		  mtk_drm_ddp_comp.o \
		  mtk_drm_drv.o \
		  mtk_drm_gem.o \
		  mtk_drm_plane.o \
		  mtk_dsi.o \
		  mtk_dpi.o \
		  mtk_panel_ext.o \
		  mtk_disp_pseudo_ovl.o \
		  mtk_ethdr.o \
		  mtk_mdp_rdma.o

obj-$(CONFIG_DRM_MEDIATEK) += mediatek-drm.o

mediatek-drm-hdmi-objs := ./mediatek_v2/mtk_hdmi.o \
			 ./mediatek_v2/mtk_hdmi_ddc.o \
			 ./mediatek_v2/mtk_mt8195_hdmi_phy.o \
			 ./mediatek_v2/mtk_hdmi_hdr.o \
			 ./mediatek_v2/mtk_hdmi_debug.o \
			 ./mediatek_v2/mtk_hdmi_edid.o \

ifeq ($(CONFIG_DRM_MEDIATEK_HDMI_HDCP),y)
mediatek-drm-hdmi-objs += ./mediatek_v2/mtk_hdmi_hdcp.o
endif

obj-$(CONFIG_DRM_MEDIATEK_HDMI) += mediatek-drm-hdmi.o

ifeq ($(CONFIG_MTK_DPTX_SUPPORT), y)
obj-y += mtk_dp_intf.o \
		 mtk_dp.o \
		 mtk_dp_hal.o
endif

endif
