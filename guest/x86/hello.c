/*
 * Copyright 2010 Siemens AG
 * Author: Jan Kiszka
 *
 * Released under GPLv2.
 */

#include "libcflat.h"
#include "desc.h"
#include "processor.h"
#include "vm.h"

#define ERROR_ADDR	0x5fff0000

char buf[] = "Hello, this is a memory strings";

int main(int ac, char **av)
{
	u32 offset = ((u32)buf) & 0xfff;
	char *str_read_only = (void *)ERROR_ADDR + offset;

	setup_idt();
	setup_vm();


	//install_pages(phys_to_virt(read_cr3()), (u32)buf, 4096, (void *)ERROR_ADDR);
	install_read_only_page(phys_to_virt(read_cr3()), ((u32)buf) & 0xfffff000, (void *)ERROR_ADDR);

	printf("read_only:%s\n", str_read_only);

	str_read_only[0] = 'h';

	/* will #PF */
	printf("read_only:%s\n", str_read_only);

	return 0;
}
