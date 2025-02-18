#include "minitalk.h"

#include <unistd.h>

static volatile sig_atomic_t	g_ack_received;

static void	handle_ack(int signal)
{
	(void)signal;
	g_ack_received = 1;
}

static int	install_ack_handler(void)
{
	struct sigaction	action;

	action.sa_handler = handle_ack;
	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	return (sigaction(MT_ACK_SIGNAL, &action, NULL));
}

static int	send_bit(pid_t server_pid, int bit, const sigset_t *old_mask)
{
	int	signal;

	signal = MT_ZERO_SIGNAL;
	if (bit != 0)
		signal = MT_ONE_SIGNAL;
	g_ack_received = 0;
	if (kill(server_pid, signal) == -1)
		return (-1);
	while (!g_ack_received)
		sigsuspend(old_mask);
	return (0);
}

static int	send_byte(pid_t server_pid, unsigned char byte,
		const sigset_t *old_mask)
{
	int	shift;

	shift = 7;
	while (shift >= 0)
	{
		if (send_bit(server_pid, (byte >> shift) & 1, old_mask) == -1)
			return (-1);
		shift--;
	}
	return (0);
}

int	main(int argc, char **argv)
{
	pid_t	server_pid;
	sigset_t	blocked;
	sigset_t	old_mask;
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
	if (install_ack_handler() == -1)
	{
		mt_putstr_fd("client: failed to install signal handlers\n",
			STDERR_FILENO);
		return (1);
	}
	sigemptyset(&blocked);
	sigaddset(&blocked, MT_ACK_SIGNAL);
	if (sigprocmask(SIG_BLOCK, &blocked, &old_mask) == -1)
	{
		mt_putstr_fd("client: failed to block acknowledgement signal\n",
			STDERR_FILENO);
		return (1);
	}
	index = 0;
	while (argv[2][index] != '\0')
	{
		if (send_byte(server_pid, (unsigned char)argv[2][index],
				&old_mask) == -1)
		{
			mt_putstr_fd("client: failed to send signal\n", STDERR_FILENO);
			return (1);
		}
		index++;
	}
	if (send_byte(server_pid, '\0', &old_mask) == -1)
	{
		mt_putstr_fd("client: failed to finish message\n", STDERR_FILENO);
		return (1);
	}
	return (0);
}
