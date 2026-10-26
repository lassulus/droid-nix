// launcher: runs droid-nix's /nix/store binaries in the app domain, in the
// guest view of the login's binds (/nix, /etc, /tmp, ...), in-process. It
// replaced proot (issue #1, see experiments/sud-loader for how it came about).
//
//   launcher [-b host:guest]... TARGET [argv0 argv1 ...]
//
// It installs one seccomp SECCOMP_RET_TRAP filter for every syscall that
// takes a path (+ execve/execveat, getcwd, rt_sigaction/rt_sigprocmask),
// exempting its own fixed-address syscall stub, and a SIGSYS handler that
// canonicalises each path the way the kernel's path walk would, but in the
// guest view given by proot-style binds ("-b host:guest", e.g.
// "<prefix>/nix:/nix"): every component is checked, and symlinks are read
// and followed by the handler, so an absolute "/nix/..." target is
// re-translated. The syscall is then reissued through the stub. It
// also acts as a userspace ELF loader: it maps the target binary (PIE or not)
// and its PT_INTERP (a /nix/store ld.so) itself and jumps to the interpreter.
//
// A trapped execve/execveat re-execs the loader with the target as argument,
// so every new image gets the handler again (the filter and NO_NEW_PRIVS
// survive execve; the handler does not).
//
// Keep it minimal: no heuristics, no program-specific behaviour, no limits
// beyond the kernel's own. Unsupported cases print a clear error instead of
// being papered over.

#define _GNU_SOURCE
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/audit.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/prctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <termios.h>
#include <sys/types.h>
#include <ucontext.h>
#include <unistd.h>

#ifndef SA_RESTORER
#define SA_RESTORER 0x04000000
#endif
#ifndef AT_EMPTY_PATH
#define AT_EMPTY_PATH 0x1000
#endif
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif

// Path syscalls newer than this toolchain's musl headers (asm-generic numbers).
#ifndef SYS_fchmodat2
#define SYS_fchmodat2 452
#endif
#ifndef SYS_clone3
#define SYS_clone3 435
#endif
#define SYS_setxattrat 463
#define SYS_getxattrat 464
#define SYS_listxattrat 465
#define SYS_removexattrat 466
#define SYS_open_tree_attr 467
#define SYS_file_getattr 468
#define SYS_file_setattr 469

#define PATHBUF 4096
#define PAGE 4096UL
#define PAGE_DOWN(x) ((x) & ~(PAGE - 1))
#define PAGE_UP(x) PAGE_DOWN((x) + PAGE - 1)

// The kernel's own limits, mirrored so errors match what it would report.
#define MAXSYMLINKS 40      // symlinks followed in one lookup (ELOOP)
#define BINPRM_BUF_SIZE 256 // bytes of a "#!" line the kernel reads
#define MAX_INTERP_DEPTH 5  // nested "#!" interpreters (exec_binprm, ELOOP)

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

static const char *g_target;   // canonical guest path of the image (/proc/self/exe)
static const char *g_execfn;    // the path as given (AT_EXECFN)
static const char *g_launcher;  // this loader's real path (for re-exec)
static volatile int g_debug;    // LAUNCHER_DEBUG: log each trapped syscall

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
	out(2, "launcher: ");
	out(2, msg);
	out(2, "\n");
	SYS1(SYS_exit_group, 127);
	__builtin_unreachable();
}

static void die2(const char *msg, const char *arg)
{
	out(2, "launcher: ");
	out(2, msg);
	out(2, arg);
	out(2, "\n");
	SYS1(SYS_exit_group, 127);
	__builtin_unreachable();
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

static void die_errno(const char *msg, const char *arg, long err)
{
	out(2, "launcher: ");
	out(2, msg);
	out(2, arg);
	out(2, ": errno ");
	outn(2, -err);
	out(2, "\n");
	SYS1(SYS_exit_group, 127);
	__builtin_unreachable();
}

// ---- binds: proot's -b host:guest as one prefix table ------------------

// One bind, canonical (no trailing "/"). host and guest are not
// NUL-terminated (they point into arg); arg is "host:guest", which every
// re-exec gets on its command line.
struct bind {
	const char *host, *guest, *arg;
	size_t hlen, glen;
};
static struct bind *g_binds;
static int g_nbinds;

// Is p (length pl) the directory d (length dl) or inside it?
static int under(const char *p, size_t pl, const char *d, size_t dl)
{
	return pl >= dl && memcmp(p, d, dl) == 0 && (pl == dl || p[dl] == '/');
}

// Guest path g (length gl; 0 is the root) to its host path in out: through the
// bind with the longest guest path containing g, as proot's
// substitute_binding(GUEST); outside every bind, the host path is g itself
// (proot's rootfs is "/"). Returns the length or -ENAMETOOLONG.
static long to_host(const char *g, size_t gl, char *out)
{
	const struct bind *b = 0;
	for (int i = 0; i < g_nbinds; i++)
		if (under(g, gl, g_binds[i].guest, g_binds[i].glen) &&
		    (!b || g_binds[i].glen > b->glen))
			b = &g_binds[i];
	const char *h = "", *rest = g;
	size_t hl = 0, rl = gl;
	if (b) {
		h = b->host;
		hl = b->hlen;
		rest = g + b->glen;
		rl = gl - b->glen;
		if (hl == 1 && rl)
			hl = 0;  // host "/": "/x", not "//x"
	}
	if (hl + rl == 0) {
		out[0] = '/';
		out[1] = 0;
		return 1;
	}
	if (hl + rl >= PATHBUF)
		return -ENAMETOOLONG;
	memcpy(out, h, hl);
	memcpy(out + hl, rest, rl);
	out[hl + rl] = 0;
	return (long)(hl + rl);
}

// The bind with the longest host path containing the host path p, as proot's
// get_binding(HOST). Host "/" never counts: proot's rootfs binding "/:/" comes
// first among equal host paths, so it wins over "-b /:/android".
static const struct bind *host_bind(const char *p, size_t pl)
{
	const struct bind *b = 0;
	for (int i = 0; i < g_nbinds; i++)
		if (g_binds[i].hlen > 1 && under(p, pl, g_binds[i].host, g_binds[i].hlen) &&
		    (!b || g_binds[i].hlen > b->hlen))
			b = &g_binds[i];
	return b;
}

// Host path to guest path, in place in buf (capacity PATHBUF, len without the
// NUL): a path inside a bind's host path gets the bind's guest path in front,
// as proot's substitute_binding(HOST); any other path is unchanged. Returns
// the new length or -ENAMETOOLONG.
static long to_guest(char *buf, long len)
{
	const struct bind *b = host_bind(buf, (size_t)len);
	if (!b)
		return len;
	long rl = len - (long)b->hlen;
	if ((long)b->glen + rl >= (long)PATHBUF)
		return -ENAMETOOLONG;
	memmove(buf + b->glen, buf + b->hlen, (size_t)rl);
	memcpy(buf, b->guest, b->glen);
	buf[b->glen + rl] = 0;
	return (long)b->glen + rl;
}

// Is the path component c (length cl) the last component of a bind's guest
// path? A walk can only enter a bind through such a name.
static int bind_name(const char *c, size_t cl)
{
	for (int i = 0; i < g_nbinds; i++) {
		const char *e = g_binds[i].guest + g_binds[i].glen, *s = e;
		while (s[-1] != '/')
			s--;
		if ((size_t)(e - s) == cl && memcmp(s, c, cl) == 0)
			return 1;
	}
	return 0;
}

static int is_proc(const char *p)
{
	return p[0] == '/' && p[1] == 'p' && p[2] == 'r' && p[3] == 'o' && p[4] == 'c' &&
	       (p[5] == '/' || p[5] == 0);
}

// ---- path canonicalisation ---------------------------------------------

// The guest path of dirfd (or of the cwd for AT_FDCWD), written to g
// (PATHBUF). Returns its length with the root as "" (length 0), or a negative
// errno.
static long guest_base(long dirfd, char *g)
{
	long n;
	if (dirfd == AT_FDCWD) {
		n = SYS2(SYS_getcwd, g, PATHBUF);
		if (n < 0)
			return n;
		n--;  // getcwd counts the NUL
	} else {
		char fdp[40];
		memcpy(fdp, "/proc/self/fd/", 14);
		utoa((unsigned long)dirfd, fdp + 14);
		n = SYS4(SYS_readlinkat, AT_FDCWD, fdp, g, PATHBUF - 1);
		if (n < 0)
			return -EBADF;
		if (n >= (long)PATHBUF - 1)
			return -ENAMETOOLONG;
		g[n] = 0;
	}
	if (g[0] != '/')
		return -ENOTDIR;  // a pipe, socket, anon inode, "(unreachable)" cwd
	n = to_guest(g, n);
	if (n == 1)
		n = 0;
	if (n >= 0)
		g[n] = 0;
	return n;
}

// A relative path can go to the kernel unchanged, resolved against the real
// dirfd/cwd, when the guest walk could not differ: no ".." (which could climb
// out of a bind), no component named like a bind's guest path (the only way
// to step into a bind), and no symlink among the components the kernel
// follows (its target could be "/nix/..."). Returns 0 for "unchanged", 1 for
// "needs the full walk", or the negative errno the kernel would report for an
// intermediate component. tmp is scratch.
static long rel_plain(long dirfd, const char *path, int follow, char *tmp)
{
	size_t pl = zlen(path);
	if (pl >= PATHBUF)
		return 0;  // the kernel reports ENAMETOOLONG
	memcpy(tmp, path, pl + 1);
	char *p = tmp;
	for (;;) {
		while (*p == '/')
			p++;
		if (!*p)
			return 0;
		char *c = p;
		while (*p && *p != '/')
			p++;
		char *q = p;
		while (*q == '/')
			q++;
		int last = !*q;
		if (p - c == 2 && c[0] == '.' && c[1] == '.')
			return 1;
		if (p - c == 1 && c[0] == '.')
			continue;
		if (bind_name(c, (size_t)(p - c)))
			return 1;
		if (last && !follow && q == p)
			return 0;
		char save = *p, one;
		*p = 0;
		long n = SYS4(SYS_readlinkat, dirfd, tmp, &one, 1);
		*p = save;
		if (n >= 0)
			return 1;
		if (n != -EINVAL)
			return last ? 0 : n;
	}
}

// Canonicalise (dirfd, path) in the guest view as the kernel's path walk
// would (proot's approach): starting from "/" or the guest path of
// dirfd/cwd, every component is appended and, unless it is the final one and
// !follow, its host path (to_host) is checked with readlink; a symlink is
// followed by splicing its target in front of the rest (an absolute target
// restarts at "/", so "/nix/..." targets are re-translated), ".." pops a
// component of the already-resolved path, and a trailing slash or final "."
// keeps its meaning. procfs magic links (/proc/<pid>/fd/N, cwd, ...) name
// kernel objects rather than paths, so at the first symlink under /proc the
// rest is left to the kernel. A missing intermediate component fails with the
// kernel's errno; a missing final one is passed on (O_CREAT, mkdir, ...).
//
// Writes the guest path to g (PATHBUF) and returns its length, or -errno.
// hb (PATHBUF) is scratch for host paths.
static long walk(long dirfd, const char *path, int follow, char *g, char *hb)
{
	size_t pl = zlen(path);
	if (pl >= PATHBUF)
		return -ENAMETOOLONG;
	// The unconsumed rest of the path, right-aligned in rb; the consumed
	// space in front of it receives symlink targets.
	char rb[PATHBUF];
	char *rem = rb + PATHBUF - 1 - pl;
	memcpy(rem, path, pl + 1);
	size_t gl = 0;
	g[0] = 0;
	if (path[0] != '/') {
		long n = guest_base(dirfd, g);
		if (n < 0)
			return n;
		gl = (size_t)n;
	}
	int links = 0, trail = 0, dot = 0;
	for (;;) {
		while (*rem == '/')
			rem++;
		if (!*rem)
			break;
		char *c = rem;
		while (*rem && *rem != '/')
			rem++;
		size_t cl = (size_t)(rem - c);
		char *q = rem;
		while (*q == '/')
			q++;
		int last = !*q;
		trail = last && q != rem;
		dot = 0;
		if (c[0] == '.' && (cl == 1 || (cl == 2 && c[1] == '.'))) {
			if (cl == 2) {
				while (gl && g[gl - 1] != '/')
					gl--;
				if (gl)
					gl--;
				g[gl] = 0;
			}
			dot = last;
			continue;
		}
		if (gl + 1 + cl >= PATHBUF)
			return -ENAMETOOLONG;
		size_t prev = gl;
		g[gl++] = '/';
		memcpy(g + gl, c, cl);
		gl += cl;
		g[gl] = 0;
		if (last && !follow && !trail)
			break;
		long hl = to_host(g, gl, hb);
		if (hl < 0)
			return hl;
		size_t room = (size_t)(rem - rb);
		long n = SYS4(SYS_readlinkat, AT_FDCWD, hb, rb, room);
		if (n == -EINVAL)
			continue;  // not a symlink
		if (n < 0) {
			if (last)
				break;
			return n;
		}
		if (is_proc(g)) {
			size_t rl = zlen(rem);
			if (gl + rl >= PATHBUF)
				return -ENAMETOOLONG;
			memcpy(g + gl, rem, rl + 1);
			gl += rl;
			trail = dot = 0;
			break;
		}
		if (n >= (long)room)
			return -ENAMETOOLONG;
		if (n == 0)
			return -ENOENT;
		if (++links > MAXSYMLINKS)
			return -ELOOP;
		gl = rb[0] == '/' ? 0 : prev;
		g[gl] = 0;
		rem -= n;
		memmove(rem, rb, (size_t)n);
	}
	if (dot || trail) {
		const char *sfx = dot ? "/." : "/";
		size_t sl = zlen(sfx);
		if (gl + sl >= PATHBUF)
			return -ENAMETOOLONG;
		memcpy(g + gl, sfx, sl + 1);
		gl += sl;
	}
	if (!gl) {
		g[0] = '/';
		g[1] = 0;
		gl = 1;
	}
	return (long)gl;
}

// The host path the kernel should get for (dirfd, path): *res is out (an
// absolute host path, PATHBUF) or the original path when it can go to the
// kernel unchanged (rel_plain). Returns 0 or -errno.
static long canon(long dirfd, const char *path, int follow, char *out, const char **res)
{
	*res = path;
	if (!path || !*path)
		return 0;  // NULL / "": AT_EMPTY_PATH or the kernel's own error
	if (path[0] != '/') {
		long r = rel_plain(dirfd, path, follow, out);
		if (r <= 0)
			return r;
	}
	char g[PATHBUF];
	long n = walk(dirfd, path, follow, g, out);
	if (n < 0)
		return n;
	n = to_host(g, (size_t)n, out);
	if (n < 0)
		return n;
	*res = out;
	return 0;
}

// (dirfd, path) as an absolute guest path: path itself if absolute, else the
// guest path of dirfd/cwd joined with it ("" names the dirfd itself). *res is
// path or buf (PATHBUF). Returns 0 or -errno.
static long guest_abs(long dirfd, const char *path, char *buf, const char **res)
{
	*res = path;
	if (path[0] == '/')
		return 0;
	long n = guest_base(dirfd, buf);
	if (n < 0)
		return n;
	size_t pl = zlen(path);
	if ((size_t)n + 1 + pl >= PATHBUF)
		return -ENAMETOOLONG;
	if (pl) {
		buf[n] = '/';
		memcpy(buf + n + 1, path, pl + 1);
	} else if (!n) {
		buf[0] = '/';
		buf[1] = 0;
	}
	*res = buf;
	return 0;
}

// ---- execve: checks, "#!" and the re-exec ------------------------------

// Split a "#!" line in place, as binfmt_script does: interpreter, then one
// optional argument (the rest of the line, trailing blanks removed). Returns
// the interpreter, or NULL if the line has none.
static char *parse_shebang(char *line, long n, char **arg)
{
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
	*arg = 0;
	if (*p) {
		*p++ = 0;
		while (*p == ' ' || *p == '\t')
			p++;
		if (*p) {
			*arg = p;
			char *q = p + zlen(p);
			while (q > p && (q[-1] == ' ' || q[-1] == '\t' || q[-1] == '\r'))
				*--q = 0;
		}
	}
	return *interp ? interp : 0;
}

// Open path (guest view) read-only and read its first bytes into line.
// Returns the byte count or -errno; *isreg reports a regular file.
static long read_head(const char *path, char *line, int check_exec)
{
	char buf[PATHBUF];
	const char *h;
	long e = canon(AT_FDCWD, path, 1, buf, &h);
	if (e < 0)
		return e;
	if (check_exec) {
		e = SYS4(SYS_faccessat, AT_FDCWD, h, X_OK, 0);
		if (e < 0)
			return e;
		struct stat st;
		e = SYS4(SYS_newfstatat, AT_FDCWD, h, &st, 0);
		if (e < 0)
			return e;
		if (!S_ISREG(st.st_mode))
			return -EACCES;
	}
	long fd = SYS4(SYS_openat, AT_FDCWD, h, O_RDONLY | O_CLOEXEC, 0);
	if (fd < 0)
		return fd;
	long n = SYS3(SYS_read, fd, line, BINPRM_BUF_SIZE - 1);
	SYS1(SYS_close, fd);
	return n;
}

// What the kernel's execve would fail with for path (0 if it would run).
// Checked before re-exec'ing ourselves, after which an error could no longer
// be returned to the caller (execvp's PATH search relies on ENOENT/EACCES).
static long exec_check(const char *path)
{
	char line[BINPRM_BUF_SIZE], next[BINPRM_BUF_SIZE];
	for (int depth = 0;; depth++) {
		if (depth > MAX_INTERP_DEPTH)
			return -ELOOP;
		long n = read_head(path, line, 1);
		if (n < 0)
			return n;
		if (n >= 4 && memcmp(line, ELFMAG, SELFMAG) == 0)
			return 0;
		if (n < 2 || line[0] != '#' || line[1] != '!')
			return -ENOEXEC;
		char *arg, *interp = parse_shebang(line, n, &arg);
		if (!interp)
			return -ENOEXEC;
		memcpy(next, interp, zlen(interp) + 1);
		path = next;
	}
}

// Re-exec the loader itself: argv = [loader, --reexec, -b host:guest ...,
// TARGET, orig argv...], envp kept. "--reexec" tells the new image the filter
// is already installed and the binds are canonical; both are passed in argv
// because programs (nix for its builders) clear the environment. On success
// this never returns; else it returns -errno.
static long reexec(const char *target, char **argv, char **envp)
{
	long e = exec_check(target);
	if (e < 0)
		return e;
	int n = 0;
	while (argv && argv[n])
		n++;
	// VLA on the signal stack: loader + flag + binds + target + n args + NULL.
	char *na[n + 4 + 2 * g_nbinds];
	int k = 0;
	na[k++] = (char *)g_launcher;
	na[k++] = "--reexec";
	for (int i = 0; i < g_nbinds; i++) {
		na[k++] = "-b";
		na[k++] = (char *)g_binds[i].arg;
	}
	na[k++] = (char *)target;
	for (int i = 0; i < n; i++)
		na[k++] = argv[i];
	na[k] = 0;
	return SYS3(SYS_execve, g_launcher, na, envp);
}

// ---- the SIGSYS handler ------------------------------------------------

// The termios2 ioctls (asm-generic numbers, sizeof(struct termios2) == 44)
// plus TCSETSF, which Android's app SELinux policy denies on ttys; trapped
// by cmd so every other ioctl stays untrapped. See the SYS_ioctl case.
#define TCGETS2 0x802c542aU
#define TCSETS2 0x402c542bU
#define TCSETSW2 0x402c542cU
#define TCSETSF2 0x402c542dU
static const uint32_t traced_ioctls[] = { TCGETS2, TCSETS2, TCSETSW2, TCSETSF2, TCSETSF };
#define NIOCTLS (int)(sizeof traced_ioctls / sizeof traced_ioctls[0])

// Baud rate of a CBAUD code (Bnnn), as the kernel's tty_termios_baud_rate.
static uint32_t baud(uint32_t code)
{
	static const uint32_t lo[16] = { 0, 50, 75, 110, 134, 150, 200, 300, 600, 1200,
					 1800, 2400, 4800, 9600, 19200, 38400 };
	static const uint32_t hi[16] = { 0, 57600, 115200, 230400, 460800, 500000,
					 576000, 921600, 1000000, 1152000, 1500000,
					 2000000, 2500000, 3000000, 3500000, 4000000 };
	return (code & CBAUDEX) ? hi[code & 017] : lo[code & 017];
}

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

// Canonicalise the path in r[pa] (dirfd in r[da], or the cwd if da < 0) into
// buf and point r[pa] at the result. On error, fail the syscall with it.
static int fix(unsigned long *r, int da, int pa, int follow, char *buf)
{
	const char *res;
	long dirfd = da < 0 ? AT_FDCWD : (long)(int)r[da];
	long e = canon(dirfd, (const char *)r[pa], follow, buf, &res);
	if (e < 0) {
		r[0] = (unsigned long)e;
		return -1;
	}
	r[pa] = (unsigned long)res;
	return 0;
}

#define FIX(da, pa, follow, buf)                    \
	do {                                        \
		if (fix(r, (da), (pa), (follow), (buf))) \
			return;                     \
	} while (0)

// AF_UNIX socket path (bind creates it: no follow; connect follows).
static int fix_sockaddr(unsigned long *r, int follow, char *buf, char *sabuf)
{
	const char *sa = (const char *)r[1];
	long len = (long)r[2];
	if (!sa || len <= 2 || *(const uint16_t *)sa != AF_UNIX || sa[2] == 0)
		return 0;  // not a pathname socket (abstract, unnamed, other family)
	long pl = 0;
	while (2 + pl < len && sa[2 + pl])
		pl++;
	if (pl >= PATHBUF) {
		r[0] = (unsigned long)-ENAMETOOLONG;
		return -1;
	}
	char path[pl + 1];
	memcpy(path, sa + 2, (size_t)pl);
	path[pl] = 0;
	const char *res;
	long e = canon(AT_FDCWD, path, follow, buf, &res);
	if (e < 0) {
		r[0] = (unsigned long)e;
		return -1;
	}
	size_t rl = zlen(res);
	if (rl >= 108) {  // sizeof sun_path
		r[0] = (unsigned long)-ENAMETOOLONG;
		return -1;
	}
	*(uint16_t *)sabuf = AF_UNIX;
	memcpy(sabuf + 2, res, rl + 1);
	r[1] = (unsigned long)sabuf;
	r[2] = 2 + rl + 1;
	return 0;
}

static void sigsys_handler(int sig, siginfo_t *si, void *ctx)
{
	(void)sig;
	unsigned long *r = ((ucontext_t *)ctx)->uc_mcontext.regs;
	long nr = si->si_syscall;
	char b1[PATHBUF], b2[PATHBUF];

	if (g_debug) {
		out(2, "[pid ");
		outn(2, SYS0(SYS_getpid));
		out(2, "] t nr=");
		outn(2, nr);
		out(2, " a0=");
		outn(2, (long)r[0]);
		out(2, " a1=");
		outn(2, (long)r[1]);
		out(2, " a2=");
		outn(2, (long)r[2]);
		out(2, "\n");
	}

	switch (nr) {
	// One path at arg1 with its dirfd at arg0. The final component is
	// followed as the kernel would for each syscall and its flags.
	case SYS_openat: {
		int f = (int)r[2];
		FIX(0, 1, !(f & O_NOFOLLOW) && (f & (O_CREAT | O_EXCL)) != (O_CREAT | O_EXCL), b1);
		break;
	}
	case SYS_openat2: {
		// struct open_how { u64 flags, mode, resolve; }. With RESOLVE_*
		// flags the kernel must do the walk itself to enforce them, so
		// an absolute path is only translated lexically (to_host): a
		// "/nix"-absolute symlink inside it is then not re-translated.
		const uint64_t *how = (const uint64_t *)r[2];
		if (how && r[3] >= 24 && how[2]) {
			const char *p = (const char *)r[1];
			if (p && p[0] == '/') {
				long e = to_host(p, zlen(p), b1);
				if (e < 0) {
					r[0] = (unsigned long)e;
					return;
				}
				r[1] = (unsigned long)b1;
			}
			break;
		}
		int f = how ? (int)how[0] : 0;
		FIX(0, 1, !(f & O_NOFOLLOW) && (f & (O_CREAT | O_EXCL)) != (O_CREAT | O_EXCL), b1);
		break;
	}
	case SYS_newfstatat:
	case SYS_utimensat:
		FIX(0, 1, !(r[3] & AT_SYMLINK_NOFOLLOW), b1);
		break;
	case SYS_statx:
	case SYS_open_tree:
	case SYS_open_tree_attr:
	case SYS_mount_setattr:
	case SYS_setxattrat:
	case SYS_getxattrat:
	case SYS_listxattrat:
	case SYS_removexattrat:
		FIX(0, 1, !(r[2] & AT_SYMLINK_NOFOLLOW), b1);
		break;
	case SYS_faccessat2:
	case SYS_fchmodat2:
		FIX(0, 1, !(r[3] & AT_SYMLINK_NOFOLLOW), b1);
		break;
	case SYS_fchownat:
	case SYS_file_getattr:
	case SYS_file_setattr:
		FIX(0, 1, !(r[4] & AT_SYMLINK_NOFOLLOW), b1);
		break;
	case SYS_faccessat:
	case SYS_fchmodat:
		FIX(0, 1, 1, b1);
		break;
	case SYS_mkdirat:
	case SYS_mknodat:
	case SYS_unlinkat:
		FIX(0, 1, 0, b1);
		break;
	case SYS_name_to_handle_at:
		FIX(0, 1, (r[4] & AT_SYMLINK_FOLLOW) != 0, b1);
		break;
	case SYS_fspick:
		FIX(0, 1, !(r[2] & 0x2 /* FSPICK_SYMLINK_NOFOLLOW */), b1);
		break;

	// One path at arg0, relative to the cwd.
	case SYS_chdir:
	case SYS_chroot:
	case SYS_truncate:
	case SYS_statfs:
	case SYS_acct:
	case SYS_swapon:
	case SYS_swapoff:
	case SYS_getxattr:
	case SYS_setxattr:
	case SYS_listxattr:
	case SYS_removexattr:
		FIX(-1, 0, 1, b1);
		break;
	case SYS_lgetxattr:
	case SYS_lsetxattr:
	case SYS_llistxattr:
	case SYS_lremovexattr:
		FIX(-1, 0, 0, b1);
		break;
	case SYS_umount2:
		FIX(-1, 0, !(r[1] & 0x8 /* UMOUNT_NOFOLLOW */), b1);
		break;

	// Paths elsewhere.
	case SYS_inotify_add_watch:
		FIX(-1, 1, !(r[2] & 0x02000000 /* IN_DONT_FOLLOW */), b1);
		break;
	case SYS_fanotify_mark:  // (fd, flags, mask, dirfd, path)
		FIX(3, 4, !(r[1] & 0x4 /* FAN_MARK_DONT_FOLLOW */), b1);
		break;
	case SYS_quotactl:  // (cmd, special, id, addr); Q_QUOTAON's addr is a path
		FIX(-1, 1, 1, b1);
		if ((r[0] >> 8) == 0x800002 /* Q_QUOTAON */)
			FIX(-1, 3, 1, b2);
		break;
	case SYS_fsconfig:  // (fd, cmd, key, value, aux)
		if (r[1] == 3 /* FSCONFIG_SET_PATH */ || r[1] == 4 /* _SET_PATH_EMPTY */)
			FIX(4, 3, 1, b1);
		break;
	case SYS_mount:  // (source, target, type, flags, data)
		// The source is a path only for bind and move mounts; otherwise
		// its meaning belongs to the filesystem type and it is left as is.
		if (r[3] & (MS_BIND | MS_MOVE))
			FIX(-1, 0, 1, b1);
		FIX(-1, 1, 1, b2);
		break;
	case SYS_pivot_root:
		FIX(-1, 0, 1, b1);
		FIX(-1, 1, 1, b2);
		break;

	// Two paths: (olddirfd, old, newdirfd, new, ...). Neither final
	// component is followed, except linkat's old with AT_SYMLINK_FOLLOW.
	case SYS_linkat:
		FIX(0, 1, (r[4] & AT_SYMLINK_FOLLOW) != 0, b1);
		FIX(2, 3, 0, b2);
		break;
	case SYS_renameat:
	case SYS_renameat2:
		FIX(0, 1, 0, b1);
		FIX(2, 3, 0, b2);
		break;
	case SYS_move_mount:  // (from_dfd, from, to_dfd, to, flags)
		FIX(0, 1, (r[4] & 0x1 /* MOVE_MOUNT_F_SYMLINKS */) != 0, b1);
		FIX(2, 3, (r[4] & 0x10 /* MOVE_MOUNT_T_SYMLINKS */) != 0, b2);
		break;
	// symlinkat(target, newdirfd, linkpath): only the linkpath is a path
	// to resolve. The target is literal link content and stays as written
	// (e.g. "/nix/store/..."): the handler re-translates it when the link
	// is followed, and readlink returns it unchanged, as proot does.
	case SYS_symlinkat:
		FIX(1, 2, 0, b1);
		break;

	case SYS_bind:
	case SYS_connect:
		if (fix_sockaddr(r, nr == SYS_connect, b1, b2))
			return;
		break;

	// readlinkat(dirfd, path, buf, bufsiz): /proc/self/exe reports the
	// target; otherwise translate the path. As in proot (detranslate_path
	// with a referrer), only the content of links under /proc is
	// reverse-translated: those name kernel objects by their host path.
	// Every other link's content is returned as stored. The full content is
	// read first, so the result is truncated to bufsiz after translation.
	case SYS_readlinkat: {
		const char *path = (const char *)r[1];
		char *ubuf = (char *)r[2];
		long sz = (long)r[3];
		if (path && strcmp(path, "/proc/self/exe") == 0) {
			long n = (long)zlen(g_target);
			if (n > sz)
				n = sz;
			memcpy(ubuf, g_target, n);
			r[0] = (unsigned long)n;
			return;
		}
		FIX(0, 1, 0, b1);
		if (sz <= 0)
			break;  // the kernel's EINVAL
		long ret = SYS4(nr, r[0], r[1], b2, PATHBUF - 1);
		if (ret > 0 && host_bind(b2, (size_t)ret)) {
			const char *h = (const char *)r[1];
			int in_proc = h[0] == '/' ? is_proc(h) : guest_base((long)(int)r[0], b1) >= 0 && is_proc(b1);
			if (in_proc) {
				b2[ret] = 0;
				ret = to_guest(b2, ret);
			}
		}
		if (ret > 0) {
			if (ret > sz)
				ret = sz;
			memcpy(ubuf, b2, (size_t)ret);
		}
		r[0] = (unsigned long)ret;
		return;
	}

	// getcwd(buf, size): reverse-translate the cwd, as proot's (emulated)
	// getcwd always does. The kernel returns the length with the NUL.
	case SYS_getcwd: {
		char *ubuf = (char *)r[0];
		long ret = SYS2(nr, b2, PATHBUF);
		if (ret > 0) {
			ret = to_guest(b2, ret - 1);
			if (ret >= 0 && ret + 1 > (long)r[1])
				ret = -ERANGE;
			if (ret >= 0)
				memcpy(ubuf, b2, (size_t)++ret);
		}
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
		break;

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
		break;

	// Android's app SELinux policy allows only the classic termios ioctls on
	// ttys (TCGETS, TCSETS, TCSETSW, TCFLSH), but glibc >= 2.42 issues the
	// termios2 forms (nix's builder pty fails with EACCES otherwise).
	// Rewrite them as proot does on Android: struct termios is the first 36
	// bytes of struct termios2; TCGETS2 derives its two speed fields from the
	// CBAUD bits; TCSETSF(2) becomes TCFLSH(TCIFLUSH) + TCSETSW. An arbitrary
	// (BOTHER) speed cannot be expressed this way and is not carried over.
	case SYS_ioctl: {
		uint32_t cmd = (uint32_t)r[1];
		long fd = (long)(int)r[0];
		char *t = (char *)r[2];
		long e;
		if (cmd == TCGETS2) {
			e = SYS3(SYS_ioctl, fd, TCGETS, t);
			if (e == 0) {
				uint32_t cflag, is, os;
				memcpy(&cflag, t + 8, 4);
				os = baud(cflag & CBAUD);
				is = (cflag >> 16) & CBAUD ? baud((cflag >> 16) & CBAUD) : os;
				memcpy(t + 36, &is, 4);
				memcpy(t + 40, &os, 4);
			}
		} else if (cmd == TCSETS2) {
			e = SYS3(SYS_ioctl, fd, TCSETS, t);
		} else if (cmd == TCSETSW2) {
			e = SYS3(SYS_ioctl, fd, TCSETSW, t);
		} else {  // TCSETSF, TCSETSF2
			e = SYS3(SYS_ioctl, fd, TCFLSH, TCIFLUSH);
			if (e == 0)
				e = SYS3(SYS_ioctl, fd, TCSETSW, t);
		}
		r[0] = (unsigned long)e;
		return;
	}

	// clone3 cannot be supported in-process: its CLONE_CLEAR_SIGHAND (used
	// by glibc's posix_spawn) would drop our SIGSYS handler in the child,
	// and a clone3 with a new stack cannot be reissued from inside the
	// handler (the child would return into handler frames on an empty
	// stack). Fail it with ENOSYS, as seccomp sandboxes commonly do; glibc
	// and musl then fall back to clone(), which runs untrapped and keeps
	// the handler.
	case SYS_clone3:
		r[0] = (unsigned long)-ENOSYS;
		return;

	// The new image is started by re-exec'ing the loader with the guest
	// target path; it canonicalises the path itself against the same cwd.
	case SYS_execve:
		r[0] = (unsigned long)reexec((const char *)r[0], (char **)r[1], (char **)r[2]);
		return;

	// execveat(dirfd, path, argv, envp, flags): turn (dirfd, path) into an
	// absolute guest path, since the dirfd may not survive the re-exec.
	case SYS_execveat: {
		const char *path = (const char *)r[1];
		int flags = (int)r[4];
		if (!path || (!*path && !(flags & AT_EMPTY_PATH))) {
			r[0] = (unsigned long)(path ? -ENOENT : -EFAULT);
			return;
		}
		if (flags & AT_SYMLINK_NOFOLLOW) {
			const char *h;
			long e = canon((long)(int)r[0], path, 0, b1, &h);
			if (e < 0) {
				r[0] = (unsigned long)e;
				return;
			}
			char one;
			if (SYS4(SYS_readlinkat, (long)(int)r[0], h, &one, 1) >= 0) {
				r[0] = (unsigned long)-ELOOP;
				return;
			}
		}
		long e = guest_abs((long)(int)r[0], path, b2, &path);
		if (e < 0) {
			r[0] = (unsigned long)e;
			return;
		}
		r[0] = (unsigned long)reexec(path, (char **)r[2], (char **)r[3]);
		return;
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
	r[0] = (unsigned long)SYS6(nr, r[0], r[1], r[2], r[3], r[4], r[5]);
}

static void install_handler(void)
{
	if (SYS4(SYS_rt_sigaction, SIGSYS, &g_ka, 0, 8) < 0)
		die("rt_sigaction(SIGSYS) failed");
}

// A trapped execve re-execs the loader from inside the SIGSYS handler, where
// SIGSYS is masked; that mask survives execve, so the new image (and the
// target it jumps to) would otherwise start with SIGSYS blocked, and a
// seccomp trap on a blocked SIGSYS is force-killed regardless of the handler.
static void unblock_sigsys(void)
{
	uint64_t set = 1ULL << (SIGSYS - 1);
	SYS4(SYS_rt_sigprocmask, 1 /*SIG_UNBLOCK*/, &set, 0, 8);
}

// ---- seccomp filter ----------------------------------------------------

// Every syscall on aarch64 (asm-generic table) that takes a path or a
// dirfd+path, plus getcwd, execve/execveat, rt_sigaction/rt_sigprocmask and
// clone3 (and ioctl, but only for the termios cmds in traced_ioctls).
// Not handled (documented in RESULTS.md): unix datagram destinations in
// sendto/sendmsg/sendmmsg, paths inside bpf() attrs, and the removed,
// privileged lookup_dcookie. Everything else is left untrapped.
static const int traced[] = {
	SYS_setxattr,     SYS_lsetxattr,      SYS_getxattr,       SYS_lgetxattr,
	SYS_listxattr,    SYS_llistxattr,     SYS_removexattr,    SYS_lremovexattr,
	SYS_getcwd,       SYS_inotify_add_watch, SYS_mknodat,     SYS_mkdirat,
	SYS_unlinkat,     SYS_symlinkat,      SYS_linkat,         SYS_renameat,
	SYS_umount2,      SYS_mount,          SYS_pivot_root,     SYS_statfs,
	SYS_truncate,     SYS_faccessat,      SYS_chdir,          SYS_chroot,
	SYS_fchmodat,     SYS_fchownat,       SYS_openat,         SYS_quotactl,
	SYS_readlinkat,   SYS_newfstatat,     SYS_utimensat,      SYS_acct,
	SYS_bind,         SYS_connect,        SYS_execve,         SYS_swapon,
	SYS_swapoff,      SYS_fanotify_mark,  SYS_name_to_handle_at, SYS_renameat2,
	SYS_execveat,     SYS_statx,          SYS_open_tree,      SYS_move_mount,
	SYS_fsconfig,     SYS_fspick,         SYS_openat2,        SYS_faccessat2,
	SYS_mount_setattr, SYS_fchmodat2,     SYS_setxattrat,     SYS_getxattrat,
	SYS_listxattrat,  SYS_removexattrat,  SYS_open_tree_attr, SYS_file_getattr,
	SYS_file_setattr, SYS_rt_sigaction,   SYS_rt_sigprocmask, SYS_clone3,
};
#define NTRACED (int)(sizeof traced / sizeof traced[0])

static void install_filter(void)
{
	uint64_t lo = (uintptr_t)allow_start, hi = (uintptr_t)allow_end;
	if ((lo >> 32) != (hi >> 32))
		die("exempt stub range crosses a 4GiB boundary");

	// Layout: arch check; load nr; one JEQ per traced syscall (match ->
	// IPCHK); ioctl? load the cmd and one JEQ per traced cmd (match ->
	// IPCHK); else ALLOW. IPCHK ALLOWs the exempt stub, TRAPs the rest.
	enum { ALLOW = 4 + NTRACED + 2 + NIOCTLS, IPCHK = ALLOW + 1, LEN = IPCHK + 7 };
	struct sock_filter f[LEN];
	int k = 0;
#define TO(t) ((t) - k - 1)  // relative jump from instruction k to index t
	f[k++] = (struct sock_filter)BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
					      offsetof(struct seccomp_data, arch));
	f[k++] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, AUDIT_ARCH_AARCH64, 1, 0);
	f[k++] = (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
	f[k++] = (struct sock_filter)BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
					      offsetof(struct seccomp_data, nr));
	for (int i = 0; i < NTRACED; i++, k++)
		f[k] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, traced[i],
						    TO(IPCHK), 0);
	f[k] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_ioctl, 0, TO(ALLOW));
	k++;
	// The cmd is an unsigned int: its low 32 bits (little endian) decide.
	f[k++] = (struct sock_filter)BPF_STMT(BPF_LD | BPF_W | BPF_ABS,
					      offsetof(struct seccomp_data, args[1]));
	for (int i = 0; i < NIOCTLS; i++, k++)
		f[k] = (struct sock_filter)BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, traced_ioctls[i],
						    TO(IPCHK), 0);
	f[k++] = (struct sock_filter)BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW);
#undef TO
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
	if (k != LEN)
		die("internal error: filter length");

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

static void xpread(int fd, void *buf, size_t n, off_t off, const char *what)
{
	long r = SYS4(SYS_pread64, fd, buf, n, off);
	if (r != (long)n)
		die2("short read of ", what);
}

static long xmmap(void *addr, size_t len, int prot, int flags, int fd, off_t off)
{
	long r = SYS6(SYS_mmap, addr, len, prot, flags, fd, off);
	if (r < 0 && r > -4096)
		die_errno("mmap failed", "", r);
	return r;
}

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

// Map an ELF (guest path) like the kernel's load_elf_binary: a PIE (ET_DYN)
// anywhere the kernel picks, a non-PIE (ET_EXEC) at its own link addresses.
// The loader itself is linked high (Makefile), out of the ET_EXEC range.
static void load_elf(const char *path, struct loaded *l)
{
	memset(l, 0, sizeof *l);
	char b[PATHBUF];
	const char *h;
	long fd = canon(AT_FDCWD, path, 1, b, &h);
	if (fd == 0)
		fd = SYS4(SYS_openat, AT_FDCWD, h, O_RDONLY | O_CLOEXEC, 0);
	if (fd < 0)
		die_errno("cannot open ", path, fd);

	Elf64_Ehdr eh;
	xpread(fd, &eh, sizeof eh, 0, "ELF header");
	if (memcmp(eh.e_ident, ELFMAG, SELFMAG) != 0 || eh.e_ident[EI_CLASS] != ELFCLASS64 ||
	    eh.e_machine != EM_AARCH64)
		die2("not an aarch64 ELF64: ", path);
	if (eh.e_type != ET_DYN && eh.e_type != ET_EXEC)
		die2("not an executable (ET_EXEC/ET_DYN): ", path);
	if (eh.e_phnum == 0 || eh.e_phentsize != sizeof(Elf64_Phdr))
		die2("bad program headers in ", path);

	Elf64_Phdr ph[eh.e_phnum];
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

	// Reserve the whole span so the segments stay contiguous: a PIE where
	// the kernel picks, a non-PIE exactly at its vaddr. MAP_FIXED_NOREPLACE
	// fails with EEXIST instead of overwriting anything already there.
	unsigned long bias;
	if (eh.e_type == ET_DYN) {
		bias = (unsigned long)xmmap(0, maxv - minv, PROT_NONE,
					    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0) - minv;
	} else {
		long res = SYS6(SYS_mmap, minv, maxv - minv, PROT_NONE,
				MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
		if (res < 0 && res > -4096)
			die_errno("cannot map the non-PIE image at its link address: ", path, res);
		if ((unsigned long)res != minv)
			die2("kernel ignored MAP_FIXED_NOREPLACE for ", path);
		bias = 0;
	}

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
				      MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS, -1, 0);
			if (mprot != prot && fend > start)
				SYS3(SYS_mprotect, start, fend - start, prot);
		}
	}

	l->base = bias;
	l->entry = bias + eh.e_entry;
	l->phnum = eh.e_phnum;
	l->phent = eh.e_phentsize;
	// AT_PHDR: PT_PHDR if present, else the PT_LOAD that holds e_phoff.
	for (int i = 0; i < eh.e_phnum; i++) {
		if (ph[i].p_type == PT_PHDR) {
			l->phdr = bias + ph[i].p_vaddr;
			break;
		}
		if (ph[i].p_type == PT_LOAD && !l->phdr && eh.e_phoff >= ph[i].p_offset &&
		    eh.e_phoff < ph[i].p_offset + ph[i].p_filesz)
			l->phdr = bias + ph[i].p_vaddr + (eh.e_phoff - ph[i].p_offset);
	}
	if (!l->phdr)
		die2("program headers are not mapped in ", path);

	for (int i = 0; i < eh.e_phnum; i++) {
		if (ph[i].p_type != PT_INTERP)
			continue;
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
	char *stk = (char *)xmmap(0, STACK_SIZE, PROT_READ | PROT_WRITE,
				  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
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
	size_t tn = zlen(g_execfn) + 1;  // AT_EXECFN
	s -= tn;
	memcpy(s, g_execfn, tn);
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

// ---- bind table setup --------------------------------------------------

// Drop the trailing "/" or "/." a walk keeps (proot's chop_finality).
static long chop(char *p, long n)
{
	if (n > 1 && p[n - 1] == '/')
		n--;
	else if (n >= 2 && p[n - 1] == '.' && p[n - 2] == '/')
		n = n == 2 ? 1 : n - 2;
	p[n] = 0;
	return n;
}

// Canonicalise the binds once, in the first process, as proot does at
// startup (new_binding, initialize_binding): the host path with every
// symlink resolved, and it must exist; the guest path in the guest view of
// the binds it is nested in (containers first), its final component followed
// unless the guest path ends in "!". The canonical "host:guest" strings go to
// store (2 * PATHBUF per bind) and are what re-execs get. Not supported, with
// an error: a relative guest path, binding the guest root "/" (proot's -r),
// and two binds with the same guest path or the same host path other than
// "/" (proot picks one by its list order).
static void init_binds(int nb, char *store)
{
	for (int i = 1; i < nb; i++)
		for (int j = i; j > 0 && g_binds[j - 1].glen > g_binds[j].glen; j--) {
			struct bind t = g_binds[j];
			g_binds[j] = g_binds[j - 1];
			g_binds[j - 1] = t;
		}
	for (int i = 0; i < nb; i++) {
		struct bind *b = &g_binds[i];
		char raw[PATHBUF], g[PATHBUF], hb[PATHBUF];
		char *out = store + (size_t)i * 2 * PATHBUF;
		if (b->hlen >= PATHBUF || b->glen >= PATHBUF || !b->hlen || !b->glen)
			die2("bad bind: ", b->arg);
		memcpy(raw, b->host, b->hlen);
		raw[b->hlen] = 0;
		g_nbinds = 0;  // the host path in the plain host view
		long hl = walk(AT_FDCWD, raw, 1, out, hb);
		struct stat st;
		if (hl >= 0)
			hl = chop(out, hl);
		long e = hl < 0 ? hl : SYS4(SYS_newfstatat, AT_FDCWD, out, &st, 0);
		if (e < 0)
			die_errno("bind host path ", raw, e);
		size_t rl = b->glen;
		memcpy(raw, b->guest, rl + 1);
		int follow = raw[rl - 1] != '!';
		if (!follow)
			raw[--rl] = 0;
		if (raw[0] != '/')
			die2("bind guest path must be absolute: ", b->arg);
		g_nbinds = i;  // the guest path through the binds before it
		long gl = walk(AT_FDCWD, raw, follow, g, hb);
		if (gl < 0)
			die_errno("bind guest path ", raw, gl);
		gl = chop(g, gl);
		if (gl == 1)
			die2("binding the guest root / is not supported: ", b->arg);
		if (hl + 1 + gl >= (long)(2 * PATHBUF))
			die2("bad bind: ", b->arg);
		out[hl] = ':';
		memcpy(out + hl + 1, g, (size_t)gl + 1);
		*b = (struct bind){ out, out + hl + 1, out, (size_t)hl, (size_t)gl };
	}
	for (int i = 0; i < nb; i++)
		for (int j = i + 1; j < nb; j++) {
			const struct bind *x = &g_binds[i], *y = &g_binds[j];
			if ((x->glen == y->glen && memcmp(x->guest, y->guest, x->glen) == 0) ||
			    (x->hlen > 1 && x->hlen == y->hlen && memcmp(x->host, y->host, x->hlen) == 0))
				die2("two binds share a path: ", y->arg);
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

	// A re-exec from our own execve handler: the filter is already there
	// and the binds are canonical.
	int first = !(argc > 1 && strcmp(argv[1], "--reexec") == 0);
	if (!first) {
		argv++;
		argc--;
	}
	int nb = 0;
	while (1 + 2 * nb + 1 < argc && strcmp(argv[1 + 2 * nb], "-b") == 0)
		nb++;
	if (nb) {
		size_t sz = (size_t)nb * sizeof(struct bind) + (first ? (size_t)nb * PATHBUF * 2 : 0);
		g_binds = (struct bind *)xmmap(0, sz, PROT_READ | PROT_WRITE,
					       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		for (int i = 0; i < nb; i++) {
			const char *a = argv[2 + 2 * i], *c = strchr(a, ':');
			if (!c)
				die2("-b needs host:guest: ", a);
			g_binds[i] = (struct bind){ a, c + 1, a, (size_t)(c - a), zlen(c + 1) };
		}
		if (first)
			init_binds(nb, (char *)(g_binds + nb));
		g_nbinds = nb;
	}
	argv += 2 * nb;
	argc -= 2 * nb;
	if (argc < 2)
		die("usage: launcher [-b host:guest]... <target> [argv0 argv1 ...]");

	const char *target = argv[1];
	g_execfn = target;

	// This loader's own real path, for re-exec on a trapped execve.
	static char self[PATHBUF];
	long sn = SYS4(SYS_readlinkat, AT_FDCWD, "/proc/self/exe", self, sizeof self - 1);
	if (sn <= 0)
		die("cannot readlink /proc/self/exe");
	self[sn] = 0;
	g_launcher = self;

	g_debug = getenv("LAUNCHER_DEBUG") != NULL;

	// Handler before the filter, so no trapped syscall can arrive unhandled.
	install_handler();
	unblock_sigsys();
	if (first) {
		if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0)
			die("prctl(NO_NEW_PRIVS) failed");
		install_filter();
	}

	long e = exec_check(target);
	if (e < 0)
		die_errno("cannot execute ", target, e);

	// The target's argv: explicit args if given, else just the target path.
	// Kept at the end of A so each "#!" level can prepend interp [arg].
	int targc = (argc > 2) ? argc - 2 : 1;
	int s = 2 * MAX_INTERP_DEPTH;
	char *A[s + targc + 1];
	if (argc > 2)
		memcpy(&A[s], &argv[2], (size_t)targc * sizeof A[0]);
	else
		A[s] = (char *)target;
	A[s + targc] = 0;

	// Run "#!" scripts by loading their interpreter, as binfmt_script does:
	// argv becomes interp [arg] script args... (exec_check bounded the depth).
	static char lines[MAX_INTERP_DEPTH][BINPRM_BUF_SIZE];
	for (int d = 0; d < MAX_INTERP_DEPTH; d++) {
		long n = read_head(target, lines[d], 0);
		if (n < 2 || lines[d][0] != '#' || lines[d][1] != '!')
			break;
		char *arg, *interp = parse_shebang(lines[d], n, &arg);
		A[s] = (char *)target;
		if (arg)
			A[--s] = arg;
		A[--s] = interp;
		target = interp;
	}
	targc = 0;
	while (A[s + targc])
		targc++;

	// /proc/self/exe reports the canonical guest path of the image.
	static char tg[PATHBUF];
	char hb[PATHBUF];
	e = walk(AT_FDCWD, target, 1, tg, hb);
	if (e < 0)
		die_errno("cannot resolve ", target, e);
	g_target = tg;

	struct loaded exe, interp;
	load_elf(target, &exe);
	interp.has_interp = 0;
	if (exe.has_interp)
		load_elf(exe.interp, &interp);

	void *sp = build_stack(targc, &A[s], envp, our_auxv, &exe, &interp);
	unsigned long entry = exe.has_interp ? interp.entry : exe.entry;
	jump_to((void *)entry, sp);
}
