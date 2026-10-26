// Shared pieces: the SIGSYS handler, syscall user dispatch and the seccomp
// trap filter. The handler answers getpid with MAGIC_PID when asked to (the
// probes use that to see that the handler answered), rewrites a /nix prefix
// for openat/newfstatat and forwards everything through raw_syscall6, which
// lives in the exempt range from stub.S.
#define _GNU_SOURCE
#include <errno.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unistd.h>

#ifndef PR_SET_SYSCALL_USER_DISPATCH
#define PR_SET_SYSCALL_USER_DISPATCH 59
#define PR_SYS_DISPATCH_OFF 0
#define PR_SYS_DISPATCH_ON 1
#define SYSCALL_DISPATCH_FILTER_ALLOW 0
#define SYSCALL_DISPATCH_FILTER_BLOCK 1
#endif
#ifndef SYS_USER_DISPATCH
#define SYS_USER_DISPATCH 2
#endif
#ifndef SA_RESTORER
#define SA_RESTORER 0x04000000
#endif

#define MAGIC_PID 4242

extern char allow_start[], allow_end[];
extern long raw_syscall6(long nr, long a0, long a1, long a2, long a3, long a4, long a5);
extern void sig_restorer(void);

static volatile char sud_selector = SYSCALL_DISPATCH_FILTER_ALLOW;
static volatile long sigsys_count;
static volatile int sigsys_code = -1;
static int answer_getpid_magic;
static const char *nix_prefix = "";
static size_t nix_prefix_len;

// "/nix/..." -> "<prefix>/nix/..."; returns 0 if the path is left alone.
static int rewrite(const char *path, char *buf, size_t size)
{
	if (strncmp(path, "/nix", 4) != 0 || (path[4] != '/' && path[4] != 0))
		return 0;
	size_t n = strlen(path);
	if (nix_prefix_len + n + 1 > size)
		return 0;
	memcpy(buf, nix_prefix, nix_prefix_len);
	memcpy(buf + nix_prefix_len, path, n + 1);
	return 1;
}

static void sigsys_handler(int sig, siginfo_t *si, void *ctx)
{
	(void)sig;
	unsigned long *r = ((ucontext_t *)ctx)->uc_mcontext.regs;
	long nr = si->si_syscall;
	char buf[512];
	long a1 = r[1];
	sigsys_count++;
	sigsys_code = si->si_code;
	if (nr == SYS_getpid && answer_getpid_magic) {
		r[0] = MAGIC_PID;
		return;
	}
	if ((nr == SYS_openat || nr == SYS_newfstatat) && rewrite((const char *)r[1], buf, sizeof buf))
		a1 = (long)buf;
	r[0] = raw_syscall6(nr, r[0], a1, r[2], r[3], r[4], r[5]);
}

// Raw rt_sigaction so that sigreturn goes through sig_restorer (inside the
// exempt range) rather than libc's or the vDSO's trampoline.
static int install_sigsys_handler(void)
{
	struct {
		void *handler;
		unsigned long flags;
		void *restorer;
		uint64_t mask;
	} ka = { (void *)sigsys_handler, SA_SIGINFO | SA_RESTORER, (void *)sig_restorer, 0 };
	long rc = raw_syscall6(SYS_rt_sigaction, SIGSYS, (long)&ka, 0, 8, 0, 0);
	if (rc < 0) {
		errno = -rc;
		return -1;
	}
	return 0;
}

static int sud_enable(void)
{
	return prctl(PR_SET_SYSCALL_USER_DISPATCH, PR_SYS_DISPATCH_ON, (unsigned long)allow_start,
		     (unsigned long)(allow_end - allow_start), (unsigned long)&sud_selector);
}

// Trap getpid/openat/newfstatat unless issued from [allow_start, allow_end).
static int seccomp_trap_install(void)
{
	uint64_t lo = (uintptr_t)allow_start, hi = (uintptr_t)allow_end;
	if ((lo >> 32) != (hi >> 32)) {
		errno = ERANGE;
		return -1;
	}
	struct sock_filter f[] = {
		BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, arch)),
		BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_AARCH64, 1, 0),
		BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
		BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
		BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_getpid, 3, 0),
		BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_openat, 2, 0),
		BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_newfstatat, 1, 0),
		BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
		// instruction_pointer: high word must match, low word in range
		BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, instruction_pointer) + 4),
		BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (uint32_t)(lo >> 32), 0, 4),
		BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, instruction_pointer)),
		BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, (uint32_t)lo, 0, 2),
		BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, (uint32_t)hi, 1, 0),
		BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
		BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),
	};
	struct sock_fprog prog = { sizeof f / sizeof f[0], f };
	return (int)syscall(SYS_seccomp, SECCOMP_SET_MODE_FILTER, 0, &prog);
}

static void set_prefix(const char *p)
{
	nix_prefix = p ? p : "";
	nix_prefix_len = strlen(nix_prefix);
}
