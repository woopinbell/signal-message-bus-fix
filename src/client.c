#include "minitalk.h"

#include <unistd.h>

static int	send_bit(pid_t server_pid, int bit)
{
	int	signal;

	signal = MT_ZERO_SIGNAL;
	if (bit != 0)
		signal = MT_ONE_SIGNAL;
	if (kill(server_pid, signal) == -1)
		return (-1);
	usleep(150);
	return (0);
}

static int	send_byte(pid_t server_pid, unsigned char byte)
{
	int	shift;

	shift = 7;
	while (shift >= 0)
	{
		if (send_bit(server_pid, (byte >> shift) & 1) == -1)
			return (-1);
		shift--;
	}
	return (0);
}

int	main(int argc, char **argv)
{
	pid_t	server_pid;
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
	index = 0;
	while (argv[2][index] != '\0')
	{
		if (send_byte(server_pid, (unsigned char)argv[2][index]) == -1)
		{
			mt_putstr_fd("client: failed to send signal\n", STDERR_FILENO);
			return (1);
		}
		index++;
	}
	if (send_byte(server_pid, '\0') == -1)
	{
		mt_putstr_fd("client: failed to finish message\n", STDERR_FILENO);
		return (1);
	}
	return (0);
}
