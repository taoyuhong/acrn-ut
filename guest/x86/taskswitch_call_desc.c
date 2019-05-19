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
#include "vm.h"

#define TARGET_TSS		(FIRST_SPARE_SEL + 16)
#define ERROR_ADDR	0x5fff0000

u32  target_sel = TARGET_TSS <<16;

struct tss_desc {
        u16 limit_low;
        u16 base_low;
        u8 base_middle;
        u8 type :4;
        u8 system :1;
        u8 dpl :2;
        u8 p :1;
        u8 granularity;
        u8 base_high;
} *desc_intr = (void*)&gdt32[TSS_INTR >>3],
  *desc_main = (void*)&gdt32[TSS_MAIN >>3],
  *desc_target = (void*)&gdt32[TARGET_TSS >>3];

struct tss_desc desc_intr_backup, desc_cur_backup;
 
//#define dbg_printf(...)	printf(__VA_ARGS__)
#define dbg_printf(...)

static unsigned long rip_skip = 0;

void gp_handler(struct ex_regs *regs)
{
	dbg_printf("rip->(%lx + %lx)! ", regs->rip, rip_skip);
	regs->rip += rip_skip;
	rip_skip = 0;

	if ((regs->error_code & 0xf000) && ((regs->error_code & 0xfff) == TARGET_TSS))
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

/* conditions, 0:false 1:tr */

void reset_desc(void)
{
	*desc_target = *desc_intr;
	desc_target->dpl = 2;

	desc_main->base_low = desc_cur_backup.base_low;
	desc_main->base_middle = desc_cur_backup.base_middle;
	desc_main->base_high = desc_cur_backup.base_high;

	target_sel = TARGET_TSS << 16;
}

/* cond 0 */
void cond_0_sel_null(int violation)
{
	dbg_printf("%s\n", __FUNCTION__);

	if (violation) {
		target_sel = 0;
	}
}

/* cond 1 */
void cond_1_sel_ti(int violation)
{
	dbg_printf("%s\n", __FUNCTION__);

	if (violation)
		target_sel = (TARGET_TSS | 0x4) << 16;
}

/* cond 2 */
void cond_2_sel_rpl(int violation)
{
	dbg_printf("%s\n", __FUNCTION__);

	if (violation)
		target_sel = (TARGET_TSS | 0x3) << 16;
}

/* cond 3 */
void cond_3_tss_type(int violation)
{
	dbg_printf("%s\n", __FUNCTION__);

	if (violation)
		desc_target->type = 0;
}

/* cond 4 */
void cond_4_tss_present(int violation)
{
	dbg_printf("%s\n", __FUNCTION__);

	if (violation)
		desc_target->p = 0;
}

/* cound 5 */
void cond_5_tss_bit_22_21(int violation)
{
	dbg_printf("%s\n", __FUNCTION__);

	if (violation)
		desc_target->granularity |= 0x60;
}

/* cond 6 */
void cond_6_tss_g_limit(int violation)
{
	dbg_printf("%s\n", __FUNCTION__);

	if (violation) {
		desc_target->granularity &= 0x70;
		desc_target->limit_low = 0x66;
	}
}

/* cond 7 */
void cond_7_tss_busy(int violation)
{
	dbg_printf("%s\n", __FUNCTION__);

	if (violation)
		desc_target->type |= 0x2;
}

/* cond 8 */
void cond_8_tss_base(int violation)
{
	dbg_printf("%s\n", __FUNCTION__);

	if (violation) {
		desc_target->base_low = ERROR_ADDR & 0xffff;
		desc_target->base_middle = (ERROR_ADDR >> 16) & 0xff;
		desc_target->base_high = (ERROR_ADDR >> 24) & 0xff;
	}
}

/* cond 9 */
void cond_9_tss_readonly(int violation)
{
	u32 tss_addr;
	u32 page_addr;
	u32 page_off;

	dbg_printf("%s\n", __FUNCTION__);

	if (violation) {
		tss_addr = (desc_target->base_high <<24) +
				(desc_target->base_middle <<16) +
				desc_target->base_low;

		page_addr = tss_addr & (~0xfff);
		page_off = tss_addr & 0xfff;

#define RD_ONLY_PAGE        (ERROR_ADDR + 0x1000)
		install_read_only_page(phys_to_virt(read_cr3()),
					page_addr, (void*)RD_ONLY_PAGE);

		desc_target->base_low = (RD_ONLY_PAGE + page_off) & 0xffff;
		desc_target->base_middle = ((RD_ONLY_PAGE + page_off) >>16) & 0xff;
		desc_target->base_high = ((RD_ONLY_PAGE + page_off) >>24) & 0xff;
	}
}

/* cond 10 */
void cond_10_cur_base(int violation)
{
	dbg_printf("%s\n", __FUNCTION__);

	if (violation) {
		desc_main->base_low = ERROR_ADDR & 0xffff;
		desc_main->base_middle = (ERROR_ADDR >> 16) & 0xff;
		desc_main->base_high = (ERROR_ADDR >> 24) & 0xff;
	}
}

void (*cond_funs[])(int) = {
	cond_0_sel_null,
	cond_1_sel_ti,
	cond_2_sel_rpl,
	cond_3_tss_type,
	cond_4_tss_present,
	cond_5_tss_bit_22_21,
	cond_6_tss_g_limit,
	cond_7_tss_busy,
	cond_8_tss_base,
	cond_9_tss_readonly,
	cond_10_cur_base,
};

#define NCOND	(sizeof(cond_funs)/sizeof(void (*)(int)))

void set_violation_map(u32 vmap)
{
	int i;

	for (i = 0; i < NCOND; i++) {
		if ((vmap >> i) & 0x1)
			cond_funs[i](1);
	}

	for (i = 0; i < NCOND; i++) {
		if ((vmap >> i) & 0x1) {
			printf("1 ");
		} else
			printf("0 ");
	}
}

void far_call_task_gate(const char *arg)
{
	extern void lcall_start(void);
	extern void lcall_end(void);

	dbg_printf("%s CS:%x\n", __FUNCTION__, read_cs());

	rip_skip = lcall_end - lcall_start;
	asm volatile ("lcall_start:\n"
			"	lcallw *%0\n"
			"lcall_end:\n" : : "m"(target_sel));
}

void test_cond(u32 vmap)
{
	set_violation_map(vmap);

	do_less_privilege(far_call_task_gate, NULL, 2);
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

	for (vmap = 0x0; vmap <  (0x1 <<NCOND); vmap ++) {
		reset_desc();
		test_cond(vmap);
	}

/*
	for (vmap = 0x0; vmap < NCOND; vmap ++) {
		reset_desc();
		test_cond(0x1 << vmap);
	}
*/
	return 0;
}
