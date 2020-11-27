/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020 MediaTek Inc.
 * Author: Yong Wu <yong.wu@mediatek.com>
 */
#ifndef __DT_BINDINGS_MEMORY_MTK_MEMORY_PORT_H_
#define __DT_BINDINGS_MEMORY_MTK_MEMORY_PORT_H_

#define MTK_LARB_NR_MAX			32

#define MTK_IOMMU_INFRA_AO		0
#define MTK_IOMMU_INFRA_PERI		1

#define MTK_IOMMU_ID(type, larb, port)	(((type) & 0x3) << 12 |\
					 ((larb) << 5) | (port))
#define MTK_IOMMU_INFRA_ID(index, port)	\
			MTK_IOMMU_ID(MTK_IOMMU_INFRA_AO, index, port)
#define MTK_IOMMU_PERI_ID(index, port)	\
			MTK_IOMMU_ID(MTK_IOMMU_INFRA_PERI, index, port)

#define MTK_M4U_ID(larb, port)		MTK_IOMMU_ID(0, larb, port)
#define MTK_M4U_TO_LARB(id)		(((id) >> 5) & 0x1f)
#define MTK_M4U_TO_PORT(id)		((id) & 0x1f)
#define MTK_IOMMU_TO_TYPE(id)		(((id) >> 12) & 0x3)

#endif
