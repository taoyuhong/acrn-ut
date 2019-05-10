/*
 * Copyright 2010 Siemens AG
 * Author: Jan Kiszka
 *
 * Released under GPLv2.
 */

#include "libcflat.h"
#include "processor.h"
#include "desc.h"

static void test_func(const char *arg)
{
	printf("%s CS:%x\n", arg, read_cs());
}

int main(int ac, char **av)
{
	printf("Hello, World!\n");
	setup_idt();
	init_do_ring3();
	do_ring3(test_func, "hahahaha");
	return 0;
}
