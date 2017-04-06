#ifdef SANDBOX_INTERNAL

//The sandbox environment is quite strict. It can't run normal programs unchanged, they'd call open() which fails.
//LD_PRELOAD doesn't help either, the loader open()s all libraries before initializing that.
//Its lesser known cousin LD_AUDIT works better, but the library itself is open()ed.
//This can be worked around with ptrace to make that specific syscall return 3, but that's way too much effort.
//Instead, we will be the loader. Of course we don't want to actually load the program, that's even harder than ptrace,
// so we'll just load the real loader.
//We won't do any error checking. We know everything will succeed, and if it doesn't, there's no way to recover.

//#define _GNU_SOURCE // default (mandatory) in c++
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/mman.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <ucontext.h>
#include <errno.h>

#include "syscall.h"

//gcc recognizes these function names and reads attributes (such as extern) from the headers, force it not to
namespace {

int open(const char * pathname, int flags, mode_t mode = 0)
{
	#define FD_PARENT 3
	//TODO
	return syscall3(__NR_open, (long)pathname, flags, mode);
}

int openat(int dirfd, const char * pathname, int flags, mode_t mode = 0)
{
	return syscall4(__NR_openat, dirfd, (long)pathname, flags, mode);
}

ssize_t read(int fd, void * buf, size_t count)
{
	return syscall3(__NR_read, fd, (long)buf, count);
}

ssize_t write(int fd, const void * buf, size_t count)
{
	return syscall3(__NR_write, fd, (long)buf, count);
}

int close(int fd)
{
	return syscall1(__NR_close, fd);
}

//TODO: remove inline once it's used, or remove function if it's not
inline int fstat(int fd, struct stat * buf)
{
	return syscall2(__NR_fstat, fd, (long)buf);
}

void* mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset)
{
	return (void*)syscall6(__NR_mmap, (long)addr, length, prot, flags, fd, offset);
}

int munmap(void* addr, size_t length)
{
	return syscall2(__NR_munmap, (long)addr, length);
}

#define sigaction kabi_sigaction
#undef sa_handler
#undef sa_sigaction
#define SA_RESTORER 0x04000000
struct kabi_sigaction {
	union {
		void (*sa_handler)(int);
		void (*sa_sigaction)(int, siginfo_t*, void*);
	};
	unsigned long sa_flags;
	void (*sa_restorer)(void);
	char sa_mask[_NSIG/8]; // this isn't the correct declaration, but we don't use this field, we only need its size
};

int rt_sigaction(int sig, const struct sigaction * act, struct sigaction * oact, size_t sigsetsize)
{
	return syscall4(__NR_rt_sigaction, sig, (long)act, (long)oact, sigsetsize);
}

void memset(void* ptr, int value, size_t num)
{
	//compiler probably optimizes this
	uint8_t* ptr_ = (uint8_t*)ptr;
	for (size_t i=0;i<num;i++) ptr_[i] = value;
}



#include <elf.h>

//Returns the entry point.
#define ALIGN 4096
#define ALIGN_MASK (ALIGN-1)
#define ALIGN_OFF(ptr) ((uintptr_t)(ptr)&ALIGN_MASK)
#define ALIGN_DOWN(ptr) ((ptr) - ALIGN_OFF(ptr))
#define ALIGN_UP(sz) (ALIGN_DOWN(sz) + (ALIGN_OFF(sz) ? ALIGN : 0))

template<typename T> void debug(T x) { size_t y=(size_t)x; write(1,&y,8); }

typedef void(*funcptr)();
funcptr map_binary(int fd)
{
	//uselib() would be the easy way out, but it doesn't tell where it's mapped, and it may be compiled out of the kernel
	//so instead, this is pretty much kernel's load_elf_interp() minus error checks
	uint8_t hbuf[832]; // FILEBUF_SIZE from glibc elf/dl-load.c
	read(fd, hbuf, sizeof(hbuf)); // ld-linux has 7 segments, this buffer fits 13 (plus ELF header)
	Elf64_Ehdr * ehdr = (Elf64_Ehdr*)hbuf;
	Elf64_Phdr * phdr = (Elf64_Phdr*)(hbuf + ehdr->e_phoff);
	
	int first = -1;
	int last = -1;
	for (int i=0;i<ehdr->e_phnum;i++)
	{
		if (phdr[i].p_type == PT_LOAD)
		{
			if (first<0) first=i;
			last=i;
		}
	}
	size_t total_size = phdr[last].p_vaddr + phdr[last].p_memsz - ALIGN_DOWN(phdr[first].p_vaddr);
	
	//DENYWRITE is documented ignored, but ld-linux uses it so let's follow suit
	uint8_t * base = (uint8_t*)mmap(NULL, total_size, PROT_NONE, MAP_PRIVATE|MAP_DENYWRITE|MAP_ANONYMOUS, -1, 0);
	munmap(base, total_size); // unmap the thing, no point leaving PROT_NONE holes
	//this would be a race condition if the program wasn't single threaded at this point
	//kernel does it like this, glibc unmaps only the holes (which isn't a race)
	
	for (int i=0;i<ehdr->e_phnum;i++)
	{
		if (phdr[i].p_type == PT_LOAD)
		{
			int prot = 0;
			if (phdr[i].p_flags & PF_R) prot |= PROT_READ;
			if (phdr[i].p_flags & PF_W) prot |= PROT_WRITE;
			if (phdr[i].p_flags & PF_X) prot |= PROT_EXEC;
			
			size_t addr = phdr[i].p_vaddr;
			size_t size = phdr[i].p_filesz + ALIGN_OFF(addr);
			size_t off = phdr[i].p_offset - ALIGN_OFF(addr);
			
			addr = ALIGN_DOWN(addr);
			size = ALIGN_UP(size);
			
			mmap(base+addr, size, prot, MAP_PRIVATE|MAP_FIXED|MAP_DENYWRITE, fd, off);
			
			size_t faddr = ALIGN_UP(phdr[i].p_vaddr + phdr[i].p_filesz);
			size_t maddr = ALIGN_UP(phdr[i].p_vaddr + phdr[i].p_memsz);
			if (ALIGN_UP(maddr) > ALIGN_UP(faddr))
			{
				mmap(base+faddr, maddr-faddr, prot, MAP_PRIVATE|MAP_FIXED|MAP_DENYWRITE|MAP_ANONYMOUS, -1, 0);
			}
		}
	}
	return (funcptr)(base + ehdr->e_entry);
}


//ld-linux can be the main program, in which case it opens the main binary as a normal library and executes that.
// (Coincidentally, this also solves some other security issues, but those were solved more robustly in other ways.)
//It checks this by checking if the main program's entry point is its own.
//So how does the loader find this entry point?
//As we all know, main() has three arguments: argc, argv, envp.
//But the loader actually gets a fourth argument, auxv, containing:
//Some entropy (for stack cookies), user ID, page size, ELF header size, the main program's entry point, and some other stuff.
//Since we're the main program, we're the entry point. We know where ld-linux starts, so let's put it in auxv.
void fake_entry(void** stack, funcptr ld_start)
{
	int* argc = (int*)stack;
	const char * * argv = (const char**)(stack+1);
	const char * * envp = argv+*argc+1;
	void** tmp = (void**)envp;
	while (*tmp) tmp++;
	
	Elf64_auxv_t* auxv = (Elf64_auxv_t*)(tmp+1);
	for (int i=0;auxv[i].a_type!=AT_NULL;i++)
	{
		if (auxv[i].a_type == AT_ENTRY)
		{
			//a_un is a union, but it only has one member. Apparently the rest were removed to allow
			// a 64bit program to use the 32bit structs. Even though half of the values don't make
			// sense on wrong bitness. I guess someone removed all pointers, usable or not.
			//Backwards compatibility, fun for everyone...
			auxv[i].a_un.a_val = (uintptr_t)ld_start;
		}
	}
}





//errors are returned as -ENOENT, not {errno=ENOENT, -1}; we are (pretend to be) the kernel, not libc
long syscall_emul(greg_t* regs)
{
//register assignment per http://stackoverflow.com/a/2538212
#define ARG_S(t) ((t)regs[REG_RAX])
#define ARG_0(t) ((t)regs[REG_RDI])
#define ARG_1(t) ((t)regs[REG_RSI])
#define ARG_2(t) ((t)regs[REG_RDX])
#define ARG_3(t) ((t)regs[REG_R10])
#define ARG_4(t) ((t)regs[REG_R8])
#define ARG_5(t) ((t)regs[REG_R9])
	switch (ARG_S(int)) // syscall number
	{
	case __NR_open:
		return openat(AT_FDCWD, ARG_0(char*), ARG_1(int), ARG_2(mode_t));
	default: return -ENOSYS;
	}
#undef ARG_S
#undef ARG_0
#undef ARG_1
#undef ARG_2
#undef ARG_3
#undef ARG_4
#undef ARG_5
}

void sa_sigsys(int signo, siginfo_t* info, void* context)
{
	ucontext_t* uctx = (ucontext_t*)context;
	long ret = syscall_emul(uctx->uc_mcontext.gregs);
	uctx->uc_mcontext.gregs[REG_RAX] = ret;
}

//static void ud2() { asm volatile("ud2"); }

//extern "C" void __attribute__((noreturn)) restore_rt();
//void restore_rt() { asm volatile("ud2"); }

//called from assembly
extern "C" funcptr preld_start(void** stack, void(*restore_rt)())
{
	struct sigaction act;
	act.sa_sigaction = sa_sigsys;
	act.sa_flags = SA_SIGINFO | SA_RESTORER;
	act.sa_restorer = restore_rt; // required for whatever reason, probably to allow nonexecutable stack
	memset(&act.sa_mask, 0, sizeof(act.sa_mask));
	rt_sigaction(SIGSYS, &act, NULL, sizeof(act.sa_mask));
	
	int fd = open("/lib64/ld-linux-x86-64.so.2", O_RDONLY);
	
	//char ggg[42];
	//long r = syscall4(SYS_readlinkat, open("/home/alcaro", O_RDONLY), (long)"./.", (long)ggg, 42);
	//debug(r);
	//write(1, ggg, 42);
	//AT_EMPTY_PATH
	
	funcptr ldlinux = map_binary(fd);
	close(fd);
	
	fake_entry(stack, ldlinux);
	
	return ldlinux;
}

}

#define STR_(x) #x
#define STR(x) STR_(x)
//we need to write the bootloader in assembly, so we need the stack pointer
//I could take the address of an argument, but I'm pretty sure that's undefined behavior
//and our callee also needs the stack, and I don't trust gcc to enforce a tail call
__asm__(
R"( # asm works great with raw strings, surprised I haven't seen it elsewhere
    # guess c++ and asm is a rare combination
.globl _start
_start:
mov rdi, rsp

#sigreturn is documented as taking an argument, but on x86, it doesn't. it takes its arguments from stack
#as such, any stack tricks (such as push %rbp) are fatal
#since gcc doesn't support naked functions, I'll implement the entire thing in assembly
#and I can't access it from C++ either, it ends up as nonsense (probably a runtime relocation)
#so I have to pass it from assembly as well
lea rsi, [%rip+restore_rt]
call preld_start
jmp rax

restore_rt:
mov eax, )" STR(__NR_rt_sigreturn) R"(
syscall
)"
);


#else
extern const char sandbox_preload_bin[];
extern const unsigned sandbox_preload_len;
asm(R"(
.section .rodata
.global sandbox_preload_bin
sandbox_preload_bin:
.incbin "obj/sand-preload.elf"
.equ my_len, .-sandbox_preload_bin
.align 4
.global sandbox_preload_len
sandbox_preload_len:
.int my_len
.section .text
)");
#endif
