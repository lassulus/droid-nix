// Probe d: execve of an ELF whose PT_INTERP does not exist. Writes two tiny
// aarch64 ELFs into the directory given as argv[1]: a control without
// PT_INTERP that exits 42, and the same with PT_INTERP=/nonexistent/ld-fake.so.1.
#define _GNU_SOURCE
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#define BASE 0x400000
static const char interp[] = "/nonexistent/ld-fake.so.1";

static int write_elf(const char *path, int with_interp)
{
	static const unsigned int code[] = {
		0xd2800540, // mov x0, #42
		0xd2800ba8, // mov x8, #93 (exit)
		0xd4000001, // svc #0
	};
	unsigned char img[512] = { 0 };
	Elf64_Ehdr *eh = (Elf64_Ehdr *)img;
	Elf64_Phdr *ph = (Elf64_Phdr *)(img + sizeof *eh);
	size_t interp_off = sizeof *eh + 2 * sizeof *ph;
	size_t code_off = (interp_off + sizeof interp + 3) & ~3UL;
	size_t size = code_off + sizeof code;
	Elf64_Phdr *load = ph;

	memcpy(eh->e_ident, ELFMAG, SELFMAG);
	eh->e_ident[EI_CLASS] = ELFCLASS64;
	eh->e_ident[EI_DATA] = ELFDATA2LSB;
	eh->e_ident[EI_VERSION] = EV_CURRENT;
	eh->e_type = ET_EXEC;
	eh->e_machine = EM_AARCH64;
	eh->e_version = EV_CURRENT;
	eh->e_entry = BASE + code_off;
	eh->e_phoff = sizeof *eh;
	eh->e_ehsize = sizeof *eh;
	eh->e_phentsize = sizeof *ph;
	eh->e_phnum = with_interp ? 2 : 1;
	if (with_interp) {
		ph[0].p_type = PT_INTERP;
		ph[0].p_flags = PF_R;
		ph[0].p_offset = interp_off;
		ph[0].p_vaddr = ph[0].p_paddr = BASE + interp_off;
		ph[0].p_filesz = ph[0].p_memsz = sizeof interp;
		ph[0].p_align = 1;
		memcpy(img + interp_off, interp, sizeof interp);
		load = &ph[1];
	}
	load->p_type = PT_LOAD;
	load->p_flags = PF_R | PF_X;
	load->p_offset = 0;
	load->p_vaddr = load->p_paddr = BASE;
	load->p_filesz = load->p_memsz = size;
	load->p_align = 0x1000;
	memcpy(img + code_off, code, sizeof code);

	unlink(path);
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0755);
	if (fd < 0 || write(fd, img, size) != (ssize_t)size)
		return -1;
	return close(fd);
}

// Returns the child's exit status, or -errno if execve failed.
static int run(const char *path)
{
	int pipefd[2];
	if (pipe2(pipefd, O_CLOEXEC) != 0)
		return -errno;
	pid_t pid = fork();
	if (pid == 0) {
		char *argv[] = { (char *)path, NULL };
		char *envp[] = { NULL };
		execve(path, argv, envp);
		int e = errno;
		write(pipefd[1], &e, sizeof e);
		_exit(127);
	}
	close(pipefd[1]);
	int e = 0, st;
	ssize_t n = read(pipefd[0], &e, sizeof e);
	close(pipefd[0]);
	waitpid(pid, &st, 0);
	if (n == sizeof e)
		return -e;
	return WIFEXITED(st) ? WEXITSTATUS(st) : 1000 + WTERMSIG(st);
}

static void describe(char *buf, size_t size, int r)
{
	if (r < 0)
		snprintf(buf, size, "execve errno=%d (%s)", -r, strerror(-r));
	else
		snprintf(buf, size, "ran, exit %d", r);
}

int main(int argc, char **argv)
{
	const char *dir = argc > 1 ? argv[1] : ".";
	char ctl[4096], bad[4096], cs[64], bs[64];
	snprintf(ctl, sizeof ctl, "%s/elf-nointerp", dir);
	snprintf(bad, sizeof bad, "%s/elf-badinterp", dir);
	if (write_elf(ctl, 0) || write_elf(bad, 1)) {
		printf("interp: writing test ELFs into %s failed errno=%d (%s)\n", dir, errno, strerror(errno));
		return 1;
	}
	int c = run(ctl), b = run(bad);
	describe(cs, sizeof cs, c);
	describe(bs, sizeof bs, b);
	printf("interp: control (no PT_INTERP): %s; PT_INTERP=%s: %s\n", cs, interp, bs);
	unlink(ctl);
	unlink(bad);
	return b == -ENOENT ? 0 : 1;
}
