// Probe a: syscall user dispatch. Does the prctl succeed, does a dispatched
// getpid() raise SIGSYS that the handler can answer, and does a syscall from
// the exempt range bypass dispatch?
#include "common.h"

int main(void)
{
	pid_t real = (pid_t)raw_syscall6(SYS_getpid, 0, 0, 0, 0, 0, 0);
	if (install_sigsys_handler() != 0) {
		printf("sud: sigaction failed errno=%d (%s)\n", errno, strerror(errno));
		return 1;
	}
	if (sud_enable() != 0) {
		printf("sud: prctl(PR_SET_SYSCALL_USER_DISPATCH) failed errno=%d (%s)\n", errno, strerror(errno));
		return 1;
	}
	answer_getpid_magic = 1;
	sud_selector = SYSCALL_DISPATCH_FILTER_BLOCK;
	pid_t dispatched = getpid();
	pid_t exempt = (pid_t)raw_syscall6(SYS_getpid, 0, 0, 0, 0, 0, 0);
	sud_selector = SYSCALL_DISPATCH_FILTER_ALLOW;
	pid_t allowed = getpid();
	printf("sud: prctl ok; dispatched getpid=%d (magic %d => %s, si_code=%d, sigsys=%ld); "
	       "exempt-range getpid=%d (%s); selector-allow getpid=%d (%s)\n",
	       dispatched, MAGIC_PID, dispatched == MAGIC_PID ? "handler answered" : "NOT answered",
	       sigsys_code, sigsys_count, exempt, exempt == real ? "real pid, ok" : "WRONG",
	       allowed, allowed == real ? "real pid, ok" : "WRONG");
	return dispatched == MAGIC_PID && exempt == real && allowed == real ? 0 : 1;
}
