#pragma once
//usage: int fd = syscall2<__NR_open>("foo", O_RDONLY);
//WARNING: uses the raw kernel interface!
//If the manpage splits an argument in high/low, you'd better follow suit.
//If the argument order changes between platforms, you must follow that.
//If the syscall is completely different from the wrapper (hi clone()), you must use syscall semantics.
//In particular, there is no errno in this environment. Instead, that's handled by returning -ENOENT.

template<int arg_count, int nr, typename Tret, typename... Targs>
Tret syscall_inner(Targs... args)
{
	static_assert(arg_count == sizeof...(Targs));
	static_assert(sizeof...(Targs) <= 6);
	long args_v[6] = { (long)args... };
	
#ifdef __x86_64__
	register long sysno __asm__("rax") = nr; // https://en.wikibooks.org/wiki/X86_Assembly/Interfacing_with_Linux#syscall
	register long a1r __asm__("rdi") = args_v[0]; // Linux nolibc https://github.com/torvalds/linux/blob/1c4e395cf7ded47f/tools/include/nolibc/nolibc.h#L403
	register long a2r __asm__("rsi") = args_v[1]; // claims r10, r8 and r9 are clobbered too, but wikibooks doesn't
	register long a3r __asm__("rdx") = args_v[2]; // and nolibc syscall6 doesn't clobber r9, but syscall5 and below do
	register long a4r __asm__("r10") = args_v[3]; // testing reveals no clobbering of those registers either
	register long a5r __asm__("r8") = args_v[4]; // I'll assume it's nolibc being overcautious, despite being in the kernel tree
	register long a6r __asm__("r9") = args_v[5];
	register long ret __asm__("rax");
#define CLOBBER "memory", "rcx", "r11" // "cc" is not clobbered per https://stackoverflow.com/a/2538212, but it's auto clobber on x86
#define SYSCALL_INSTR "syscall" // and it's hard to find anywhere where cc clobber or not would make a difference
#endif
#ifdef __i386__
	register long sysno __asm__("eax") = nr; // https://en.wikibooks.org/wiki/X86_Assembly/Interfacing_with_Linux#int_$0x80
	register long a1r __asm__("ebx") = args_v[0];
	register long a2r __asm__("ecx") = args_v[1];
	register long a3r __asm__("edx") = args_v[2];
	register long a4r __asm__("esi") = args_v[3];
	register long a5r __asm__("edi") = args_v[4];
	register long a6r __asm__("ebp") = args_v[5];
	register long ret __asm__("eax");
#define CLOBBER "memory", "cc" // don't know if it clobbers cc
#define SYSCALL_INSTR "int {$|}0x80"
#endif
	if constexpr (sizeof...(Targs) == 0)
		__asm__ volatile(SYSCALL_INSTR : "=r"(ret) : "r"(sysno) : CLOBBER);
	if constexpr (sizeof...(Targs) == 1)
		__asm__ volatile(SYSCALL_INSTR : "=r"(ret) : "r"(sysno), "r"(a1r) : CLOBBER);
	if constexpr (sizeof...(Targs) == 2)
		__asm__ volatile(SYSCALL_INSTR : "=r"(ret) : "r"(sysno), "r"(a1r), "r"(a2r) : CLOBBER);
	if constexpr (sizeof...(Targs) == 3)
		__asm__ volatile(SYSCALL_INSTR : "=r"(ret) : "r"(sysno), "r"(a1r), "r"(a2r), "r"(a3r) : CLOBBER);
	if constexpr (sizeof...(Targs) == 4)
		__asm__ volatile(SYSCALL_INSTR : "=r"(ret) : "r"(sysno), "r"(a1r), "r"(a2r), "r"(a3r), "r"(a4r) : CLOBBER);
	if constexpr (sizeof...(Targs) == 5)
		__asm__ volatile(SYSCALL_INSTR : "=r"(ret) : "r"(sysno), "r"(a1r), "r"(a2r), "r"(a3r), "r"(a4r), "r"(a5r) : CLOBBER);
	if constexpr (sizeof...(Targs) == 6)
		__asm__ volatile(SYSCALL_INSTR : "=r"(ret) : "r"(sysno), "r"(a1r), "r"(a2r), "r"(a3r), "r"(a4r), "r"(a5r), "r"(a6r) : CLOBBER);
	return (Tret)ret;
#undef CLOBBER
#undef SYSCALL_INSTR
}

template<int nr, typename Tret = long, typename... Ts> Tret syscall0(Ts... args) { return syscall_inner<0, nr, Tret>(args...); }
template<int nr, typename Tret = long, typename... Ts> Tret syscall1(Ts... args) { return syscall_inner<1, nr, Tret>(args...); }
template<int nr, typename Tret = long, typename... Ts> Tret syscall2(Ts... args) { return syscall_inner<2, nr, Tret>(args...); }
template<int nr, typename Tret = long, typename... Ts> Tret syscall3(Ts... args) { return syscall_inner<3, nr, Tret>(args...); }
template<int nr, typename Tret = long, typename... Ts> Tret syscall4(Ts... args) { return syscall_inner<4, nr, Tret>(args...); }
template<int nr, typename Tret = long, typename... Ts> Tret syscall5(Ts... args) { return syscall_inner<5, nr, Tret>(args...); }
template<int nr, typename Tret = long, typename... Ts> Tret syscall6(Ts... args) { return syscall_inner<6, nr, Tret>(args...); }
