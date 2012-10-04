/* $OpenBSD: prcmvar.h,v 1.4 2010/02/13 06:03:37 drahn Exp $ */
/*
 * Copyright (c) 2007,2009 Dale Rahn <drahn@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

void prcm_setclock(int clock, int speed);
#define PRCM_CLK_SPEED_32	0
#define PRCM_CLK_SPEED_SYS	1

void prcm_enableclock(int bit);

/* XXX - verify these definitions do these need a mapping betwen omap* */
#define PRCM_REG_CORE_CLK1_BASE	(PRCM_REG_CORE_CLK1*32)
#define		PRCM_CLK_EN_MMC3	(PRCM_REG_CORE_CLK1_BASE + 30)
#define		PRCM_CLK_EN_ICR		(PRCM_REG_CORE_CLK1_BASE + 29)
#define		PRCM_CLK_EN_AES2 	(PRCM_REG_CORE_CLK1_BASE + 28)
#define		PRCM_CLK_EN_SHA12 	(PRCM_REG_CORE_CLK1_BASE + 27)
#define		PRCM_CLK_EN_DES2 	(PRCM_REG_CORE_CLK1_BASE + 26)
#define		PRCM_CLK_EN_MMC2	(PRCM_REG_CORE_CLK1_BASE + 25)
#define		PRCM_CLK_EN_MMC1	(PRCM_REG_CORE_CLK1_BASE + 24)
#define		PRCM_CLK_EN_MSPRO	(PRCM_REG_CORE_CLK1_BASE + 23)
#define		PRCM_CLK_EN_HDQ		(PRCM_REG_CORE_CLK1_BASE + 22)
#define		PRCM_CLK_EN_MCSPI4	(PRCM_REG_CORE_CLK1_BASE + 21)
#define		PRCM_CLK_EN_MCSPI3	(PRCM_REG_CORE_CLK1_BASE + 20)
#define		PRCM_CLK_EN_MCSPI2	(PRCM_REG_CORE_CLK1_BASE + 19)
#define		PRCM_CLK_EN_MCSPI1	(PRCM_REG_CORE_CLK1_BASE + 18)
#define		PRCM_CLK_EN_I2C3	(PRCM_REG_CORE_CLK1_BASE + 17)
#define		PRCM_CLK_EN_I2C2	(PRCM_REG_CORE_CLK1_BASE + 16)
#define		PRCM_CLK_EN_I2C1	(PRCM_REG_CORE_CLK1_BASE + 15)
#define		PRCM_CLK_EN_UART2	(PRCM_REG_CORE_CLK1_BASE + 14)
#define		PRCM_CLK_EN_UART1	(PRCM_REG_CORE_CLK1_BASE + 13)
#define		PRCM_CLK_EN_GPT11	(PRCM_REG_CORE_CLK1_BASE + 12)
#define		PRCM_CLK_EN_GPT10	(PRCM_REG_CORE_CLK1_BASE + 11)
#define		PRCM_CLK_EN_MCBSP5	(PRCM_REG_CORE_CLK1_BASE + 10)
#define		PRCM_CLK_EN_MCBSP1	(PRCM_REG_CORE_CLK1_BASE + 9)
#define		PRCM_CLK_EN_MAILBOXES 	(PRCM_REG_CORE_CLK1_BASE + 7)
#define		PRCM_CLK_EN_OMAPCTRL 	(PRCM_REG_CORE_CLK1_BASE + 6)
#define		PRCM_CLK_EN_HSOTGUSB 	(PRCM_REG_CORE_CLK1_BASE + 4)
#define		PRCM_CLK_EN_SDRC 	(PRCM_REG_CORE_CLK1_BASE + 1)

#define PRCM_REG_CORE_CLK1	0
#define PRCM_REG_CORE_CLK2	1
#define PRCM_REG_CORE_CLK3	2

#define PRCM_REG_USBHOST	3
#define PRCM_REG_USBHOST_BASE	(PRCM_REG_USBHOST*32)
#define PRCM_CLK_EN_USB		(PRCM_REG_USBHOST_BASE + 0)

#define O4_MAX_MODULE_ENABLE_WAIT    1000

#define O4_CLKCTRL_IDLEST_MASK           0x00030000UL
#define O4_CLKCTRL_IDLEST_ENABLED        0x00000000UL

/*XXX - is this really constant across omap* */
#define	PRCM_REG_CORE_CLK3_BASE (PRCM_REG_CORE_CLK3*32)
#define		CM_CORE_EN_USBTLL       (PRCM_REG_CORE_CLK3_BASE + 2)
#define		CM_CORE_EN_TS           (PRCM_REG_CORE_CLK3_BASE + 1)
#define		CM_CORE_EN_CPEFUSE      (PRCM_REG_CORE_CLK3_BASE + 0)

/* need interface for CM_AUTOIDLE */

uint32_t prcm_pcnf1_get_value(uint32_t reg);
void prcm_pcnf1_set_value(uint32_t reg, uint32_t val);
uint32_t prcm_pcnf2_get_value(uint32_t reg);
void prcm_pcnf2_set_value(uint32_t reg, uint32_t val);

uint32_t prcm_scrm_get_value(uint32_t reg);
void prcm_scrm_set_value(uint32_t reg, uint32_t val);
int prcm_hsusbhost_deactivate(int);
int prcm_hsusbhost_activate(int);

#define	USBHSHOST_CLK	0
#define	USBP1_PHY_CLK	1
#define	USBP2_PHY_CLK	2
#define EXT_CLK		0

int prcm_hsusbhost_set_source(int, int);
