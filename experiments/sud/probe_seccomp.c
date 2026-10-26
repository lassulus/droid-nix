// Probe b: NO_NEW_PRIVS + a seccomp filter returning SECCOMP_RET_TRAP for
// getpid unless instruction_pointer is in the exempt range.
#include "common.h"

int main(void)
{
	pid_t real = (pid_t)raw_syscall6(SYS_getpid, 0, 0, 0, 0, 0, 0);
	if (install_sigsys_handler() != 0) {
		printf("seccomp: sigaction failed errno=%d (%s)\n", errno, strerror(errno));
		return 1;
	}
	if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
		printf("seccomp: prctl(PR_SET_NO_NEW_PRIVS) failed errno=%d (%s)\n", errno, strerror(errno));
		return 1;
	}
	if (seccomp_trap_install() != 0) {
		printf("seccomp: NNP ok; seccomp(SET_MODE_FILTER) failed errno=%d (%s)\n", errno, strerror(errno));
		return 1;
	}
	answer_getpid_magic = 1;
	pid_t trapped = getpid();
	pid_t exempt = (pid_t)raw_syscall6(SYS_getpid, 0, 0, 0, 0, 0, 0);
	printf("seccomp: NNP ok, filter ok; libc getpid=%d (magic %d => %s, si_code=%d, sigsys=%ld); "
	       "exempt-range getpid=%d (%s)\n",
	       trapped, MAGIC_PID, trapped == MAGIC_PID ? "SIGSYS arrived, handler answered" : "NOT trapped",
	       sigsys_code, sigsys_count, exempt, exempt == real ? "real pid, exception works" : "WRONG");
	return trapped == MAGIC_PID && exempt == real ? 0 : 1;
}
