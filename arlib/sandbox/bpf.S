let arch = (offsetof(struct seccomp_data, arch))
let sysno = (offsetof(struct seccomp_data, nr))
let arg1l = (offsetof(struct seccomp_data, args[0]))
let arg1h = (offsetof(struct seccomp_data, args[0])+4)
let arg2l = (offsetof(struct seccomp_data, args[1]))
let arg2h = (offsetof(struct seccomp_data, args[1])+4)
let arg3l = (offsetof(struct seccomp_data, args[2]))
let arg3h = (offsetof(struct seccomp_data, args[2])+4)
let arg4l = (offsetof(struct seccomp_data, args[3]))
let arg4h = (offsetof(struct seccomp_data, args[3])+4)
let arg5l = (offsetof(struct seccomp_data, args[4]))
let arg5h = (offsetof(struct seccomp_data, args[4])+4)
let arg6l = (offsetof(struct seccomp_data, args[5]))
let arg6h = (offsetof(struct seccomp_data, args[5])+4)

defines /usr/include/x86_64-linux-gnu/asm/unistd_64.h

ld [arch]
jne #AUDIT_ARCH_X86_64, reject

ld [sysno]
jeq #__NR_open, emulate
jeq #__NR_close, accept
jeq #__NR_read, accept
jeq #__NR_write, accept

jeq #__NR_stat, emulate
jeq #__NR_fstat, accept
jeq #__NR_access, emulate
jeq #__NR_ioctl, ioctl

jeq #__NR_dup, accept
jeq #__NR_dup2, accept

#setrlimit seems harmful, but it can't raise the hard limits so it's fine
jeq #__NR_getrlimit, accept
jeq #__NR_setrlimit, accept

#PID is private, but this is in a PID namespace so it won't get the real one
jeq #__NR_getpid, accept

jeq #__NR_brk, accept
jeq #__NR_mmap, accept
jeq #__NR_mprotect, accept
jeq #__NR_munmap, accept

jeq #__NR_arch_prctl, arch_prctl
jeq #__NR_exit_group, accept
jeq #__NR_set_tid_address, accept
jeq #__NR_set_robust_list, accept

jeq #__NR_fork, emulate
jeq #__NR_vfork, emulate
jeq #__NR_clone, clone

jeq #__NR_execve, emulate
jeq #__NR_execveat, execveat

jeq #__NR_rt_sigaction, accept
jeq #__NR_rt_sigreturn, accept
jeq #__NR_sendto, sendto
jeq #__NR_recvmsg, accept

jmp reject

ioctl:
#int ioctl(int fd, int cmd, ...)
defines /usr/include/asm-generic/ioctls.h
ld [arg2h]
jne #0, reject
ld [arg2l]
jeq #TCGETS, accept
jeq #TIOCGWINSZ, accept

jmp reject

execveat:
#int execveat(int dirfd, const char * pathname, char * const * argv, char * const * envp, int flags)
#ensure pathname == 0x00007fff'ffffefff
#that's the last mappable byte, so it must be a empty string (or not valid) and ensures it doesn't touch filesystem
#there is no filesystem, so there's no security hole if bypassed, it's just to ensure only sysemu issues this syscall
ld [arg2h]
jne #0x00007fff, emulate
ld [arg2l]
jne #0xffffefff, emulate
jmp accept

clone:
#pid_t clone(unsigned long clone_flags, unsigned long newsp, int* parent_tidptr, int* child_tidptr, unsigned long tls)
ld [arg1h]
jne #0, reject
ld [arg1l]
jset #CLONE_THREAD, reject ; TODO
jnset #CLONE_CHILD_SETTID, emulate
ld [arg4h]
jne #0, emulate
ld [arg4l]
jne #0, emulate
jmp accept

arch_prctl:
#int arch_prctl(int code, unsigned long addr)
#int arch_prctl(int code, unsigned long * addr)
#ensure code is one of the four documented values and nothing new is added
defines /usr/include/x86_64-linux-gnu/asm/prctl.h
ld [arg1h]
jne #0, reject
ld [arg1l]
jeq #ARCH_SET_FS, accept
jeq #ARCH_GET_FS, accept
jeq #ARCH_SET_GS, accept
jeq #ARCH_GET_GS, accept
jmp reject

sendto:
#ssize_t sendto(int sockfd, const void * buf, size_t len, int flags,
#               const struct sockaddr * dest_addr, socklen_t addrlen)
#there is no send syscall, it's implemented as sendto(dest_addr=NULL, addrlen=0)
#ensure this isn't a sendto trying to sneak around
ld [arg5l]
jne #0, reject
ld [arg5h]
jne #0, reject
ld [arg6l]
jne #0, reject
ld [arg6h]
jne #0, reject
jmp accept

accept:
ret #SECCOMP_RET_ALLOW

reject:
#could return SECCOMP_RET_ERRNO|ENOSYS here, but let's let sysemu print an error instead
emulate:
ret #SECCOMP_RET_TRAP
