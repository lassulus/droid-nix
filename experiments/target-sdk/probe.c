// Probes for running droid-nix with a current targetSdkVersion (untrusted_app
// domain of API 29+, where app data files must not be executed). Run in the
// app's domain from the native library directory:
//
//   <nativeLibraryDir>/libprobe.so <writable dir in the app's data>
//
// Each probe runs in a forked child so a crash only fails that probe.

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include <unistd.h>

#define HELLO_EXIT 42

static char copy[4096];

// movz w0, #42; ret
static const uint32_t code[] = { 0x52800540, 0xd65f03c0 };

static int call(void *p)
{
	__builtin___clear_cache((char *)p, (char *)p + sizeof code);
	return ((int (*)(void))p)();
}

static void show_file(const char *what, const char *path)
{
	char label[256] = "?";
	ssize_t n = getxattr(path, "security.selinux", label, sizeof label - 1);
	if (n > 0)
		label[n] = 0;
	printf("%-28s %s  [%s]\n", what, path, label);
}

static void show_text(const char *what, const char *path, const char *grep)
{
	char buf[8192];
	int fd = open(path, O_RDONLY);
	ssize_t n = fd < 0 ? -1 : read(fd, buf, sizeof buf - 1);
	if (fd >= 0)
		close(fd);
	if (n < 0) {
		printf("%-28s %s: %s\n", what, path, strerror(errno));
		return;
	}
	buf[n] = 0;
	for (char *l = strtok(buf, "\n"); l; l = strtok(0, "\n"))
		if (!grep || strncmp(l, grep, strlen(grep)) == 0)
			printf("%-28s %s\n", what, l);
}

// Run fn in a child; it returns 0 for "yes" or an errno. A signal is
// reported as such.
static void probe(const char *name, int (*fn)(void))
{
	fflush(stdout);
	pid_t pid = fork();
	if (pid == 0) {
		int e = fn();
		fflush(stdout);
		_exit(e);  // errnos are < 256
	}
	int st;
	waitpid(pid, &st, 0);
	if (WIFSIGNALED(st))
		printf("%-40s NO   (killed by signal %d)\n", name, WTERMSIG(st));
	else if (WEXITSTATUS(st) == 0)
		printf("%-40s yes\n", name);
	else
		printf("%-40s NO   (%s)\n", name, strerror(WEXITSTATUS(st)));
}

static int copy_file(const char *from, const char *to, mode_t mode)
{
	int in = open(from, O_RDONLY), out = open(to, O_WRONLY | O_CREAT | O_TRUNC, mode);
	if (in < 0 || out < 0)
		return errno;
	char buf[65536];
	ssize_t n;
	while ((n = read(in, buf, sizeof buf)) > 0)
		if (write(out, buf, n) != n)
			return errno;
	close(in);
	close(out);
	return chmod(to, mode) ? errno : 0;
}

// execve path (or fexecve fd) in a grandchild that exits HELLO_EXIT.
static int run_hello(const char *path, int fd)
{
	pid_t pid = fork();
	if (pid == 0) {
		char *argv[] = { "probe", "--hello", 0 };
		if (fd >= 0)
			syscall(SYS_execveat, fd, "", argv, environ, AT_EMPTY_PATH);
		else
			execve(path, argv, environ);
		_exit(100 + errno);
	}
	int st;
	waitpid(pid, &st, 0);
	if (WIFEXITED(st) && WEXITSTATUS(st) == HELLO_EXIT)
		return 0;
	if (WIFEXITED(st) && WEXITSTATUS(st) >= 100)
		return WEXITSTATUS(st) - 100;
	return EIO;
}

static int p_access_x(void) { return access(copy, X_OK) ? errno : 0; }
static int p_exec_data(void) { return run_hello(copy, -1); }

static int p_exec_self(void)
{
	char self[4096];
	ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
	if (n < 0)
		return errno;
	self[n] = 0;
	return run_hello(self, -1);
}

static int map_file(const char *path, int prot)
{
	int fd = open(path, O_RDONLY);
	if (fd < 0)
		return errno;
	void *p = mmap(0, 4096, prot, MAP_PRIVATE, fd, 0);
	return p == MAP_FAILED ? errno : 0;
}

static int p_mmap_x_data(void) { return map_file(copy, PROT_READ | PROT_EXEC); }

static int p_mprotect_x_data(void)
{
	int fd = open(copy, O_RDONLY);
	if (fd < 0)
		return errno;
	void *p = mmap(0, 4096, PROT_READ, MAP_PRIVATE, fd, 0);
	if (p == MAP_FAILED)
		return errno;
	return mprotect(p, 4096, PROT_READ | PROT_EXEC) ? errno : 0;
}

static int p_mmap_x_self(void)
{
	char self[4096];
	ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
	if (n < 0)
		return errno;
	self[n] = 0;
	return map_file(self, PROT_READ | PROT_EXEC);
}

static int p_anon_rwx(void)
{
	void *p = mmap(0, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED)
		return errno;
	memcpy(p, code, sizeof code);
	return call(p) == 42 ? 0 : EIO;
}

static int p_anon_rw_rx(void)
{
	void *p = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED)
		return errno;
	memcpy(p, code, sizeof code);
	if (mprotect(p, 4096, PROT_READ | PROT_EXEC))
		return errno;
	return call(p) == 42 ? 0 : EIO;
}

// The loader's case: a file's content read into anonymous memory, then made
// executable (here a whole ELF read with pread, then one page made RX).
static int p_anon_copy_rx(void)
{
	int fd = open(copy, O_RDONLY);
	if (fd < 0)
		return errno;
	struct stat st;
	fstat(fd, &st);
	size_t len = (st.st_size + 4095) & ~4095UL;
	char *p = mmap(0, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED)
		return errno;
	if (pread(fd, p, st.st_size, 0) != st.st_size)
		return EIO;
	memcpy(p + len - 4096, code, sizeof code);
	if (mprotect(p, len, PROT_READ | PROT_EXEC))
		return errno;
	return call(p + len - 4096) == 42 ? 0 : EIO;
}

static int memfd_with(const void *data, size_t n)
{
	int fd = syscall(SYS_memfd_create, "probe", 0);
	if (fd < 0)
		return -errno;
	if (write(fd, data, n) != (ssize_t)n)
		return -errno;
	return fd;
}

static int p_memfd_mmap_x(int flags)
{
	char page[4096] = { 0 };
	memcpy(page, code, sizeof code);
	int fd = memfd_with(page, sizeof page);
	if (fd < 0)
		return -fd;
	void *p = mmap(0, 4096, PROT_READ | PROT_EXEC, flags, fd, 0);
	if (p == MAP_FAILED)
		return errno;
	return call(p) == 42 ? 0 : EIO;
}

static int p_memfd_mmap_x_private(void) { return p_memfd_mmap_x(MAP_PRIVATE); }
static int p_memfd_mmap_x_shared(void) { return p_memfd_mmap_x(MAP_SHARED); }

static int p_memfd_exec(void)
{
	int in = open("/proc/self/exe", O_RDONLY);
	if (in < 0)
		return errno;
	struct stat st;
	fstat(in, &st);
	char *buf = malloc(st.st_size);
	if (read(in, buf, st.st_size) != st.st_size)
		return EIO;
	int fd = memfd_with(buf, st.st_size);
	if (fd < 0)
		return -fd;
	return run_hello(0, fd);
}

static void on_sigsys(int sig, siginfo_t *si, void *uc)
{
	(void)sig;
	(void)si;
	((ucontext_t *)uc)->uc_mcontext.regs[0] = 4242;
}

static int p_seccomp_trap(void)
{
	struct sigaction sa = { .sa_sigaction = on_sigsys, .sa_flags = SA_SIGINFO };
	sigaction(SIGSYS, &sa, 0);
	if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0))
		return errno;
	struct sock_filter f[] = {
		BPF_STMT(BPF_LD | BPF_W | BPF_ABS, offsetof(struct seccomp_data, nr)),
		BPF_JUMP(BPF_JMP | BPF_JEQ | BPF_K, SYS_getppid, 0, 1),
		BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_TRAP),
		BPF_STMT(BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
	};
	struct sock_fprog prog = { sizeof f / sizeof f[0], f };
	if (syscall(SYS_seccomp, SECCOMP_SET_MODE_FILTER, 0, &prog))
		return errno;
	return syscall(SYS_getppid) == 4242 ? 0 : EIO;
}

int main(int argc, char **argv)
{
	if (argc > 1 && strcmp(argv[1], "--hello") == 0)
		return HELLO_EXIT;
	if (argc < 2) {
		fprintf(stderr, "usage: %s <writable dir in the app's data>\n", argv[0]);
		return 2;
	}
	snprintf(copy, sizeof copy, "%s/probe-copy", argv[1]);

	char self[4096];
	ssize_t n = readlink("/proc/self/exe", self, sizeof self - 1);
	self[n < 0 ? 0 : n] = 0;
	show_text("process context", "/proc/self/attr/current", 0);
	show_text("status", "/proc/self/status", "Seccomp");
	show_file("this binary", self);
	show_file("data dir", argv[1]);
	int e = copy_file(self, copy, 0700);
	if (e) {
		printf("cannot copy to %s: %s\n", copy, strerror(e));
		return 1;
	}
	show_file("copy in data dir", copy);
	printf("\n");

	probe("access(X_OK) of an app data file", p_access_x);
	probe("execve of an app data file", p_exec_data);
	probe("mmap PROT_EXEC of an app data file", p_mmap_x_data);
	probe("mprotect +EXEC of an app data mapping", p_mprotect_x_data);
	probe("execve of the native lib", p_exec_self);
	probe("mmap PROT_EXEC of the native lib", p_mmap_x_self);
	probe("anonymous mmap RWX", p_anon_rwx);
	probe("anonymous RW -> mprotect RX", p_anon_rw_rx);
	probe("data file copied to anon RW -> RX", p_anon_copy_rx);
	probe("memfd mmap PROT_EXEC MAP_PRIVATE", p_memfd_mmap_x_private);
	probe("memfd mmap PROT_EXEC MAP_SHARED", p_memfd_mmap_x_shared);
	probe("memfd execveat (fexecve)", p_memfd_exec);
	probe("seccomp NNP + RET_TRAP filter", p_seccomp_trap);
	unlink(copy);
	return 0;
}
