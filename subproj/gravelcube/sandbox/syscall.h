#pragma once
//usage: int fd = syscall<__NR_open>("foo", O_RDONLY);
//WARNING: uses the raw kernel interface!
//If the manpage splits an argument in high/low, you'd better follow suit.
//If the argument order changes between platforms, you must follow that.
//If the syscall is completely different from the wrapper (hi clone()), you must use syscall semantics.
//In particular, there is no errno in this environment. Instead, that's handled by returning -ENOENT.

#ifdef __x86_64__
#define CLOBBER "memory", "rcx", "r11" // https://en.wikibooks.org/wiki/X86_Assembly/Interfacing_with_Linux#syscall
#define REG_SYSNO "rax" // Linux nolibc https://github.com/torvalds/linux/blob/1c4e395cf7ded47f/tools/include/nolibc/nolibc.h#L403
#define REG_ARG1 "rdi" // claims r10, r8 and r9 are clobbered too, but wikibooks doesn't
#define REG_ARG2 "rsi" // and nolibc syscall6 doesn't clobber r9, but syscall5 and below do
#define REG_ARG3 "rdx" // testing reveals no clobbering of those registers either
#define REG_ARG4 "r10" // I'll assume it's nolibc being overcautious, despite being in the kernel tree
#define REG_ARG5 "r8"
#define REG_ARG6 "r9" // "cc" is not clobbered per https://stackoverflow.com/a/2538212, but it's auto clobber on x86
#define SYSCALL_INSTR "syscall" // and it's hard to find anywhere where cc clobber or not would make a difference
#define REG_RET "rax"
#endif
#ifdef __i386__
#define CLOBBER "memory", "cc" // https://en.wikibooks.org/wiki/X86_Assembly/Interfacing_with_Linux#int_$0x80
#define REG_SYSNO "eax" // don't know if it clobbers cc
#define REG_ARG1 "ebx"
#define REG_ARG2 "ecx"
#define REG_ARG3 "edx"
#define REG_ARG4 "esi"
#define REG_ARG5 "edi"
#define REG_ARG6 "ebp"
#define SYSCALL_INSTR "int {$|}0x80"
#define REG_RET "eax"
#endif

template<int nr, typename T1, typename T2, typename T3, typename T4, typename T5, typename T6>
long syscall6(T1 a1, T2 a2, T3 a3, T4 a4, T5 a5, T6 a6)
{
	register long sysno __asm__(REG_SYSNO) = nr;
	register long a1r __asm__(REG_ARG1) = (long)a1;
	register long a2r __asm__(REG_ARG2) = (long)a2;
	register long a3r __asm__(REG_ARG3) = (long)a3;
	register long a4r __asm__(REG_ARG4) = (long)a4;
	register long a5r __asm__(REG_ARG5) = (long)a5;
	register long a6r __asm__(REG_ARG6) = (long)a6;
	register long ret __asm__(REG_RET);
	__asm__ volatile(SYSCALL_INSTR : "=r"(ret) : "r"(sysno), "r"(a1r), "r"(a2r), "r"(a3r), "r"(a4r), "r"(a5r), "r"(a6r) : CLOBBER);
	return ret;
}

template<int nr, typename T1, typename T2, typename T3, typename T4, typename T5>
long syscall5(T1 a1, T2 a2, T3 a3, T4 a4, T5 a5)
{
	register long sysno __asm__(REG_SYSNO) = nr;
	register long a1r __asm__(REG_ARG1) = (long)a1;
	register long a2r __asm__(REG_ARG2) = (long)a2;
	register long a3r __asm__(REG_ARG3) = (long)a3;
	register long a4r __asm__(REG_ARG4) = (long)a4;
	register long a5r __asm__(REG_ARG5) = (long)a5;
	register long ret __asm__(REG_RET);
	__asm__ volatile(SYSCALL_INSTR : "=r"(ret) : "r"(sysno), "r"(a1r), "r"(a2r), "r"(a3r), "r"(a4r), "r"(a5r) : CLOBBER);
	return ret;
}

template<int nr, typename T1, typename T2, typename T3, typename T4>
long syscall4(T1 a1, T2 a2, T3 a3, T4 a4)
{
	register long sysno __asm__(REG_SYSNO) = nr;
	register long a1r __asm__(REG_ARG1) = (long)a1;
	register long a2r __asm__(REG_ARG2) = (long)a2;
	register long a3r __asm__(REG_ARG3) = (long)a3;
	register long a4r __asm__(REG_ARG4) = (long)a4;
	register long ret __asm__(REG_RET);
	__asm__ volatile(SYSCALL_INSTR : "=r"(ret) : "r"(sysno), "r"(a1r), "r"(a2r), "r"(a3r), "r"(a4r) : CLOBBER);
	return ret;
}

template<int nr, typename T1, typename T2, typename T3>
long syscall3(T1 a1, T2 a2, T3 a3)
{
	register long sysno __asm__(REG_SYSNO) = nr;
	register long a1r __asm__(REG_ARG1) = (long)a1;
	register long a2r __asm__(REG_ARG2) = (long)a2;
	register long a3r __asm__(REG_ARG3) = (long)a3;
	register long ret __asm__(REG_RET);
	__asm__ volatile(SYSCALL_INSTR : "=r"(ret) : "r"(sysno), "r"(a1r), "r"(a2r), "r"(a3r) : CLOBBER);
	return ret;
}

template<int nr, typename T1, typename T2>
long syscall2(T1 a1, T2 a2)
{
	register long sysno __asm__(REG_SYSNO) = nr;
	register long a1r __asm__(REG_ARG1) = (long)a1;
	register long a2r __asm__(REG_ARG2) = (long)a2;
	register long ret __asm__(REG_RET);
	__asm__ volatile(SYSCALL_INSTR : "=r"(ret) : "r"(sysno), "r"(a1r), "r"(a2r) : CLOBBER);
	return ret;
}

template<int nr, typename T1>
long syscall1(T1 a1)
{
	register long sysno __asm__(REG_SYSNO) = nr;
	register long a1r __asm__(REG_ARG1) = (long)a1;
	register long ret __asm__(REG_RET);
	__asm__ volatile(SYSCALL_INSTR : "=r"(ret) : "r"(sysno), "r"(a1r) : CLOBBER);
	return ret;
}

template<int nr>
long syscall0()
{
	register long sysno __asm__(REG_SYSNO) = nr;
	register long ret __asm__(REG_RET);
	__asm__ volatile(SYSCALL_INSTR : "=r"(ret) : "r"(sysno) : CLOBBER);
	return ret;
}
