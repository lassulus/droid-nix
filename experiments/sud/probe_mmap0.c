// Probe c: can the process map a page at address 0?
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>

int main(void)
{
	char min[32] = "?";
	FILE *f = fopen("/proc/sys/vm/mmap_min_addr", "r");
	if (f) {
		if (!fgets(min, sizeof min, f))
			strcpy(min, "?");
		min[strcspn(min, "\n")] = 0;
		fclose(f);
	}
	void *p = mmap((void *)0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	if (p == MAP_FAILED)
		printf("mmap0: MAP_FIXED at 0 failed errno=%d (%s); mmap_min_addr=%s\n", errno, strerror(errno), min);
	else
		printf("mmap0: MAP_FIXED at 0 succeeded (%p); mmap_min_addr=%s\n", p, min);
	return p == MAP_FAILED;
}
