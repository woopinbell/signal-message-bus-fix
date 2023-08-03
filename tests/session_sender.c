#define _POSIX_C_SOURCE 200809L

#include "minitalk.h"

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t	g_ack_received;
static volatile sig_atomic_t	g_timed_out;
static volatile sig_atomic_t	g_rejected;

static void	handle_response(int signal)
{
	if (signal == MT_ACK_SIGNAL)
		g_ack_received = 1;
	else if (signal == MT_NACK_SIGNAL)
		g_rejected = 1;
	else if (signal == SIGALRM)
		g_timed_out = 1;
}

static int	install_handlers(void)
{
	struct sigaction	action;

	action.sa_handler = handle_response;
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	if (sigaction(MT_ACK_SIGNAL, &action, NULL) == -1)
		return (-1);
	if (sigaction(MT_NACK_SIGNAL, &action, NULL) == -1)
		return (-1);
	if (sigaction(SIGALRM, &action, NULL) == -1)
		return (-1);
	return (0);
}

static int	wait_for_ack(pid_t server_pid, int bit, const sigset_t *wait_mask)
{
	struct timespec	gap;
	int				signal;

	signal = MT_ZERO_SIGNAL;
	if (bit != 0)
		signal = MT_ONE_SIGNAL;
	g_ack_received = 0;
	g_timed_out = 0;
	g_rejected = 0;
	if (kill(server_pid, signal) == -1)
		return (-1);
	alarm(MT_ACK_TIMEOUT_SECONDS);
	while (!g_ack_received && !g_timed_out && !g_rejected)
		sigsuspend(wait_mask);
	alarm(0);
	if (!g_ack_received || g_timed_out || g_rejected)
		return (-1);
	gap.tv_sec = 0;
	gap.tv_nsec = MT_SIGNAL_GAP_US * 1000L;
	while (nanosleep(&gap, &gap) == -1)
	{
		if (errno != EINTR)
			return (-1);
	}
	return (0);
}

static int	send_byte(pid_t server_pid, unsigned char byte,
		const sigset_t *wait_mask)
{
	int	shift;

	shift = 7;
	while (shift >= 0)
	{
		if (wait_for_ack(server_pid, (byte >> shift) & 1, wait_mask) == -1)
			return (-1);
		shift--;
	}
	return (0);
}

static int	send_partial(pid_t server_pid, const sigset_t *wait_mask)
{
	if (send_byte(server_pid, 'X', wait_mask) == -1)
		return (-1);
	if (wait_for_ack(server_pid, 0, wait_mask) == -1)
		return (-1);
	if (wait_for_ack(server_pid, 1, wait_mask) == -1)
		return (-1);
	return (wait_for_ack(server_pid, 0, wait_mask));
}

static int	prepare_masks(sigset_t *wait_mask)
{
	sigset_t	blocked;

	sigemptyset(&blocked);
	sigaddset(&blocked, MT_ACK_SIGNAL);
	sigaddset(&blocked, MT_NACK_SIGNAL);
	sigaddset(&blocked, SIGALRM);
	if (sigprocmask(SIG_BLOCK, &blocked, wait_mask) == -1)
		return (-1);
	sigdelset(wait_mask, MT_ACK_SIGNAL);
	sigdelset(wait_mask, MT_NACK_SIGNAL);
	sigdelset(wait_mask, SIGALRM);
	return (0);
}

int	main(int argc, char **argv)
{
	pid_t		server_pid;
	sigset_t	wait_mask;

	if (argc != 3 || !mt_parse_pid(argv[1], &server_pid))
		return (1);
	if (install_handlers() == -1 || prepare_masks(&wait_mask) == -1)
		return (1);
	if (strcmp(argv[2], "bit") == 0)
	{
		if (wait_for_ack(server_pid, 0, &wait_mask) == -1)
			return (1);
	}
	else if (strcmp(argv[2], "partial") == 0
		|| strcmp(argv[2], "hold") == 0)
	{
		if (send_partial(server_pid, &wait_mask) == -1)
			return (1);
	}
	else
		return (1);
	if (strcmp(argv[2], "hold") == 0)
	{
		write(STDOUT_FILENO, "ready\n", 6);
		while (1)
			pause();
	}
	return (0);
}
