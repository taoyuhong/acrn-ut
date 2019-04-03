/*
 * Copyright 2019 Intel
 * Author: XXX
 *
 * Released under GPLv2.
 */

#include "libcflat.h"
#include "desc.h"
#include "processor.h"

static unsigned long rip_skip = 0;

void gp_handler(struct ex_regs *regs)
{
	regs->rip += rip_skip;
	rip_skip = 0;

	printf("#GP happen!\n");
}

/* use #UD as Benign Exception */
int ud_count = 0;
void ud_handler(struct ex_regs *regs)
{
	printf("#UD rip:%lx!\n", regs->rip);
	regs->rip += rip_skip;
	rip_skip = 0;

	ud_count++;
	printf("#UD happen:%d!\n", ud_count);
}


void np_2nd_handler(struct ex_regs *regs)
{
	regs->rip += rip_skip;
	rip_skip = 0;

	printf("#NP happen!\n");
}

static void show_idt(uint32_t vector)
{
	printf("boot_idt[%u] p:%x dpl:%x system:%x type:%x, sel:%x\n", vector,
			boot_idt[vector].p,
			boot_idt[vector].dpl,
			boot_idt[vector].system,
			boot_idt[vector].type,
			boot_idt[vector].selector);
}

int main(int ac, char **av)
{
	//setup_vm();
	setup_idt();
	//setup_alt_stack();

	handle_exception(13, gp_handler);
	handle_exception(11, np_2nd_handler);
	handle_exception(6, ud_handler);

	/* Trigger a #GP */
	//rip_skip = 2;	/* rdmsr instruction takes 2 bytes */
	//msr = rdmsr(0xffffffff);

	/* IDT entry check: GP > NP */
	boot_idt[6].type = 0;	/* #GP */
	boot_idt[6].system = 1;	/* *GP */
	boot_idt[6].p = 0;	/* *NP */
	
	/* Trigger a #UD*/
	show_idt(6);
	rip_skip = 1;
	asm volatile (".byte 0x0f\n");

	return 0;
}


