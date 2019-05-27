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
#include "alloc_page.h"

#define TASK_GATE		(FIRST_SPARE_SEL + 8)
#define TARGET_TSS		(FIRST_SPARE_SEL + 16)
#define ERROR_ADDR	0x5fff0000

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
} *desc_intr = NULL,
  *desc_main = NULL,
  *desc_target = NULL;

struct tss_desc desc_intr_backup, desc_cur_backup;

struct task_gate {
	u16 resv_0;
	u16 selector;
	u8 resv_1 :8;
	u8 type :4;
        u8 system :1;
        u8 dpl :2;
        u8 p :1;
	u16 resv_2;
} *gate = NULL;

#define dbg_printf(...)	printf(__VA_ARGS__)
//#define dbg_printf(...)

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

/* gate conditions, 0:false 1:tr */
void reset_gate(void)
{
	*gate = *((struct task_gate *)(&boot_idt[6]));
	gate->type = 0x5;
	gate->p = 1;
	gate->dpl = 2;
	//gate->selector = TARGET_TSS;
	gate->selector = 0x1008;
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

	if (violation)
		desc_target->granularity |= 0x60;
}

/* cond 9 */
void cond_9_desc_g_limit(int violation)
{
	dbg_printf("%s\n", __FUNCTION__);

	if (violation) {
		desc_target->granularity &= 0x70;
		desc_target->limit_low = 0x66;
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
		desc_target->base_low = ERROR_ADDR & 0xffff;
		desc_target->base_middle = (ERROR_ADDR >> 16) & 0xff;
		desc_target->base_high = (ERROR_ADDR >> 24) & 0xff;
	}
}

/* cond 12 */
void cond_12_desc_tss_readonly(int violation)
{
	u32 tss_addr;
	u32 page_addr;
	u32 page_off;
	//tss32_t *readonly_tss;
	//tss32_t *target_tss;

	dbg_printf("%s\n", __FUNCTION__);
#define RD_ONLY_TSS_PAGE	(ERROR_ADDR + 0x1000)

	if (violation) {
		tss_addr = (desc_target->base_high <<24) +
				(desc_target->base_middle <<16) +
				desc_target->base_low;

		page_addr = tss_addr & (~0xfff);
		page_off = tss_addr & 0xfff;

		dbg_printf("tss_addr:%x size:%x page:%x off:%x\n", tss_addr, sizeof(tss), page_addr, page_off);

		install_read_only_page(phys_to_virt(read_cr3()), page_addr, (void*)RD_ONLY_TSS_PAGE);
		desc_target->base_low = (RD_ONLY_TSS_PAGE + page_off) & 0xffff;
		desc_target->base_middle = ((RD_ONLY_TSS_PAGE + page_off) >>16) & 0xff;
		desc_target->base_high = ((RD_ONLY_TSS_PAGE + page_off) >>24) & 0xff;

		//readonly_tss = (void*)(RD_ONLY_TSS_PAGE + page_off);
		//target_tss = (void*)tss_addr;

		/* should #pF */
		//rip_skip = 7;
		//readonly_tss->ss0 = 0x11;
/*
		dbg_printf("ss0: %x %x\n"
			"ss1: %x %x\n"
			"ss2: %x %x\n",
			readonly_tss->ss0, target_tss->ss0,
			readonly_tss->ss1, target_tss->ss1,
			readonly_tss->ss2, target_tss->ss2);
*/
	}
}

/* cond 13 */
void cond_13_desc_cur_base(int violation)
{
	dbg_printf("%s\n", __FUNCTION__);

	if (violation) {
		desc_main->base_low = ERROR_ADDR & 0xffff;
		desc_main->base_middle = (ERROR_ADDR >> 16) & 0xff;
		desc_main->base_high = (ERROR_ADDR >> 24) & 0xff;
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

void far_call_task_gate(const char *arg)
{
	extern void lcall_start(void);
	extern void lcall_end(void);

	dbg_printf("%s CS:%x\n", __FUNCTION__, read_cs());

	rip_skip = lcall_end - lcall_start;
	asm volatile ("lcall_start:\n"
			"	lcall $" xstr(TASK_GATE) " , $0\n"
			"lcall_end:\n");
}

void test_cond(u32 vmap)
{
	set_violation_map(vmap);

	do_less_privilege(far_call_task_gate, NULL, 2);
}

struct descriptor_table_ptr old_gdt_desc;
struct descriptor_table_ptr new_gdt_desc;

extern gdt_entry_t *new_gdt;

int main(int ac, char **av)
{
	u32 vmap = 0;
	//char *str = NULL;
	void *gdt_page = NULL;

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
/*
	for (vmap = 0x0; vmap <  (0x1 <<NCOND); vmap ++) {
		reset_gate();
		reset_desc();
		test_cond(vmap);
	}


	for (vmap = 0x0;vmap < NCOND;vmap ++) {
		reset_gate();
		reset_desc();
		test_cond(0x1<<vmap);
	}
*/
	
	gdt_page =  alloc_page(); /*alloc 1 page */
#define GDT_PAGE        ((void*)(ERROR_ADDR + 0x2000))
	install_pages(phys_to_virt(read_cr3()), (u32)gdt_page, PAGE_SIZE, GDT_PAGE);
	new_gdt = GDT_PAGE;

	sgdt(&old_gdt_desc);
	printf("gdt32(%x) gdt: %lx  limit:%x\n", (u32)gdt32, old_gdt_desc.base, old_gdt_desc.limit);

	memset(new_gdt, 0, PAGE_SIZE);
	memcpy(new_gdt, gdt32, old_gdt_desc.limit);
	printf("alloc gdt page at: %x\n", (u32)new_gdt);

	new_gdt_desc.base = (u32)new_gdt;
	new_gdt_desc.limit = PAGE_SIZE * 2;
	lgdt(&new_gdt_desc);
	sgdt(&old_gdt_desc);
	printf("gdt32(%x) gdt: %lx  limit:%x\n", (u32)gdt32, old_gdt_desc.base, old_gdt_desc.limit);

	desc_intr = (void*)&new_gdt[TSS_INTR >>3],
  	desc_main = (void*)&new_gdt[TSS_MAIN >>3],
  	desc_target = (void*)&new_gdt[TARGET_TSS >>3];
	gate = (void*)&new_gdt[TASK_GATE >>3];

	desc_intr_backup = *desc_intr;
	desc_cur_backup = *desc_main;

	/* make sure #PF */
	/*
	str = (void*)new_gdt + 0x1000;
	rip_skip = 7;
	str[128] = 'h';
	printf("str[128]:%c\n", str[128]);
	*/
	reset_gate();
	reset_desc();
	test_cond(vmap);

	return 0;
}