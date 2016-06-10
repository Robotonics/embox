/**
 * @file
 *
 * @data 01.04.2016
 * @author: Anton Bondarev
 */

#include <drivers/diag.h>
#include <framework/mod/options.h>
#include <hal/reg.h>
#include <kernel/printk.h>

#define UART_BASE OPTION_GET(NUMBER, base_addr)
#define UART(x)   (*(volatile unsigned long *)(UART_BASE + (x)))

#define USR2_RDR   1
#define USR2_TXFE (1 << 14)

#define RXR  0x00
#define TXR  0x40
#define UCR1 0x80
#define UCR2 0x84
#define UCR3 0x88
#define UCR4 0x8C
#define UFCR 0x90
#define USR2 0x98
#define UBIR 0xA4
#define UBMR 0xA8

#define UCR1_UARTEN (1 << 0)
#define UCR2_TXEN   (1 << 2)
#define UCR2_RXEN   (1 << 1)

static int imxuart_diag_init(const struct diag *diag) {
	//UART(UCR1) = UCR1_UARTEN;
	//UART(UCR2) = 0x2127;//|= UCR2_RXEN | UCR2_TXEN;
	//UART(UCR3) = 0x0704;
	//UART(UCR4) = 0x7C00;
	return 0;
}

static int imxuart_diag_hasrx(const struct diag *diag) {
	return UART(USR2) & USR2_RDR;
}

static char imxuart_diag_getc(const struct diag *diag) {
	return (char)UART(RXR);
}

static void imxuart_diag_putc(const struct diag *diag, char ch) {
	while (!(UART(USR2) & USR2_TXFE)) {
	}

	UART(TXR) = ch;
}

DIAG_OPS_DECLARE(
	.init = imxuart_diag_init,
	.putc = imxuart_diag_putc,
	.getc = imxuart_diag_getc,
	.kbhit = imxuart_diag_hasrx,
);