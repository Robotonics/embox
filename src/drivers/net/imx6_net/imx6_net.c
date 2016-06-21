/**
 * @file imx6_net.c
 * @brief iMX6 MAC-NET driver
 * @author Denis Deryugin <deryugin.denis@gmail.com>
 * @version 0.1
 * @date 2016-05-11
 */

#include <assert.h>
#include <errno.h>
#include <string.h>

#include <hal/reg.h>

#include <kernel/irq.h>
#include <kernel/printk.h>

#include <net/inetdevice.h>
#include <net/l0/net_entry.h>
#include <net/l2/ethernet.h>
#include <net/netdevice.h>
#include <net/skbuff.h>

#include <util/log.h>

#include <embox/unit.h>

#include <framework/mod/options.h>

#include "imx6_net.h"

static void _reg_dump(void) {
	log_debug("ENET_EIR  %10x", REG32_LOAD(ENET_EIR ));
	log_debug("ENET_EIMR %10x", REG32_LOAD(ENET_EIMR));
	log_debug("ENET_RDAR %10x", REG32_LOAD(ENET_RDAR));
	log_debug("ENET_TDAR %10x", REG32_LOAD(ENET_TDAR));
	log_debug("ENET_ECR  %10x", REG32_LOAD(ENET_ECR ));
	log_debug("ENET_MSCR %10x", REG32_LOAD(ENET_MSCR));
	log_debug("ENET_RCR  %10x", REG32_LOAD(ENET_RCR ));
	log_debug("ENET_TCR  %10x", REG32_LOAD(ENET_TCR ));
	log_debug("MAC_LOW   %10x", REG32_LOAD(MAC_LOW  ));
	log_debug("MAC_HI    %10x", REG32_LOAD(MAC_HI   ));
	log_debug("ENET_IAUR %10x", REG32_LOAD(ENET_IAUR));
	log_debug("ENET_IALR %10x", REG32_LOAD(ENET_IALR));
	log_debug("ENET_GAUR %10x", REG32_LOAD(ENET_GAUR));
	log_debug("ENET_GALR %10x", REG32_LOAD(ENET_GALR));
	log_debug("ENET_TFWR %10x", REG32_LOAD(ENET_TFWR));
	log_debug("ENET_RDSR %10x", REG32_LOAD(ENET_RDSR));
	log_debug("ENET_TDSR %10x", REG32_LOAD(ENET_TDSR));
	log_debug("ENET_MRBR %10x", REG32_LOAD(ENET_MRBR));
}

static uint8_t _macaddr[6];

static void _write_macaddr(void) {
	uint32_t mac_hi, mac_lo;

	mac_hi  = (_macaddr[5] << 16) |
	          (_macaddr[4] << 24);
	mac_lo  = (_macaddr[3] <<  0) |
	          (_macaddr[2] <<  8) |
	          (_macaddr[1] << 16) |
	          (_macaddr[0] << 24);

	REG32_STORE(MAC_LOW, mac_lo);
	REG32_STORE(MAC_HI, mac_hi);
}

static struct imx6_buf_desc _tx_desc_ring[TX_BUF_FRAMES];
static struct imx6_buf_desc _rx_desc_ring[RX_BUF_FRAMES];

static uint8_t _tx_buf[TX_BUF_LEN] __attribute__ ((aligned(16)));
static uint8_t _rx_buf[RX_BUF_LEN] __attribute__ ((aligned(16)));

//extern void dcache_inval(const void *p, size_t size);
//extern void dcache_flush(const void *p, size_t size);

#define dcache_flush(x, y) ;
#define dcache_inval(x, y) ;

static int imx6_net_xmit(struct net_device *dev, struct sk_buff *skb) {
	uint8_t *data;
	int desc_num;
	struct imx6_buf_desc *desc;

	ipl_t sp;

	assert(dev);
	assert(skb);

	sp = ipl_save();
	{
		data = (uint8_t*) skb_data_cast_in(skb->data);

		if (!data)
			return -1;

		skb_free(skb);

		desc_num = 0;
		memcpy(&_tx_buf[0], data, skb->len);
		dcache_flush(&_tx_buf[0], skb->len);

		desc = &_tx_desc_ring[desc_num];

		desc->data_pointer = (uint32_t) &_tx_buf[0];
		desc->len          = skb->len;
		desc->flags1      |= FLAG_W;
		desc->flags1      |= FLAG_R | FLAG_L | FLAG_TC;
		dcache_flush(desc, sizeof(struct imx6_buf_desc));

		printk("Frame status %#4x\n", desc->flags1);
		int t = 0xFFFFF;
		while (t--);

		REG32_STORE(ENET_TDAR, 0x01000000);

		t = 0xFFFFF;
		while(t--) {
			if (!(REG32_LOAD(ENET_TDAR) & 0x01000000))
				break;
			else
				printk("wait\n");
		}

		printk("Frame status %#4x\n", desc->flags1);

		t = 0xFFFFF;
		while(t--);
		printk("Frame status %#4x\n", desc->flags1);
	}
	ipl_restore(sp);

	_reg_dump();

	return 0;
}

static void _init_buffers() {
	struct imx6_buf_desc *desc;

	memset(&_tx_desc_ring[0], 0,
	        TX_BUF_FRAMES * sizeof(struct imx6_buf_desc));
	memset(&_rx_desc_ring[0], 0,
	        RX_BUF_FRAMES * sizeof(struct imx6_buf_desc));

	/* Mark last buffer (i.e. set wrap flag) */
	_rx_desc_ring[RX_BUF_FRAMES - 1].flags1 = FLAG_W;
	_tx_desc_ring[TX_BUF_FRAMES - 1].flags1 = FLAG_W;

	for (int i = 0; i < RX_BUF_FRAMES; i++) {
		desc = &_rx_desc_ring[i];
		desc->data_pointer = (uint32_t) _rx_buf[i * FRAME_LEN];
		desc->flags1 |= FLAG_E;
		desc->flags2 |= FLAG_INT_RX;
	}

	for (int i = 0; i < TX_BUF_FRAMES; i++) {
		_tx_desc_ring[i].flags2 |= FLAG_INT_TX;
	}

	dcache_flush(&_tx_desc_ring[0],
	              TX_BUF_FRAMES * sizeof(struct imx6_buf_desc));
	dcache_flush(&_rx_desc_ring[0],
	              RX_BUF_FRAMES * sizeof(struct imx6_buf_desc));
}

static void _setup_mii_speed(void) {
	/* Value written by u-boot */
	REG32_STORE(ENET_MSCR, 0x14);
}

static void _reg_setup(void) {
	uint32_t t;

	/* Disable IRQ */
	REG32_STORE(ENET_EIMR, 0);

	/* Clear pending interrupts */
	REG32_STORE(ENET_EIR, EIR_MASK);

	/* Setup RX */
	t  = FRAME_LEN << FRAME_LEN_OFFSET;
	t |= RCR_FCE | RCR_MII_MODE;
	REG32_STORE(ENET_RCR, t);

	_setup_mii_speed();

	/* Enable IRQ */
	REG32_STORE(ENET_EIMR, 0x7FFF0000);
}

static void _reset(void) {
	REG32_STORE(ENET_ECR, RESET);
	_reg_dump();
	_reg_setup();


	REG32_STORE(ENET_TCR, (1 << 2));
	/* TODO set FEC clock? */

	REG32_STORE(ENET_IAUR, 0);
	REG32_STORE(ENET_IALR, 0);
	REG32_STORE(ENET_GAUR, 0);
	REG32_STORE(ENET_GALR, 0);

	_init_buffers();

	REG32_STORE(ENET_EIMR, EIMR_TXF | EIMR_RXF | EIR_MASK);

	_write_macaddr();

	REG32_STORE(ENET_TFWR, (1 << 8));
	REG32_STORE(ENET_ECR, ETHEREN | ECR_DBSWP); /* Note: should be last init step */
}

static int imx6_net_open(struct net_device *dev) {
	//_reset();
	uint32_t t;

	_write_macaddr();

	_init_buffers();
	_reg_setup();

	REG32_STORE(ENET_OPD, 0x00010020);
	REG32_STORE(ENET_TFWR, 0x2);

	REG32_STORE(ENET_GAUR, 0x0);
	REG32_STORE(ENET_GALR, 0x0);

	assert((RX_BUF_LEN & 0xF) == 0);
	REG32_STORE(ENET_MRBR, RX_BUF_LEN);

	assert((((uint32_t) &_tx_desc_ring[0]) & 0xF) == 0);
	REG32_STORE(ENET_TDSR, ((uint32_t) &_tx_desc_ring[0]));

	assert((((uint32_t) &_rx_desc_ring[0]) & 0xF) == 0);
	REG32_STORE(ENET_RDSR, ((uint32_t) &_rx_desc_ring[0]));

	//miiphy_restart_aneg

	/* Full duplex, heartbeat disabled */
	REG32_STORE(ENET_TCR, TCR_FDEN);
	/* TODO setup RX buf status */

	t  = REG32_LOAD(ENET_ECR);
	t |= ECR_DBSWP;
	REG32_STORE(ENET_ECR, t);

	t  = REG32_LOAD(ENET_TFWR);
	t |= TFWR_STRFWD;
	REG32_STORE(ENET_TFWR, t);

	t  = REG32_LOAD(ENET_ECR);
	t |= ETHEREN;
	REG32_STORE(ENET_ECR, t); /* Note: should be last ENET-related init step */

	t = 0xFFFFF;
	while(t--);

	/* Some u-boot magic */
	REG32_STORE(NIC_BASE + 0x308, 0);

	t = 0xFFFFF;
	while(t--);

	REG32_STORE(NIC_BASE + 0x300, 1);

	REG32_STORE(NIC_BASE + 0x308, 1 << 1);

	t = 0xFFFFF;
	while(t--);

	/* Phy start up miss */

	t  = REG32_LOAD(ENET_ECR);
	t &= ~(1 << 5);
	REG32_STORE(ENET_ECR, t);

	t  = REG32_LOAD(ENET_RCR);
	t |= (0x200);
	REG32_STORE(ENET_RCR, t);

	REG32_STORE(ENET_RDAR, (1 << 24));

	t = 0xFFFFFF;
	printk("Last NIC init delay...\n");
	while(t--);
	return 0;
}

static int imx6_net_set_macaddr(struct net_device *dev, const void *addr) {
	assert(dev);
	assert(addr);

	memcpy(&_macaddr[0], (uint8_t *) addr, 6);
	_write_macaddr();

	return 0;
}

static irq_return_t imx6_irq_handler(unsigned int irq_num, void *dev_id) {
	uint32_t state;

	state = REG32_LOAD(ENET_EIR);

	log_debug("Interrupt mask %#08x\n", state);

	if (state & EIR_EBERR) {
		log_error("Ethernet bus error, resetting ENET!\n");
		REG32_STORE(ENET_ECR, RESET);
		_reset();

		return IRQ_HANDLED;
	}

	if (state & EIR_RXB) {
		int id = 0;
		log_debug("RX interrupt; len %d bytes; status %#010x\n", _rx_desc_ring[id].len, _rx_desc_ring[id].flags1);

		REG32_STORE(ENET_EIR, EIR_RXB);
	}

	if (state & (EIR_TXB | EIR_TXF)) {
		int id = 0;
		log_debug("finished TX; len %d bytes; status %#010x\n", _tx_desc_ring[id].len, _tx_desc_ring[id].flags1);

		REG32_STORE(ENET_EIR, EIR_TXB | EIR_TXF);
	}

	return IRQ_HANDLED;
}

static const struct net_driver imx6_net_drv_ops = {
	.xmit = imx6_net_xmit,
	.start = imx6_net_open,
	.set_macaddr = imx6_net_set_macaddr
};

EMBOX_UNIT_INIT(imx6_net_init);
static int imx6_net_init(void) {
	_reg_dump();
	struct net_device *nic;
	int tmp;

	if (NULL == (nic = etherdev_alloc(0))) {
                return -ENOMEM;
        }

	nic->drv_ops = &imx6_net_drv_ops;

	tmp = irq_attach(ENET_IRQ, imx6_irq_handler, 0, nic, "i.MX6 enet");
	if (tmp)
		return tmp;
	REG32_STORE(ENET_ECR, RESET);
	_reg_setup();

	/* Here u-boot configures PHY layer, we miss it */

	return inetdev_register_dev(nic);
}
