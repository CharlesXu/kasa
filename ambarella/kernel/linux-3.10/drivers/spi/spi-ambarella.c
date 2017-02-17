/*
 * linux/drivers/spi/spi-ambarella.c
 *
 * History:
 *	2014/08/29 - [Zhenwu Xue]

 *
 * Copyright (C) 2012-2016, Ambarella, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/gpio.h>
#include <linux/clk.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/spi/spi.h>
#include <linux/spi/spidev.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/dmapool.h>
#include <asm/io.h>
#include <mach/io.h>
#include <plat/spi.h>
#include <plat/ambcache.h>
#include <plat/dma.h>
#include <plat/rct.h>

#define	AMBARELLA_SPI_MAX_LEN			4096
#define	AMBARELLA_SPI_MAX_XFER_PER_MSG		32

struct ambarella_spi {
	u8					txb[AMBARELLA_SPI_MAX_LEN];
	u8					rxb[AMBARELLA_SPI_MAX_LEN];

	u32					phys;
	u32					virt;

	struct clk			*clk;
	u32					clk_freq;
	int					cs_pins[8];

	struct dma_chan		*txc;
	struct dma_chan		*rxc;

	struct spi_message	*msg;
	struct spi_transfer		*transfers[AMBARELLA_SPI_MAX_XFER_PER_MSG];
	int					xfer_id, n_xfer;

	int					cs_active;
	int					rw ;
	int					irq;
	u32					dma_used:1;
	u32					msb_first_only:1;
	u32					ridx, widx;

	struct tasklet_struct	tasklet;
};

static void ambarella_spi_next_transfer(void *args);

static int ambarella_spi_of_parse(struct platform_device *pdev,
			struct spi_master *master)
{
	struct device_node *np = pdev->dev.of_node;
	struct ambarella_spi *ambspi = spi_master_get_devdata(master);
	int rval;

	ambspi->clk = clk_get(NULL, "gclk_ssi");
	if (IS_ERR(ambspi->clk)) {
		dev_err(&pdev->dev, "Get PLL failed!\n");
		return PTR_ERR(ambspi->clk);
	}

	rval = of_property_read_u32(np, "amb,clk-freq", &ambspi->clk_freq);
	if (rval < 0) {
		dev_err(&pdev->dev, "invalid clk-freq! %d\n", rval);
		return rval;
	}

	return 0;
}

static int of_dev_node_match(struct device *dev, void *data)
{
	return dev->of_node == data;
}

static struct spi_device *ambarella_spi_of_find_device(struct device_node *np)
{
	struct device *dev;

	dev = bus_find_device(&spi_bus_type, NULL, np,
					 of_dev_node_match);
	if (!dev)
		return NULL;

	return to_spi_device(dev);
}

static bool ambarella_spi_dma_filter(struct dma_chan *chan, void *fparam)
{
	if (ambarella_dma_channel_id(chan) == *(int *)fparam) {
		return true;
	} else {
		return false;
	}
}

static void ambarella_spi_setup(struct ambarella_spi *bus, struct spi_device *spi)
{
	u32			virt;
	spi_ctrl0_reg_t		cr0;
	u32 			ssi_clk, sckdv;

	virt		= bus->virt;

	cr0.w		= 0;
	cr0.s.dfs	= spi->bits_per_word - 1;
	if (spi->mode & SPI_CPHA) {
		cr0.s.scph	= 1;
	} else {
		cr0.s.scph	= 0;
	}
	if (spi->mode & SPI_CPOL) {
		cr0.s.scpol	= 1;
	} else {
		cr0.s.scpol	= 0;
	}
	if (spi->mode & SPI_LOOP) {
		cr0.s.srl = 1;
	} else {
		cr0.s.srl = 0;
	}
	if (spi->mode & SPI_LSB_FIRST) {
		cr0.s.tx_lsb = 1;
		cr0.s.rx_lsb = 1;
	} else {
		cr0.s.tx_lsb = 0;
		cr0.s.rx_lsb = 0;
	}

	cr0.s.hold		= 0;
	cr0.s.byte_ws		= 0;
	cr0.s.fc_en		= 0;
	cr0.s.residue		= 1;
	amba_writel(virt + SPI_CTRLR0_OFFSET, cr0.w);

	ssi_clk = bus->clk_freq;
	if(spi->max_speed_hz > ssi_clk / 2) {
	    spi->max_speed_hz = ssi_clk / 2;
	}
	sckdv = (ssi_clk / spi->max_speed_hz + 1) & 0xfffe;
	amba_writel(virt + SPI_BAUDR_OFFSET, sckdv);

	if (spi->cs_gpio) {
		gpio_set_value(spi->cs_gpio, 0);
		bus->cs_active = 1;
	}
}

static void ambarella_spi_stop(struct ambarella_spi *bus)
{
	gpio_set_value(bus->msg->spi->cs_gpio, 1);
	bus->cs_active = 0;

	amba_readl(bus->virt + SPI_ICR_OFFSET);
	amba_readl(bus->virt + SPI_ISR_OFFSET);

	amba_writel(bus->virt + SPI_SSIENR_OFFSET, 0);
	amba_writel(bus->virt + SPI_SER_OFFSET, 0);

	if (bus->dma_used)
		amba_writel(bus->virt + 0x4c, 0);
}

static void ambarella_spi_prepare_transfer(struct ambarella_spi *bus)
{
	struct spi_message		*msg;
	struct spi_transfer		*xfer;
	u32				virt;
	const void			*wbuf, *rbuf;
	spi_ctrl0_reg_t			cr0;

	bus->widx = 0;
	bus->ridx = 0;

	virt		= bus->virt;

	msg		= bus->msg;
	xfer		= bus->transfers[bus->xfer_id];

	wbuf		= xfer->tx_buf;
	rbuf		= xfer->rx_buf;
	if (wbuf && !rbuf) {
		bus->rw	= SPI_WRITE_ONLY;
	}
	if (!wbuf && rbuf) {
		bus->rw	= SPI_READ_ONLY;
	}
	if (wbuf && rbuf) {
		bus->rw	= SPI_WRITE_READ;
	}
	msg->actual_length += xfer->len;

	cr0.w		= amba_readl(virt + SPI_CTRLR0_OFFSET);
	cr0.s.tmod	= SPI_WRITE_READ;
	amba_writel(virt + SPI_CTRLR0_OFFSET, cr0.w);

	if (!bus->cs_active) {
		gpio_set_value(msg->spi->cs_gpio, 0);
		bus->cs_active = 1;
	}

	amba_writel(virt + SPI_SSIENR_OFFSET, 0);
	amba_writel(virt + SPI_SER_OFFSET, 0);
	if (bus->dma_used) {
		if (bus->msg->spi->bits_per_word <= 8) {
			amba_writel(virt + SPI_RXFTLR_OFFSET, 8 - 1);
		} else {
			amba_writel(virt + SPI_RXFTLR_OFFSET, 4 - 1);
		}
		amba_writel(virt + 0x4c, 0x03);
	} else {
		disable_irq_nosync(bus->irq);
		amba_writel(virt + SPI_IMR_OFFSET, SPI_TXEIS_MASK);
		amba_writel(virt + SPI_TXFTLR_OFFSET, 0);
		amba_writel(virt + SPI_RXFTLR_OFFSET, 1);
	}
	amba_writel(virt + SPI_SSIENR_OFFSET, 1);
}

static void ambarella_spi_start_transfer(struct ambarella_spi *bus)
{
	struct spi_device		*spi;
	struct spi_transfer		*xfer;
	struct dma_slave_config		tx_cfg, rx_cfg;
	struct dma_async_tx_descriptor	*txd, *rxd;
	int				ret;
	int				cs_id;
	u32				virt;
	u32				len, txa, rxa, widx, ridx, xfer_len, i;
	void *wbuf, *rbuf;
	u16 tmp;

	xfer	= bus->transfers[bus->xfer_id];
	spi	= bus->msg->spi;
	virt	= bus->virt;
	cs_id = spi->chip_select;
	txa	= virt_to_phys(bus->txb);
	rxa	= virt_to_phys(bus->rxb);
	wbuf = (void *)xfer->tx_buf;
	rbuf = (void *)xfer->rx_buf;
	widx	= bus->widx;
	ridx	= bus->ridx;

	len	= xfer->len;
	if (!bus->dma_used) {
		if (bus->msg->spi->bits_per_word > 8)
			len >>= 1;
	}

	switch (bus->rw) {
	case SPI_WRITE_ONLY:
	case SPI_WRITE_READ:
		if (!bus->dma_used) {
			xfer_len = min_t(int, len - widx, SPI_DATA_FIFO_SIZE_16);
			for(i = 0; i < xfer_len; i++) {
				if (bus->msg->spi->bits_per_word <= 8)
					tmp = ((u8 *)wbuf)[widx++];
				else
					tmp = ((u16 *)wbuf)[widx++];
				amba_writel(virt + SPI_DR_OFFSET, tmp);
			}
		} else {
			memcpy(bus->txb, xfer->tx_buf, len);
		}
		break;
	case SPI_READ_ONLY:
		if (!bus->dma_used) {
			xfer_len = min_t(int, len - ridx, SPI_DATA_FIFO_SIZE_16);
			for(i = 0; i < xfer_len; i++)
				amba_writel(virt + SPI_DR_OFFSET, SPI_DUMMY_DATA);
		} else {
			memset(bus->txb, 0xff, len);
		}

		break;
	default:
		break;
	}

	if (bus->dma_used) {
		ambcache_clean_range(bus->txb, len);
		tx_cfg.dst_addr			= bus->phys + SPI_DR_OFFSET;
		if (spi->bits_per_word <= 8) {
			tx_cfg.dst_addr_width	= DMA_SLAVE_BUSWIDTH_1_BYTE;
		} else {
			tx_cfg.dst_addr_width	= DMA_SLAVE_BUSWIDTH_2_BYTES;
		}
		tx_cfg.dst_maxburst		= 8;
		tx_cfg.direction		= DMA_MEM_TO_DEV;

		ret = dmaengine_slave_config(bus->txc, &tx_cfg);
		BUG_ON(ret < 0);

		dma_sync_single_for_device(NULL, txa, len, DMA_TO_DEVICE);

		txd = dmaengine_prep_slave_single(bus->txc, txa, len,
			DMA_MEM_TO_DEV, DMA_PREP_INTERRUPT |
			DMA_COMPL_SKIP_SRC_UNMAP | DMA_COMPL_SKIP_DEST_UNMAP | DMA_CTRL_ACK);
		BUG_ON (!txd);

		txd->callback		= NULL;
		txd->callback_param	= NULL;
		dmaengine_submit(txd);

		dma_async_issue_pending(bus->txc);

		rx_cfg.src_addr			= bus->phys + SPI_DR_OFFSET;
		if (spi->bits_per_word <= 8) {
			rx_cfg.src_addr_width	= DMA_SLAVE_BUSWIDTH_1_BYTE;
		} else {
			rx_cfg.src_addr_width	= DMA_SLAVE_BUSWIDTH_2_BYTES;
		}
		rx_cfg.src_maxburst		= 8;
		rx_cfg.direction		= DMA_DEV_TO_MEM;
		ret = dmaengine_slave_config(bus->rxc, &rx_cfg);
		BUG_ON(ret < 0);

		rxd = dmaengine_prep_slave_single(bus->rxc, rxa, len,
			DMA_DEV_TO_MEM,	DMA_PREP_INTERRUPT | DMA_CTRL_ACK |
			DMA_COMPL_SKIP_SRC_UNMAP | DMA_COMPL_SKIP_DEST_UNMAP);
		BUG_ON(!rxd);

		rxd->callback		= ambarella_spi_next_transfer;
		rxd->callback_param	= bus;
		dma_sync_single_for_device(NULL, rxa, len, DMA_FROM_DEVICE);

		dmaengine_submit(rxd);
		dma_async_issue_pending(bus->rxc);
	} else {
		bus->widx = widx;
		enable_irq(bus->irq);
	}

	amba_writel(virt + SPI_SER_OFFSET, 1 << cs_id);
}

static void ambarella_spi_next_transfer(void *args)
{
	struct ambarella_spi	*bus = args;
	struct spi_transfer	*xfer;

	xfer = bus->transfers[bus->xfer_id++];

	if (bus->dma_used) {
		switch (bus->rw) {
		case SPI_WRITE_READ:
		case SPI_READ_ONLY:
			ambcache_inv_range(bus->rxb, xfer->len);
			memcpy(xfer->rx_buf, bus->rxb, xfer->len);
			break;
		default:
			break;
		}
	}

	if (xfer->cs_change) {
		gpio_set_value(bus->msg->spi->cs_gpio, 1);
		bus->cs_active = 0;
	}

	if (bus->xfer_id >= bus->n_xfer) {
		ambarella_spi_stop(bus);
		spi_finalize_current_message(bus->msg->spi->master);
	} else {
		ambarella_spi_prepare_transfer(bus);
		ambarella_spi_start_transfer(bus);
	}
}

static void ambarella_spi_tasklet(unsigned long data)
{
	struct ambarella_spi *bus = (struct ambarella_spi *)data;
	struct spi_transfer *xfer;
	u32 virt;
	void *rbuf;
	u32 widx, ridx, len, rxflr, xfer_len;
	u32 status, finish_transfer = 0;
	u16 i, tmp;

	virt = bus->virt;
	xfer = bus->transfers[bus->xfer_id];

	/* Wait until SPI idle */
	status = amba_readl(virt + SPI_SR_OFFSET);
	if (status & 0x1) {
		/* Transfer is still in progress */
		for (i = 0; i < MAX_QUERY_TIMES; i++) {
			status = amba_readl(virt + SPI_SR_OFFSET);
			if (!(status & 0x1))
				break;
		}
		if (status & 0x1) {
			tasklet_schedule(&bus->tasklet);
			return;
		}
	}

	rbuf	= (void *)xfer->rx_buf;
	widx	= bus->widx;
	ridx	= bus->ridx;
	len	= xfer->len;

	if (!bus->dma_used) {
		if (bus->msg->spi->bits_per_word > 8)
			len >>= 1;
	}

	/* Fetch data from FIFO */
	switch (bus->rw) {
	case SPI_READ_ONLY:
	case SPI_WRITE_READ:
		xfer_len = len - ridx;
		rxflr = amba_readl(virt + SPI_RXFLR_OFFSET);
		if (xfer_len > rxflr)
			xfer_len = rxflr;
		for(i = 0; i < xfer_len; i++) {
			tmp	= amba_readl(virt + SPI_DR_OFFSET);
			if (bus->msg->spi->bits_per_word <= 8)
				((u8 *)rbuf)[ridx++] = tmp & 0xff;
			else
				((u16 *)rbuf)[ridx++] = tmp;
		}
		bus->ridx = ridx;
		break;
	default:
		break;
	}

	/* Check whether the current transfer ends */
	switch (bus->rw) {
	case SPI_WRITE_ONLY:
		if (widx == len)
			finish_transfer = 1;
		break;
	case SPI_READ_ONLY:
		if (ridx == len)
			finish_transfer = 1;
		break;
	case SPI_WRITE_READ:
		if (ridx == len && widx == len)
			finish_transfer = 1;
		break;
	default:
		break;
	}

	/* End transfer or continue filling FIFO */
	if (finish_transfer) {
		ambarella_spi_next_transfer((void *) bus);
		enable_irq(bus->irq);
	} else {
		ambarella_spi_start_transfer(bus);
	}
}

static int ambarella_spi_one_message(struct spi_master *master, struct spi_message *msg)
{
	int				err = 0;
	struct spi_device		*spi;
	struct ambarella_spi		*bus;
	struct spi_transfer		*xfer;

	spi = msg->spi;
	bus = spi_master_get_devdata(master);

	msg->status		= 0;
	msg->actual_length	= 0;
	bus->msg		= msg;

	if (list_empty(&msg->transfers)) {
		spi_finalize_current_message(master);
		goto ambarella_spi_transfer_exit;
	}

	if (spi->bits_per_word < 4 || spi->bits_per_word > 16) {
		err = -EINVAL;
		goto ambarella_spi_transfer_exit;
	}

	if (!spi->max_speed_hz) {
		err = -EINVAL;
		goto ambarella_spi_transfer_exit;
	}

	if (spi->bits_per_word > 8) {
		list_for_each_entry(xfer, &msg->transfers, transfer_list) {
			if (xfer->len & 0x1) {
				err = -EINVAL;
				goto ambarella_spi_transfer_exit;
			}
		}
	}

	if ((spi->mode & SPI_LSB_FIRST) && bus->msb_first_only) {
		dev_err(&master->dev, "SPI[%d] only supports msb first tx-rx", master->bus_num);
		err = -EINVAL;
		goto ambarella_spi_transfer_exit;
	}

	bus->n_xfer	= 0;
	bus->xfer_id	= 0;
	list_for_each_entry(xfer, &msg->transfers, transfer_list) {
		if (xfer->len > AMBARELLA_SPI_MAX_LEN) {
			err = -EINVAL;
			goto ambarella_spi_transfer_exit;
		}
		bus->transfers[bus->n_xfer++] = xfer;
	}

	ambarella_spi_setup(bus, spi);
	ambarella_spi_prepare_transfer(bus);
	ambarella_spi_start_transfer(bus);

ambarella_spi_transfer_exit:
	return err;
}

static irqreturn_t ambarella_spi_isr(int irq, void *dev_data)
{
	struct ambarella_spi *bus = dev_data;

	if (amba_readl(bus->virt + SPI_ISR_OFFSET)) {
		disable_irq_nosync(bus->irq);
		amba_writel(bus->virt + SPI_ISR_OFFSET, 0);

		ambarella_spi_tasklet((unsigned long)bus);
	}

	return IRQ_HANDLED;
}

static int ambarella_spi_dma_channel_allocate(struct spi_master *master)
{
	struct ambarella_spi *bus;
	dma_cap_mask_t	mask;
	int chan_id;
	u32 val;

	bus = spi_master_get_devdata(master);

	amba_writel(bus->virt + 0x4c, 0);
	if (bus->dma_used) {
		/* Enable DMA Channel 0/1 as SSI0 Tx and Rx */
		val	 = amba_readl(AHB_SCRATCHPAD_REG(0x0c));
		val	&= 0xff9fffff;
		val	|= 0x00200000;
		amba_writel(AHB_SCRATCHPAD_REG(0x0c), val);
	}

	chan_id = 2 * master->bus_num;
	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);
	bus->txc = dma_request_channel(mask, ambarella_spi_dma_filter, &chan_id);
	if (!bus->txc) {
		dev_err(&master->dev, "can't request DMA channel %d\n", chan_id);
		return -EPERM;
	}

	chan_id = 2 * master->bus_num + 1;
	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);
	bus->rxc = dma_request_channel(mask, ambarella_spi_dma_filter, &chan_id);
	if (!bus->rxc) {
		dev_err(&master->dev, "can't request DMA channel %d\n", chan_id);
		return -EPERM;
	}

	return 0;
}

static int ambarella_spi_probe(struct platform_device *pdev)
{
	struct resource 			*res;
	void __iomem				*reg;
	struct ambarella_spi			*bus;
	struct spi_master			*master;
	struct device_node			*nc;
	int					err = 0;
	int					irq;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		err = -EINVAL;
		goto ambarella_spi_probe_exit;
	}

	reg = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!reg) {
		err = -ENOMEM;
		goto ambarella_spi_probe_exit;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "no irq resource!\n");
		return -ENODEV;
	}

	master = spi_alloc_master(&pdev->dev, sizeof(*bus));
	if (!master) {
		err = -ENOMEM;
		goto ambarella_spi_probe_exit;
	}

	bus				= spi_master_get_devdata(master);
	bus->phys		= res->start;
	bus->virt			= (u32)reg;
	bus->irq			= irq;

	err = ambarella_spi_of_parse(pdev, master);
	if (err < 0) {
		goto ambarella_spi_probe_exit;
	}

	clk_set_rate(bus->clk, bus->clk_freq);
	amba_writel(reg + SPI_SSIENR_OFFSET, 0);
	amba_writel(reg + SPI_IMR_OFFSET, 0);

	master->dev.of_node		= pdev->dev.of_node;
	master->mode_bits		= SPI_CPHA | SPI_CPOL;
	master->transfer_one_message	= ambarella_spi_one_message;
	platform_set_drvdata(pdev, master);

	err = spi_register_master(master);
	if (err) {
		goto ambarella_spi_probe_exit;
	}

	/* check if hw only supports msb first tx/rx */
	if (of_find_property(pdev->dev.of_node, "amb,msb-first-only", NULL)) {
		bus->msb_first_only = 1;
		dev_info(&pdev->dev,"SPI[%d] only supports msb first tx-rx\n", master->bus_num);
	} else {
		bus->msb_first_only = 0;
	}

	/* check if using dma */
	if (of_find_property(pdev->dev.of_node, "amb,dma-used", NULL)) {
		bus->dma_used = 1;
		dev_info(&pdev->dev,"SPI[%d] uses DMA\n", master->bus_num);
	} else {
		bus->dma_used = 0;
	}

	for_each_available_child_of_node(master->dev.of_node, nc) {
		struct spi_device	*spi;

		spi = ambarella_spi_of_find_device(nc);
		if (!spi) {
			continue;
		}

		if (gpio_is_valid(spi->cs_gpio)) {
			err = devm_gpio_request(&pdev->dev,
					spi->cs_gpio, dev_name(&spi->dev));
			if (err < 0) {
				dev_err(&pdev->dev, "can't get CS: %d\n", err);
				goto ambarella_spi_probe_exit;
			}
			gpio_direction_output(spi->cs_gpio, 1);
			bus->cs_pins[spi->chip_select] = spi->cs_gpio;
		} else {
			bus->cs_pins[spi->chip_select] = -1;
		}
	}

	if (bus->dma_used) {
		err = ambarella_spi_dma_channel_allocate(master);
		if (err < 0)
			goto ambarella_spi_probe_exit;
	} else {
		/* request IRQ */
		err = devm_request_irq(&pdev->dev, irq, ambarella_spi_isr,
				IRQF_TRIGGER_HIGH, dev_name(&pdev->dev), bus);
		if (err)
			goto ambarella_spi_probe_exit;

		tasklet_init(&bus->tasklet, ambarella_spi_tasklet, (unsigned long)bus);
	}

	dev_info(&pdev->dev, "Ambarella spi controller %d created.\r\n", master->bus_num);

ambarella_spi_probe_exit:
	return err;
}

static int ambarella_spi_remove(struct platform_device *pdev)
{
	struct spi_master		*master;
	struct ambarella_spi		*bus;

	master	= platform_get_drvdata(pdev);
	bus	= spi_master_get_devdata(master);

	if (!bus->dma_used)
		tasklet_kill(&bus->tasklet);

	ambarella_spi_stop(bus);
	spi_unregister_master(master);

	return 0;
}

int ambarella_spi_write(amba_spi_cfg_t *spi_cfg, amba_spi_write_t *spi_w)
{
	int					err = 0;
	struct spi_master			*master;
	struct spi_device			spi;
	struct ambarella_spi			*bus;

	master = spi_busnum_to_master(spi_w->bus_id);
	if (!master) {
		err = -EINVAL;
		goto ambarella_spi_write_exit;
	}

	bus = spi_master_get_devdata(master);

	spi.master		= master;
	spi.bits_per_word	= spi_cfg->cfs_dfs;
	spi.mode		= spi_cfg->spi_mode;
	spi.max_speed_hz	= spi_cfg->baud_rate;
	spi.chip_select		= spi_w->cs_id;
	spi.cs_gpio		= bus->cs_pins[spi.chip_select];

	err = spi_write_then_read(&spi, spi_w->buffer, spi_w->n_size, NULL, 0);

ambarella_spi_write_exit:
	return err;
}
EXPORT_SYMBOL(ambarella_spi_write);

int ambarella_spi_read(amba_spi_cfg_t *spi_cfg, amba_spi_read_t *spi_r)
{
	int					err = 0;
	struct spi_master			*master;
	struct spi_device			spi;
	struct ambarella_spi			*bus;

	master = spi_busnum_to_master(spi_r->bus_id);
	if (!master) {
		err = -EINVAL;
		goto ambarella_spi_read_exit;
	}

	bus = spi_master_get_devdata(master);

	spi.master		= master;
	spi.bits_per_word	= spi_cfg->cfs_dfs;
	spi.mode		= spi_cfg->spi_mode;
	spi.max_speed_hz	= spi_cfg->baud_rate;
	spi.chip_select		= spi_r->cs_id;
	spi.cs_gpio		= bus->cs_pins[spi.chip_select];

	err = spi_write_then_read(&spi, NULL, 0, spi_r->buffer, spi_r->n_size);

ambarella_spi_read_exit:
	return err;
}
EXPORT_SYMBOL(ambarella_spi_read);

int ambarella_spi_write_then_read(amba_spi_cfg_t *spi_cfg,
	amba_spi_write_then_read_t *spi_wtr)
{
	int					err = 0;
	struct spi_master			*master;
	struct spi_device			spi;
	struct ambarella_spi			*bus;

	master = spi_busnum_to_master(spi_wtr->bus_id);
	if (!master) {
		err = -EINVAL;
		goto ambarella_spi_write_then_read_exit;
	}

	bus = spi_master_get_devdata(master);

	spi.master		= master;
	spi.bits_per_word	= spi_cfg->cfs_dfs;
	spi.mode		= spi_cfg->spi_mode;
	spi.max_speed_hz	= spi_cfg->baud_rate;
	spi.chip_select		= spi_wtr->cs_id;
	spi.cs_gpio		= bus->cs_pins[spi.chip_select];

	err = spi_write_then_read(&spi,
		spi_wtr->w_buffer, spi_wtr->w_size,
		spi_wtr->r_buffer, spi_wtr->r_size);

ambarella_spi_write_then_read_exit:
	return err;
}
EXPORT_SYMBOL(ambarella_spi_write_then_read);

int ambarella_spi_write_and_read(amba_spi_cfg_t *spi_cfg,
	amba_spi_write_and_read_t *spi_war)
{
	int					err = 0;
	struct spi_master			*master;
	struct spi_device			spi;
	struct ambarella_spi			*bus;
	struct spi_message			msg;
	struct spi_transfer			x[1];

	master = spi_busnum_to_master(spi_war->bus_id);
	if (!master) {
		err = -EINVAL;
		goto ambarella_spi_write_and_read_exit;
	}

	bus = spi_master_get_devdata(master);

	spi.master		= master;
	spi.bits_per_word	= spi_cfg->cfs_dfs;
	spi.mode		= spi_cfg->spi_mode;
	spi.max_speed_hz	= spi_cfg->baud_rate;
	spi.chip_select		= spi_war->cs_id;
	spi.cs_gpio		= bus->cs_pins[spi.chip_select];

	spi_message_init(&msg);
	memset(x, 0, sizeof x);
	x->tx_buf	= spi_war->w_buffer;
	x->rx_buf	= spi_war->r_buffer;
	x->len		= spi_war->n_size;
	spi_message_add_tail(x, &msg);

	err = spi_sync(&spi, &msg);

ambarella_spi_write_and_read_exit:
	return err;
}
EXPORT_SYMBOL(ambarella_spi_write_and_read);

static const struct of_device_id ambarella_spi_dt_ids[] = {
	{.compatible = "ambarella,spi", },
	{},
};
MODULE_DEVICE_TABLE(of, ambarella_spi_dt_ids);

static struct platform_driver ambarella_spi_driver = {
	.probe		= ambarella_spi_probe,
	.remove		= ambarella_spi_remove,
	.driver		= {
		.name	= "ambarella-spi",
		.owner	= THIS_MODULE,
		.of_match_table = ambarella_spi_dt_ids,
	},
};

static int ambarella_spi_init(void)
{
	return platform_driver_register(&ambarella_spi_driver);
}

static void ambarella_spi_exit(void)
{
	platform_driver_unregister(&ambarella_spi_driver);
}

module_init(ambarella_spi_init);
module_exit(ambarella_spi_exit);

MODULE_DESCRIPTION("Ambarella Media Processor SPI Bus Controller");
MODULE_AUTHOR("Zhenwu Xue, <zwxue@ambarella.com>");
MODULE_LICENSE("GPL");
