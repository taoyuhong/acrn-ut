/*
 * Copyright 2010 Siemens AG
 * Author: Jan Kiszka
 *
 * Released under GPLv2.
 */

#include "libcflat.h"
#include "x86/desc.h"
#include "vmalloc.h"
#include "alloc.h"
#include "processor.h"

#define TARGET_TSS		(FIRST_SPARE_SEL + 16)
#define ERROR_ADDR	0x5fff0000

struct tss_desc {
	u16 limit_low;
	u16 base_low;
	u8 base_middle;
	u8 type:4;
	u8 system:1;
	u8 dpl:2;
	u8 p:1;
	u8 granularity;
	u8 base_high;
} *desc_intr = (void *)&gdt32[TSS_INTR >> 3],
    *desc_main = (void *)&gdt32[TSS_MAIN >> 3],
    *desc_target = (void *)&gdt32[TARGET_TSS >> 3];

struct tss_desc desc_intr_backup, desc_cur_backup;

struct task_gate {
	u16 resv_0;
	u16 selector;
	u8 resv_1:8;
	u8 type:4;
	u8 system:1;
	u8 dpl:2;
	u8 p:1;
	u16 resv_2;
} *gate = (void *)&boot_idt[6];	/* #UD */

//#define dbg_printf(...)	printf(__VA_ARGS__)
#define dbg_printf(...)

static unsigned long rip_skip = 0;

void gp_handler(struct ex_regs *regs)
{
	dbg_printf("rip->(%lx + %lx)! ", regs->rip, rip_skip);
	regs->rip += rip_skip;
	rip_skip = 0;

	if ((regs->error_code & 0xf000)
	    && ((regs->error_code & 0xfff) == TARGET_TSS))
		printf("#GP(%lx)<-HV\n", regs->error_code & 0x0fff);
	else
		printf("#GP(%lx)\n", regs->error_code);
}

void np_handler(struct ex_regs *regs)
{
	regs->rip += rip_skip;
	rip_skip = 0;
	printf("#NP(%lx)\n", regs->error_code);
}

void pf_handler(struct ex_regs *regs)
{
	regs->rip += rip_skip;
	rip_skip = 0;
	printf("#PF(%lx)\n", read_cr2());
}

void ts_handler(struct ex_regs *regs)
{
	regs->rip += rip_skip;
	rip_skip = 0;
	printf("#TS(%lx)\n", regs->error_code);
}

/* gate conditions, 0:false 1:tr */
void reset_gate(void)
{
	*gate = *((struct task_gate *)(&boot_idt[6]));
	gate->type = 0x5;
	gate->p = 1;
	gate->dpl = 2;
	gate->selector = TARGET_TSS;
}

void reset_desc(void)
{
	*desc_target = *desc_intr;
	desc_target->dpl = 2;

	desc_main->base_low = desc_cur_backup.base_low;
	desc_main->base_middle = desc_cur_backup.base_middle;
	desc_main->base_high = desc_cur_backup.base_high;
}

/* cond 0 */
void cond_0_gate_resv(int violation)
{
	dbg_printf("%s\n", __FUNCTION__);

	if (violation) {
		/* interleave of 0 & 1 */
		gate->resv_0 = 0xaaaa;
		gate->resv_1 = 0xaa;
		gate->resv_2 = 0xaaaa;
	}
}

/* cond 1 */
void cond_1_gate_present(int violation)
{
	dbg_printf("%s\n", __FUNCTION__);

	if (violation)
		gate->p = 0;
}

/* cond 2 */
void cond_2_gate_null_sel(int violation)
{
	dbg_printf("%s\n", __FUNCTION__);

	if (violation)
		gate->selector = 0;
}

/* cond 3 */
void cond_3_desc_ti(int violation)
{
	dbg_printf("%s\n", __FUNCTION__);

	if (violation)
		gate->selector |= 0x4;
}

/* cond 4 */
void cond_4_desc_type(int violation)
{
	dbg_printf("%s\n", __FUNCTION__);

	if (violation)
		desc_target->type = 0;
}

/* cond 5 */
void cond_5_desc_present(int violation)
{
	dbg_printf("%s\n", __FUNCTION__);

	if (violation)
		desc_target->p = 0;
}

/* cond 6 */
void cond_6_desc_bit_22_21(int violation)
{
	dbg_printf("%s\n", __FUNCTION__);

	if (violation)
		desc_target->granularity |= 0x60;
}

/* cond 7 */
void cond_7_desc_g_limit(int violation)
{
	dbg_printf("%s\n", __FUNCTION__);

	if (violation) {
		desc_target->granularity &= 0x70;
		desc_target->limit_low = 0x66;
	}
}

/* cond 8 */
void cond_8_desc_busy(int violation)
{
	dbg_printf("%s\n", __FUNCTION__);

	if (violation)
		desc_target->type |= 0x2;
}

/* cond 9 */
void cond_9_desc_tss_base(int violation)
{
	dbg_printf("%s\n", __FUNCTION__);

	if (violation) {
		desc_target->base_low = ERROR_ADDR & 0xffff;
		desc_target->base_middle = (ERROR_ADDR >> 16) & 0xff;
		desc_target->base_high = (ERROR_ADDR >> 24) & 0xff;
	}
}

/* cond 10 */
void cond_10_desc_tss_readonly(int violation)
{
	dbg_printf("%s\n", __FUNCTION__);

	if (violation) {
		desc_target->type |= 0x2;
	}
}

/* cond 11 */
void cond_11_desc_cur_base(int violation)
{
	dbg_printf("%s\n", __FUNCTION__);

	if (violation) {
		desc_main->base_low = ERROR_ADDR & 0xffff;
		desc_main->base_middle = (ERROR_ADDR >> 16) & 0xff;
		desc_main->base_high = (ERROR_ADDR >> 24) & 0xff;
	}
}

void (*cond_funs[]) (int) = {
	    cond_0_gate_resv,
	    cond_1_gate_present,
	    cond_2_gate_null_sel,
	    cond_3_desc_ti,
	    cond_4_desc_type,
	    cond_5_desc_present,
	    cond_6_desc_bit_22_21,
	    cond_7_desc_g_limit,
	    cond_8_desc_busy,
	    cond_9_desc_tss_base,
	    cond_10_desc_tss_readonly,
	    cond_11_desc_cur_base,
};

#define NCOND	(sizeof(cond_funs)/sizeof(void (*)(int)))

void set_violation_map(u32 vmap)
{
	int i;

	for (i = 0; i < NCOND; i++) {
		if ((vmap >> i) & 0x1)
			cond_funs[i] (1);
	}

	for (i = 0; i < NCOND; i++) {
		if ((vmap >> i) & 0x1) {
			printf("1 ");
		} else
			printf("0 ");
	}
}

void gen_ud(const char *arg)
{
	extern void ud_begin(void);
	extern void ud_end(void);

	dbg_printf("Gen #UD with CS: %x\n", read_cs());
	rip_skip = ud_end - ud_begin;
	asm volatile (" ud_begin:\n" "    .word 0xffff\n" " ud_end:\n");
}

void test_cond(u32 vmap)
{
	set_violation_map(vmap);

	do_less_privilege(gen_ud, NULL, 2);
}

int main(int ac, char **av)
{
	u32 vmap = 0;

	setup_vm();
	setup_idt();
	setup_tss32();
	init_do_less_privilege();

	handle_exception(13, gp_handler);
	handle_exception(14, pf_handler);
	handle_exception(10, ts_handler);
	handle_exception(11, np_handler);

	desc_intr_backup = *desc_intr;
	desc_cur_backup = *desc_main;

	for (vmap = 0x0; vmap < (0x1 << NCOND); vmap++) {
		reset_gate();
		reset_desc();
		test_cond(vmap);
	}
/*
	for (vmap = 0x0; vmap < NCOND; vmap++) {
		reset_gate();
		reset_desc();
		test_cond(0x1 << vmap);
	}
*/
	return 0;
}
