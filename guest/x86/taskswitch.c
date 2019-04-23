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

#define TSS_RETURN		(FIRST_SPARE_SEL)
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
} *desc_intr = (void*)&gdt32[TSS_INTR >>3],
  *desc_main = (void*)&gdt32[TSS_MAIN >>3];

void fault_entry(void);

static __attribute__((used, regparm(1))) void
fault_handler(unsigned long error_code)
{
	print_current_tss_info();
	printf("error code %lx\n", error_code);

	tss.eip += 2;

	gdt32[TSS_MAIN / 8].access &= ~2;

	set_gdt_task_gate(TSS_RETURN, tss_intr.prev);
}

asm (
	"fault_entry:\n"
	"	mov (%esp),%eax\n"
	"	call fault_handler\n"
	"	jmp $" xstr(TSS_RETURN) ", $0\n"
);

static unsigned long rip_skip = 0;

void gp_handler(struct ex_regs *regs)
{
	regs->rip += rip_skip;
	rip_skip = 0;
	printf("#GP(%lx) happen!\n", regs->error_code);
}

void pf_handler(struct ex_regs *regs)
{
	regs->rip += rip_skip;
	rip_skip = 0;
	printf("#PF(%lx) happen!\n", read_cr2());
}

void ts_handler(struct ex_regs *regs)
{
	regs->rip += rip_skip;
	rip_skip = 0;
	printf("#TS(%lx) happen!\n", regs->error_code);
}

static void show_idt(uint32_t vector)
{
       printf("boot_idt[%u] p:%x dpl:%x type:%x, sel:%x\n", vector,
                       boot_idt[vector].p,
                       boot_idt[vector].dpl,
                       boot_idt[vector].type,
                       boot_idt[vector].selector);
}

static void show_gdt(uint32_t sel)
{
	int index = sel >>3;
	uint32_t base;

	base =  (gdt32[index].base_high << 24) +
		(gdt32[index].base_middle << 16) +
		(gdt32[index].base_low);

	printf("sel: %x, gdt32[%d] base:%x\n", sel, index, base);
}

/*
static void gen_ud(void)
{
	rip_skip = 1;
	asm volatile (".byte 0x0f\n");
}
*/

/*
static void gen_pf(void)
{
	char *buf = (void *)ERROR_ADDR;

	rip_skip = 7;
	buf[0] = 'h';

	printf("%c\n", buf[0]);
}
*/

void long_jmp_tss(void)
{
	printf("%s\n", __FUNCTION__);	
	rip_skip = 7;
	asm volatile ("ljmp $" xstr(TSS_INTR) " , $0\n");
}

void far_call_tss(void)
{
	printf("%s\n", __FUNCTION__);	
	rip_skip = 7;
	asm volatile ("lcall $" xstr(TSS_INTR) " , $0\n");
}

void iret_tss(void)
{
	printf("%s\n", __FUNCTION__);

	u64 rflags = read_rflags();
	write_rflags(rflags | X86_EFLAGS_NT);

	tss.prev = TSS_INTR;
	desc_intr->type |= 0x2;	/* backlink to busy TSS, or #TS */

	rip_skip = 1;
	asm volatile ("iret");
}

int main(int ac, char **av)
{
	setup_vm();
	setup_idt();
	setup_tss32();

	set_intr_task_gate(6, fault_entry);	/* #UD trigger task switch */

	handle_exception(13, gp_handler);
	handle_exception(14, pf_handler);
	handle_exception(10, ts_handler);

	show_gdt(TSS_INTR);
	show_gdt(TSS_MAIN);
	//set_gdt_entry(TSS_INTR, ERROR_ADDR, sizeof(tss32_t) - 1, 0x89, 0x0f);
	//show_gdt(TSS_INTR);

	show_idt(6);

	//gen_ud();	/* #UD trigger a task switch */
	//gen_pf();	/* test page fault */

	//desc->type = 0;
	//long_jmp_tss();
	//far_call_tss();
	iret_tss();

	return 0;
}
