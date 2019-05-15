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

//#define TSS_CUR_BACKUP		(FIRST_SPARE_SEL) 	/* need 2 64-bit gdt entry */
#define TARGET_TSS		(FIRST_SPARE_SEL + 16)
#define TASK_GATE		(FIRST_SPARE_SEL + 32)
#define ERROR_ADDR	0x5fff0000ULL

struct segment_desc64 desc_main_b;

struct segment_desc64
  *desc_cur_backup = &desc_main_b,
  *desc_main = (void*)&gdt64[TSS_MAIN >>3],
  *desc_target = (void*)&gdt64[TARGET_TSS >>3];

struct task_gate {
	u16 resv_0;
	u16 selector;
	u8 resv_1 :8;
	u8 type :4;
        u8 system :1;
        u8 dpl :2;
        u8 p :1;
	u16 resv_2;
} *gate = (void*)&gdt64[TASK_GATE >>3];

//#define dbg_printf(...)	printf(__VA_ARGS__)
#define dbg_printf(...)

static unsigned long rip_skip = 0;

void gp_handler(struct ex_regs *regs)
{
	dbg_printf("Rip->(%lx + %lx)! ", regs->rip, rip_skip);
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

/* gate conditions, 0:false 1:tr */
void reset_gate(void)
{
	//*gate = *((struct task_gate *)(&boot_idt[6]));
	gate->type = 0x5;
	gate->system = 0;
	gate->p = 1;
	gate->dpl = 2;
	gate->selector = TARGET_TSS;
}

void reset_desc(void)
{
	*desc_target = *desc_cur_backup;
	desc_target->dpl = 2;

	desc_main->base1= desc_cur_backup->base1;
	desc_main->base2= desc_cur_backup->base2;
	desc_main->base3= desc_cur_backup->base3;
	desc_main->base4= desc_cur_backup->base4;
}

/* cond 0 */
void cond_0_gate_resv(int violation)
{
	dbg_printf("%s\n", __FUNCTION__);

	if (violation) {
		/* interleave of 0 & 1 */
		gate->resv_0 = 0xaaaa;
		gate->resv_1 = 0xaa;
		gate->resv_2= 0xaaaa;
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
void cond_2_gate_dpl(int violation)
{
	dbg_printf("%s\n", __FUNCTION__);

	if (violation)
		gate->dpl = 1;
}

/* cond 3 */
void cond_3_gate_null_sel(int violation)
{
	dbg_printf("%s\n", __FUNCTION__);

	if (violation)
		gate->selector = 0;
}

/* cond 4 */
void cond_4_desc_ti(int violation)
{
	dbg_printf("%s\n", __FUNCTION__);

	if (violation)
		gate->selector |= 0x4;
}

/* cound 5 */
void cond_5_desc_dpl_greater(int violation)
{
	dbg_printf("%s\n", __FUNCTION__);

	if (violation)
		desc_target->dpl = 3;
}

/* cond 6 */
void cond_6_desc_type(int violation)
{
	dbg_printf("%s\n", __FUNCTION__);

	if (violation)
		desc_target->type = 0;
}

/* cond 7 */
void cond_7_desc_present(int violation)
{
	dbg_printf("%s\n", __FUNCTION__);

	if (violation)
		desc_target->p = 0;
}

/* cond 8 */
void cond_8_desc_bit_22_21(int violation)
{
	dbg_printf("%s\n", __FUNCTION__);

	if (violation) {
		desc_target->l = 1;
		desc_target->db = 1;
	}
}

/* cond 9 */
void cond_9_desc_g_limit(int violation)
{
	dbg_printf("%s\n", __FUNCTION__);

	if (violation) {
		desc_target->g = 0;
		desc_target->limit1 = 0x66;
		desc_target->limit = 0;
	}
}

/* cond 10 */
void cond_10_desc_busy(int violation)
{
	dbg_printf("%s\n", __FUNCTION__);

	if (violation)
		desc_target->type |= 0x2;
}

/* cond 11 */
void cond_11_desc_tss_base(int violation)
{
	dbg_printf("%s\n", __FUNCTION__);

	if (violation) {
		desc_target->base1 = ERROR_ADDR & 0xffff;
		desc_target->base2 = (ERROR_ADDR >> 16) & 0xff;
		desc_target->base3 = (ERROR_ADDR >> 24) & 0xff;
		desc_target->base4 = (ERROR_ADDR >> 32) & 0xffffffff;
	}
}

/* cond 12 */
void cond_12_desc_tss_readonly(int violation)
{
	dbg_printf("%s\n", __FUNCTION__);

	if (violation) {
		desc_target->type |= 0x2;
	}
}

/* cond 13 */
void cond_13_desc_cur_base(int violation)
{
	dbg_printf("%s\n", __FUNCTION__);

	if (violation) {
		desc_main->base1 = ERROR_ADDR & 0xffff;
		desc_main->base2 = (ERROR_ADDR >> 16) & 0xff;
		desc_main->base3 = (ERROR_ADDR >> 24) & 0xff;
		desc_main->base4 = (ERROR_ADDR >> 32) & 0xffffffff;
	}
}

void (*cond_funs[])(int) = {
	cond_0_gate_resv,
	cond_1_gate_present,
	cond_2_gate_dpl,
	cond_3_gate_null_sel,
	cond_4_desc_ti,
	cond_5_desc_dpl_greater,
	cond_6_desc_type,
	cond_7_desc_present,
	cond_8_desc_bit_22_21,
	cond_9_desc_g_limit,
	cond_10_desc_busy,
	cond_11_desc_tss_base,
	cond_12_desc_tss_readonly,
	cond_13_desc_cur_base,
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

u32 long_ptr = TASK_GATE << 16;

void far_call_task_gate(const char *arg)
{
	extern void lcall_start(void);
	extern void lcall_end(void);
	//u32 long_ptr = TASK_GATE << 16;

	dbg_printf("%s CS:%x\n", __FUNCTION__, read_cs());

	rip_skip = lcall_end - lcall_start;
	dbg_printf("rip_skip:%lx\n", rip_skip);
	asm volatile ("lcall_start:\n"
			"lcallw *%0\n" 
		"lcall_end:\n" : : "m"(long_ptr));	
	dbg_printf("rip_skip:%lx\n", rip_skip);
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
	init_do_less_privilege();

	handle_exception(13, gp_handler);
	handle_exception(14, pf_handler);
	handle_exception(10, ts_handler);
	handle_exception(11, np_handler);

	*desc_target = *desc_main;
	*desc_cur_backup = *desc_main;
/*
	for (vmap = 0x0; vmap <  (0x1 <<NCOND); vmap ++) {
		reset_gate();
		reset_desc();
		test_cond(vmap);
	}
*/
	for (vmap = 0x0; vmap < NCOND; vmap ++) {
		reset_gate();
		reset_desc();
		test_cond(0x1 <<vmap);
	}
	return 0;
}
