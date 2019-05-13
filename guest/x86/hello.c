/*
 * Copyright 2010 Siemens AG
 * Author: Jan Kiszka
 *
 * Released under GPLv2.
 */

#include "libcflat.h"
#include "desc.h"
#include "processor.h"

void test_fun(void)
{
	printf("%s CS:%x\n", __FUNCTION__, read_cs());
}

int main(int ac, char **av)
{
	call_gate_t *gate = (void *)&gdt32[10];
	uint32_t offset = (uint32_t)test_fun;

	setup_vm();
	setup_idt();
	setup_alt_stack();
	printf("Hello, World!\n");

	gdt32[11] = gdt32[1];
	//gdt32[11].access |= 0x40; /* DPL = 2 */ 	
	gdt32[11].access |= 0x04; /* conforming */ 	
	printf("access:%x\n",  gdt32[11].access);
	gate->offset_low = offset & 0xffff;
	gate->offset_high = (offset & 0xffff0000) >>16;
	gate->selector = 11 <<3;
	gate->reserve = 0;
	gate->type = 0xc;
	gate->p = 1;
	gate->system = 0;
	gate->dpl = 2;
		
	asm volatile ("lcall $0x52, $0");

	return 0;
}
