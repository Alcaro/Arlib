ld [I_arch]
jne #ARCH_TRUE, reject

ld [I_sysno]
jeq #__NR_open, reject
jmp accept

accept:
ret #SECCOMP_RET_ALLOW

reject:
ret #SECCOMP_RET_TRAP


#jeq #__NR_exit,   accept
#jeq #__NR_write,  fd1only
#jeq #__NR_fstat,  fd1only
#jeq #__NR_mmap,   mmap
##jeq #__NR_execve, execve
#jmp die
#
#
#fd1only:
##write, fstat
#ld [I_arg0h]
#jne #0, die
#ld [I_arg0l]
#jne #1, die
#jmp accept
#
#
#mmap:
##extern void *mmap (void *__addr, size_t __len, int __prot,
##		   int __flags, int __fd, __off_t __offset) __THROW;
##prot
#ld [I_arg2h]
#jne #0, die
#ld [I_arg2l]
#jne #PROT_READ|PROT_WRITE, die ; 0x03
#
##flags
#ld [I_arg3h]
#jne #0, die
#ld [I_arg3l]
#jne #MAP_PRIVATE|MAP_ANONYMOUS, die ; 0x22
#
##fd, why is it (uint32_t)-1
#ld [I_arg4h]
#jne #0, die
#ld [I_arg4l]
#jne #0xFFFFFFFF, die
#jmp accept
#die:
#
#
##execve:
##int execve(const char *filename, char *const argv[], char *const envp[]); 
##jmp die
