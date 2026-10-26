// Microbenchmarks: getpid, stat("/nix/store"), openat+close of a store file.
// usage: bench plain|sud|seccomp <prefix> <store-file> [iterations]
//   plain:   syscalls on <prefix>/nix/... directly (no interception)
//   sud:     syscall user dispatch, handler rewrites /nix -> <prefix>/nix
//   seccomp: SECCOMP_RET_TRAP filter, same handler
// Prints the median of 5 runs in ns/call per operation, plus the runs.
#include "common.h"
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <time.h>

#define RUNS 5

static long iters = 1000000;
static int use_sud;

static uint64_t now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000000u + ts.tv_nsec;
}

static void block(void)
{
	if (use_sud)
		sud_selector = SYSCALL_DISPATCH_FILTER_BLOCK;
}

static void unblock(void)
{
	if (use_sud)
		sud_selector = SYSCALL_DISPATCH_FILTER_ALLOW;
}

static double op_getpid(const char *unused)
{
	(void)unused;
	block();
	uint64_t s = now();
	for (long i = 0; i < iters; i++)
		getpid();
	uint64_t e = now();
	unblock();
	return (double)(e - s) / iters;
}

static double op_stat(const char *path)
{
	struct stat st;
	int bad = 0;
	block();
	uint64_t s = now();
	for (long i = 0; i < iters; i++)
		bad |= stat(path, &st);
	uint64_t e = now();
	unblock();
	return bad ? -1 : (double)(e - s) / iters;
}

static double op_openclose(const char *path)
{
	int bad = 0;
	block();
	uint64_t s = now();
	for (long i = 0; i < iters; i++) {
		int fd = open(path, O_RDONLY | O_CLOEXEC);
		bad |= fd < 0;
		close(fd);
	}
	uint64_t e = now();
	unblock();
	return bad ? -1 : (double)(e - s) / iters;
}

static int cmp(const void *a, const void *b)
{
	double x = *(const double *)a, y = *(const double *)b;
	return (x > y) - (x < y);
}

static void measure(const char *mode, const char *name, double (*op)(const char *), const char *arg)
{
	double r[RUNS], sorted[RUNS];
	long before = sigsys_count;
	for (int i = 0; i < RUNS; i++)
		r[i] = sorted[i] = op(arg);
	qsort(sorted, RUNS, sizeof sorted[0], cmp);
	printf("bench mode=%s op=%s median=%.0f ns/call runs=", mode, name, sorted[RUNS / 2]);
	for (int i = 0; i < RUNS; i++)
		printf("%s%.0f", i ? "," : "", r[i]);
	printf(" sigsys/iter=%.2f\n", (double)(sigsys_count - before) / ((double)iters * RUNS));
	fflush(stdout);
}

int main(int argc, char **argv)
{
	if (argc < 4) {
		fprintf(stderr, "usage: %s plain|sud|seccomp <prefix> <store-file> [iterations]\n", argv[0]);
		return 2;
	}
	const char *mode = argv[1];
	char store[512], file[512];
	if (argc > 4)
		iters = atol(argv[4]);
	set_prefix(argv[2]);
	if (strcmp(mode, "plain") == 0) {
		snprintf(store, sizeof store, "%s/nix/store", argv[2]);
		snprintf(file, sizeof file, "%s%s", argv[2], argv[3]);
	} else {
		snprintf(store, sizeof store, "/nix/store");
		snprintf(file, sizeof file, "%s", argv[3]);
		if (install_sigsys_handler() != 0) {
			printf("bench mode=%s: sigaction failed errno=%d (%s)\n", mode, errno, strerror(errno));
			return 1;
		}
		if (strcmp(mode, "sud") == 0) {
			if (sud_enable() != 0) {
				printf("bench mode=sud: prctl failed errno=%d (%s)\n", errno, strerror(errno));
				return 1;
			}
			use_sud = 1;
		} else if (strcmp(mode, "seccomp") == 0) {
			if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0 || seccomp_trap_install() != 0) {
				printf("bench mode=seccomp: filter failed errno=%d (%s)\n", errno, strerror(errno));
				return 1;
			}
		} else {
			fprintf(stderr, "unknown mode %s\n", mode);
			return 2;
		}
	}
	struct stat st;
	if (stat(store, &st) != 0 || stat(file, &st) != 0) {
		printf("bench mode=%s: stat %s / %s failed errno=%d (%s)\n", mode, store, file, errno, strerror(errno));
		return 1;
	}
	measure(mode, "getpid", op_getpid, NULL);
	measure(mode, "stat", op_stat, store);
	measure(mode, "openat+close", op_openclose, file);
	return 0;
}
