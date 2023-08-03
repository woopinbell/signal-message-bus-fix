#define _POSIX_C_SOURCE 200809L

#include "minitalk.h"

#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static int	wait_for_child(pid_t child)
{
	struct timespec	pause_time;
	int				status;
	int				tries;
	pid_t			result;

	tries = 0;
	while (tries < 50)
	{
		result = waitpid(child, &status, WNOHANG);
		if (result == child)
		{
			if (WIFEXITED(status))
				return (WEXITSTATUS(status));
			if (WIFSIGNALED(status))
				return (128 + WTERMSIG(status));
			return (1);
		}
		if (result == -1)
		{
			if (errno == EINTR)
				continue ;
			kill(child, SIGKILL);
			waitpid(child, &status, 0);
			return (1);
		}
		pause_time.tv_sec = 0;
		pause_time.tv_nsec = 100000000L;
		while (nanosleep(&pause_time, &pause_time) == -1)
		{
			if (errno != EINTR)
			{
				kill(child, SIGKILL);
				waitpid(child, &status, 0);
				return (1);
			}
		}
		tries++;
	}
	kill(child, SIGKILL);
	waitpid(child, &status, 0);
	return (124);
}

int	main(int argc, char **argv)
{
	sigset_t	blocked;
	sigset_t	old_mask;
	pid_t		child;

	if (argc < 2)
		return (1);
	sigemptyset(&blocked);
	sigaddset(&blocked, MT_ACK_SIGNAL);
	sigaddset(&blocked, MT_NACK_SIGNAL);
	sigaddset(&blocked, SIGALRM);
	if (sigprocmask(SIG_BLOCK, &blocked, &old_mask) == -1)
		return (1);
	child = fork();
	if (child == -1)
		return (1);
	if (child == 0)
	{
		execv(argv[1], &argv[1]);
		_exit(127);
	}
	if (sigprocmask(SIG_SETMASK, &old_mask, NULL) == -1)
	{
		kill(child, SIGKILL);
		waitpid(child, NULL, 0);
		return (1);
	}
	return (wait_for_child(child));
}
