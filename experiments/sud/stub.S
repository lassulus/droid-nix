// Syscall instructions that the SUD range and the seccomp filter exempt.
// Everything between allow_start and allow_end may issue syscalls without
// being intercepted: the handler's own syscalls and its sigreturn.
	.text
	.balign 16
	.globl allow_start, allow_end, raw_syscall6, sig_restorer
allow_start:
// long raw_syscall6(long nr, long a0, long a1, long a2, long a3, long a4, long a5)
raw_syscall6:
	mov	x8, x0
	mov	x0, x1
	mov	x1, x2
	mov	x2, x3
	mov	x3, x4
	mov	x4, x5
	mov	x5, x6
	svc	#0
	ret
sig_restorer:
	mov	x8, #139	// rt_sigreturn
	svc	#0
	nop		// the PC after the svc is still inside the range
allow_end:
