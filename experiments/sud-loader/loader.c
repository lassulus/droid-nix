// sud-loader (issue #1, phase 2): a minimal in-process launcher that runs
// real /nix/store binaries in the droid-nix app domain without proot.
//
// It installs one seccomp SECCOMP_RET_TRAP filter for the path-taking
// syscalls (+ execve/execveat, chdir, getcwd, readlinkat), exempting its own
// fixed-address syscall stub, and a SIGSYS handler that rewrites "/nix/..."
// to "<prefix>/nix/..." and reissues the syscall through the stub. It then
// acts as a userspace ELF loader: it maps the target binary and its
// PT_INTERP (a /nix/store ld.so) itself and jumps to the interpreter.
//
// A trapped execve/execveat re-execs the loader with the target as argument,
// so every new image gets the handler again (the filter and NO_NEW_PRIVS
// survive execve; the handler does not).
//
// Keep it minimal: no heuristics, no program-specific behaviour, no limits.
// Unsupported cases print a clear error instead of being papered over.

#define _GNU_SOURCE
#include <elf.h>
#include <errno.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <ucontext.h>
#include <unistd.h>

#ifndef SA_RESTORER
#define SA_RESTORER 0x04000000
#endif
#ifndef AT_EMPTY_PATH
#define AT_EMPTY_PATH 0x1000
#endif

#define PATHBUF 4096
#define PAGE 4096UL
#define PAGE_DOWN(x) ((x) & ~(PAGE - 1))
#define PAGE_UP(x) PAGE_DOWN((x) + PAGE - 1)

extern char allow_start[], allow_end[];
extern long raw_syscall6(long nr, long a0, long a1, long a2, long a3, long a4, long a5);
extern void sig_restorer(void);
extern void jump_to(void *entry, void *sp) __attribute__((noreturn));

#define SYS0(nr) raw_syscall6((nr), 0, 0, 0, 0, 0, 0)
#define SYS1(nr, a) raw_syscall6((nr), (long)(a), 0, 0, 0, 0, 0)
#define SYS2(nr, a, b) raw_syscall6((nr), (long)(a), (long)(b), 0, 0, 0, 0)
#define SYS3(nr, a, b, c) raw_syscall6((nr), (long)(a), (long)(b), (long)(c), 0, 0, 0)
#define SYS4(nr, a, b, c, d) raw_syscall6((nr), (long)(a), (long)(b), (long)(c), (long)(d), 0, 0)
#define SYS6(nr, a, b, c, d, e, f) \
	raw_syscall6((nr), (long)(a), (long)(b), (long)(c), (long)(d), (long)(e), (long)(f))

// The default on-device prefix; overridable with SUD_PREFIX for adb testing.
static const char *g_prefix = "/data/data/us.lassul.droidnix/files/usr";
static size_t g_prefixlen;
static const char *g_target;    // untranslated target path (for /proc/self/exe)
static const char *g_launcher;  // this loader's real path (for re-exec)
static volatile int g_debug;    // SUD_DEBUG: log each trapped syscall

// ---- tiny stdio-free helpers -------------------------------------------

static size_t zlen(const char *s)
{
	const char *p = s;
	while (*p)
		p++;
	return (size_t)(p - s);
}

static void out(int fd, const char *s)
{
	SYS3(SYS_write, fd, s, zlen(s));
}

static void die(const char *msg)
{
	out(2, "sud-loader: ");
	out(2, msg);
	out(2, "\n");
	SYS1(SYS_exit_group, 127);
	__builtin_unreachable();
}

static void die2(const char *msg, const char *arg)
{
	out(2, "sud-loader: ");
	out(2, msg);
	out(2, arg);
	out(2, "\n");
	SYS1(SYS_exit_group, 127);
	__builtin_unreachable();
}

static int utoa(unsigned long v, char *buf);

static void outn(int fd, long v)
{
	char b[24];
	if (v < 0) {
		out(fd, "-");
		v = -v;
	}
	utoa((unsigned long)v, b);
	out(fd, b);
}


// base-10 unsigned into buf, returns length.
static int utoa(unsigned long v, char *buf)
{
	char tmp[20];
	int n = 0;
	if (v == 0)
		tmp[n++] = '0';
	while (v) {
		tmp[n++] = '0' + (v % 10);
		v /= 10;
	}
	for (int i = 0; i < n; i++)
		buf[i] = tmp[n - 1 - i];
	buf[n] = 0;
	return n;
}

// ---- path translation --------------------------------------------------

static int is_nix(const char *p)
{
	return p && p[0] == '/' && p[1] == 'n' && p[2] == 'i' && p[3] == 'x' &&
	       (p[4] == '/' || p[4] == 0);
}

// "/nix/..." -> "<prefix>/nix/...". Writes into buf (>= PATHBUF). Returns the
// pointer the syscall should use (buf, or the original path untouched).
static const char *xlate(const char *path, char *buf)
{
	if (!is_nix(path))
		return path;
	size_t n = zlen(path);
	if (g_prefixlen + n + 1 > PATHBUF)
		return path;  // too long: leave it, the syscall will just fail cleanly
	memcpy(buf, g_prefix, g_prefixlen);
	memcpy(buf + g_prefixlen, path, n + 1);
	return buf;
}

// Reverse: if a result of length len starts with "<prefix>/nix", strip the
// prefix in place. len counts bytes (with or without a trailing NUL); the NUL,
// if present and included in len, moves with the rest.
static long unxlate(char *buf, long len)
{
	if (len > (long)g_prefixlen && memcmp(buf, g_prefix, g_prefixlen) == 0 &&
	    is_nix(buf + g_prefixlen)) {
		long rest = len - (long)g_prefixlen;
		memmove(buf, buf + g_prefixlen, rest);
		return rest;
	}
	return len;
}

// Resolve an absolute path to a real, prefix-based path, following any
// symlink chain at the final component. Store symlinks commonly point at
// absolute "/nix/store/..." targets, which the kernel would otherwise follow
// literally (and fail, since /nix does not exist). We read each link ourselves
// and re-translate "/nix" targets. Relative and dirfd-relative paths are left
// to the kernel (resolved against an already-real dirfd/cwd). This does not
// resolve "/nix" symlinks that appear on interior path components; full
// per-component canonicalisation (as proot does) is left as future work.
static const char *translate(const char *path, char *buf)
{
	if (!path || path[0] != '/')
		return path;  // relative: the kernel resolves it against the dirfd/cwd
	const char *cur = xlate(path, buf);
	if (cur != buf) {  // not a /nix path: copy into buf so we can rewrite it
		size_t n = zlen(cur);
		if (n >= PATHBUF)
			return cur;
		memcpy(buf, cur, n + 1);
	}
	char tmp[PATHBUF];
	for (int i = 0; i < 40; i++) {
		long n = raw_syscall6(SYS_readlinkat, (long)-100, (long)buf, (long)tmp,
				      PATHBUF - 1, 0, 0);
		if (n < 0)
			break;  // not a symlink (or missing): use buf as-is
		tmp[n] = 0;
		if (tmp[0] == '/') {  // absolute target
			const char *t = xlate(tmp, buf);
			if (t != buf) {  // absolute non-/nix target
				size_t m = zlen(t);
				if (m >= PATHBUF)
					break;
				memcpy(buf, t, m + 1);
			}
		} else {  // relative target: replace the final component of buf
			char *slash = strrchr(buf, '/');
			if (!slash)
				break;
			size_t tl = zlen(tmp);
			if ((size_t)(slash + 1 - buf) + tl >= PATHBUF)
				break;
			memcpy(slash + 1, tmp, tl + 1);  // ".." left for the kernel to resolve
		}
	}
	return buf;
}

static int is_proc_self_exe(const char *p)
{
	return p && strcmp(p, "/proc/self/exe") == 0;
}

// ---- the execve re-exec path -------------------------------------------

// Re-exec the loader itself: argv = [loader, TARGET, orig argv...], envp kept.
// On success this never returns; on failure it returns the (negative) errno.
static long reexec(const char *target, char **argv, char **envp)
{
	int n = 0;
	while (argv[n])
		n++;
	// VLA on the signal stack: loader + target + n original args + NULL.
	char *na[n + 3];
	na[0] = (char *)g_launcher;
	na[1] = (char *)target;
	for (int i = 0; i < n; i++)
		na[2 + i] = argv[i];
	na[2 + n] = 0;
	return SYS3(SYS_execve, g_launcher, na, envp);
}

// ---- the SIGSYS handler ------------------------------------------------

// Our SIGSYS sigaction. A raw rt_sigaction with SA_RESTORER makes sigreturn
// go through sig_restorer (in the exempt range), as in phase 1. Kept global so
// the handler can re-pin it when a program tries to change SIGSYS.
struct kernel_sigaction {
	void *handler;
	unsigned long flags;
	void *restorer;
	uint64_t mask;
};
static void sigsys_handler(int sig, siginfo_t *si, void *ctx);
static struct kernel_sigaction g_ka = { (void *)sigsys_handler, SA_SIGINFO | SA_RESTORER,
					(void *)sig_restorer, 0 };

static void sigsys_handler(int sig, siginfo_t *si, void *ctx)
{
	(void)sig;
	unsigned long *r = ((ucontext_t *)ctx)->uc_mcontext.regs;
	long nr = si->si_syscall;
	char b1[PATHBUF], b2[PATHBUF];

	if (g_debug) {
		out(2, "[pid ");
		outn(2, raw_syscall6(SYS_getpid, 0, 0, 0, 0, 0, 0));
		out(2, "] t nr=");
		outn(2, nr);
		out(2, "\n");
	}

	switch (nr) {
	// path at arg1, dirfd at arg0 (or AT_FDCWD); relative paths pass through.
	// openat(2): follow absolute /nix symlink chains ourselves.
	case SYS_openat:
	case SYS_openat2:
		r[1] = (unsigned long)translate((const char *)r[1], b1);
		r[0] = SYS6(nr, r[0], r[1], r[2], r[3], r[4], r[5]);
		return;

	case SYS_newfstatat:
	case SYS_statx:
	case SYS_faccessat:
	case SYS_faccessat2:
	case SYS_mkdirat:
	case SYS_unlinkat:
	case SYS_mknodat:
	case SYS_fchownat:
	case SYS_fchmodat:
	case SYS_utimensat:
		r[1] = (unsigned long)xlate((const char *)r[1], b1);
		r[0] = SYS6(nr, r[0], r[1], r[2], r[3], r[4], r[5]);
		return;

	// path at arg0.
	case SYS_chdir:
	case SYS_truncate:
	case SYS_statfs:
	case SYS_getxattr:
	case SYS_lgetxattr:
	case SYS_setxattr:
	case SYS_lsetxattr:
	case SYS_listxattr:
	case SYS_llistxattr:
	case SYS_removexattr:
	case SYS_lremovexattr:
		r[0] = (unsigned long)xlate((const char *)r[0], b1);
		r[0] = SYS6(nr, r[0], r[1], r[2], r[3], r[4], r[5]);
		return;

	// two paths: arg1 (old) and arg3 (new).
	case SYS_linkat:
	case SYS_renameat:
	case SYS_renameat2:
		r[1] = (unsigned long)xlate((const char *)r[1], b1);
		r[3] = (unsigned long)xlate((const char *)r[3], b2);
		r[0] = SYS6(nr, r[0], r[1], r[2], r[3], r[4], r[5]);
		return;

	// symlinkat(target, newdirfd, linkpath): translate only the linkpath
	// (arg2). The target (arg0) is literal symlink content, left as-is.
	case SYS_symlinkat:
		r[2] = (unsigned long)xlate((const char *)r[2], b2);
		r[0] = SYS6(nr, r[0], r[1], r[2], r[3], r[4], r[5]);
		return;

	// readlinkat(dirfd, path, buf, bufsiz): /proc/self/exe reports the
	// target; otherwise translate the path and reverse-translate the result.
	case SYS_readlinkat: {
		const char *path = (const char *)r[1];
		char *ubuf = (char *)r[2];
		long sz = (long)r[3];
		if (is_proc_self_exe(path)) {
			long n = (long)zlen(g_target);
			if (n > sz)
				n = sz;
			memcpy(ubuf, g_target, n);
			r[0] = (unsigned long)n;
			return;
		}
		r[1] = (unsigned long)xlate(path, b1);
		long ret = SYS6(nr, r[0], r[1], r[2], r[3], r[4], r[5]);
		if (ret > 0)
			ret = unxlate(ubuf, ret);
		r[0] = (unsigned long)ret;
		return;
	}

	// getcwd(buf, size): reverse-translate the cwd. The kernel returns the
	// length including the trailing NUL.
	case SYS_getcwd: {
		char *ubuf = (char *)r[0];
		long ret = SYS6(nr, r[0], r[1], r[2], r[3], r[4], r[5]);
		if (ret > 0)
			ret = unxlate(ubuf, ret);
		r[0] = (unsigned long)ret;
		return;
	}

	// Protect our own SIGSYS handler: programs (notably bash in its forked
	// children) reset signal dispositions, which would drop our handler and
	// make the next trapped syscall fatal. Force any change to SIGSYS to
	// re-pin our action, while still letting the kernel fill oldact and
	// handle every other signal normally.
	case SYS_rt_sigaction:
		if ((int)r[0] == SIGSYS && r[1])
			r[1] = (unsigned long)&g_ka;
		r[0] = SYS6(nr, r[0], r[1], r[2], r[3], r[4], r[5]);
		return;

	// Never let SIGSYS be blocked: a seccomp trap on a blocked SIGSYS is
	// force-killed. bash blocks signals around fork, so without this its
	// children die on their first trapped syscall. Clear SIGSYS from the
	// requested set for SIG_BLOCK/SIG_SETMASK (how 0/2); leave UNBLOCK.
	case SYS_rt_sigprocmask:
		if (r[1] && ((int)r[0] == 0 || (int)r[0] == 2)) {
			uint64_t set;
			memcpy(&set, (void *)r[1], 8);
			set &= ~(1ULL << (SIGSYS - 1));
			memcpy(b1, &set, 8);
			r[1] = (unsigned long)b1;
		}
		r[0] = SYS6(nr, r[0], r[1], r[2], r[3], r[4], r[5]);
		return;

	case SYS_execve:
		r[0] = (unsigned long)reexec((const char *)r[0], (char **)r[1], (char **)r[2]);
		return;

	// execveat(dirfd, path, argv, envp, flags).
	case SYS_execveat: {
		const char *path = (const char *)r[1];
		char fdpath[64];
		if (r[4] & AT_EMPTY_PATH) {
			// fexecve: resolve the fd to a real path via /proc/self/fd.
			memcpy(fdpath, "/proc/self/fd/", 14);
			utoa((unsigned long)(long)r[0], fdpath + 14);
			char real[PATHBUF];
			long n = SYS4(SYS_readlinkat, (long)-100 /*AT_FDCWD*/, fdpath, real,
				      sizeof real - 1);
			if (n <= 0)
				die("execveat(AT_EMPTY_PATH): cannot resolve fd");
			real[n] = 0;
			r[0] = (unsigned long)reexec(real, (char **)r[2], (char **)r[3]);
			return;
		}
		if (path && path[0] == '/') {
			r[0] = (unsigned long)reexec(path, (char **)r[2], (char **)r[3]);
			return;
		}
		die("execveat with a dirfd-relative path is not supported");
	}

	default:
		// This syscall is not one we translate, so our own filter did not
		// trap it: it reached us only because Android's app seccomp filter
		// traps it with SECCOMP_RET_TRAP (e.g. set_robust_list, rseq).
		// Reissuing it would trap again against Android's filter while
		// SIGSYS is masked and kill the process, so we emulate proot and
		// fail it with -ENOSYS. glibc/musl tolerate these the same way
		// they do under proot (and on kernels without the syscall).
		r[0] = (unsigned long)-ENOSYS;
		return;
	}
}

static void install_handler(void)
{
	if (raw_syscall6(SYS_rt_sigaction, SIGSYS, (long)&g_ka, 0, 8, 0, 0) < 0)
		die("rt_sigaction(SIGSYS) failed");
}

// A trapped execve re-execs the loader from inside the SIGSYS handler, where
// SIGSYS is masked; that mask survives execve, so the new image (and the
// target it jumps to) would otherwise start with SIGSYS blocked, and a
// seccomp trap on a blocked SIGSYS is force-killed regardless of the handler.
static void unblock_sigsys(void)
{
	uint64_t set = 1ULL << (SIGSYS - 1);
	raw_syscall6(SYS_rt_sigprocmask, 1 /*SIG_UNBLOCK*/, (long)&set, 0, 8, 0, 0);
}

// ---- seccomp filter ----------------------------------------------------

// Every path-taking syscall we translate, plus execve/execveat, chdir,
// getcwd and readlinkat. getpid/read/write/close/mmap/... are left untrapped.
static const int traced[] = {
	SYS_openat,    SYS_openat2,     SYS_newfstatat,  SYS_statx,
	SYS_faccessat, SYS_faccessat2,  SYS_readlinkat,  SYS_execve,
	SYS_execveat,  SYS_chdir,       SYS_getcwd,      SYS_mkdirat,
	SYS_unlinkat,  SYS_mknodat,     SYS_fchownat,    SYS_fchmodat,
	SYS_utimensat, SYS_linkat,      SYS_renameat,    SYS_renameat2,
	SYS_symlinkat, SYS_truncate,    SYS_statfs,      SYS_getxattr,
	SYS_lgetxattr, SYS_setxattr,    SYS_lsetxattr,   SYS_listxattr,
	SYS_llistxattr,SYS_removexattr, SYS_lremovexattr, SYS_rt_sigaction,
	SYS_rt_sigprocmask,
};
#define NTRACED (int)(sizeof traced / sizeof traced[0])

static void install_filter(void)
{
	uint64_t lo = (uintptr_t)allow_start, hi = (uintptr_t)allow_end;
	if ((lo >> 32) != (hi >> 32))
		die("exempt stub range crosses a 4GiB boundary");

	// Layout: arch check, load nr, one JEQ per traced syscall (match ->
	// jump to the IP-range check), else ALLOW; the IP-range check ALLOWs
	// the exempt stub and TRAPs everything else. Fixed part is 12
	// instructions (4 prologue + 1 no-match ALLOW + 7 IP-range check).
	struct sock_filter f[12 + NTRACED];
	int k = 0;
	f[k++] = (struct sock_filter)BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
					      offsetof(struct seccomp_data, arch));
	f[k++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_AARCH64, 1, 0);
	f[k++] = (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
	f[k++] = (struct sock_filter)BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
					      offsetof(struct seccomp_data, nr));
	// For compare i (0-based), skip (NTRACED - i) instructions on a match to
	// land exactly on the IP-range check (past the remaining compares and
	// the no-match ALLOW).
	for (int i = 0; i < NTRACED; i++)
		f[k++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, traced[i],
						      NTRACED - i, 0);
	f[k++] = (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
	// IP-range check (verified in phase 1).
	f[k++] = (struct sock_filter)BPF_STMT(
		BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, instruction_pointer) + 4);
	f[k++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, (uint32_t)(lo >> 32), 0, 4);
	f[k++] = (struct sock_filter)BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
					      offsetof(struct seccomp_data, instruction_pointer));
	f[k++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, (uint32_t)lo, 0, 2);
	f[k++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JGE | BPF_K, (uint32_t)hi, 1, 0);
	f[k++] = (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
	f[k++] = (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP);

	struct sock_fprog prog = { (unsigned short)k, f };
	if (SYS3(SYS_seccomp, SECCOMP_SET_MODE_FILTER, 0, &prog) != 0)
		die("seccomp(SET_MODE_FILTER) failed");
}

// ---- userspace ELF loader ----------------------------------------------

struct loaded {
	unsigned long entry;  // e_entry + bias
	unsigned long phdr;   // program-header table, mapped
	unsigned long phnum;
	unsigned long phent;
	unsigned long base;   // load bias
	char interp[PATHBUF]; // PT_INTERP string, untranslated
	int has_interp;
};

static long xopen(const char *path)
{
	char b[PATHBUF];
	return SYS4(SYS_openat, (long)-100 /*AT_FDCWD*/, translate(path, b), 0 /*O_RDONLY*/, 0);
}

static void xpread(int fd, void *buf, size_t n, off_t off, const char *what)
{
	long r = SYS4(SYS_pread64, fd, buf, n, off);
	if (r != (long)n)
		die2("short read of ", what);
}

static void *xmmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
	long r = SYS6(SYS_mmap, addr, len, prot, flags, fd, off);
	if (r < 0 && r > -4096)
		die("mmap failed");
	return (void *)r;
}

#define MAP_PRIVATE 0x02
#define MAP_FIXED 0x10
#define MAP_ANON 0x20
#define PROT_READ 1
#define PROT_WRITE 2
#define PROT_EXEC 4

static int seg_prot(uint32_t flags)
{
	int p = 0;
	if (flags & PF_R)
		p |= PROT_READ;
	if (flags & PF_W)
		p |= PROT_WRITE;
	if (flags & PF_X)
		p |= PROT_EXEC;
	return p;
}

static void load_elf(const char *path, struct loaded *l)
{
	memset(l, 0, sizeof *l);
	long fd = xopen(path);
	if (fd < 0) {
		out(2, "sud-loader: cannot open ");
		out(2, path);
		out(2, " errno=");
		outn(2, -fd);
		out(2, "\n");
		SYS1(SYS_exit_group, 127);
	}

	Elf64_Ehdr eh;
	xpread(fd, &eh, sizeof eh, 0, "ELF header");
	if (memcmp(eh.e_ident, ELFMAG, SELFMAG) != 0 || eh.e_ident[EI_CLASS] != ELFCLASS64 ||
	    eh.e_machine != EM_AARCH64)
		die2("not an aarch64 ELF64: ", path);
	// Only PIE (ET_DYN) targets are supported: a non-PIE ET_EXEC loads at
	// its own fixed vaddr (typically 0x400000), which collides with this
	// non-PIE loader's own image. Supporting it needs the loader linked out
	// of the way (a high fixed address). gcc's bootstrap xgcc is ET_EXEC.
	if (eh.e_type != ET_DYN)
		die2("unsupported non-PIE (ET_EXEC) target; only PIE is supported: ", path);
	if (eh.e_phnum == 0 || eh.e_phnum > 128)
		die2("bad e_phnum in ", path);

	Elf64_Phdr ph[128];
	xpread(fd, ph, (size_t)eh.e_phnum * sizeof ph[0], eh.e_phoff, "program headers");

	// span of all PT_LOAD segments
	unsigned long minv = ~0UL, maxv = 0;
	for (int i = 0; i < eh.e_phnum; i++) {
		if (ph[i].p_type != PT_LOAD)
			continue;
		if (ph[i].p_vaddr < minv)
			minv = ph[i].p_vaddr;
		if (ph[i].p_vaddr + ph[i].p_memsz > maxv)
			maxv = ph[i].p_vaddr + ph[i].p_memsz;
	}
	if (minv == ~0UL)
		die2("no PT_LOAD in ", path);
	minv = PAGE_DOWN(minv);
	maxv = PAGE_UP(maxv);

	// Reserve the whole span so the segments stay contiguous, letting the
	// kernel pick a high base that cannot collide with the non-PIE loader at
	// its fixed low address.
	void *res = xmmap(0, maxv - minv, PROT_READ, MAP_PRIVATE | MAP_ANON, -1, 0);
	unsigned long bias = (unsigned long)res - minv;

	for (int i = 0; i < eh.e_phnum; i++) {
		if (ph[i].p_type != PT_LOAD)
			continue;
		unsigned long va = bias + ph[i].p_vaddr;
		unsigned long start = PAGE_DOWN(va);
		unsigned long fend = PAGE_UP(va + ph[i].p_filesz);
		unsigned long mend = PAGE_UP(va + ph[i].p_memsz);
		unsigned long foff = PAGE_DOWN(ph[i].p_offset);
		int prot = seg_prot(ph[i].p_flags);
		// File-backed part. Map writable first if we must zero a partial
		// bss page, then restore prot.
		int needzero = ph[i].p_memsz > ph[i].p_filesz;
		int mprot = prot | (needzero ? PROT_WRITE : 0);
		if (fend > start)
			xmmap((void *)start, fend - start, mprot,
			      MAP_PRIVATE | MAP_FIXED, fd, foff);
		if (needzero) {
			unsigned long bss = va + ph[i].p_filesz;
			memset((void *)bss, 0, fend - bss);  // tail of the last file page
			if (mend > fend)
				xmmap((void *)fend, mend - fend, prot,
				      MAP_PRIVATE | MAP_FIXED | MAP_ANON, -1, 0);
			if (mprot != prot && fend > start)
				SYS3(SYS_mprotect, start, fend - start, prot);
		}
	}

	l->base = bias;
	l->entry = bias + eh.e_entry;
	l->phdr = bias + eh.e_phoff;  // e_phoff lies inside the first PT_LOAD
	l->phnum = eh.e_phnum;
	l->phent = eh.e_phentsize;

	for (int i = 0; i < eh.e_phnum; i++) {
		if (ph[i].p_type != PT_INTERP) {
			continue;
		}
		if (ph[i].p_filesz >= sizeof l->interp)
			die2("PT_INTERP too long in ", path);
		xpread(fd, l->interp, ph[i].p_filesz, ph[i].p_offset, "PT_INTERP");
		l->interp[ph[i].p_filesz] = 0;
		l->has_interp = 1;
	}
	SYS1(SYS_close, fd);
}

// ---- stack construction ------------------------------------------------

#define STACK_SIZE (8UL * 1024 * 1024)

static unsigned long auxval(unsigned long *av, unsigned long type)
{
	for (; av[0]; av += 2)
		if (av[0] == type)
			return av[1];
	return 0;
}

// Builds argc/argv/envp/auxv on a fresh stack and returns the SP to jump with.
static void *build_stack(int targc, char **targv, char **envp, unsigned long *our_auxv,
			 struct loaded *exe, struct loaded *interp)
{
	char *stk = xmmap(0, STACK_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	char *top = stk + STACK_SIZE;

	int nenv = 0;
	while (envp[nenv])
		nenv++;

	// Strings grow down from the top.
	char *s = top;
	char *p_argv[targc];
	char *p_env[nenv];
	for (int i = 0; i < targc; i++) {
		size_t n = zlen(targv[i]) + 1;
		s -= n;
		memcpy(s, targv[i], n);
		p_argv[i] = s;
	}
	for (int i = 0; i < nenv; i++) {
		size_t n = zlen(envp[i]) + 1;
		s -= n;
		memcpy(s, envp[i], n);
		p_env[i] = s;
	}
	s -= 8;  // AT_PLATFORM
	memcpy(s, "aarch64", 8);
	char *p_platform = s;
	s -= 16;  // AT_RANDOM
	char *p_random = s;
	unsigned long rnd = auxval(our_auxv, AT_RANDOM);
	if (rnd)
		memcpy(p_random, (void *)rnd, 16);
	size_t tn = zlen(g_target) + 1;  // AT_EXECFN
	s -= tn;
	memcpy(s, g_target, tn);
	char *p_execfn = s;

	// auxv: fixed set below (count the pairs, including AT_NULL).
	struct {
		unsigned long t, v;
	} aux[24];
	int na = 0;
#define AUX(T, V)             \
	do {                  \
		aux[na].t = (T); \
		aux[na].v = (unsigned long)(V); \
		na++;         \
	} while (0)
	AUX(AT_PHDR, exe->phdr);
	AUX(AT_PHENT, exe->phent);
	AUX(AT_PHNUM, exe->phnum);
	AUX(AT_PAGESZ, PAGE);
	AUX(AT_BASE, interp->has_interp ? interp->base : 0);
	AUX(AT_FLAGS, 0);
	AUX(AT_ENTRY, exe->entry);
	AUX(AT_UID, auxval(our_auxv, AT_UID));
	AUX(AT_EUID, auxval(our_auxv, AT_EUID));
	AUX(AT_GID, auxval(our_auxv, AT_GID));
	AUX(AT_EGID, auxval(our_auxv, AT_EGID));
	AUX(AT_SECURE, 0);
	AUX(AT_CLKTCK, auxval(our_auxv, AT_CLKTCK));
	AUX(AT_HWCAP, auxval(our_auxv, AT_HWCAP));
	AUX(AT_HWCAP2, auxval(our_auxv, AT_HWCAP2));
	AUX(AT_RANDOM, p_random);
	AUX(AT_PLATFORM, p_platform);
	AUX(AT_EXECFN, p_execfn);
	unsigned long vdso = auxval(our_auxv, AT_SYSINFO_EHDR);
	if (vdso)
		AUX(AT_SYSINFO_EHDR, vdso);
	AUX(AT_NULL, 0);
#undef AUX

	// Array block: argc, argv[], NULL, envp[], NULL, auxv[]. Place it so the
	// final SP is 16-byte aligned (as the kernel delivers it).
	size_t words = 1 + (targc + 1) + (nenv + 1) + (size_t)na * 2;
	unsigned long *sp = (unsigned long *)(((uintptr_t)s - words * 8) & ~15UL);
	unsigned long *w = sp;
	*w++ = (unsigned long)targc;
	for (int i = 0; i < targc; i++)
		*w++ = (unsigned long)p_argv[i];
	*w++ = 0;
	for (int i = 0; i < nenv; i++)
		*w++ = (unsigned long)p_env[i];
	*w++ = 0;
	for (int i = 0; i < na; i++) {
		*w++ = aux[i].t;
		*w++ = aux[i].v;
	}
	return sp;
}

// ---- shebang (#!) handling ---------------------------------------------

// If *target names a "#!" script, rewrite *target/*argv to run its
// interpreter instead, as the kernel's binfmt_script would, but translating
// the interpreter's /nix path. Loops for a bounded number of nested scripts.
static char sheb_line[4][256];
static char *sheb_argv[4][96];

static void expand_shebang(const char **target, char ***pargv, int *pargc)
{
	for (int it = 0; it < 4; it++) {
		char b[PATHBUF];
		long fd = SYS4(SYS_openat, (long)-100, translate(*target, b), 0, 0);
		if (fd < 0)
			return;
		char *line = sheb_line[it];
		long n = SYS3(SYS_read, fd, line, 255);
		SYS1(SYS_close, fd);
		if (n < 2 || line[0] != '#' || line[1] != '!')
			return;
		line[n] = 0;
		for (char *q = line; *q; q++)
			if (*q == '\n') {
				*q = 0;
				break;
			}
		char *p = line + 2;
		while (*p == ' ' || *p == '\t')
			p++;
		char *interp = p;
		while (*p && *p != ' ' && *p != '\t')
			p++;
		char *arg = 0;
		if (*p) {
			*p++ = 0;
			while (*p == ' ' || *p == '\t')
				p++;
			if (*p) {
				arg = p;
				char *q = p + zlen(p);
				while (q > p && (q[-1] == ' ' || q[-1] == '\t' || q[-1] == '\r'))
					*--q = 0;
			}
		}
		// New argv: interp [arg] script origargs...  (drop the script's argv0)
		char **A = sheb_argv[it];
		int k = 0;
		A[k++] = interp;
		if (arg)
			A[k++] = arg;
		A[k++] = (char *)*target;
		for (int j = 1; j < *pargc && k < 94; j++)
			A[k++] = (*pargv)[j];
		A[k] = 0;
		*target = interp;
		*pargv = A;
		*pargc = k;
	}
}

// ---- entry -------------------------------------------------------------

int main(int argc, char **argv, char **envp)
{
	// musl passes auxv right after envp.
	unsigned long *our_auxv;
	{
		char **e = envp;
		while (*e)
			e++;
		our_auxv = (unsigned long *)(e + 1);
	}

	const char *pfx = getenv("SUD_PREFIX");
	if (pfx && *pfx)
		g_prefix = pfx;
	g_prefixlen = zlen(g_prefix);

	if (argc < 2)
		die("usage: loader <target> [argv0 argv1 ...]   (target may be a /nix/store path)");

	g_target = argv[1];

	// This loader's own real path, for re-exec on a trapped execve.
	static char self[PATHBUF];
	long sn = SYS4(SYS_readlinkat, (long)-100 /*AT_FDCWD*/, "/proc/self/exe", self,
		       sizeof self - 1);
	if (sn <= 0)
		die("cannot readlink /proc/self/exe");
	self[sn] = 0;
	g_launcher = self;

	// The target's argv: explicit args if given, else just the target path.
	int targc = (argc > 2) ? argc - 2 : 1;
	char *one[1] = { (char *)g_target };
	char **targv = (argc > 2) ? &argv[2] : one;

	int first = getenv("SUD_LOADER_ACTIVE") == NULL;
	g_debug = getenv("SUD_DEBUG") != NULL;

	// Handler before the filter, so no trapped syscall can arrive unhandled.
	install_handler();
	unblock_sigsys();
	if (first) {
		if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0)
			die("prctl(NO_NEW_PRIVS) failed");
		install_filter();
	}
	// Mark the process tree so a re-exec does not stack a second filter.
	setenv("SUD_LOADER_ACTIVE", "1", 1);
	// Rebuild envp: setenv may have reallocated environ.
	extern char **environ;
	envp = environ;

	// Run "#!" scripts by loading their (translated) interpreter.
	expand_shebang(&g_target, &targv, &targc);

	struct loaded exe, interp;
	load_elf(g_target, &exe);
	interp.has_interp = 0;
	if (exe.has_interp)
		load_elf(exe.interp, &interp);

	void *sp = build_stack(targc, targv, envp, our_auxv, &exe, &interp);
	unsigned long entry = exe.has_interp ? interp.entry : exe.entry;
	jump_to((void *)entry, sp);
}
