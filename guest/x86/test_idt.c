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
	printf("CS: %x, rip: %lx\n", read_cs(), regs->rip);
	regs->rip += rip_skip;
	rip_skip = 0;

	printf("#GP(%lx) happen!\n", regs->error_code);
}

/* use #UD as Benign Exception */
void ud_handler(struct ex_regs *regs)
{
	printf("#UD rip:%lx CS:%x\n", regs->rip, read_cs());
	regs->rip += rip_skip;
	rip_skip = 0;

	printf("#UD happen, jump to:%lx!\n", regs->rip);
}

void np_2nd_handler(struct ex_regs *regs)
{
	regs->rip += rip_skip;
	rip_skip = 0;

	printf("#NP(%lx) happen!\n", regs->error_code);
}

static void* pf_ret_addr = NULL;

void pf_handler(struct ex_regs *regs)
{
	printf("#PF(%lx) happen!, rip:%lx\n", read_cr2(), regs->rip);
	if (pf_ret_addr)
		regs->rip = (uint32_t)pf_ret_addr;
	else
		regs->rip += rip_skip;
	rip_skip = 0;
	pf_ret_addr = NULL;
}

static void show_idt(uint32_t vector)
{
	printf("boot_idt[%u] p:%x dpl:%x system:%x type:%x, sel:%x, b5:%x\n", vector,
			boot_idt[vector].p,
			boot_idt[vector].dpl,
			boot_idt[vector].system,
			boot_idt[vector].type,
			boot_idt[vector].selector,
			boot_idt[vector].b5);
}

void gen_ud(const char *arg)
{
	extern void ud_begin(void);
	extern void ud_end(void);

	pf_ret_addr = ud_end;

	printf("Gen #UD with CS: %x\n", read_cs());
	rip_skip = ud_end - ud_begin ;
	asm volatile (" ud_begin:\n"
		      "    .word 0xffff\n"
		      " ud_end:\n");
}

void int_ud(const char *args)
{
	asm volatile("int $6");
}

int main(int ac, char **av)
{
	setup_vm();
	setup_idt();
	init_do_less_privilege();
	setup_alt_stack();

	handle_exception(14, pf_handler);
	handle_exception(13, gp_handler);
	handle_exception(11, np_2nd_handler);
	handle_exception(6, ud_handler);

	/* Trigger a #GP */
	//rip_skip = 2;	/* rdmsr instruction takes 2 bytes */
	//msr = rdmsr(0xffffffff);

	/* When INTn, dpl(#GP) > offset(#PF) >GDT(#NP) */
	/* IDT entry check: PF > GP > NP */
	//boot_idt[6].type = 0;	/* #GP */
	//boot_idt[6].system = 1;	/* *GP */
	//boot_idt[6].p = 0;	/* #NP */
	//boot_idt[6].b5 = 28;	/* no effect */
	//boot_idt[6].dpl = 3;	/* no effect for real exception, #GP for int6 */
	//boot_idt[6].selector |= 3;	/* make no sense for both exception &  int6 */
					/* Only call sel+rpl need to consider seg-desc.dpl */

	//set_idt_entry(6,  (void*)0x5fff0000, 0); /* offet, #GP if paging disabled, else #PF */

	/* GDT related */
	gdt32[10] = gdt32[1];
	boot_idt[6].selector = 10 <<3;
	//gdt32[10].limit_low = 0x0; /* no effect */
	//gdt32[10].access &= 0x7f; /* clear P, #NP */
	//gdt32[10].access &= 0xef; /* clear S, #GP */
	//gdt32[10].access &= 0xf0; /* clear type, #GP(!IDT-flag) */
	//gdt32[10].access |= 0x60; /* DPL = 3, #GP(!IDT-flag) */
	printf("gdt32[10].limit_low: %x, gdt32[10].access:%x\n", gdt32[10].limit_low, gdt32[10].access);
	show_idt(6);

	printf("Call do_ring3() with CS: %x\n", read_cs());
	do_less_privilege(gen_ud, NULL, 3);

	//gen_ud(NULL);

	//boot_idt[6].dpl = 3;
	//do_less_privilege(int_ud, NULL, 3);

	return 0;
}


