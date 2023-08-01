#include "minitalk.h"

#include <unistd.h>

#define SEND_ERROR 1
#define SEND_TIMEOUT 2
#define SEND_REJECTED 3

static volatile sig_atomic_t	g_ack_received;
static volatile sig_atomic_t	g_timed_out;
static volatile sig_atomic_t	g_rejected;

static void	handle_client_signal(int signal)
{
	if (signal == MT_ACK_SIGNAL)
		g_ack_received = 1;
	else if (signal == MT_NACK_SIGNAL)
		g_rejected = 1;
	else if (signal == SIGALRM)
		g_timed_out = 1;
}

static int	install_client_handlers(void)
{
	struct sigaction	action;

	action.sa_handler = handle_client_signal;
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	if (sigaction(MT_ACK_SIGNAL, &action, NULL) == -1)
		return (-1);
	if (sigaction(SIGALRM, &action, NULL) == -1)
		return (-1);
	if (sigaction(MT_NACK_SIGNAL, &action, NULL) == -1)
		return (-1);
	return (0);
}

static int	send_bit(pid_t server_pid, int bit, const sigset_t *old_mask)
{
	int	signal;

	signal = MT_ZERO_SIGNAL;
	if (bit != 0)
		signal = MT_ONE_SIGNAL;
	g_ack_received = 0;
	g_timed_out = 0;
	g_rejected = 0;
	if (kill(server_pid, signal) == -1)
		return (SEND_ERROR);
	alarm(MT_ACK_TIMEOUT_SECONDS);
	while (!g_ack_received && !g_timed_out && !g_rejected)
		sigsuspend(old_mask);
	alarm(0);
	if (g_rejected)
		return (SEND_REJECTED);
	if (g_timed_out)
		return (SEND_TIMEOUT);
	return (0);
}

static int	send_byte(pid_t server_pid, unsigned char byte,
		const sigset_t *old_mask)
{
	int	status;
	int	shift;

	shift = 7;
	while (shift >= 0)
	{
		status = send_bit(server_pid, (byte >> shift) & 1, old_mask);
		if (status != 0)
			return (status);
		shift--;
	}
	return (0);
}

static int	report_send_status(int status)
{
	if (status == SEND_TIMEOUT)
		mt_putstr_fd("client: timed out waiting for acknowledgement\n",
			STDERR_FILENO);
	else if (status == SEND_REJECTED)
		mt_putstr_fd("client: server is busy with another sender\n",
			STDERR_FILENO);
	else
		mt_putstr_fd("client: failed to send signal\n", STDERR_FILENO);
	return (1);
}

int	main(int argc, char **argv)
{
	pid_t	server_pid;
	sigset_t	blocked;
	sigset_t	old_mask;
	int		status;
	size_t	index;

	if (argc != 3)
	{
		mt_putstr_fd("usage: ./client <server_pid> <message>\n", STDERR_FILENO);
		return (1);
	}
	if (!mt_parse_pid(argv[1], &server_pid) || kill(server_pid, 0) == -1)
	{
		mt_putstr_fd("client: invalid server pid\n", STDERR_FILENO);
		return (1);
	}
	if (install_client_handlers() == -1)
	{
		mt_putstr_fd("client: failed to install signal handlers\n",
			STDERR_FILENO);
		return (1);
	}
	sigemptyset(&blocked);
	sigaddset(&blocked, MT_ACK_SIGNAL);
	sigaddset(&blocked, MT_NACK_SIGNAL);
	sigaddset(&blocked, SIGALRM);
	if (sigprocmask(SIG_BLOCK, &blocked, &old_mask) == -1)
	{
		mt_putstr_fd("client: failed to block acknowledgement signal\n",
			STDERR_FILENO);
		return (1);
	}
	index = 0;
	while (argv[2][index] != '\0')
	{
		status = send_byte(server_pid, (unsigned char)argv[2][index],
				&old_mask);
		if (status != 0)
			return (report_send_status(status));
		index++;
	}
	status = send_byte(server_pid, '\0', &old_mask);
	if (status != 0)
		return (report_send_status(status));
	return (0);
}
