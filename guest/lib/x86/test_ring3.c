
#include "libcflat.h"
#include "desc.h"
#include "processor.h"

/* The ugly mode switching code */

int do_less_privilege(void (*fn)(const char *), const char *arg, int rpl)
{
    static unsigned char user_stack[4096];
    int ret;
    int cpl = read_cs() & 0x3;
    u16 user_ds = USER_DS;
    u16 user_cs = USER_CS;

    if ((rpl <= cpl) || (rpl > 3)) {
        printf("error: %s rpl:%d cpl:%d\n", __FUNCTION__, rpl, cpl);
        return -1;
    }

    user_ds &= ~0x3;
    user_ds |= rpl;
    user_cs &= ~0x3;
    user_cs |= rpl;

    gdt32[USER_DS >>3].access &= 0x9f;
    gdt32[USER_DS >>3].access |= rpl <<5;
    gdt32[USER_CS >>3].access &= 0x9f;
    gdt32[USER_CS >>3].access |= rpl <<5;

    asm volatile ("mov %[user_ds], %%" R "dx\n\t"
		  "mov %%dx, %%ds\n\t"
		  "mov %%dx, %%es\n\t"
		  "mov %%dx, %%fs\n\t"
		  "mov %%dx, %%gs\n\t"
		  "mov %%" R "sp, %%" R "cx\n\t"
		  "push" W " %%" R "dx \n\t"
		  "lea %[user_stack_top], %%" R "dx \n\t"
		  "push" W " %%" R "dx \n\t"
		  "pushf" W "\n\t"
		  "push" W " %[user_cs] \n\t"
		  "push" W " $1f \n\t"
		  "iret" W "\n"
		  "1: \n\t"
		  "push %%" R "cx\n\t"   /* save kernel SP */

#ifndef __x86_64__
		  "push %[arg]\n\t"
#endif
		  "call *%[fn]\n\t"
#ifndef __x86_64__
		  "pop %%ecx\n\t"
#endif

		  "pop %%" R "cx\n\t"
		  "mov $1f, %%" R "dx\n\t"
		  "int %[kernel_entry_vector]\n\t"
		  ".section .text.entry \n\t"
		  "kernel_entry: \n\t"
		  "mov %%" R "cx, %%" R "sp \n\t"
		  "mov %[kernel_ds], %%cx\n\t"
		  "mov %%cx, %%ds\n\t"
		  "mov %%cx, %%es\n\t"
		  "mov %%cx, %%fs\n\t"
		  "mov %%cx, %%gs\n\t"
		  "jmp *%%" R "dx \n\t"
		  ".section .text\n\t"
		  "1:\n\t"
		  : [ret] "=&a" (ret)
		  : [user_ds] "m" (user_ds),
		    [user_cs] "m" (user_cs),
		    [user_stack_top]"m"(user_stack[sizeof user_stack]),
		    [fn]"r"(fn),
		    [arg]"D"(arg),
		    [kernel_ds]"i"(KERNEL_DS),
		    [kernel_entry_vector]"i"(0x20)
		  : "rcx", "rdx");
    return ret;
}

void init_do_less_privilege(void)
{
    extern unsigned char kernel_entry;

    set_idt_entry(0x20, &kernel_entry, 3);
}
