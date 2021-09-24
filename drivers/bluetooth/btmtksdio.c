// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 MediaTek Inc.

/*
 * Bluetooth support for MediaTek SDIO devices
 *
 * This file is written based on btsdio.c and btmtkuart.c.
 *
 * Author: Sean Wang <sean.wang@mediatek.com>
 *
 */

#include <asm/unaligned.h>
#include <linux/atomic.h>
#include <linux/firmware.h>
#include <linux/init.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/skbuff.h>

#include <linux/mmc/host.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/sdio_func.h>

#include <net/bluetooth/bluetooth.h>
#include <net/bluetooth/hci_core.h>

#include "h4_recv.h"
#include <asm/unaligned.h>

#define VERSION "0.1"

#define FIRMWARE_MT7663		"mediatek/mt7663pr2h.bin"
#define FIRMWARE_MT7668		"mediatek/mt7668pr2h.bin"
#define FIRMWARE_MT7961		"mediatek/BT_RAM_CODE_MT7961_1_2_hdr.bin"

#define MTKBTSDIO_AUTOSUSPEND_DELAY	8000

static bool enable_autosuspend;

struct btmtksdio_data {
	const char *fwname;
};

static const struct btmtksdio_data mt7663_data = {
	.fwname = FIRMWARE_MT7663,
};

static const struct btmtksdio_data mt7668_data = {
	.fwname = FIRMWARE_MT7668,
};

static const struct btmtksdio_data mt7961_data = {
	.fwname = FIRMWARE_MT7961,
};

static const struct sdio_device_id btmtksdio_table[] = {
	{SDIO_DEVICE(SDIO_VENDOR_ID_MEDIATEK, SDIO_DEVICE_ID_MEDIATEK_MT7663),
	 .driver_data = (kernel_ulong_t)&mt7663_data },
	{SDIO_DEVICE(SDIO_VENDOR_ID_MEDIATEK, SDIO_DEVICE_ID_MEDIATEK_MT7668),
	 .driver_data = (kernel_ulong_t)&mt7668_data },
	{SDIO_DEVICE(SDIO_VENDOR_ID_MEDIATEK, SDIO_DEVICE_ID_MEDIATEK_MT7961),
	 .driver_data = (kernel_ulong_t)&mt7961_data },
	{ }	/* Terminating entry */
};
MODULE_DEVICE_TABLE(sdio, btmtksdio_table);

#define MTK_REG_CHLPCR		0x4	/* W1S */
#define C_INT_EN_SET		BIT(0)
#define C_INT_EN_CLR		BIT(1)
#define C_FW_OWN_REQ_SET	BIT(8)  /* For write */
#define C_COM_DRV_OWN		BIT(8)  /* For read */
#define C_FW_OWN_REQ_CLR	BIT(9)

#define MTK_REG_CSDIOCSR	0x8
#define SDIO_RE_INIT_EN		BIT(0)
#define SDIO_INT_CTL		BIT(2)

#define MTK_REG_CHCR		0xc
#define C_INT_CLR_CTRL		BIT(1)

/* CHISR have the same bits field definition with CHIER */
#define MTK_REG_CHISR		0x10
#define MTK_REG_CHIER		0x14
#define FW_OWN_BACK_INT		BIT(0)
#define RX_DONE_INT		BIT(1)
#define TX_EMPTY		BIT(2)
#define TX_FIFO_OVERFLOW	BIT(8)
#define TX_COMPLETE_COUNT	0x00000070
#define INT_DEFAULT		(TX_FIFO_OVERFLOW | TX_EMPTY | RX_DONE_INT)
#define RX_PKT_LEN		GENMASK(31, 16)

#define FIRMWARE_INT_BIT31		0x80000000
#define FIRMWARE_INT_BIT15		0x00008000
#define FIRMWARE_INT			0x0000FE00
#define FW_INT_IND_INDICATOR		0x00000080
#define TX_COMPLETE_COUNT		0x00000070
#define TX_UNDER_THOLD			0x00000008

#define MTK_REG_CTDR		0x18

#define MTK_REG_CRDR		0x1c

#define MTK_REG_CRPLR		0x24

#define MTK_SDIO_BLOCK_SIZE	512

#define BTMTKSDIO_TX_WAIT_VND_EVT	1

#define MTK_CHIP_ID_REG	0x70010200

enum {
	MTK_WMT_PATCH_DWNLD = 0x1,
	MTK_WMT_TEST = 0x2,
	MTK_WMT_WAKEUP = 0x3,
	MTK_WMT_HIF = 0x4,
	MTK_WMT_FUNC_CTRL = 0x6,
	MTK_WMT_RST = 0x7,
	MTK_WMT_REGISTER = 0x8,
	MTK_WMT_SEMAPHORE = 0x17,
};

enum {
	BTMTK_WMT_INVALID,
	BTMTK_WMT_PATCH_UNDONE,
	BTMTK_WMT_PATCH_PROGRESS,
	BTMTK_WMT_PATCH_DONE,
	BTMTK_WMT_ON_UNDONE,
	BTMTK_WMT_ON_DONE,
	BTMTK_WMT_ON_PROGRESS,
};

struct mtkbtsdio_hdr {
	__le16	len;
	__le16	reserved;
	u8	bt_type;
} __packed;

struct mtk_wmt_hdr {
	u8	dir;
	u8	op;
	__le16	dlen;
	u8	flag;
} __packed;

struct mtk_hci_wmt_cmd {
	struct mtk_wmt_hdr hdr;
	u8 data[256];
} __packed;

struct btmtk_hci_wmt_evt {
	struct hci_event_hdr hhdr;
	struct mtk_wmt_hdr whdr;
} __packed;

struct btmtk_hci_wmt_evt_funcc {
	struct btmtk_hci_wmt_evt hwhdr;
	__be16 status;
} __packed;

struct btmtk_tci_sleep {
	u8 mode;
	__le16 duration;
	__le16 host_duration;
	u8 host_wakeup_pin;
	u8 time_compensation;
} __packed;

struct btmtk_hci_wmt_params {
	u8 op;
	u8 flag;
	u16 dlen;
	const void *data;
	u32 *status;
};

struct btmtksdio_dev {
	struct hci_dev *hdev;
	struct sdio_func *func;
	struct device *dev;

	struct work_struct txrx_work;
	unsigned long tx_state;
	struct sk_buff_head txq;
	atomic_t tx_ready;

	struct sk_buff *evt_skb;

	const struct btmtksdio_data *data;
};

static int mtk_hci_wmt_sync(struct hci_dev *hdev,
			    struct btmtk_hci_wmt_params *wmt_params)
{
	struct btmtksdio_dev *bdev = hci_get_drvdata(hdev);
	struct btmtk_hci_wmt_evt_funcc *wmt_evt_funcc;
	u32 hlen, status = BTMTK_WMT_INVALID;
	struct btmtk_hci_wmt_evt *wmt_evt;
	struct mtk_hci_wmt_cmd wc;
	struct mtk_wmt_hdr *hdr;
	int err;

	hlen = sizeof(*hdr) + wmt_params->dlen;
	if (hlen > 255)
		return -EINVAL;

	hdr = (struct mtk_wmt_hdr *)&wc;
	hdr->dir = 1;
	hdr->op = wmt_params->op;
	hdr->dlen = cpu_to_le16(wmt_params->dlen + 1);
	hdr->flag = wmt_params->flag;
	memcpy(wc.data, wmt_params->data, wmt_params->dlen);

	set_bit(BTMTKSDIO_TX_WAIT_VND_EVT, &bdev->tx_state);

	err = __hci_cmd_send(hdev, 0xfc6f, hlen, &wc);
	if (err < 0) {
		clear_bit(BTMTKSDIO_TX_WAIT_VND_EVT, &bdev->tx_state);
		return err;
	}

	/* The vendor specific WMT commands are all answered by a vendor
	 * specific event and will not have the Command Status or Command
	 * Complete as with usual HCI command flow control.
	 *
	 * After sending the command, wait for BTMTKSDIO_TX_WAIT_VND_EVT
	 * state to be cleared. The driver specific event receive routine
	 * will clear that state and with that indicate completion of the
	 * WMT command.
	 */
	err = wait_on_bit_timeout(&bdev->tx_state, BTMTKSDIO_TX_WAIT_VND_EVT,
				  TASK_INTERRUPTIBLE, HCI_INIT_TIMEOUT);
	if (err == -EINTR) {
		bt_dev_err(hdev, "Execution of wmt command interrupted");
		clear_bit(BTMTKSDIO_TX_WAIT_VND_EVT, &bdev->tx_state);
		return err;
	}

	if (err) {
		bt_dev_err(hdev, "Execution of wmt command timed out");
		clear_bit(BTMTKSDIO_TX_WAIT_VND_EVT, &bdev->tx_state);
		return -ETIMEDOUT;
	}

	/* Parse and handle the return WMT event */
	wmt_evt = (struct btmtk_hci_wmt_evt *)bdev->evt_skb->data;
	if (wmt_evt->whdr.op != hdr->op) {
		bt_dev_err(hdev, "Wrong op received %d expected %d",
			   wmt_evt->whdr.op, hdr->op);
		err = -EIO;
		goto err_free_skb;
	}

	switch (wmt_evt->whdr.op) {
	case MTK_WMT_SEMAPHORE:
		if (wmt_evt->whdr.flag == 2)
			status = BTMTK_WMT_PATCH_UNDONE;
		else
			status = BTMTK_WMT_PATCH_DONE;
		break;
	case MTK_WMT_FUNC_CTRL:
		wmt_evt_funcc = (struct btmtk_hci_wmt_evt_funcc *)wmt_evt;
		if (be16_to_cpu(wmt_evt_funcc->status) == 0x404)
			status = BTMTK_WMT_ON_DONE;
		else if (be16_to_cpu(wmt_evt_funcc->status) == 0x420)
			status = BTMTK_WMT_ON_PROGRESS;
		else
			status = BTMTK_WMT_ON_UNDONE;
		break;
	case MTK_WMT_PATCH_DWNLD:
		if (wmt_evt->whdr.flag == 2)
			status = BTMTK_WMT_PATCH_DONE;
		else if (wmt_evt->whdr.flag == 1)
			status = BTMTK_WMT_PATCH_PROGRESS;
		else
			status = BTMTK_WMT_PATCH_UNDONE;
		break;
	case MTK_WMT_REGISTER:
		if (bdev->evt_skb->len == 18) {
			memcpy(&status, bdev->evt_skb->data + 14, sizeof(u32));
			status = le32_to_cpu(status);
		}
		break;
	}

	if (wmt_params->status)
		*wmt_params->status = status;

err_free_skb:
	kfree_skb(bdev->evt_skb);
	bdev->evt_skb = NULL;

	return err;
}

static int btmtksdio_tx_packet(struct btmtksdio_dev *bdev,
			       struct sk_buff *skb)
{
	struct mtkbtsdio_hdr *sdio_hdr;
	int err;

	/* Make sure that there are enough rooms for SDIO header */
	if (unlikely(skb_headroom(skb) < sizeof(*sdio_hdr))) {
		err = pskb_expand_head(skb, sizeof(*sdio_hdr), 0,
				       GFP_ATOMIC);
		if (err < 0)
			return err;
	}

	/* Prepend MediaTek SDIO Specific Header */
	skb_push(skb, sizeof(*sdio_hdr));

	sdio_hdr = (void *)skb->data;
	sdio_hdr->len = cpu_to_le16(skb->len);
	sdio_hdr->reserved = cpu_to_le16(0);
	sdio_hdr->bt_type = hci_skb_pkt_type(skb);

	atomic_set(&bdev->tx_ready, 0);
	err = sdio_writesb(bdev->func, MTK_REG_CTDR, skb->data,
			   round_up(skb->len, MTK_SDIO_BLOCK_SIZE));
	if (err < 0)
		goto err_skb_pull;

	bdev->hdev->stat.byte_tx += skb->len;

	kfree_skb(skb);

	return 0;

err_skb_pull:
	skb_pull(skb, sizeof(*sdio_hdr));

	return err;
}

static u32 btmtksdio_drv_own_query(struct btmtksdio_dev *bdev)
{
	return sdio_readl(bdev->func, MTK_REG_CHLPCR, NULL);
}

static int btmtksdio_recv_event(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct btmtksdio_dev *bdev = hci_get_drvdata(hdev);
	struct hci_event_hdr *hdr = (void *)skb->data;
	int err;

	/* Fix up the vendor event id with 0xff for vendor specific instead
	 * of 0xe4 so that event send via monitoring socket can be parsed
	 * properly.
	 */
	if (hdr->evt == 0xe4)
		hdr->evt = HCI_EV_VENDOR;

	/* When someone waits for the WMT event, the skb is being cloned
	 * and being processed the events from there then.
	 */
	if (test_bit(BTMTKSDIO_TX_WAIT_VND_EVT, &bdev->tx_state)) {
		bdev->evt_skb = skb_clone(skb, GFP_KERNEL);
		if (!bdev->evt_skb) {
			err = -ENOMEM;
			goto err_out;
		}
	}

	err = hci_recv_frame(hdev, skb);
	if (err < 0)
		goto err_free_skb;

	if (hdr->evt == HCI_EV_VENDOR) {
		if (test_and_clear_bit(BTMTKSDIO_TX_WAIT_VND_EVT,
				       &bdev->tx_state)) {
			/* Barrier to sync with other CPUs */
			smp_mb__after_atomic();
			wake_up_bit(&bdev->tx_state, BTMTKSDIO_TX_WAIT_VND_EVT);
		}
	}

	return 0;

err_free_skb:
	kfree_skb(bdev->evt_skb);
	bdev->evt_skb = NULL;

err_out:
	return err;
}

static const struct h4_recv_pkt mtk_recv_pkts[] = {
	{ H4_RECV_ACL,      .recv = hci_recv_frame },
	{ H4_RECV_SCO,      .recv = hci_recv_frame },
	{ H4_RECV_EVENT,    .recv = btmtksdio_recv_event },
};

static int btmtksdio_rx_packet(struct btmtksdio_dev *bdev, u16 rx_size)
{
	const struct h4_recv_pkt *pkts = mtk_recv_pkts;
	int pkts_count = ARRAY_SIZE(mtk_recv_pkts);
	struct mtkbtsdio_hdr *sdio_hdr;
	int err, i, pad_size;
	struct sk_buff *skb;
	u16 dlen;

	if (rx_size < sizeof(*sdio_hdr))
		return -EILSEQ;

	/* A SDIO packet is exactly containing a Bluetooth packet */
	skb = bt_skb_alloc(rx_size, GFP_KERNEL);
	if (!skb)
		return -ENOMEM;

	skb_put(skb, rx_size);

	err = sdio_readsb(bdev->func, skb->data, MTK_REG_CRDR, rx_size);
	if (err < 0)
		goto err_kfree_skb;

	sdio_hdr = (void *)skb->data;

	/* We assume the default error as -EILSEQ simply to make the error path
	 * be cleaner.
	 */
	err = -EILSEQ;

	if (rx_size != le16_to_cpu(sdio_hdr->len)) {
		bt_dev_err(bdev->hdev, "Rx size in sdio header is mismatched ");
		goto err_kfree_skb;
	}

	hci_skb_pkt_type(skb) = sdio_hdr->bt_type;

	/* Remove MediaTek SDIO header */
	skb_pull(skb, sizeof(*sdio_hdr));

	/* We have to dig into the packet to get payload size and then know how
	 * many padding bytes at the tail, these padding bytes should be removed
	 * before the packet is indicated to the core layer.
	 */
	for (i = 0; i < pkts_count; i++) {
		if (sdio_hdr->bt_type == (&pkts[i])->type)
			break;
	}

	if (i >= pkts_count) {
		bt_dev_err(bdev->hdev, "Invalid bt type 0x%02x",
			   sdio_hdr->bt_type);
		goto err_kfree_skb;
	}

	/* Remaining bytes cannot hold a header*/
	if (skb->len < (&pkts[i])->hlen) {
		bt_dev_err(bdev->hdev, "The size of bt header is mismatched");
		goto err_kfree_skb;
	}

	switch ((&pkts[i])->lsize) {
	case 1:
		dlen = skb->data[(&pkts[i])->loff];
		break;
	case 2:
		dlen = get_unaligned_le16(skb->data +
					  (&pkts[i])->loff);
		break;
	default:
		goto err_kfree_skb;
	}

	pad_size = skb->len - (&pkts[i])->hlen -  dlen;

	/* Remaining bytes cannot hold a payload */
	if (pad_size < 0) {
		bt_dev_err(bdev->hdev, "The size of bt payload is mismatched");
		goto err_kfree_skb;
	}

	/* Remove padding bytes */
	skb_trim(skb, skb->len - pad_size);

	/* Complete frame */
	(&pkts[i])->recv(bdev->hdev, skb);

	bdev->hdev->stat.byte_rx += rx_size;

	return 0;

err_kfree_skb:
	kfree_skb(skb);

	return err;
}

static void btmtksdio_txrx_work(struct work_struct *work)
{
	struct btmtksdio_dev *bdev = container_of(work, struct btmtksdio_dev,
						  txrx_work);
	struct sk_buff *skb;
	int err, count = 0;
	u32 int_status;
	u16 rx_size = 0;
	u32 rx_pkt_len;

	pm_runtime_get_sync(bdev->dev);

	sdio_claim_host(bdev->func);

	/* Disable interrupt */
	sdio_writel(bdev->func, C_INT_EN_CLR, MTK_REG_CHLPCR, 0);

	int_status = sdio_readl(bdev->func, MTK_REG_CHISR, NULL);
	int_status &= INT_DEFAULT;

	if (int_status & INT_DEFAULT)
		count = 0;

	if (int_status & FW_OWN_BACK_INT)
		sdio_writel(bdev->func, FW_OWN_BACK_INT, MTK_REG_CHISR, NULL);

	if (int_status & TX_EMPTY) {
		atomic_set(&bdev->tx_ready, 1);
		sdio_writel(bdev->func, TX_EMPTY | TX_COMPLETE_COUNT, MTK_REG_CHISR, NULL);
	} else if (unlikely(int_status & TX_FIFO_OVERFLOW)) {
		sdio_writel(bdev->func, TX_FIFO_OVERFLOW, MTK_REG_CHISR, NULL);
	}

	if (int_status & RX_DONE_INT) {
		int_status &= 0xFFFB;
		rx_pkt_len = sdio_readl(bdev->func, MTK_REG_CRPLR, NULL);
		rx_size = (rx_pkt_len & RX_PKT_LEN) >> 16;
		sdio_writel(bdev->func, int_status, MTK_REG_CHISR, NULL);
		if (btmtksdio_rx_packet(bdev, rx_size) < 0)
			bdev->hdev->stat.err_rx++;
	}

	if (atomic_read(&bdev->tx_ready)) {
		skb = skb_dequeue(&bdev->txq);
		if (skb) {
			err = btmtksdio_tx_packet(bdev, skb);
			if (err < 0) {
				bdev->hdev->stat.err_tx++;
				skb_queue_head(&bdev->txq, skb);
			}
		}
	}

	/* Enable interrupt */
	sdio_writel(bdev->func, C_INT_EN_SET, MTK_REG_CHLPCR, 0);

	sdio_release_host(bdev->func);

	pm_runtime_mark_last_busy(bdev->dev);
	pm_runtime_put_autosuspend(bdev->dev);
}

static void btmtksdio_interrupt(struct sdio_func *func)
{
	struct btmtksdio_dev *bdev = sdio_get_drvdata(func);

	schedule_work(&bdev->txrx_work);
}

static int btmtksdio_open(struct hci_dev *hdev)
{
	struct btmtksdio_dev *bdev = hci_get_drvdata(hdev);
	int err;
	u32 status, val;

	bt_dev_err(hdev, "btmtksdio_open");
	sdio_claim_host(bdev->func);

	err = sdio_enable_func(bdev->func);
	if (err < 0)
		goto err_release_host;

	err = sdio_claim_irq(bdev->func, btmtksdio_interrupt);
	if (err < 0) {
		bt_dev_err(bdev->hdev, "Cannot claim irq, err = %d", err);
		goto err_disable_func;
	}

	err = sdio_set_block_size(bdev->func, MTK_SDIO_BLOCK_SIZE);
	if (err < 0) {
		bt_dev_err(bdev->hdev, "Cannot sdio_set_block_size, err = %d", err);
		goto err_release_irq;
	}

	/* SDIO CMD 5 allows the SDIO device back to idle state an
	 * synchronous interrupt is supported in SDIO 4-bit mode
	 */
	val = sdio_readl(bdev->func, MTK_REG_CSDIOCSR, &err);
	if (err < 0) {
		bt_dev_err(bdev->hdev, "read CSDIOCSR fail, err = %d", err);
		goto err_release_irq;
	}

	val |= SDIO_INT_CTL;
	sdio_writel(bdev->func, val, MTK_REG_CSDIOCSR, &err);
	if (err < 0) {
		bt_dev_err(bdev->hdev, "write CSDIOCSR fail, err = %d", err);
		goto err_release_irq;
	}

	/* Get ownership from the device */
	sdio_writel(bdev->func, C_FW_OWN_REQ_CLR, MTK_REG_CHLPCR, &err);
	if (err < 0) {
		bt_dev_err(bdev->hdev, "ownship write fail, err = %d", err);
		goto err_disable_func;
	}

	err = readx_poll_timeout(btmtksdio_drv_own_query, bdev, status,
				 status & C_COM_DRV_OWN, 2000, 1000000);
	if (err < 0) {
		bt_dev_err(bdev->hdev, "Cannot get ownership from device, err = %d", err);
		goto err_disable_func;
	}


	/* Setup interrupt sources */
	sdio_writel(bdev->func, FIRMWARE_INT_BIT31 | FIRMWARE_INT_BIT15 |
		FIRMWARE_INT | TX_FIFO_OVERFLOW |
		FW_INT_IND_INDICATOR | TX_COMPLETE_COUNT |
		TX_UNDER_THOLD | TX_EMPTY | RX_DONE_INT,
		MTK_REG_CHIER, &err);

	if (err < 0) {
		bt_dev_err(bdev->hdev, "Cannot Setup interrupt, err = %d", err);
		goto err_release_irq;
	}

	/* Enable interrupt */
	sdio_writel(bdev->func, C_INT_EN_SET, MTK_REG_CHLPCR, &err);
	if (err < 0) {
		bt_dev_err(bdev->hdev, "interrupt write fail, err = %d", err);
		goto err_release_irq;
	}

	/* Write clear method */
	val = sdio_readl(bdev->func, MTK_REG_CHCR, &err);
	if (err < 0) {
		bt_dev_err(bdev->hdev, "write clear read1 fail, err = %d", err);
		goto err_release_irq;
	}

	val |= C_INT_CLR_CTRL;
	sdio_writel(bdev->func, val, MTK_REG_CHCR, &err);
	if (err < 0) {
		bt_dev_err(bdev->hdev, "write clear write fail, err = %d", err);
		goto err_release_irq;
	}
	val = sdio_readl(bdev->func, MTK_REG_CHCR, &err);
	if (err < 0) {
		bt_dev_err(bdev->hdev, "write clear read2 fail, err = %d", err);
		goto err_release_irq;
	}

	if (val & C_INT_CLR_CTRL)
		bt_dev_info(bdev->hdev, "%s write clear", __func__);
	else
		bt_dev_info(bdev->hdev, "%s read clear", __func__);

	sdio_release_host(bdev->func);
	device_wakeup_enable(bdev->dev);

	return 0;

err_release_irq:
	sdio_release_irq(bdev->func);

err_disable_func:
	sdio_disable_func(bdev->func);
	device_wakeup_disable(bdev->dev);

err_release_host:
	sdio_release_host(bdev->func);

	return err;
}

static int btmtksdio_close(struct hci_dev *hdev)
{
	struct btmtksdio_dev *bdev = hci_get_drvdata(hdev);
	u32 status;
	int err;

	sdio_claim_host(bdev->func);

	/* Disable interrupt */
	sdio_writel(bdev->func, C_INT_EN_CLR, MTK_REG_CHLPCR, NULL);

	sdio_release_irq(bdev->func);

	cancel_work_sync(&bdev->txrx_work);

	/* Return ownership to the device */
	sdio_writel(bdev->func, C_FW_OWN_REQ_SET, MTK_REG_CHLPCR, NULL);

	err = readx_poll_timeout(btmtksdio_drv_own_query, bdev, status,
				 !(status & C_COM_DRV_OWN), 2000, 1000000);
	if (err < 0)
		bt_dev_err(bdev->hdev, "Cannot return ownership to device");

	sdio_disable_func(bdev->func);

	sdio_release_host(bdev->func);

	return 0;
}

static int btmtksdio_flush(struct hci_dev *hdev)
{
	struct btmtksdio_dev *bdev = hci_get_drvdata(hdev);

	skb_queue_purge(&bdev->txq);

	cancel_work_sync(&bdev->txrx_work);

	return 0;
}

static int btmtksdio_func_query(struct hci_dev *hdev)
{
	struct btmtk_hci_wmt_params wmt_params;
	int status, err;
	u8 param = 0;

	/* Query whether the function is enabled */
	wmt_params.op = MTK_WMT_FUNC_CTRL;
	wmt_params.flag = 4;
	wmt_params.dlen = sizeof(param);
	wmt_params.data = &param;
	wmt_params.status = &status;

	err = mtk_hci_wmt_sync(hdev, &wmt_params);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to query function status (%d)", err);
		return err;
	}

	return status;
}

struct btmtk_patch_header {
	u8 datetime[16];
	u8 platform[4];
	__le16 hwver;
	__le16 swver;
	__le32 magicnum;
} __packed;

struct btmtk_global_desc {
	__le32 patch_ver;
	__le32 sub_sys;
	__le32 feature_opt;
	__le32 section_num;
} __packed;

struct btmtk_section_map {
	__le32 sectype;
	__le32 secoffset;
	__le32 secsize;
	union {
		__le32 u4secspec[13];
		struct {
			__le32 dladdr;
			__le32 dlsize;
			__le32 seckeyidx;
			__le32 alignlen;
			__le32 sectype;
			__le32 dlmodecrctype;
			__le32 crc;
			__le32 reserved[6];
		} bin_info_spec;
	};
} __packed;

/* It is for mt79xx download rom patch*/
#define MTK_FW_ROM_PATCH_HEADER_SIZE	32
#define MTK_FW_ROM_PATCH_GD_SIZE	64
#define MTK_FW_ROM_PATCH_SEC_MAP_SIZE	64
#define MTK_SEC_MAP_COMMON_SIZE	12
#define MTK_SEC_MAP_NEED_SEND_SIZE	52

static int btsdio_mtk_setup_firmware_79xx(struct hci_dev *hdev, const char *fwname)
{
	struct btmtk_hci_wmt_params wmt_params;
	struct btmtk_patch_header *patchhdr = NULL;
	struct btmtk_global_desc *globaldesc = NULL;
	struct btmtk_section_map *sectionmap;
	const struct firmware *fw;
	const u8 *fw_ptr;
	const u8 *fw_bin_ptr;
	size_t fw_size;
	int err, dlen, i, status;
	u8 flag, first_block, retry;
	u32 section_num, dl_size, section_offset;
	u8 cmd[64];

	err = request_firmware(&fw, fwname, &hdev->dev);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to load firmware file (%d)", err);
		return err;
	}

	fw_ptr = fw->data;
	fw_bin_ptr = fw_ptr;
	fw_size = fw->size;
	patchhdr = (struct btmtk_patch_header *)fw_ptr;
	globaldesc = (struct btmtk_global_desc *)(fw_ptr + MTK_FW_ROM_PATCH_HEADER_SIZE);
	section_num = globaldesc->section_num;

	for (i = 0; i < section_num; i++) {
		first_block = 1;
		fw_ptr = fw_bin_ptr;
		sectionmap = (struct btmtk_section_map *)(fw_ptr + MTK_FW_ROM_PATCH_HEADER_SIZE +
			      MTK_FW_ROM_PATCH_GD_SIZE + MTK_FW_ROM_PATCH_SEC_MAP_SIZE * i);

		section_offset = sectionmap->secoffset;
		dl_size = sectionmap->bin_info_spec.dlsize;

		if (dl_size > 0) {
			retry = 20;
			while (retry > 0) {
				cmd[0] = 0; /* 0 means legacy dl mode. */
				memcpy(cmd + 1,
				       fw_ptr + MTK_FW_ROM_PATCH_HEADER_SIZE +
				       MTK_FW_ROM_PATCH_GD_SIZE +
				       MTK_FW_ROM_PATCH_SEC_MAP_SIZE * i +
				       MTK_SEC_MAP_COMMON_SIZE,
				       MTK_SEC_MAP_NEED_SEND_SIZE + 1);

				wmt_params.op = MTK_WMT_PATCH_DWNLD;
				wmt_params.status = &status;
				wmt_params.flag = 0;
				wmt_params.dlen = MTK_SEC_MAP_NEED_SEND_SIZE + 1;
				wmt_params.data = &cmd;

				err = mtk_hci_wmt_sync(hdev, &wmt_params);
				if (err < 0) {
					bt_dev_err(hdev, "Failed to send wmt patch dwnld (%d)",
						   err);
					goto err_release_fw;
				}

				if (status == BTMTK_WMT_PATCH_UNDONE) {
					break;
				} else if (status == BTMTK_WMT_PATCH_PROGRESS) {
					msleep(100);
					retry--;
				} else if (status == BTMTK_WMT_PATCH_DONE) {
					goto next_section;
				} else {
					bt_dev_err(hdev, "Failed wmt patch dwnld status (%d)",
						   status);
					goto err_release_fw;
				}
			}

			fw_ptr += section_offset;
			wmt_params.op = MTK_WMT_PATCH_DWNLD;
			wmt_params.status = NULL;

			while (dl_size > 0) {
				dlen = min_t(int, 250, dl_size);
				if (first_block == 1) {
					flag = 1;
					first_block = 0;
				} else if (dl_size - dlen <= 0) {
					flag = 3;
				} else {
					flag = 2;
				}

				wmt_params.flag = flag;
				wmt_params.dlen = dlen;
				wmt_params.data = fw_ptr;

				err = mtk_hci_wmt_sync(hdev, &wmt_params);
				if (err < 0) {
					bt_dev_err(hdev, "Failed to send wmt patch dwnld (%d)",
						   err);
					goto err_release_fw;
				}

				dl_size -= dlen;
				fw_ptr += dlen;
			}
		}

next_section:
		continue;
	}

	/* Wait a few moments for firmware activation done */
	usleep_range(100000, 120000);

	bt_dev_err(hdev, "%s success, err: %d", __func__, err);

err_release_fw:
	release_firmware(fw);

	return err;
}

static int mtk_setup_firmware(struct hci_dev *hdev, const char *fwname)
{
	struct btmtk_hci_wmt_params wmt_params;
	const struct firmware *fw;
	const u8 *fw_ptr;
	size_t fw_size;
	int err, dlen;
	u8 flag, param;

	err = request_firmware(&fw, fwname, &hdev->dev);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to load firmware file (%d)", err);
		return err;
	}

	/* Power on data RAM the firmware relies on. */
	param = 1;
	wmt_params.op = MTK_WMT_FUNC_CTRL;
	wmt_params.flag = 3;
	wmt_params.dlen = sizeof(param);
	wmt_params.data = &param;
	wmt_params.status = NULL;

	err = mtk_hci_wmt_sync(hdev, &wmt_params);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to power on data RAM (%d)", err);
		goto free_fw;
	}

	fw_ptr = fw->data;
	fw_size = fw->size;

	/* The size of patch header is 30 bytes, should be skip */
	if (fw_size < 30) {
		err = -EINVAL;
		goto free_fw;
	}

	fw_size -= 30;
	fw_ptr += 30;
	flag = 1;

	wmt_params.op = MTK_WMT_PATCH_DWNLD;
	wmt_params.status = NULL;

	while (fw_size > 0) {
		dlen = min_t(int, 250, fw_size);

		/* Tell device the position in sequence */
		if (fw_size - dlen <= 0)
			flag = 3;
		else if (fw_size < fw->size - 30)
			flag = 2;

		wmt_params.flag = flag;
		wmt_params.dlen = dlen;
		wmt_params.data = fw_ptr;

		err = mtk_hci_wmt_sync(hdev, &wmt_params);
		if (err < 0) {
			bt_dev_err(hdev, "Failed to send wmt patch dwnld (%d)",
				   err);
			goto free_fw;
		}

		fw_size -= dlen;
		fw_ptr += dlen;
	}

	wmt_params.op = MTK_WMT_RST;
	wmt_params.flag = 4;
	wmt_params.dlen = 0;
	wmt_params.data = NULL;
	wmt_params.status = NULL;

	/* Activate funciton the firmware providing to */
	err = mtk_hci_wmt_sync(hdev, &wmt_params);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to send wmt rst (%d)", err);
		goto free_fw;
	}

	/* Wait a few moments for firmware activation done */
	usleep_range(10000, 12000);

free_fw:
	release_firmware(fw);
	return err;
}

static int btsdio_mtk_reg_read(struct hci_dev *hdev, u32 reg, u32 *val)
{
	struct btmtk_hci_wmt_params wmt_params;
	u8 cmd[7] = {0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00};
	int err, status;

	memcpy(&cmd[3], &reg, sizeof(reg));
	wmt_params.op = MTK_WMT_REGISTER;
	wmt_params.flag = 2;
	wmt_params.dlen = 7;
	wmt_params.data = &cmd;
	wmt_params.status = &status;

	err = mtk_hci_wmt_sync(hdev, &wmt_params);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to read reg(%d)", err);
		return err;
	}

	*val = status;

	return err;
}

static int btsdio_mtk_reg_write(struct hci_dev *hdev, u32 reg, u32 *val)
{
	struct btmtk_hci_wmt_params wmt_params;
	u8 cmd[15] = {0x01, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00,
			0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF};
	int err, status;

	memcpy(&cmd[3], &reg, sizeof(reg));
	memcpy(&cmd[7], val, sizeof(*val));
	wmt_params.op = MTK_WMT_REGISTER;
	wmt_params.flag = 1;
	wmt_params.dlen = 15;
	wmt_params.data = &cmd;
	wmt_params.status = &status;

	err = mtk_hci_wmt_sync(hdev, &wmt_params);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to write reg(%d)", err);
		return err;
	}

	*val = status;

	return err;
}

static int btsdio_mtk_audio_setting(struct hci_dev *hdev)
{
	int err;
	u8 cmd[4] = {0x49, 0x00, 0x80, 0x00};
	u32 pinmux;
	struct sk_buff *skb;

	skb =  __hci_cmd_sync(hdev, 0xfc72, 4, cmd, HCI_CMD_TIMEOUT);
	if (IS_ERR(skb))
		return PTR_ERR(skb);

	err = btsdio_mtk_reg_read(hdev, 0x70005050, &pinmux);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to get register value (%d)", err);
		return err;
	}
	pinmux |= 0x11000000;
	err = btsdio_mtk_reg_write(hdev, 0x70005050, &pinmux);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to get register value (%d)", err);
		return err;
	}

	err = btsdio_mtk_reg_read(hdev, 0x70005054, &pinmux);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to get register value (%d)", err);
		return err;
	}
	pinmux |= 0x00000101;
	err = btsdio_mtk_reg_write(hdev, 0x70005054, &pinmux);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to get register value (%d)", err);
		return err;
	}

	return err;
}

static int btmtksdio_setup(struct hci_dev *hdev)
{
	struct btmtksdio_dev *bdev = hci_get_drvdata(hdev);
	struct btmtk_hci_wmt_params wmt_params;
	ktime_t calltime, delta, rettime;
	struct btmtk_tci_sleep tci_sleep;
	unsigned long long duration;
	struct sk_buff *skb;
	int err, status, dev_id;
	u8 param = 0x1;
	char fw_bin_name[64];
	u32 fw_version = 0;

	calltime = ktime_get();
	atomic_set(&bdev->tx_ready, 1);

	err = btsdio_mtk_reg_read(hdev, 0x80000008, &dev_id);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to get device id (%d)", err);
		return err;
	}

	if (!dev_id) {
		err = btsdio_mtk_reg_read(hdev, 0x70010200, &dev_id);
		if (err < 0) {
			bt_dev_err(hdev, "Failed to get device id (%d)", err);
			return err;
		}
		err = btsdio_mtk_reg_read(hdev, 0x80021004, &fw_version);
		if (err < 0) {
			bt_dev_err(hdev, "Failed to get fw version (%d)", err);
			return err;
		}
	}

	switch (dev_id) {
	case SDIO_DEVICE_ID_MEDIATEK_MT7961:
		snprintf(fw_bin_name, sizeof(fw_bin_name),
			 "mediatek/BT_RAM_CODE_MT%04x_1_%x_hdr.bin",
			 dev_id & 0xffff, (fw_version & 0xff) + 1);
		err = btsdio_mtk_setup_firmware_79xx(hdev, fw_bin_name);
		if (err < 0) {
			bt_dev_err(hdev, "Failed to setup 79xx firmware (%d)", err);
			return err;
		}

		/* Enable Bluetooth protocol */
		wmt_params.op = MTK_WMT_FUNC_CTRL;
		wmt_params.flag = 0;
		wmt_params.dlen = sizeof(param);
		wmt_params.data = &param;
		wmt_params.status = NULL;

		err = mtk_hci_wmt_sync(hdev, &wmt_params);
		if (err < 0) {
			bt_dev_err(hdev, "Failed to send wmt func ctrl (%d)", err);
			return err;
		}

		break;
	case SDIO_DEVICE_ID_MEDIATEK_MT7663:
	case SDIO_DEVICE_ID_MEDIATEK_MT7668:
		/* Query whether the firmware is already download */
		wmt_params.op = MTK_WMT_SEMAPHORE;
		wmt_params.flag = 1;
		wmt_params.dlen = 0;
		wmt_params.data = NULL;
		wmt_params.status = &status;

		err = mtk_hci_wmt_sync(hdev, &wmt_params);
		if (err < 0) {
			bt_dev_err(hdev, "Failed to query firmware status (%d)", err);
			return err;
		}

		if (status == BTMTK_WMT_PATCH_DONE) {
			bt_dev_info(hdev, "Firmware already downloaded");
			goto ignore_setup_fw;
		}

		/* Setup a firmware which the device definitely requires */
		err = mtk_setup_firmware(hdev, bdev->data->fwname);

ignore_setup_fw:
		/* Query whether the device is already enabled */
		err = readx_poll_timeout(btmtksdio_func_query, hdev, status,
					 status < 0 || status != BTMTK_WMT_ON_PROGRESS,
					 2000, 5000000);
		/* -ETIMEDOUT happens */
		if (err < 0)
			return err;

		/* The other errors happen in btusb_mtk_func_query */
		if (status < 0)
			return status;

		if (status == BTMTK_WMT_ON_DONE) {
			bt_dev_info(hdev, "function already on");
			goto ignore_func_on;
		}

		if (err < 0)
			return err;
		break;
	default:
		bt_dev_err(hdev, "Unknown chip id(0x%x)", dev_id);
		return 0;
	}

	/* Enable Bluetooth protocol */
	wmt_params.op = MTK_WMT_FUNC_CTRL;
	wmt_params.flag = 0;
	wmt_params.dlen = sizeof(param);
	wmt_params.data = &param;
	wmt_params.status = NULL;

	err = mtk_hci_wmt_sync(hdev, &wmt_params);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to send wmt func ctrl (%d)", err);
		return err;
	}

	/* Audio setting */
	err = btsdio_mtk_audio_setting(hdev);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to do audio setting(%d)", err);
		return err;
	}
	set_bit(HCI_QUIRK_WIDEBAND_SPEECH_SUPPORTED, &hdev->quirks);

ignore_func_on:
	if (bdev->func->device == SDIO_DEVICE_ID_MEDIATEK_MT7668 ||
	    bdev->func->device == SDIO_DEVICE_ID_MEDIATEK_MT7663) {
		/* Apply the low power environment setup for 7663/7668*/
		tci_sleep.mode = 0x5;
		tci_sleep.duration = cpu_to_le16(0x640);
		tci_sleep.host_duration = cpu_to_le16(0x640);
		tci_sleep.host_wakeup_pin = 0;
		tci_sleep.time_compensation = 0;

		skb = __hci_cmd_sync(hdev, 0xfc7a, sizeof(tci_sleep), &tci_sleep,
				     HCI_INIT_TIMEOUT);
		if (IS_ERR(skb)) {
			err = PTR_ERR(skb);
			bt_dev_err(hdev, "Failed to apply low power setting (%d)", err);
			return err;
		}
		kfree_skb(skb);
	}

	pm_runtime_set_autosuspend_delay(bdev->dev,
					 MTKBTSDIO_AUTOSUSPEND_DELAY);
	pm_runtime_use_autosuspend(bdev->dev);

	err = pm_runtime_set_active(bdev->dev);
	if (err < 0)
		return err;

	/* Default forbid runtime auto suspend, that can be allowed by
	 * enable_autosuspend flag or the PM runtime entry under sysfs.
	 */
	pm_runtime_forbid(bdev->dev);
	pm_runtime_enable(bdev->dev);

	if (enable_autosuspend)
		pm_runtime_allow(bdev->dev);

	rettime = ktime_get();
	delta = ktime_sub(rettime, calltime);
	duration = (unsigned long long)ktime_to_ns(delta) >> 10;

	bt_dev_info(hdev, "Device setup in %llu usecs", duration);

	return 0;
}

static int btmtksdio_shutdown(struct hci_dev *hdev)
{
	struct btmtksdio_dev *bdev = hci_get_drvdata(hdev);
	struct btmtk_hci_wmt_params wmt_params;
	u8 param = 0x0;
	int err;

	/* Get back the state to be consistent with the state
	 * in btmtksdio_setup.
	 */
	pm_runtime_get_sync(bdev->dev);

	/* Disable the device */
	wmt_params.op = MTK_WMT_FUNC_CTRL;
	wmt_params.flag = 0;
	wmt_params.dlen = sizeof(param);
	wmt_params.data = &param;
	wmt_params.status = NULL;

	err = mtk_hci_wmt_sync(hdev, &wmt_params);
	if (err < 0) {
		bt_dev_err(hdev, "Failed to send wmt func ctrl (%d)", err);
		return err;
	}

	pm_runtime_put_noidle(bdev->dev);
	pm_runtime_disable(bdev->dev);

	return 0;
}

static int btmtksdio_send_frame(struct hci_dev *hdev, struct sk_buff *skb)
{
	struct btmtksdio_dev *bdev = hci_get_drvdata(hdev);

	switch (hci_skb_pkt_type(skb)) {
	case HCI_COMMAND_PKT:
		hdev->stat.cmd_tx++;
		break;

	case HCI_ACLDATA_PKT:
		hdev->stat.acl_tx++;
		break;

	case HCI_SCODATA_PKT:
		hdev->stat.sco_tx++;
		break;

	default:
		return -EILSEQ;
	}

	skb_queue_tail(&bdev->txq, skb);

	schedule_work(&bdev->txrx_work);

	return 0;
}

static int btmtksdio_probe(struct sdio_func *func,
			   const struct sdio_device_id *id)
{
	struct btmtksdio_dev *bdev;
	struct hci_dev *hdev;
	int err;

	bdev = devm_kzalloc(&func->dev, sizeof(*bdev), GFP_KERNEL);
	if (!bdev)
		return -ENOMEM;

	bdev->data = (void *)id->driver_data;
	if (!bdev->data)
		return -ENODEV;

	bdev->dev = &func->dev;
	bdev->func = func;

	INIT_WORK(&bdev->txrx_work, btmtksdio_txrx_work);
	skb_queue_head_init(&bdev->txq);

	/* Initialize and register HCI device */
	hdev = hci_alloc_dev();
	if (!hdev) {
		dev_err(&func->dev, "Can't allocate HCI device\n");
		return -ENOMEM;
	}

	bdev->hdev = hdev;

	hdev->bus = HCI_SDIO;
	hci_set_drvdata(hdev, bdev);

	hdev->open     = btmtksdio_open;
	hdev->close    = btmtksdio_close;
	hdev->flush    = btmtksdio_flush;
	hdev->setup    = btmtksdio_setup;
	hdev->shutdown = btmtksdio_shutdown;
	hdev->send     = btmtksdio_send_frame;
	SET_HCIDEV_DEV(hdev, &func->dev);

	hdev->manufacturer = 70;
	set_bit(HCI_QUIRK_NON_PERSISTENT_SETUP, &hdev->quirks);

	err = hci_register_dev(hdev);
	if (err < 0) {
		dev_err(&func->dev, "Can't register HCI device\n");
		hci_free_dev(hdev);
		return err;
	}

	sdio_set_drvdata(func, bdev);

	/* pm_runtime_enable would be done after the firmware is being
	 * downloaded because the core layer probably already enables
	 * runtime PM for this func such as the case host->caps &
	 * MMC_CAP_POWER_OFF_CARD.
	 */
	if (pm_runtime_enabled(bdev->dev))
		pm_runtime_disable(bdev->dev);

	/* As explaination in drivers/mmc/core/sdio_bus.c tells us:
	 * Unbound SDIO functions are always suspended.
	 * During probe, the function is set active and the usage count
	 * is incremented.  If the driver supports runtime PM,
	 * it should call pm_runtime_put_noidle() in its probe routine and
	 * pm_runtime_get_noresume() in its remove routine.
	 *
	 * So, put a pm_runtime_put_noidle here !
	 */
	pm_runtime_put_noidle(bdev->dev);

	return 0;
}

static void btmtksdio_remove(struct sdio_func *func)
{
	struct btmtksdio_dev *bdev = sdio_get_drvdata(func);
	struct hci_dev *hdev;

	if (!bdev)
		return;

	/* Be consistent the state in btmtksdio_probe */
	pm_runtime_get_noresume(bdev->dev);

	hdev = bdev->hdev;

	sdio_set_drvdata(func, NULL);
	hci_unregister_dev(hdev);
	hci_free_dev(hdev);
}

#ifdef CONFIG_PM
static int btmtksdio_runtime_suspend(struct device *dev)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct btmtksdio_dev *bdev;
	u32 status;
	int err;
	int ret = 0;
	mmc_pm_flag_t pm_flags;

	bdev = sdio_get_drvdata(func);
	if (!bdev)
		return 0;

	sdio_claim_host(bdev->func);

	sdio_writel(bdev->func, C_FW_OWN_REQ_SET, MTK_REG_CHLPCR, &err);
	if (err < 0)
		goto out;

	err = readx_poll_timeout(btmtksdio_drv_own_query, bdev, status,
				 !(status & C_COM_DRV_OWN), 2000, 1000000);

	if (func) {
		pm_flags = sdio_get_host_pm_caps(func);
		if (!(pm_flags & MMC_PM_KEEP_POWER)) {
			bt_dev_err(bdev->hdev, "%s cannot remain alive suspended(0x%x)",
				   sdio_func_id(func), pm_flags);
		}

		pm_flags = MMC_PM_KEEP_POWER;
		ret = sdio_set_host_pm_flags(func, pm_flags);
		if (ret) {
			bt_dev_err(bdev->hdev, "set flag 0x%x err %d", pm_flags, (int)ret);
			return ret;
		}
	} else {
		bt_dev_err(bdev->hdev, "sdio_func is not specified");
		return 0;
	}
out:
	bt_dev_info(bdev->hdev, "status (%d) return ownership to device", err);

	sdio_release_host(bdev->func);

	return err;
}

static int btmtksdio_runtime_resume(struct device *dev)
{
	struct sdio_func *func = dev_to_sdio_func(dev);
	struct btmtksdio_dev *bdev;
	u32 status;
	int err;

	bdev = sdio_get_drvdata(func);
	if (!bdev)
		return 0;

	sdio_claim_host(bdev->func);

	sdio_writel(bdev->func, C_FW_OWN_REQ_CLR, MTK_REG_CHLPCR, &err);
	if (err < 0)
		goto out;

	err = readx_poll_timeout(btmtksdio_drv_own_query, bdev, status,
				 status & C_COM_DRV_OWN, 2000, 1000000);
out:
	bt_dev_info(bdev->hdev, "status (%d) get ownership from device", err);

	sdio_release_host(bdev->func);

	return err;
}

static UNIVERSAL_DEV_PM_OPS(btmtksdio_pm_ops, btmtksdio_runtime_suspend,
			    btmtksdio_runtime_resume, NULL);
#define BTMTKSDIO_PM_OPS (&btmtksdio_pm_ops)
#else	/* CONFIG_PM */
#define BTMTKSDIO_PM_OPS NULL
#endif	/* CONFIG_PM */

static struct sdio_driver btmtksdio_driver = {
	.name		= "btmtksdio",
	.probe		= btmtksdio_probe,
	.remove		= btmtksdio_remove,
	.id_table	= btmtksdio_table,
	.drv = {
		.owner = THIS_MODULE,
		.pm = BTMTKSDIO_PM_OPS,
	}
};

module_sdio_driver(btmtksdio_driver);

module_param(enable_autosuspend, bool, 0644);
MODULE_PARM_DESC(enable_autosuspend, "Enable autosuspend by default");

MODULE_AUTHOR("Sean Wang <sean.wang@mediatek.com>");
MODULE_DESCRIPTION("MediaTek Bluetooth SDIO driver ver " VERSION);
MODULE_VERSION(VERSION);
MODULE_LICENSE("GPL");
MODULE_FIRMWARE(FIRMWARE_MT7663);
MODULE_FIRMWARE(FIRMWARE_MT7668);
MODULE_FIRMWARE(FIRMWARE_MT7961);
