// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/of_gpio.h>
#include <linux/of_irq.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/cec.h>
#include <uapi/linux/cec.h>
#include <media/cec.h>
#include <linux/module.h>

#include "mtk_cec.h"

enum mtk_cec_clk_id {
	MTK_CEC_66M_H,
	MTK_CEC_66M_B,
	MTK_HDMI_32K,
	MTK_CEC_CLK_COUNT,
};

static const char * const mtk_cec_clk_names[MTK_CEC_CLK_COUNT] = {
	[MTK_CEC_66M_H] = "cec_66m_h",
	[MTK_CEC_66M_B] = "cec_66m_b",
	[MTK_HDMI_32K] = "hdmi_32k",
};

/***** New CEC IP HW START*******/
#define CEC2_TR_CONFIG		0x00
#define CEC2_RX_CHK_DST			BIT(29)
#define CEC2_BYPASS				BIT(28)
#define CEC2_DEVICE_ADDR3			(0xf << 16)
#define CEC2_LA3_SHIFT			16
#define CEC2_DEVICE_ADDR2			(0xf << 20)
#define CEC2_LA2_SHIFT			20
#define CEC2_DEVICE_ADDR1			(0xf << 24)
#define CEC2_LA1_SHIFT			24
#define CEC2_RX_ACK_ERROR_BYPASS		BIT(13)
#define CEC2_RX_ACK_ERROR_HANDLE		BIT(12)
#define CEC2_RX_DATA_ACK			BIT(11)
#define CEC2_RX_HEADER_ACK			BIT(10)
#define CEC2_RX_HEADER_TRIG_INT_EN		BIT(9)
#define CEC2_RX_ACK_SEL			BIT(8)
#define CEC2_TX_RESET_WRITE				BIT(1)
#define CEC2_TX_RESET_READ				BIT(0)
#define CEC2_RX_RESET_WRITE				BIT(0)
#define CEC2_RX_RESET_READ				BIT(1)

#define CEC2_CKGEN		0x04
#define CEC2_CLK_TX_EN			BIT(20)
#define CEC2_CLK_32K_EN			BIT(19)
#define CEC2_CLK_27M_EN			BIT(18)
#define CEC2_CLK_SEL_DIV			BIT(17)
#define CEC2_CLK_PDN				BIT(16)
#define CEC2_CLK_DIV				(0xffff << 0)
#define CEC2_DIV_SEL_100K		0x82

#define CEC2_RX_TIMER_START_R	0x08
#define CEC2_RX_TIMER_START_R_MAX		(0x7ff << 16)
#define CEC2_RX_TIMER_START_R_MIN		(0x7ff)

#define CEC2_RX_TIMER_START_F	0x0c
#define CEC2_RX_TIMER_START_F_MAX		(0x7ff << 16)
#define CEC2_RX_TIMER_START_F_MIN		(0x7ff)

#define CEC2_RX_TIMER_DATA		0x10
#define CEC2_RX_TIMER_DATA_F_MAX		(0x7ff << 16)
#define CEC2_RX_TIMER_DATA_F_MIN		(0x7ff)

#define CEC2_RX_TIMER_ACK		0x14
#define CEC2_RX_TIMER_DATA_SAMPLE		(0x7ff << 16)
#define CEC2_RX_TIMER_ACK_R			0x7ff

#define CEC2_RX_TIMER_ERROR		0x18
#define CEC2_RX_TIMER_ERROR_D		(0x7ff << 16)

#define CEC2_TX_TIMER_START		0x1c
#define CEC2_TX_TIMER_START_F		(0x7ff << 16)
#define CEC2_TX_TIMER_START_F_SHIFT	16
#define CEC2_TX_TIMER_START_R		(0x7ff)

#define CEC2_TX_TIMER_DATA_R		0x20
#define CEC2_TX_TIMER_BIT1_R			(0x7ff << 16)
#define CEC2_TX_TIMER_BIT1_R_SHIFT	16
#define CEC2_TX_TIMER_BIT0_R			(0x7ff)

#define CEC2_TX_T_DATA_F			0x24
#define CEC2_TX_TIMER_DATA_BIT		(0x7ff << 16)
#define CEC2_TX_TIMER_DATA_BIT_SHIFT	16
#define CEC2_TX_TIMER_DATA_F			(0x7ff)

#define TX_TIMER_DATA_S		0x28
#define CEC2_TX_COMP_CNT					(0x7ff << 16)
#define CEC2_TX_TIMER_DATA_SAMPLE		(0x7ff)

#define CEC2_TX_ARB		0x2c
#define CEC2_TX_MAX_RETRANSMIT_NUM_ARB		GENMASK(29, 24)
#define CEC2_TX_MAX_RETRANSMIT_NUM_COL		GENMASK(23, 20)
#define CEC2_TX_MAX_RETRANSMIT_NUM_NAK		GENMASK(19, 16)
#define CEC2_TX_BCNT_RETRANSMIT				GENMASK(11, 8)
#define CEC2_TX_BCNT_NEW_MSG				GENMASK(7, 4)
#define CEC2_TX_BCNT_NEW_INIT				GENMASK(3, 0)

#define CEC2_TX_HEADER		0x30
#define CEC2_TX_READY			BIT(16)
#define CEC2_TX_SEND_CNT			(0xf << 12)
#define CEC2_TX_HEADER_EOM			BIT(8)
#define CEC2_TX_HEADER_DST			(0x0f)
#define CEC2_TX_HEADER_SRC			(0xf0)

#define CEC2_TX_DATA0		0x34
#define CEC2_TX_DATA_B3			(0xff << 24)
#define CEC2_TX_DATA_B2			(0xff << 16)
#define CEC2_TX_DATA_B1			(0xff << 8)
#define CEC2_TX_DATA_B0			(0xff)

#define CEC2_TX_DATA1		0x38
#define CEC2_TX_DATA_B7			(0xff << 24)
#define CEC2_TX_DATA_B6			(0xff << 16)
#define CEC2_TX_DATA_B5			(0xff << 8)
#define CEC2_TX_DATA_B4			(0xff)

#define CEC2_TX_DATA2		0x3c
#define CEC2_TX_DATA_B11			(0xff << 24)
#define CEC2_TX_DATA_B10			(0xff << 16)
#define CEC2_TX_DATA_B9			(0xff << 8)
#define CEC2_TX_DATA_B8			(0xff)

#define CEC2_TX_DATA3		0x40
#define CEC2_TX_DATA_B15			(0xff << 24)
#define CEC2_TX_DATA_B14			(0xff << 16)
#define CEC2_TX_DATA_B13			(0xff << 8)
#define CEC2_TX_DATA_B12			(0xff)

#define CEC2_TX_TIMER_ACK		0x44
#define CEC2_TX_TIMER_ACK_MAX		GENMASK(26, 16)
#define CEC2_TX_TIMER_ACK_MIN		GENMASK(15, 0)

#define CEC2_LINE_DET		0x48
#define CEC2_TIMER_LOW_LONG			GENMASK(31, 16)
#define CEC2_TIMER_HIGH_LONG		GENMASK(15, 0)

#define CEC2_RX_BUF_HEADER		0x4c
#define CEC2_RX_BUF_RISC_ACK			BIT(16)
#define CEC2_RX_BUF_CNT			(0xf << 12)
#define CEC2_RX_HEADER_EOM			BIT(8)
#define	CEC2_RX_HEADER_SRC			(0xf << 4)
#define CEC2_RX_HEADER_DST			(0xf)

#define CEC2_RX_DATA0		0x50
#define CEC2_RX_DATA_B3			(0xff << 24)
#define CEC2_RX_DATA_B2			(0xff << 16)
#define CEC2_RX_DATA_B1			(0xff << 8)
#define CEC2_RX_DATA_B0			(0xff)

#define CEC2_RX_DATA1		0x54
#define CEC2_RX_DATA_B7			(0xff << 24)
#define CEC2_RX_DATA_B6			(0xff << 16)
#define CEC2_RX_DATA_B5			(0xff << 8)
#define CEC2_RX_DATA_B4			(0xff)

#define CEC2_RX_DATA2		0x58
#define CEC2_RX_DATA_B11			(0xff << 24)
#define CEC2_RX_DATA_B10			(0xff << 16)
#define CEC2_RX_DATA_B9			(0xff << 8)
#define CEC2_RX_DATA_B8			(0xff)

#define CEC2_RX_DATA3		0x5c
#define CEC2_RX_DATA_B15			(0xff << 24)
#define CEC2_RX_DATA_B14			(0xff << 16)
#define CEC2_RX_DATA_B13			(0xff << 8)
#define CEC2_RX_DATA_B12			(0xff)

#define CEC2_RX_STATUS		0x60
#define CEC2_CEC_INPUT			BIT(31)
#define CEC2_RX_FSM				GENMASK(22, 16)
#define CEC2_RX_BIT_COUNTER		(0xf << 12)
#define CEC2_RX_TIMER			(0x7ff)

#define CEC2_TX_STATUS		0x64
#define CEC2_TX_NUM_RETRANSMIT	GENMASK(31, 26)
#define CEC2_TX_FSM				GENMASK(24, 16)
#define CEC2_TX_BIT_COUNTER		(0xf << 12)
#define CEC2_TX_TIMER			(0x7ff)

#define CEC2_BACK		0x70
#define CEC2_RGS_BACK			GENMASK(31, 21)
#define CEC2_TX_LAST_ERR_STA		GENMASK(24, 16)
#define CEC2_REG_BACK			GENMASK(15, 0)

#define CEC2_INT_CLR			0x74
#define CEC2_INT_CLR_ALL			GENMASK(23, 0)
#define CEC2_RX_INT_ALL_CLR			GENMASK(23, 16)
#define CEC2_TX_INT_FAIL_CLR		(GENMASK(14, 9) | BIT(7))
#define CEC2_TX_INT_ALL_CLR (CEC2_TX_INT_FAIL_CLR | BIT(15) | BIT(8))
#define CEC2_RX_FSM_CHG_INT_CLR		BIT(23)
#define CEC2_RX_ACK_FAIL_INT_CLR		BIT(22)
#define CEC2_RX_ERROR_HANDLING_INT_CLR	BIT(21)
#define CEC2_RX_BUF_FULL_INT_CLR		BIT(20)
#define CEC2_RX_STAGE_READY_INT_CLR		BIT(19)
#define CEC2_RX_DATA_RCVD_INT_CLR		BIT(18)
#define CEC2_RX_HEADER_RCVD_INT_CLR		BIT(17)
#define CEC2_TX_FSM_CHG_INT_CLR		BIT(15)
#define CEC2_TX_FAIL_DATA_ACK_INT_CLR	BIT(14)
#define CEC2_TX_FAIL_HEADER_ACK_INT_CLR	BIT(13)
#define CEC2_TX_FAIL_RETRANSMIT_INT_CLR	BIT(12)
#define CEC2_TX_FAIL_DATA_INT_CLR		BIT(11)
#define CEC2_TX_FAIL_HEADER_INT_CLR		BIT(10)
#define CEC2_TX_FAIL_SRC_INT_CLR		BIT(9)
#define CEC2_TX_DATA_FINISH_INT_CLR		BIT(8)
#define CEC2_LINE_lOW_LONG_INT_CLR		BIT(7)
#define CEC2_LINE_HIGH_LONG_INT_CLR		BIT(6)
#define CEC2_PORD_FAIL_INT_CLR		BIT(5)
#define CEC2_PORD_RISE_INT_CLR		BIT(4)
#define CEC2_HTPLG_FAIL_INT_CLR		BIT(3)
#define CEC2_HTPLG_RISE_INT_CLR		BIT(2)
#define CEC2_FAIL_INT_CLR		BIT(1)
#define CEC2_RISE_INT_CLR		BIT(0)

#define CEC2_INT_EN			0x78
#define CEC2_INT_ALL_EN		GENMASK(23, 0)
#define CEC2_RX_INT_ALL_EN		GENMASK(23, 16)
#define CEC2_TX_INT_FAIL_EN		(GENMASK(14, 9) | BIT(7))
#define CEC2_TX_INT_ALL_EN		(CEC2_TX_INT_FAIL_EN | BIT(15) | BIT(8))
#define CEC2_RX_FSM_CHG_INT_EN		BIT(23)
#define CEC2_RX_ACK_FAIL_INT_EN		BIT(22)
#define CEC2_RX_ERROR_HANDLING_INT_EN	BIT(21)
#define CEC2_RX_BUF_FULL_INT_EN		BIT(20)
#define CEC2_RX_STAGE_READY_INT_EN		BIT(19)
#define CEC2_RX_DATA_RCVD_INT_EN		BIT(18)
#define CEC2_RX_HEADER_RCVD_INT_EN		BIT(17)
#define CEC2_RX_BUF_READY_INT_EN		BIT(16)
#define CEC2_TX_FSM_CHG_INT_EN		BIT(15)
#define CEC2_TX_FAIL_DATA_ACK_INT_EN		BIT(14)
#define CEC2_TX_FAIL_HEADER_ACK_INT_EN	BIT(13)
#define CEC2_TX_FAIL_RETRANSMIT_INT_EN	BIT(12)
#define CEC2_TX_FAIL_DATA_INT_EN		BIT(11)
#define CEC2_TX_FAIL_HEADER_INT_EN		BIT(10)
#define CEC2_TX_FAIL_SRC_INT_EN		BIT(9)
#define CEC2_TX_DATA_FINISH_INT_EN		BIT(8)
#define CEC2_LINE_lOW_LONG_INT_EN		BIT(7)
#define CEC2_LINE_HIGH_LONG_INT_EN		BIT(6)
#define CEC2_PORD_FAIL_INT_EN		BIT(5)
#define CEC2_PORD_RISE_INT_EN		BIT(4)
#define CEC2_HTPLG_FAIL_INT_EN		BIT(3)
#define CEC2_HTPLG_RISE_INT_EN		BIT(2)
#define CEC2_FALL_INT_EN			BIT(1)
#define CEC2_RISE_INT_EN			BIT(0)

#define CEC2_INT_STA			0x7c
#define CEC2_TRX_INT_STA		GENMASK(23, 0)
#define CEC2_TX_INT_FAIL		(GENMASK(14, 9) | BIT(7))
#define CEC2_RX_FSM_CHG_INT_STA		BIT(23)
#define CEC2_RX_ACK_FAIL_INT_STA		BIT(22)
#define CEC2_RX_ERROR_HANDLING_INT_STA	BIT(21)
#define CEC2_RX_BUF_FULL_INT_STA		BIT(20)
#define CEC2_RX_STAGE_READY_INT_STA		BIT(19)
#define CEC2_RX_DATA_RCVD_INT_STA		BIT(18)
#define CEC2_RX_HEADER_RCVD_INT_STA		BIT(17)
#define CEC2_RX_BUF_READY_INT_STA		BIT(16)
#define CEC2_TX_FSM_CHG_INT_STA		BIT(15)
#define CEC2_TX_FAIL_DATA_ACK_INT_STA	BIT(14)
#define CEC2_TX_FAIL_HEADER_ACK_INT_STA	BIT(13)
#define CEC2_TX_FAIL_RETRANSMIT_INT_STA	BIT(12)
#define CEC2_TX_FAIL_DATA_INT_STA		BIT(11)
#define CEC2_TX_FAIL_HEADER_INT_STA		BIT(10)
#define CEC2_TX_FAIL_SRC_INT_STA		BIT(9)
#define CEC2_TX_DATA_FINISH_INT_STA		BIT(8)
#define CEC2_LINE_lOW_LONG_INT_STA		BIT(7)
#define CEC2_LINE_HIGH_LONG_INT_STA		BIT(6)
#define CEC2_PORD_FAIL_INT_STA		BIT(5)
#define CEC2_PORD_RISE_INT_STA		BIT(4)
#define CEC2_HTPLG_FAIL_INT_STA		BIT(3)
#define CEC2_HTPLG_RISE_INT_STA		BIT(2)
#define CEC2_FAlL_INT_STA		BIT(1)
#define CEC2_RISE_INT_STA		BIT(0)
/***** register map end*******/

#define CEC_HEADER_BLOCK_SIZE 1

#define cec_clk_27m 0x1
#define cec_clk_32k 0x2
unsigned char cec_clock = cec_clk_27m;

enum cec_tx_status {
	CEC_TX_START,
	CEC_TX_Transmitting,
	CEC_TX_COMPLETE,
	CEC_TX_FAIL,
	CEC_TX_FAIL_DNAK,
	CEC_TX_FAIL_HNAK,
	CEC_TX_FAIL_RETR,
	CEC_TX_FAIL_DATA,
	CEC_TX_FAIL_HEAD,
	CEC_TX_FAIL_SRC,
	CEC_TX_FAIL_LOW,
	CEC_TX_STATUS_NUM,
};

enum cec_rx_status {
	CEC_RX_START,
	CEC_RX_Receiving,
	CEC_RX_COMPLETE,
	CEC_RX_FAIL,
	CEC_RX_STATUS_NUM,
};

struct cec_frame {
	struct cec_msg *msg;
	unsigned char retry_count;
	union {
		enum cec_tx_status tx_status;
		enum cec_rx_status rx_status;
	} status;
};

struct mtk_cec {
	void __iomem *regs;
	struct clk *clk[MTK_CEC_CLK_COUNT];
	int irq;
	struct device *hdmi_dev;
	spinlock_t lock;
	struct cec_adapter *adap;
	struct cec_frame transmitting;
	struct cec_frame received;
	struct cec_msg rx_msg;	/* dynamic alloc or fixed memory?? */
	bool cec_enabled;
};

enum cec_inner_clock {
	CLK_27M_SRC = 0,
};

enum cec_la_num {
	DEVICE_ADDR_1 = 1,
	DEVICE_ADDR_2 = 2,
	DEVICE_ADDR_3 = 3,
};

struct cec_msg_header_block {
	unsigned char destination:4;
	unsigned char initiator:4;
};

//can.zeng todo verify
struct mtk_cec *mtk_global_cec;
unsigned char mtk_cec_log = 1;

#define CEC_LOG(fmt, arg...) pr_info("[CEC]"fmt, ##arg)

#define MTK_CEC_LOG(fmt, arg...) \
	do {	if (unlikely(mtk_cec_log)) {	\
		CEC_LOG(fmt, ##arg);	\
		}	\
	} while (0)

static inline void mtk_cec_clear_bits(struct mtk_cec *cec,
	unsigned int offset, unsigned int bits)
{
	void __iomem *reg = cec->regs + offset;
	u32 tmp;

	tmp = readl(reg);
	tmp &= ~bits;
	writel(tmp, reg);
}

static inline void mtk_cec_set_bits(struct mtk_cec *cec,
	unsigned int offset, unsigned int bits)
{
	void __iomem *reg = cec->regs + offset;
	u32 tmp;

	tmp = readl(reg);
	tmp |= bits;
	writel(tmp, reg);
}

static inline void mtk_cec_mask(struct mtk_cec *cec, unsigned int offset,
			 unsigned int val, unsigned int mask)
{
	u32 tmp = readl(cec->regs + offset) & ~mask;

	tmp |= (val & mask);
	writel(tmp, cec->regs + offset);
}

static inline void mtk_cec_write(struct mtk_cec *cec,
	unsigned int offset, unsigned int val)
{
	writel(val, cec->regs + offset);
}


static inline unsigned int mtk_cec_read(struct mtk_cec *cec,
	unsigned int offset)
{
	return readl(cec->regs + offset);
}

static inline bool mtk_cec_read_bit(struct mtk_cec *cec,
	unsigned int offset, unsigned int bits)
{
	return (readl(cec->regs + offset) & bits) ? true : false;

}

inline void mtk_cec_rx_reset(struct mtk_cec *cec)
{
	unsigned int val;

	val = mtk_cec_read(cec, CEC2_TR_CONFIG);
	mtk_cec_write(cec, CEC2_TR_CONFIG, val & (~CEC2_RX_RESET_WRITE));
	udelay(1);
	val = mtk_cec_read(cec, CEC2_TR_CONFIG);
	mtk_cec_write(cec, CEC2_TR_CONFIG, val |
		(CEC2_TX_RESET_WRITE | CEC2_RX_RESET_WRITE));
}

inline void mtk_cec_tx_reset(struct mtk_cec *cec)
{
	unsigned int val;

	val = mtk_cec_read(cec, CEC2_TR_CONFIG);
	mtk_cec_write(cec, CEC2_TR_CONFIG, val & (~CEC2_TX_RESET_WRITE));
	udelay(1);
	val = mtk_cec_read(cec, CEC2_TR_CONFIG);
	mtk_cec_write(cec, CEC2_TR_CONFIG, val |
		(CEC2_TX_RESET_WRITE | CEC2_RX_RESET_WRITE));
}

static void mtk_cec_clk_div(struct mtk_cec *cec, unsigned int clk_src)
{
	if (clk_src == cec_clk_27m) {
		/* divide the clock of 26Mhz to 100khz */
		mtk_cec_mask(cec, CEC2_CKGEN, CEC2_CLK_27M_EN, CEC2_CLK_27M_EN);
		mtk_cec_mask(cec, CEC2_CKGEN, CEC2_DIV_SEL_100K, CEC2_CLK_DIV);
		mtk_cec_mask(cec, CEC2_CKGEN, 0x0, CEC2_CLK_SEL_DIV);
	} else {
		/* do not divide the clock of 32khz */
		mtk_cec_mask(cec, CEC2_CKGEN, 0x0, CEC2_CLK_27M_EN);
		mtk_cec_mask(cec, CEC2_CKGEN, CEC2_CLK_32K_EN, CEC2_CLK_32K_EN);
		mtk_cec_mask(cec, CEC2_CKGEN,
			CEC2_CLK_SEL_DIV, CEC2_CLK_SEL_DIV);
		return;
	}
}

static void mtk_cec_rx_check_addr(struct mtk_cec *cec, bool enable)
{
	if (enable) {
		mtk_cec_mask(cec, CEC2_TR_CONFIG,
			CEC2_RX_CHK_DST, CEC2_RX_CHK_DST);
		mtk_cec_mask(cec, CEC2_TR_CONFIG, 0, CEC2_BYPASS);
	} else {
		mtk_cec_mask(cec, CEC2_TR_CONFIG, 0, CEC2_RX_CHK_DST);
		mtk_cec_mask(cec, CEC2_TR_CONFIG, CEC2_BYPASS, CEC2_BYPASS);
	}
}

static void mtk_cec_rx_timing_config(struct mtk_cec *cec, unsigned int clk_src)
{
	if (clk_src == cec_clk_27m) {
		/* use default timing value */
		mtk_cec_write(cec, CEC2_RX_TIMER_START_R, 0x0186015e);
		mtk_cec_write(cec, CEC2_RX_TIMER_START_F, 0x01d601ae);
		mtk_cec_write(cec, CEC2_RX_TIMER_DATA, 0x011300cd);
		mtk_cec_write(cec, CEC2_RX_TIMER_ACK, 0x00690082);
		mtk_cec_write(cec, CEC2_RX_TIMER_ERROR, 0x01680000);
		mtk_cec_mask(cec, CEC2_CKGEN, CEC2_CLK_27M_EN, CEC2_CLK_27M_EN);
	} else {
		mtk_cec_write(cec, CEC2_RX_TIMER_START_R, 0x007d0070);
		mtk_cec_write(cec, CEC2_RX_TIMER_START_F, 0x0099008a);
		mtk_cec_write(cec, CEC2_RX_TIMER_DATA, 0x00230040);
		mtk_cec_write(cec, CEC2_RX_TIMER_ACK, 0x00000030);
		mtk_cec_write(cec, CEC2_RX_TIMER_ERROR, 0x007300aa);
		return;
	}

	mtk_cec_mask(cec, CEC2_TR_CONFIG, CEC2_RX_ACK_ERROR_BYPASS,
		CEC2_RX_ACK_ERROR_BYPASS);
	mtk_cec_rx_reset(cec);
}

static void mtk_cec_tx_timing_config(struct mtk_cec *cec, unsigned int clk_src)
{
	if (clk_src == cec_clk_27m) {
		/* use default timing value */
		mtk_cec_write(cec, CEC2_TX_TIMER_START, 0x01c20172);
		mtk_cec_write(cec, CEC2_TX_TIMER_DATA_R, 0x003c0096);
		mtk_cec_write(cec, CEC2_TX_T_DATA_F, 0x00f000f0);
		mtk_cec_write(cec, TX_TIMER_DATA_S, 0x00040069);
		mtk_cec_write(cec, CEC2_LINE_DET, 0x1f4004b0);

		mtk_cec_mask(cec, CEC2_TX_TIMER_ACK, 0x00aa0028,
			CEC2_TX_TIMER_ACK_MAX | CEC2_TX_TIMER_ACK_MIN);
		mtk_cec_mask(cec, CEC2_CKGEN, CEC2_CLK_27M_EN, CEC2_CLK_27M_EN);
	} else {
		mtk_cec_write(cec, CEC2_TX_TIMER_START, 0x00920079);
		mtk_cec_write(cec, CEC2_TX_TIMER_DATA_R, 0x00140031);
		mtk_cec_write(cec, CEC2_TX_T_DATA_F, 0x004f004f);
		mtk_cec_write(cec, TX_TIMER_DATA_S, 0x00010022);
		mtk_cec_write(cec, CEC2_LINE_DET, 0x0a000180);

		mtk_cec_mask(cec, CEC2_TX_TIMER_ACK, 0x0036000d,
			CEC2_TX_TIMER_ACK_MAX | CEC2_TX_TIMER_ACK_MIN);
		return;
	}
	mtk_cec_mask(cec, CEC2_TX_ARB, 0x01 << 24,
		CEC2_TX_MAX_RETRANSMIT_NUM_ARB);
	mtk_cec_mask(cec, CEC2_TX_ARB, 0x01 << 20,
		CEC2_TX_MAX_RETRANSMIT_NUM_COL);
	mtk_cec_mask(cec, CEC2_TX_ARB, 0x01 << 16,
		CEC2_TX_MAX_RETRANSMIT_NUM_NAK);
	mtk_cec_mask(cec, CEC2_TX_ARB, 0x03 << 8,
		CEC2_TX_BCNT_RETRANSMIT);
	mtk_cec_mask(cec, CEC2_TX_ARB, 0x07 << 4,
		CEC2_TX_BCNT_NEW_MSG);
	mtk_cec_mask(cec, CEC2_TX_ARB, 0x05,
		CEC2_TX_BCNT_NEW_INIT);

	mtk_cec_mask(cec, CEC2_CKGEN, CEC2_CLK_TX_EN, CEC2_CLK_TX_EN);
	mtk_cec_tx_reset(cec);
}

inline void mtk_cec_txrx_interrupts_config(struct mtk_cec *cec)
{
	/*disable all irq config*/
	mtk_cec_write(cec, CEC2_INT_CLR, CEC2_INT_CLR_ALL);
	mtk_cec_write(cec, CEC2_INT_CLR, 0);

	/*enable all rx irq config*/
	mtk_cec_mask(cec, CEC2_INT_EN, CEC2_RX_INT_ALL_EN, CEC2_RX_INT_ALL_EN);

	/*enable some of tx irq config*/
	mtk_cec_mask(cec, CEC2_INT_EN, CEC2_TX_FAIL_RETRANSMIT_INT_EN,
		CEC2_TX_FAIL_RETRANSMIT_INT_EN);
	mtk_cec_mask(cec, CEC2_INT_EN, CEC2_TX_DATA_FINISH_INT_EN,
		CEC2_TX_DATA_FINISH_INT_EN);
}

static void mtk_cec_set_logic_addr(struct mtk_cec *cec,
	u8 la, enum cec_la_num num)
{
	if (num == DEVICE_ADDR_1)
		mtk_cec_mask(cec, CEC2_TR_CONFIG, la << CEC2_LA1_SHIFT,
			CEC2_DEVICE_ADDR1);
	else if (num == DEVICE_ADDR_2)
		mtk_cec_mask(cec, CEC2_TR_CONFIG, la << CEC2_LA2_SHIFT,
			CEC2_DEVICE_ADDR2);
	else if (num == DEVICE_ADDR_3)
		mtk_cec_mask(cec, CEC2_TR_CONFIG, la << CEC2_LA3_SHIFT,
			CEC2_DEVICE_ADDR3);
	else
		MTK_CEC_LOG("Cannot support more than 3 logic address");
}

static void mtk_cec_logic_addr_config(struct cec_adapter *adap)
{
	__u8 unreg_la_addr = 0xff;
	struct mtk_cec *cec = adap->priv;
	struct cec_log_addrs *cec_la = &(adap->log_addrs);

	memset((void *)cec_la, 0, sizeof(struct cec_log_addrs));
	cec_la->num_log_addrs = 0;
	cec_la->log_addr[0] = CEC_LOG_ADDR_UNREGISTERED;
	cec_la->cec_version = CEC_OP_CEC_VERSION_1_4;
	cec_la->log_addr_type[0] = CEC_LOG_ADDR_TYPE_UNREGISTERED;
	cec_la->primary_device_type[0] = CEC_LOG_ADDR_TYPE_UNREGISTERED;
	cec_la->log_addr_mask = 0;

	mtk_cec_set_logic_addr(cec, unreg_la_addr, DEVICE_ADDR_1);
	mtk_cec_set_logic_addr(cec, unreg_la_addr, DEVICE_ADDR_2);
	mtk_cec_set_logic_addr(cec, unreg_la_addr, DEVICE_ADDR_3);
}

inline void mtk_cec_int_disable_all(struct mtk_cec *cec)
{
	mtk_cec_mask(cec, CEC2_INT_EN, 0x0, CEC2_INT_ALL_EN);
}

static void mtk_cec_init(struct mtk_cec *cec)
{
	mtk_cec_clk_div(cec, cec_clock);
	mtk_cec_rx_check_addr(cec, true);
	mtk_cec_rx_timing_config(cec, cec_clock);
	mtk_cec_tx_timing_config(cec, cec_clock);
	mtk_cec_txrx_interrupts_config(cec);
	mtk_cec_logic_addr_config(cec->adap);

}

static void mtk_cec_deinit(struct mtk_cec *cec)
{
	mtk_cec_int_disable_all(cec);
}

static int mtk_cec_power_enable(struct mtk_cec *cec, bool enable)
{
	int ret;
	int i;

	if (enable) {
		for (i = 0; i < ARRAY_SIZE(mtk_cec_clk_names); i++) {
			ret = clk_prepare_enable(cec->clk[i]);
			if (ret) {
				MTK_CEC_LOG("Failed to enable cec clk: %d\n", ret);
				return ret;
			}
		}
		mtk_cec_mask(cec, CEC2_CKGEN, 0, CEC2_CLK_PDN);
		cec->cec_enabled = true;
	} else {
		mtk_cec_mask(cec, CEC2_CKGEN, CEC2_CLK_PDN, CEC2_CLK_PDN);
		for (i = 0; i < ARRAY_SIZE(mtk_cec_clk_names); i++)
			clk_disable_unprepare(cec->clk[i]);
		mtk_cec_deinit(cec);
		cec->cec_enabled = false;
	}
	return 0;

}

int mtk_cec_get_phy_addr(struct device *dev, u8 *edid, int len)
{
	struct mtk_cec *cec = dev_get_drvdata(dev);
	int offset = 0;
	u16 phy_addr = CEC_PHYS_ADDR_INVALID;
	//int err;

	phy_addr = cec_get_edid_phys_addr(edid, len, &offset);
	if (offset == 0) {
		MTK_CEC_LOG("no cec phys addr found\n");
		return -EINVAL;
	};

//can.zeng todo verify
/*	err = cec_phys_addr_invalidate(phy_addr, NULL, NULL);
 *	if (err) {
 *		MTK_CEC_LOG("cec phys addr invalid\n");
 *		return err;
 *	}
 */
//can.zeng todo verify

	MTK_CEC_LOG("phy_addr is 0x%x\n", phy_addr);
	cec_s_phys_addr(cec->adap, phy_addr, true);

	return 0;
}

static unsigned int mtk_cec_get_logic_addr(struct mtk_cec *cec,
	enum cec_la_num num)
{
	if (num == DEVICE_ADDR_1)
		return ((mtk_cec_read(cec, CEC2_TR_CONFIG) &
			CEC2_DEVICE_ADDR1) >> CEC2_LA1_SHIFT);
	else if (num == DEVICE_ADDR_2)
		return ((mtk_cec_read(cec, CEC2_TR_CONFIG) &
			CEC2_DEVICE_ADDR2) >> CEC2_LA2_SHIFT);
	else if (num == DEVICE_ADDR_3)
		return ((mtk_cec_read(cec, CEC2_TR_CONFIG) &
			CEC2_DEVICE_ADDR3) >> CEC2_LA3_SHIFT);
	else
		MTK_CEC_LOG("Cannot support more than 3 logic address");
	return CEC_LOG_ADDR_UNREGISTERED;
}

static int mtk_cec_adap_enable(struct cec_adapter *adap, bool enable)
{
	struct mtk_cec *cec = adap->priv;

	if (enable) {
		if (cec->cec_enabled)
			MTK_CEC_LOG("cec has already been enabled, return\n");
		else {
			mtk_cec_power_enable(cec, true);
			mtk_cec_init(cec);
		}
	} else {
		if (!cec->cec_enabled)
			MTK_CEC_LOG("cec has already been disabled, return\n");
		else {
			mtk_cec_power_enable(cec, false);
			mtk_cec_deinit(cec);
		}
	}
	return 0;
}

static int mtk_cec_adap_log_addr(struct cec_adapter *adap, u8 logical_addr)
{
	struct mtk_cec *cec = adap->priv;

	/* to do :mtk cec can accept 3 logic address, now just accept one */
	if (logical_addr != CEC_LOG_ADDR_INVALID)
		mtk_cec_set_logic_addr(cec, logical_addr, DEVICE_ADDR_1);

	return 0;
}

static void mtk_cec_set_msg_header(struct mtk_cec *cec, struct cec_msg *msg)
{
	struct cec_msg_header_block header = { };

	header.destination = (msg->msg[0]) & 0x0f;
	header.initiator = ((msg->msg[0]) & 0xf0) >> 4;
	mtk_cec_mask(cec, CEC2_TX_HEADER,
		header.initiator << 4, CEC2_TX_HEADER_SRC);
	mtk_cec_mask(cec, CEC2_TX_HEADER,
		header.destination, CEC2_TX_HEADER_DST);
}

static void mtk_cec_print_cec_frame(struct cec_frame *frame)
{
	struct cec_msg *msg = frame->msg;
	unsigned char header = msg->msg[0];

	MTK_CEC_LOG("cec message initiator is %d\n", (header & 0xf0) >> 4);
	MTK_CEC_LOG("cec message follower is %d\n", header & 0x0f);
	MTK_CEC_LOG("cec message length is %d\n", msg->len);
	MTK_CEC_LOG("cec message opcode is 0x%x\n", msg->msg[1]);
}

inline bool mtk_cec_tx_fsm_fail(struct mtk_cec *cec)
{
	return ((mtk_cec_read(cec, CEC2_TX_STATUS) &
		CEC2_TX_FSM) >> 16) == 0x10 ? true : false;
}


inline bool mtk_cec_tx_fail_int(struct mtk_cec *cec)
{
	return (mtk_cec_read(cec, CEC2_INT_STA) &
		CEC2_TX_INT_FAIL) ? true : false;
}


inline bool mtk_cec_input(struct mtk_cec *cec)
{
	return mtk_cec_read_bit(cec, CEC2_RX_STATUS, CEC2_CEC_INPUT);
}

inline bool mtk_cec_tx_fsm_idle(struct mtk_cec *cec)
{
	return ((mtk_cec_read(cec, CEC2_TX_STATUS) &
		CEC2_TX_FSM) >> 16) == 0x01 ? true : false;
}

inline void mtk_cec_tx_int_disable_all(struct mtk_cec *cec)
{
	mtk_cec_mask(cec, CEC2_INT_EN, 0x0, CEC2_TX_INT_ALL_EN);
}

inline void mtk_cec_tx_int_clear_all(struct mtk_cec *cec)
{
	mtk_cec_mask(cec, CEC2_INT_CLR,
		CEC2_TX_INT_ALL_CLR, CEC2_TX_INT_ALL_CLR);
	mtk_cec_mask(cec, CEC2_INT_CLR, 0, CEC2_TX_INT_ALL_CLR);
}

inline void mtk_cec_tx_finish_int_en(struct mtk_cec *cec, unsigned int en)
{
	mtk_cec_mask(cec, CEC2_INT_EN, en << 8, CEC2_TX_DATA_FINISH_INT_EN);
}

inline void mtk_cec_tx_fail_longlow_int_en(struct mtk_cec *cec, unsigned int en)
{
	mtk_cec_mask(cec, CEC2_INT_EN, en << 7, CEC2_LINE_lOW_LONG_INT_EN);
}

inline void mtk_cec_tx_fail_retransmit_int_en(struct mtk_cec *cec,
	unsigned int en)
{
	mtk_cec_mask(cec, CEC2_INT_EN, en << 12,
		CEC2_TX_FAIL_RETRANSMIT_INT_EN);
}

inline void mtk_cec_tx_trigger_send(struct mtk_cec *cec)
{
	mtk_cec_mask(cec, CEC2_TX_HEADER, CEC2_TX_READY, CEC2_TX_READY);
}

inline void mtk_cec_tx_set_h_eom(struct mtk_cec *cec, unsigned int heom)
{
	mtk_cec_mask(cec, CEC2_TX_HEADER, heom << 8, CEC2_TX_HEADER_EOM);
}

inline void mtk_cec_tx_set_data_len(struct mtk_cec *cec, unsigned int len)
{
	mtk_cec_mask(cec, CEC2_TX_HEADER, len << 12, CEC2_TX_SEND_CNT);
}

static void mtk_cec_tx_set_data(struct mtk_cec *cec,
	struct cec_msg *cec_msg, unsigned char data_len)
{
	unsigned char i, addr, byte_shift;

	for (i = 0; i < data_len; i++) {
		addr = (i / 4) * 4 + CEC2_TX_DATA0;
		byte_shift = (i % 4 * 8);
		mtk_cec_mask(cec, addr, cec_msg->msg[i+1] << byte_shift,
			0xff << byte_shift);
	}
}

inline bool mtk_cec_rx_header_arrived(struct mtk_cec *cec)
{
	bool is_rx_fsm_irq;
	bool is_rx_fsm_header;

	is_rx_fsm_irq = mtk_cec_read_bit(cec, CEC2_INT_STA,
		CEC2_RX_FSM_CHG_INT_STA);
	is_rx_fsm_header = ((mtk_cec_read(cec,
		CEC2_RX_STATUS) & CEC2_RX_FSM) >> 16) == 0x08;
	if (is_rx_fsm_irq && is_rx_fsm_header)
		return true;
	else
		return false;
}

inline unsigned int mtk_cec_rx_get_dst(struct mtk_cec *cec)
{
	return mtk_cec_read(cec, CEC2_RX_BUF_HEADER) & CEC2_RX_HEADER_DST;
}

inline unsigned int mtk_cec_rx_get_src(struct mtk_cec *cec)
{
	return (mtk_cec_read(cec, CEC2_RX_BUF_HEADER) &
		CEC2_RX_HEADER_SRC) >> 4;
}

inline bool mtk_cec_rx_data_arrived(struct mtk_cec *cec)
{
	return mtk_cec_read_bit(cec, CEC2_INT_STA, CEC2_RX_DATA_RCVD_INT_STA);
}

inline bool mtk_cec_rx_buffer_ready(struct mtk_cec *cec)
{
	return mtk_cec_read_bit(cec, CEC2_INT_STA, CEC2_RX_BUF_READY_INT_STA);
}

inline bool mtk_cec_rx_buffer_full(struct mtk_cec *cec)
{
	return mtk_cec_read_bit(cec, CEC2_INT_STA, CEC2_RX_BUF_FULL_INT_STA);
}

static int mtk_cec_send_msg(struct mtk_cec *cec)
{
	unsigned char msg_data_size;
	unsigned char size;
	struct cec_frame *frame = &cec->transmitting;

	size = frame->msg->len;
	msg_data_size = size - CEC_HEADER_BLOCK_SIZE;

	mtk_cec_print_cec_frame(frame);

	if (frame->status.tx_status == CEC_TX_START) {

/*		if (mtk_cec_tx_fsm_fail(cec) || mtk_cec_tx_fail_int(cec)) {
 *			mtk_cec_tx_reset(cec);
 *			if (mtk_cec_input(cec))
 *				mtk_cec_tx_fail_longlow_intclr(cec);
 *			return 2;
 *		}
 */
		if (!mtk_cec_tx_fsm_idle(cec))
			mtk_cec_tx_reset(cec);

		/* fill header block*/
		mtk_cec_set_msg_header(cec, frame->msg);
		MTK_CEC_LOG("tx header=0x%02x,seq=%d,op=0x%x,l=%d\n",
			frame->msg->msg[0], frame->msg->sequence,
			frame->msg->msg[1], frame->msg->len);

		/*disable and clear interrupts*/
		mtk_cec_tx_int_disable_all(cec);
		mtk_cec_tx_int_clear_all(cec);

		/* fill data block*/
		if (msg_data_size == 0)
			mtk_cec_tx_set_h_eom(cec, 1);
		else {
			mtk_cec_tx_set_h_eom(cec, 0);
			mtk_cec_tx_set_data_len(cec, msg_data_size);
			mtk_cec_tx_set_data(cec, frame->msg, msg_data_size);
		}
		cec->transmitting.status.tx_status = CEC_TX_Transmitting;
	}

	mtk_cec_tx_finish_int_en(cec, 1);
	mtk_cec_tx_fail_longlow_int_en(cec, 1);
	mtk_cec_tx_fail_retransmit_int_en(cec, 1);
	mtk_cec_tx_trigger_send(cec);

	return 0;
}

static void mtk_cec_msg_init(struct mtk_cec *cec, struct cec_msg *msg)
{
	cec->transmitting.msg = msg;
	cec->transmitting.retry_count = 0;
	cec->transmitting.status.tx_status = CEC_TX_START;
}

static int mtk_cec_adap_transmit(struct cec_adapter *adap, u8 attempts,
				 u32 signal_free_time, struct cec_msg *msg)
{
	struct mtk_cec *cec = adap->priv;

	mtk_cec_msg_init(cec, msg);
	mtk_cec_send_msg(cec);

	return 0;
}

static int mtk_cec_received(struct cec_adapter *adap, struct cec_msg *msg)
{
	return -ENOMSG;
}

inline unsigned int mtk_cec_rx_data_len(struct mtk_cec *cec)
{
	unsigned int val;

	val = mtk_cec_read(cec, CEC2_RX_BUF_HEADER);
	if (val & CEC2_RX_HEADER_EOM)
		return 0;
	if (val & CEC2_RX_BUF_CNT)
		return ((val & CEC2_RX_BUF_CNT) >> 12);
	else
		return 0;
}

inline void mtk_cec_rx_notify_data_taken(struct mtk_cec *cec)
{
	mtk_cec_mask(cec, CEC2_RX_BUF_HEADER,
		CEC2_RX_BUF_RISC_ACK, CEC2_RX_BUF_RISC_ACK);
}

static void mtk_cec_receiving_msg(struct mtk_cec *cec)
{
	unsigned char rxlen;
	unsigned char i, addr, byte_shift;

	struct cec_msg_header_block header = { };
	struct cec_frame *received_msg = &cec->received;

	received_msg->status.rx_status = CEC_RX_Receiving;
	if (!mtk_cec_rx_buffer_ready(cec)) {
		received_msg->status.rx_status = CEC_RX_FAIL;
		MTK_CEC_LOG("%s,cec buffer not ready\n", __func__);
		return;
	}

	/* <polling message> only */
	if (mtk_cec_rx_data_len(cec) == 0) {
		mtk_cec_rx_notify_data_taken(cec);
		received_msg->status.rx_status = CEC_RX_COMPLETE;
		return;
	}

	header.initiator = mtk_cec_rx_get_src(cec);
	header.destination = mtk_cec_rx_get_dst(cec);
	received_msg->msg->msg[0] =
		(header.initiator << 4 | header.destination);

	rxlen = mtk_cec_rx_data_len(cec);
	received_msg->msg->len = rxlen + CEC_HEADER_BLOCK_SIZE;

	for (i = 0; i < rxlen; i++) {
		addr = (i / 4) * 4 + CEC2_RX_DATA0;
		byte_shift = (i % 4 * 8);
		received_msg->msg->msg[i+1] =
		(mtk_cec_read(cec, addr) & 0xff << byte_shift) >> byte_shift;
	}
	received_msg->status.rx_status = CEC_RX_COMPLETE;
	mtk_cec_rx_notify_data_taken(cec);
	cec_received_msg(cec->adap, received_msg->msg);

}

inline void mtk_cec_tx_detect_startbit(struct mtk_cec *cec)
{
	static unsigned int tx_fsm, pre_tx_fsm;

	tx_fsm = ((mtk_cec_read(cec, CEC2_TX_STATUS)) & CEC2_TX_FSM) >> 16;
	if ((tx_fsm == 0x08) && (pre_tx_fsm == 0x20)) {
		mtk_cec_tx_reset(cec);
		MTK_CEC_LOG("\nDetect Start bit Err\n\n");
	}
	pre_tx_fsm = tx_fsm;
}

inline bool mtk_cec_tx_fail_longlow(struct mtk_cec *cec)
{
	return mtk_cec_read_bit(cec, CEC2_INT_STA, CEC2_LINE_lOW_LONG_INT_STA);
}

inline void mtk_cec_tx_fail_longlow_intclr(struct mtk_cec *cec)
{
	mtk_cec_mask(cec, CEC2_INT_CLR,
		CEC2_LINE_lOW_LONG_INT_CLR, CEC2_LINE_lOW_LONG_INT_CLR);
	mtk_cec_mask(cec, CEC2_INT_CLR, 0, CEC2_LINE_lOW_LONG_INT_CLR);
}

inline bool mtk_cec_tx_fail_retransmit_intsta(struct mtk_cec *cec)
{
	return mtk_cec_read_bit(cec, CEC2_INT_STA,
		CEC2_TX_FAIL_RETRANSMIT_INT_STA);
}

inline void mtk_cec_tx_fail_retransmit_intclr(struct mtk_cec *cec)
{
	mtk_cec_mask(cec, CEC2_INT_CLR,
		CEC2_TX_FAIL_RETRANSMIT_INT_CLR,
		CEC2_TX_FAIL_RETRANSMIT_INT_CLR);
	mtk_cec_mask(cec, CEC2_INT_CLR, 0, CEC2_TX_FAIL_RETRANSMIT_INT_CLR);
}

inline bool mtk_cec_tx_fail_h_ack(struct mtk_cec *cec)
{
	return mtk_cec_read_bit(cec, CEC2_INT_STA,
		CEC2_TX_FAIL_HEADER_ACK_INT_STA);
}

inline void mtk_cec_tx_fail_h_ack_intclr(struct mtk_cec *cec)
{
	mtk_cec_mask(cec, CEC2_INT_CLR,
		CEC2_TX_FAIL_HEADER_ACK_INT_CLR,
		CEC2_TX_FAIL_HEADER_ACK_INT_CLR);
	mtk_cec_mask(cec, CEC2_INT_CLR, 0, CEC2_TX_FAIL_HEADER_ACK_INT_CLR);
}

inline bool mtk_cec_tx_fail_data_ack(struct mtk_cec *cec)
{
	return mtk_cec_read_bit(cec, CEC2_INT_STA,
		CEC2_TX_FAIL_DATA_ACK_INT_STA);
}

inline void mtk_cec_tx_fail_data_ack_intclr(struct mtk_cec *cec)
{
	mtk_cec_mask(cec, CEC2_INT_CLR,
		CEC2_TX_FAIL_DATA_ACK_INT_CLR, CEC2_TX_FAIL_DATA_ACK_INT_CLR);
	mtk_cec_mask(cec, CEC2_INT_CLR, 0, CEC2_TX_FAIL_DATA_ACK_INT_CLR);
}

inline bool mtk_cec_tx_fail_data(struct mtk_cec *cec)
{
	return mtk_cec_read_bit(cec, CEC2_INT_STA, CEC2_TX_FAIL_DATA_INT_STA);
}

inline void mtk_cec_tx_fail_data_intclr(struct mtk_cec *cec)
{
	mtk_cec_mask(cec, CEC2_INT_CLR,
		CEC2_TX_FAIL_DATA_INT_CLR, CEC2_TX_FAIL_DATA_INT_CLR);
	mtk_cec_mask(cec, CEC2_INT_CLR, 0, CEC2_TX_FAIL_DATA_INT_CLR);
}

inline bool mtk_cec_tx_fail_header(struct mtk_cec *cec)
{
	return mtk_cec_read_bit(cec, CEC2_INT_STA, CEC2_TX_FAIL_HEADER_INT_STA);
}

inline void mtk_cec_tx_fail_header_intclr(struct mtk_cec *cec)
{
	mtk_cec_mask(cec, CEC2_INT_CLR,
		CEC2_TX_FAIL_HEADER_INT_CLR, CEC2_TX_FAIL_HEADER_INT_CLR);
	mtk_cec_mask(cec, CEC2_INT_CLR, 0, CEC2_TX_FAIL_HEADER_INT_CLR);
}

inline bool mtk_cec_tx_fail_src(struct mtk_cec *cec)
{
	return mtk_cec_read_bit(cec, CEC2_INT_STA, CEC2_TX_FAIL_SRC_INT_STA);
}

inline void mtk_cec_tx_fail_src_intclr(struct mtk_cec *cec)
{
	mtk_cec_mask(cec, CEC2_INT_CLR,
		CEC2_TX_FAIL_SRC_INT_CLR, CEC2_TX_FAIL_SRC_INT_CLR);
	mtk_cec_mask(cec, CEC2_INT_CLR, 0, CEC2_TX_FAIL_SRC_INT_CLR);
}

inline bool mtk_cec_tx_finish(struct mtk_cec *cec)
{
	return mtk_cec_read_bit(cec, CEC2_INT_STA, CEC2_TX_DATA_FINISH_INT_STA);
}

inline void mtk_cec_tx_finish_clr(struct mtk_cec *cec)
{
	mtk_cec_mask(cec, CEC2_INT_CLR,
		CEC2_TX_DATA_FINISH_INT_CLR, CEC2_TX_DATA_FINISH_INT_CLR);
	mtk_cec_mask(cec, CEC2_INT_CLR,
		0, CEC2_TX_DATA_FINISH_INT_CLR);
}

static void mtk_cec_rx_event_handler(struct mtk_cec *cec)
{
	unsigned char ReceivedDst;

	struct cec_frame *received_msg = &cec->received;

	received_msg->msg = &cec->rx_msg;

	MTK_CEC_LOG("cecisr=0x%08x,0x%08x,0x%08x\n",
		mtk_cec_read(cec, CEC2_INT_STA),
		mtk_cec_read(cec, CEC2_INT_EN),
		(mtk_cec_read(cec, CEC2_INT_STA) &
		mtk_cec_read(cec, CEC2_INT_EN)));

	if (mtk_cec_rx_header_arrived(cec)) {
		received_msg->status.rx_status = CEC_RX_START;
		ReceivedDst = mtk_cec_rx_get_dst(cec);
		MTK_CEC_LOG("header_addr=0x%08x\n",
		(mtk_cec_rx_get_src(cec) << 4) + ReceivedDst);
		if ((ReceivedDst == mtk_cec_get_logic_addr(cec, DEVICE_ADDR_1))
		    || (ReceivedDst ==
		    mtk_cec_get_logic_addr(cec, DEVICE_ADDR_2))
		    || (ReceivedDst ==
		    mtk_cec_get_logic_addr(cec, DEVICE_ADDR_3))
		    || (ReceivedDst == 0xf)) {
			MTK_CEC_LOG("RX:H\n");
		} else {
			MTK_CEC_LOG("[mtk_cec]RX:H False\n");
		}
	}
	if (mtk_cec_rx_data_arrived(cec) || mtk_cec_rx_buffer_ready(cec)) {
		MTK_CEC_LOG("RX:D\n");
		mtk_cec_receiving_msg(cec);
	}
	if (mtk_cec_rx_buffer_full(cec)) {
		received_msg->status.rx_status = CEC_RX_FAIL;
		MTK_CEC_LOG("[mtk_cec]Overflow\n");
	}

}

static void mtk_cec_tx_event_handler(struct mtk_cec *cec)
{
	mtk_cec_tx_detect_startbit(cec);
	if (cec->transmitting.status.tx_status == CEC_TX_Transmitting) {
		if (mtk_cec_tx_fail_longlow(cec)) {
			mtk_cec_tx_fail_longlow_int_en(cec, 0);
			mtk_cec_tx_fail_longlow_intclr(cec);
			cec->transmitting.status.tx_status = CEC_TX_FAIL;
			cec_transmit_done(cec->adap,
				CEC_TX_STATUS_ERROR, 0, 0, 0, 1);
			mtk_cec_tx_reset(cec);
			MTK_CEC_LOG("CEC Direct/Brdcst msg fail:long low");
		}

		if (mtk_cec_tx_fail_retransmit_intsta(cec)) {
			/*broadcast msg*/
			if ((cec->transmitting.msg->msg[0] & 0x0f) ==
				CEC_LOG_ADDR_BROADCAST) {
				if (mtk_cec_tx_fail_h_ack(cec) ||
					mtk_cec_tx_fail_data_ack(cec)) {
					mtk_cec_tx_fail_h_ack_intclr(cec);
					mtk_cec_tx_fail_data_ack_intclr(cec);
					MTK_CEC_LOG("broardcast NACK:done\n");
					cec->transmitting.status.tx_status =
						CEC_TX_COMPLETE;
					cec_transmit_done(cec->adap,
						CEC_TX_STATUS_OK, 0, 0, 0, 1);
				} else {
					MTK_CEC_LOG("broardcast ACK:fail\n");
					cec->transmitting.status.tx_status =
						CEC_TX_FAIL;
					cec_transmit_done(cec->adap,
					CEC_TX_STATUS_ERROR, 0, 0, 0, 1);
				}
			} else { /*direct msg*/
				if (mtk_cec_tx_fail_h_ack(cec)) {
					mtk_cec_tx_fail_h_ack_intclr(cec);
					MTK_CEC_LOG("err:Header NACK\n");
					cec_transmit_done(cec->adap,
						CEC_TX_STATUS_NACK, 0, 0, 0, 1);
				}
				if (mtk_cec_tx_fail_data_ack(cec)) {
					mtk_cec_tx_fail_data_ack_intclr(cec);
					MTK_CEC_LOG("err:Data NACK\n");
					cec_transmit_done(cec->adap,
					CEC_TX_STATUS_NACK, 0, 0, 0, 1);
				}
				if (mtk_cec_tx_fail_data(cec)) {
					mtk_cec_tx_fail_data_intclr(cec);
					MTK_CEC_LOG("err:Data\n");
					cec_transmit_done(cec->adap,
					CEC_TX_STATUS_ERROR, 0, 0, 0, 1);
				}
				if (mtk_cec_tx_fail_header(cec)) {
					mtk_cec_tx_fail_header_intclr(cec);
					MTK_CEC_LOG("err:Header\n");
					cec_transmit_done(cec->adap,
					CEC_TX_STATUS_ERROR, 0, 0, 0, 1);
				}
				if (mtk_cec_tx_fail_src(cec)) {
					mtk_cec_tx_fail_src_intclr(cec);
					MTK_CEC_LOG("err:Source\n");
					cec_transmit_done(cec->adap,
					CEC_TX_STATUS_ERROR, 0, 0, 0, 1);
				}
				MTK_CEC_LOG("err:Retransmit\n");
				cec->transmitting.status.tx_status =
					CEC_TX_FAIL;
				mtk_cec_tx_fail_retransmit_intclr(cec);
			}
		}

		if (mtk_cec_tx_finish(cec)) {
			mtk_cec_tx_finish_clr(cec);
			mtk_cec_tx_int_disable_all(cec);
			mtk_cec_tx_int_clear_all(cec);
			if ((cec->transmitting.msg->msg[0] & 0x0f) ==
				CEC_LOG_ADDR_BROADCAST)
				MTK_CEC_LOG("CEC Brdcst message success\n");
			else
				MTK_CEC_LOG("CEC Direct message success\n");

			cec->transmitting.status.tx_status =
				CEC_TX_COMPLETE;
			cec_transmit_done(cec->adap,
				CEC_TX_STATUS_OK, 0, 0, 0, 1);
		}
	}
}

static void mtk_cec_tx_rx_event_handler(struct mtk_cec *cec)
{
	mtk_cec_rx_event_handler(cec);
	mtk_cec_tx_event_handler(cec);
}

static irqreturn_t mtk_cec_isr_thread(int irq, void *arg)
{
	//struct device *dev = arg;
	//can.zeng todo verify
	//struct mtk_cec *cec = dev_get_drvdata(dev);
	struct mtk_cec *cec = mtk_global_cec;

	mtk_cec_tx_rx_event_handler(cec);

	return IRQ_HANDLED;
}

static const struct cec_adap_ops mtk_hdmi_cec_adap_ops = {
	.adap_enable = mtk_cec_adap_enable,
	.adap_log_addr = mtk_cec_adap_log_addr,
	.adap_transmit = mtk_cec_adap_transmit,
	.received = mtk_cec_received,
};

static int mtk_hdmi_cec_all_clk(struct mtk_cec *cec,
				struct device_node *np)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mtk_cec_clk_names); i++) {
		cec->clk[i] = of_clk_get_by_name(np,
			mtk_cec_clk_names[i]);

		if (IS_ERR(cec->clk[i]))
			return PTR_ERR(cec->clk[i]);
	}

	//can.zeng todo verify, enable all clks temporarily
	for (i = 0; i < ARRAY_SIZE(mtk_cec_clk_names); i++)
		clk_prepare_enable(cec->clk[i]);

	return 0;
}

static int mtk_cec_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mtk_cec *cec;
	struct resource *res;
	int ret;

	cec = devm_kzalloc(dev, sizeof(*cec), GFP_KERNEL);
	if (!cec)
		return -ENOMEM;

	mtk_global_cec = cec;

	platform_set_drvdata(pdev, cec);
	spin_lock_init(&cec->lock);

	ret = mtk_hdmi_cec_all_clk(cec, dev->of_node);
	if (ret) {
		MTK_CEC_LOG("Failed to get clocks: %d\n", ret);
		return ret;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	cec->regs = devm_ioremap_resource(dev, res);
	if (IS_ERR(cec->regs)) {
		ret = PTR_ERR(cec->regs);
		dev_err(dev, "Failed to ioremap cec: %d\n", ret);
		return ret;
	}

	cec->irq = irq_of_parse_and_map(pdev->dev.of_node, 0);
	/*can.zeng todo, where to free_irq*/
	if (request_irq(cec->irq, mtk_cec_isr_thread,
		IRQF_TRIGGER_HIGH, "cecirq", NULL) < 0)
		MTK_CEC_LOG("request cec interrupt failed.\n");
	else
		MTK_CEC_LOG("request cec interrupt success\n");


	cec->adap = cec_allocate_adapter(&mtk_hdmi_cec_adap_ops,
					cec, "mtk-hdmi-cec",
					CEC_CAP_TRANSMIT | CEC_CAP_PASSTHROUGH |
					CEC_CAP_LOG_ADDRS, 1);

	ret = PTR_ERR_OR_ZERO(cec->adap);
	if (ret < 0) {
		MTK_CEC_LOG("Failed to allocate cec adapter %d\n", ret);
		return ret;
	}

	ret = cec_register_adapter(cec->adap, dev);
	if (ret) {
		MTK_CEC_LOG("Fail to register cec adapter\n");
		cec_delete_adapter(cec->adap);
		return ret;
	}

	cec->cec_enabled = false;

	//can.zeng todo remove
	mtk_cec_adap_enable(cec->adap, true);
	mtk_cec_adap_log_addr(cec->adap, 4);
	/*disable all int*/
	mtk_cec_write(cec, CEC2_INT_EN, 0);
	/*clear all irq config*/
	mtk_cec_write(cec, CEC2_INT_CLR, 0xffffffff);
	mtk_cec_write(cec, CEC2_INT_CLR, 0);


	return 0;
}

static int mtk_cec_remove(struct platform_device *pdev)
{
	int i;
	struct mtk_cec *cec = platform_get_drvdata(pdev);

	cec_unregister_adapter(cec->adap);

	for (i = 0; i < ARRAY_SIZE(mtk_cec_clk_names); i++)
		clk_disable_unprepare(cec->clk[i]);
	return 0;
}

struct mtk_hdmi_cec_data {
	bool support_cec;
};

static const struct mtk_hdmi_cec_data mt8195_cec_data = {
	.support_cec = true,
};

static const struct of_device_id mtk_cec_of_ids[] = {
	{ .compatible = "mediatek,mt8195-cec",
	  .data = &mt8195_cec_data},
	  {},
};

MODULE_DEVICE_TABLE(of, mtk_cec_of_ids);


struct platform_driver mtk_cec_driver = {
	.probe = mtk_cec_probe,
	.remove = mtk_cec_remove,
	.driver = {
		.name = "mediatek-cec",
		.of_match_table = mtk_cec_of_ids,
	},
};

