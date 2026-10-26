// Syscall instructions that the seccomp filter exempts.
//
// Everything between allow_start and allow_end may issue syscalls without
// being intercepted: the SIGSYS handler's own (re)issued syscalls, the
// loader's own syscalls, and the handler's sigreturn. Because the loader is
// a non-PIE static binary linked at a fixed address, this range is identical
// in every process the kernel starts from this binary, so one seccomp filter
// (which survives execve and cannot be removed) keeps exempting it forever.
	.text
	.balign 16
	.globl allow_start, allow_end, raw_syscall6, sig_restorer, jump_to
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
	nop			// the PC after the svc is still inside the range
allow_end:

// void jump_to(void *entry, void *sp) -- never returns.
// Sets the stack pointer and branches to entry with x0 = 0 (rtld_fini) and a
// cleared frame, as the kernel does at process start. No syscall here, so it
// need not be in the exempt range.
jump_to:
	mov	x2, x0		// entry
	mov	sp, x1		// new stack (points at argc)
	mov	x0, xzr		// rtld_fini = NULL
	mov	x1, xzr
	mov	x29, xzr
	mov	x30, xzr
	br	x2
