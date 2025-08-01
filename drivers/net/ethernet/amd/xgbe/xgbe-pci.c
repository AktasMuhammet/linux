// SPDX-License-Identifier: (GPL-2.0-or-later OR BSD-3-Clause)
/*
 * Copyright (c) 2014-2025, Advanced Micro Devices, Inc.
 * Copyright (c) 2014, Synopsys, Inc.
 * All rights reserved
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/pci.h>
#include <linux/log2.h>
#include "xgbe-smn.h"

#include "xgbe.h"
#include "xgbe-common.h"

static int xgbe_config_multi_msi(struct xgbe_prv_data *pdata)
{
	unsigned int vector_count;
	unsigned int i, j;
	int ret;

	vector_count = XGBE_MSI_BASE_COUNT;
	vector_count += max(pdata->rx_ring_count,
			    pdata->tx_ring_count);

	ret = pci_alloc_irq_vectors(pdata->pcidev, XGBE_MSI_MIN_COUNT,
				    vector_count, PCI_IRQ_MSI | PCI_IRQ_MSIX);
	if (ret < 0) {
		dev_info(pdata->dev, "multi MSI/MSI-X enablement failed\n");
		return ret;
	}

	pdata->isr_as_bh_work = 1;
	pdata->irq_count = ret;

	pdata->dev_irq = pci_irq_vector(pdata->pcidev, 0);
	pdata->ecc_irq = pci_irq_vector(pdata->pcidev, 1);
	pdata->i2c_irq = pci_irq_vector(pdata->pcidev, 2);
	pdata->an_irq = pci_irq_vector(pdata->pcidev, 3);

	for (i = XGBE_MSI_BASE_COUNT, j = 0; i < ret; i++, j++)
		pdata->channel_irq[j] = pci_irq_vector(pdata->pcidev, i);
	pdata->channel_irq_count = j;

	pdata->per_channel_irq = 1;
	pdata->channel_irq_mode = XGBE_IRQ_MODE_LEVEL;

	if (netif_msg_probe(pdata))
		dev_dbg(pdata->dev, "multi %s interrupts enabled\n",
			pdata->pcidev->msix_enabled ? "MSI-X" : "MSI");

	return 0;
}

static int xgbe_config_irqs(struct xgbe_prv_data *pdata)
{
	int ret;

	ret = xgbe_config_multi_msi(pdata);
	if (!ret)
		goto out;

	ret = pci_alloc_irq_vectors(pdata->pcidev, 1, 1,
				    PCI_IRQ_INTX | PCI_IRQ_MSI);
	if (ret < 0) {
		dev_info(pdata->dev, "single IRQ enablement failed\n");
		return ret;
	}

	pdata->isr_as_bh_work = pdata->pcidev->msi_enabled ? 1 : 0;
	pdata->irq_count = 1;
	pdata->channel_irq_count = 1;

	pdata->dev_irq = pci_irq_vector(pdata->pcidev, 0);
	pdata->ecc_irq = pci_irq_vector(pdata->pcidev, 0);
	pdata->i2c_irq = pci_irq_vector(pdata->pcidev, 0);
	pdata->an_irq = pci_irq_vector(pdata->pcidev, 0);

	if (netif_msg_probe(pdata))
		dev_dbg(pdata->dev, "single %s interrupt enabled\n",
			pdata->pcidev->msi_enabled ?  "MSI" : "legacy");

out:
	if (netif_msg_probe(pdata)) {
		unsigned int i;

		dev_dbg(pdata->dev, " dev irq=%d\n", pdata->dev_irq);
		dev_dbg(pdata->dev, " ecc irq=%d\n", pdata->ecc_irq);
		dev_dbg(pdata->dev, " i2c irq=%d\n", pdata->i2c_irq);
		dev_dbg(pdata->dev, "  an irq=%d\n", pdata->an_irq);
		for (i = 0; i < pdata->channel_irq_count; i++)
			dev_dbg(pdata->dev, " dma%u irq=%d\n",
				i, pdata->channel_irq[i]);
	}

	return 0;
}

static int xgbe_pci_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	void __iomem * const *iomap_table;
	unsigned int port_addr_size, reg;
	struct device *dev = &pdev->dev;
	struct xgbe_prv_data *pdata;
	unsigned int ma_lo, ma_hi;
	struct pci_dev *rdev;
	int bar_mask, ret;
	u32 address;

	pdata = xgbe_alloc_pdata(dev);
	if (IS_ERR(pdata)) {
		ret = PTR_ERR(pdata);
		goto err_alloc;
	}

	pdata->pcidev = pdev;
	pci_set_drvdata(pdev, pdata);

	/* Get the version data */
	pdata->vdata = (struct xgbe_version_data *)id->driver_data;

	ret = pcim_enable_device(pdev);
	if (ret) {
		dev_err(dev, "pcim_enable_device failed\n");
		goto err_pci_enable;
	}

	/* Obtain the mmio areas for the device */
	bar_mask = pci_select_bars(pdev, IORESOURCE_MEM);
	ret = pcim_iomap_regions(pdev, bar_mask, XGBE_DRV_NAME);
	if (ret) {
		dev_err(dev, "pcim_iomap_regions failed\n");
		goto err_pci_enable;
	}

	iomap_table = pcim_iomap_table(pdev);
	if (!iomap_table) {
		dev_err(dev, "pcim_iomap_table failed\n");
		ret = -ENOMEM;
		goto err_pci_enable;
	}

	pdata->xgmac_regs = iomap_table[XGBE_XGMAC_BAR];
	if (!pdata->xgmac_regs) {
		dev_err(dev, "xgmac ioremap failed\n");
		ret = -ENOMEM;
		goto err_pci_enable;
	}
	pdata->xprop_regs = pdata->xgmac_regs + XGBE_MAC_PROP_OFFSET;
	pdata->xi2c_regs = pdata->xgmac_regs + XGBE_I2C_CTRL_OFFSET;
	if (netif_msg_probe(pdata)) {
		dev_dbg(dev, "xgmac_regs = %p\n", pdata->xgmac_regs);
		dev_dbg(dev, "xprop_regs = %p\n", pdata->xprop_regs);
		dev_dbg(dev, "xi2c_regs  = %p\n", pdata->xi2c_regs);
	}

	pdata->xpcs_regs = iomap_table[XGBE_XPCS_BAR];
	if (!pdata->xpcs_regs) {
		dev_err(dev, "xpcs ioremap failed\n");
		ret = -ENOMEM;
		goto err_pci_enable;
	}
	if (netif_msg_probe(pdata))
		dev_dbg(dev, "xpcs_regs  = %p\n", pdata->xpcs_regs);

	/* Set the PCS indirect addressing definition registers */
	rdev = pci_get_domain_bus_and_slot(0, 0, PCI_DEVFN(0, 0));
	if (rdev && rdev->vendor == PCI_VENDOR_ID_AMD) {
		switch (rdev->device) {
		case XGBE_RV_PCI_DEVICE_ID:
			pdata->xpcs_window_def_reg = PCS_V2_RV_WINDOW_DEF;
			pdata->xpcs_window_sel_reg = PCS_V2_RV_WINDOW_SELECT;
			break;
		case XGBE_YC_PCI_DEVICE_ID:
			pdata->xpcs_window_def_reg = PCS_V2_YC_WINDOW_DEF;
			pdata->xpcs_window_sel_reg = PCS_V2_YC_WINDOW_SELECT;

			/* Yellow Carp devices do not need cdr workaround */
			pdata->vdata->an_cdr_workaround = 0;

			/* Yellow Carp devices do not need rrc */
			pdata->vdata->enable_rrc = 0;
			break;
		case XGBE_RN_PCI_DEVICE_ID:
			pdata->xpcs_window_def_reg = PCS_V3_RN_WINDOW_DEF;
			pdata->xpcs_window_sel_reg = PCS_V3_RN_WINDOW_SELECT;
			break;
		default:
			pdata->xpcs_window_def_reg = PCS_V2_WINDOW_DEF;
			pdata->xpcs_window_sel_reg = PCS_V2_WINDOW_SELECT;
			break;
		}
	} else {
		pdata->xpcs_window_def_reg = PCS_V2_WINDOW_DEF;
		pdata->xpcs_window_sel_reg = PCS_V2_WINDOW_SELECT;
	}
	pci_dev_put(rdev);

	/* Configure the PCS indirect addressing support */
	if (pdata->vdata->xpcs_access == XGBE_XPCS_ACCESS_V3) {
		reg = XP_IOREAD(pdata, XP_PROP_0);
		port_addr_size = PCS_RN_PORT_ADDR_SIZE *
				 XP_GET_BITS(reg, XP_PROP_0, PORT_ID);
		pdata->smn_base = PCS_RN_SMN_BASE_ADDR + port_addr_size;

		address = pdata->smn_base + (pdata->xpcs_window_def_reg);
		ret = amd_smn_read(0, address, &reg);
		if (ret) {
			pci_err(pdata->pcidev, "Failed to read data\n");
			goto err_pci_enable;
		}
	} else {
		reg = XPCS32_IOREAD(pdata, pdata->xpcs_window_def_reg);
	}

	pdata->xpcs_window = XPCS_GET_BITS(reg, PCS_V2_WINDOW_DEF, OFFSET);
	pdata->xpcs_window <<= 6;
	pdata->xpcs_window_size = XPCS_GET_BITS(reg, PCS_V2_WINDOW_DEF, SIZE);
	pdata->xpcs_window_size = 1 << (pdata->xpcs_window_size + 7);
	pdata->xpcs_window_mask = pdata->xpcs_window_size - 1;
	if (netif_msg_probe(pdata)) {
		dev_dbg(dev, "xpcs window def  = %#010x\n",
			pdata->xpcs_window_def_reg);
		dev_dbg(dev, "xpcs window sel  = %#010x\n",
			pdata->xpcs_window_sel_reg);
		dev_dbg(dev, "xpcs window      = %#010x\n",
			pdata->xpcs_window);
		dev_dbg(dev, "xpcs window size = %#010x\n",
			pdata->xpcs_window_size);
		dev_dbg(dev, "xpcs window mask = %#010x\n",
			pdata->xpcs_window_mask);
	}

	pci_set_master(pdev);

	/* Enable all interrupts in the hardware */
	XP_IOWRITE(pdata, XP_INT_EN, 0x1fffff);

	/* Retrieve the MAC address */
	ma_lo = XP_IOREAD(pdata, XP_MAC_ADDR_LO);
	ma_hi = XP_IOREAD(pdata, XP_MAC_ADDR_HI);
	pdata->mac_addr[0] = ma_lo & 0xff;
	pdata->mac_addr[1] = (ma_lo >> 8) & 0xff;
	pdata->mac_addr[2] = (ma_lo >> 16) & 0xff;
	pdata->mac_addr[3] = (ma_lo >> 24) & 0xff;
	pdata->mac_addr[4] = ma_hi & 0xff;
	pdata->mac_addr[5] = (ma_hi >> 8) & 0xff;
	if (!XP_GET_BITS(ma_hi, XP_MAC_ADDR_HI, VALID) ||
	    !is_valid_ether_addr(pdata->mac_addr)) {
		dev_err(dev, "invalid mac address\n");
		ret = -EINVAL;
		goto err_pci_enable;
	}

	/* Clock settings */
	pdata->sysclk_rate = XGBE_V2_DMA_CLOCK_FREQ;
	pdata->ptpclk_rate = XGBE_V2_PTP_CLOCK_FREQ;

	/* Set the DMA coherency values */
	pdata->coherent = 1;
	pdata->arcr = XGBE_DMA_PCI_ARCR;
	pdata->awcr = XGBE_DMA_PCI_AWCR;
	pdata->awarcr = XGBE_DMA_PCI_AWARCR;

	/* Read the port property registers */
	pdata->pp0 = XP_IOREAD(pdata, XP_PROP_0);
	pdata->pp1 = XP_IOREAD(pdata, XP_PROP_1);
	pdata->pp2 = XP_IOREAD(pdata, XP_PROP_2);
	pdata->pp3 = XP_IOREAD(pdata, XP_PROP_3);
	pdata->pp4 = XP_IOREAD(pdata, XP_PROP_4);
	if (netif_msg_probe(pdata)) {
		dev_dbg(dev, "port property 0 = %#010x\n", pdata->pp0);
		dev_dbg(dev, "port property 1 = %#010x\n", pdata->pp1);
		dev_dbg(dev, "port property 2 = %#010x\n", pdata->pp2);
		dev_dbg(dev, "port property 3 = %#010x\n", pdata->pp3);
		dev_dbg(dev, "port property 4 = %#010x\n", pdata->pp4);
	}

	/* Set the maximum channels and queues */
	pdata->tx_max_channel_count = XP_GET_BITS(pdata->pp1, XP_PROP_1,
						  MAX_TX_DMA);
	pdata->rx_max_channel_count = XP_GET_BITS(pdata->pp1, XP_PROP_1,
						  MAX_RX_DMA);
	pdata->tx_max_q_count = XP_GET_BITS(pdata->pp1, XP_PROP_1,
					    MAX_TX_QUEUES);
	pdata->rx_max_q_count = XP_GET_BITS(pdata->pp1, XP_PROP_1,
					    MAX_RX_QUEUES);
	if (netif_msg_probe(pdata)) {
		dev_dbg(dev, "max tx/rx channel count = %u/%u\n",
			pdata->tx_max_channel_count,
			pdata->rx_max_channel_count);
		dev_dbg(dev, "max tx/rx hw queue count = %u/%u\n",
			pdata->tx_max_q_count, pdata->rx_max_q_count);
	}

	/* Set the hardware channel and queue counts */
	xgbe_set_counts(pdata);

	/* Set the maximum fifo amounts */
	pdata->tx_max_fifo_size = XP_GET_BITS(pdata->pp2, XP_PROP_2,
					      TX_FIFO_SIZE);
	pdata->tx_max_fifo_size *= 16384;
	pdata->tx_max_fifo_size = min(pdata->tx_max_fifo_size,
				      pdata->vdata->tx_max_fifo_size);
	pdata->rx_max_fifo_size = XP_GET_BITS(pdata->pp2, XP_PROP_2,
					      RX_FIFO_SIZE);
	pdata->rx_max_fifo_size *= 16384;
	pdata->rx_max_fifo_size = min(pdata->rx_max_fifo_size,
				      pdata->vdata->rx_max_fifo_size);
	if (netif_msg_probe(pdata))
		dev_dbg(dev, "max tx/rx max fifo size = %u/%u\n",
			pdata->tx_max_fifo_size, pdata->rx_max_fifo_size);

	/* Configure interrupt support */
	ret = xgbe_config_irqs(pdata);
	if (ret)
		goto err_pci_enable;

	/* Configure the netdev resource */
	ret = xgbe_config_netdev(pdata);
	if (ret)
		goto err_irq_vectors;

	netdev_notice(pdata->netdev, "net device enabled\n");

	return 0;

err_irq_vectors:
	pci_free_irq_vectors(pdata->pcidev);

err_pci_enable:
	xgbe_free_pdata(pdata);

err_alloc:
	dev_notice(dev, "net device not enabled\n");

	return ret;
}

static void xgbe_pci_remove(struct pci_dev *pdev)
{
	struct xgbe_prv_data *pdata = pci_get_drvdata(pdev);

	xgbe_deconfig_netdev(pdata);

	pci_free_irq_vectors(pdata->pcidev);

	/* Disable all interrupts in the hardware */
	XP_IOWRITE(pdata, XP_INT_EN, 0x0);

	xgbe_free_pdata(pdata);
}

static int __maybe_unused xgbe_pci_suspend(struct device *dev)
{
	struct xgbe_prv_data *pdata = dev_get_drvdata(dev);
	struct net_device *netdev = pdata->netdev;
	int ret = 0;

	if (netif_running(netdev))
		ret = xgbe_powerdown(netdev, XGMAC_DRIVER_CONTEXT);

	pdata->lpm_ctrl = XMDIO_READ(pdata, MDIO_MMD_PCS, MDIO_CTRL1);
	pdata->lpm_ctrl |= MDIO_CTRL1_LPOWER;
	XMDIO_WRITE(pdata, MDIO_MMD_PCS, MDIO_CTRL1, pdata->lpm_ctrl);

	return ret;
}

static int __maybe_unused xgbe_pci_resume(struct device *dev)
{
	struct xgbe_prv_data *pdata = dev_get_drvdata(dev);
	struct net_device *netdev = pdata->netdev;
	int ret = 0;

	XP_IOWRITE(pdata, XP_INT_EN, 0x1fffff);

	pdata->lpm_ctrl &= ~MDIO_CTRL1_LPOWER;
	XMDIO_WRITE(pdata, MDIO_MMD_PCS, MDIO_CTRL1, pdata->lpm_ctrl);

	if (netif_running(netdev)) {
		ret = xgbe_powerup(netdev, XGMAC_DRIVER_CONTEXT);

		/* Schedule a restart in case the link or phy state changed
		 * while we were powered down.
		 */
		schedule_work(&pdata->restart_work);
	}

	return ret;
}

static struct xgbe_version_data xgbe_v3 = {
	.init_function_ptrs_phy_impl	= xgbe_init_function_ptrs_phy_v2,
	.xpcs_access			= XGBE_XPCS_ACCESS_V3,
	.mmc_64bit			= 1,
	.tx_max_fifo_size		= 65536,
	.rx_max_fifo_size		= 65536,
	.tx_tstamp_workaround		= 1,
	.ecc_support			= 1,
	.i2c_support			= 1,
	.irq_reissue_support		= 1,
	.tx_desc_prefetch		= 5,
	.rx_desc_prefetch		= 5,
	.an_cdr_workaround		= 0,
	.enable_rrc			= 0,
};

static struct xgbe_version_data xgbe_v2a = {
	.init_function_ptrs_phy_impl	= xgbe_init_function_ptrs_phy_v2,
	.xpcs_access			= XGBE_XPCS_ACCESS_V2,
	.mmc_64bit			= 1,
	.tx_max_fifo_size		= 229376,
	.rx_max_fifo_size		= 229376,
	.tx_tstamp_workaround		= 1,
	.tstamp_ptp_clock_freq		= 1,
	.ecc_support			= 1,
	.i2c_support			= 1,
	.irq_reissue_support		= 1,
	.tx_desc_prefetch		= 5,
	.rx_desc_prefetch		= 5,
	.an_cdr_workaround		= 1,
	.enable_rrc			= 1,
};

static struct xgbe_version_data xgbe_v2b = {
	.init_function_ptrs_phy_impl	= xgbe_init_function_ptrs_phy_v2,
	.xpcs_access			= XGBE_XPCS_ACCESS_V2,
	.mmc_64bit			= 1,
	.tx_max_fifo_size		= 65536,
	.rx_max_fifo_size		= 65536,
	.tx_tstamp_workaround		= 1,
	.tstamp_ptp_clock_freq		= 1,
	.ecc_support			= 1,
	.i2c_support			= 1,
	.irq_reissue_support		= 1,
	.tx_desc_prefetch		= 5,
	.rx_desc_prefetch		= 5,
	.an_cdr_workaround		= 1,
	.enable_rrc			= 1,
};

static const struct pci_device_id xgbe_pci_table[] = {
	{ PCI_VDEVICE(AMD, 0x1458),
	  .driver_data = (kernel_ulong_t)&xgbe_v2a },
	{ PCI_VDEVICE(AMD, 0x1459),
	  .driver_data = (kernel_ulong_t)&xgbe_v2b },
	{ PCI_VDEVICE(AMD, 0x1641),
	  .driver_data = (kernel_ulong_t)&xgbe_v3 },
	/* Last entry must be zero */
	{ 0, }
};
MODULE_DEVICE_TABLE(pci, xgbe_pci_table);

static SIMPLE_DEV_PM_OPS(xgbe_pci_pm_ops, xgbe_pci_suspend, xgbe_pci_resume);

static struct pci_driver xgbe_driver = {
	.name = XGBE_DRV_NAME,
	.id_table = xgbe_pci_table,
	.probe = xgbe_pci_probe,
	.remove = xgbe_pci_remove,
	.driver = {
		.pm = &xgbe_pci_pm_ops,
	}
};

int xgbe_pci_init(void)
{
	return pci_register_driver(&xgbe_driver);
}

void xgbe_pci_exit(void)
{
	pci_unregister_driver(&xgbe_driver);
}
